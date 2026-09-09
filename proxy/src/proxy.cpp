// CliSiTef32I proxy p/ Pix — intercepta func 122 e roteia o QR:
//   - tela   (DevolveStringQRCode=1) -> tc=584 (string Pix)
//   - pinpad (DevolveStringQRCode=0) -> MLI/MLR/MLE/DSI (imagem PNG)
// O QR (string + PNG base64) vem do pixserver via TCP (que integra o MercadoPago).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdarg.h>

#include "config.h"
#undef ERROR  // windows.h define ERROR=0 (wingdi.h); colide com PixStatus::ERROR
#include "pix_client.h"
#include "crypto_abecs.h"
#include "qr_gen.h"
#define PROXY_EXPORT

extern "C" void InitCleanStubs(void);

#define SITEF_OK 0
#define SITEF_MOREDATA 10000
#define SITEF_CANCEL_OP -2
#define SITEF_TIMEOUT -9
#define SITEF_ERR -100

#define CMD_DATA 0
#define CMD_DISPLAY_OP 1
#define CMD_DISPLAY_BOTH 3
#define CMD_QR 50
#define CMD_COUNTDOWN 52
#define FT_DATETIME 105L
#define FT_NSU_SITEF 133L
#define FT_AUTH 135L
#define FT_RECEIPT_CLIENT 121L
#define FT_RECEIPT_MERCHANT 122L
#define FT_RECEIPT_KIND 123L
#define FT_QRCODE 584L
#define FT_COUNTDOWN 4128L
#define FUNC_PIX 122
#define POLL_INTERVAL_MS 500

typedef int (__stdcall *fnSiTefIni)(int,char*,char*,char*,char*,char*,void*);
typedef int (__stdcall *fnSiTefCon)(int*,long*,short*,short*,char*,int,int);
typedef int (__stdcall *fnSiTefFin)(int,char*,char*,char*,char*);
typedef int (__stdcall *fnSiTefConfig)(char*,char*,char*,char*);
typedef int (__stdcall *fnSiTefConfigEx)(char*,char*,char*,char*,char*);

static HMODULE g_libenv = NULL;
static fnSiTefIni g_IniSiTef = NULL;
static fnSiTefCon g_ConSiTef = NULL;
static fnSiTefFin g_FinSiTef = NULL;
static fnSiTefConfig g_ConfigSiTef = NULL;
static fnSiTefConfigEx g_ConfigSiTefEx = NULL;
static CRITICAL_SECTION g_loadCs;
static char g_dllDir[MAX_PATH] = "";

// ── log de debug ──
static CRITICAL_SECTION g_logCs;
static FILE* g_log = NULL;
static void Log(const char* fmt, ...) {
    if (!g_log) {
        char p[MAX_PATH];
        if (g_dllDir[0]) snprintf(p, sizeof(p), "%spixproxy.log", g_dllDir);
        else snprintf(p, sizeof(p), "pixproxy.log");
        fopen_s(&g_log, p, "a");
        if (g_log) setvbuf(g_log, NULL, _IOFBF, 65536);
    }
    if (!g_log) return;
    EnterCriticalSection(&g_logCs);
    DWORD ms = GetTickCount();
    char b[2048];
    va_list a; va_start(a, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, a); va_end(a);
    if (n < 0) n = 0; else if (n >= (int)sizeof(b)) n = (int)sizeof(b) - 1;
    b[n] = 0;
    fprintf(g_log, "[%6lu.%03lu] %s\n", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000), b);
    fflush(g_log);
    LeaveCriticalSection(&g_logCs);
}

// ── estado ──
enum St { S_IDLE, S_CONNECT, S_CREATE, S_PINPAD, S_POLL, S_DONE };
static St g_st = S_IDLE;
static bool g_active = false;
static bool g_screenMode = false;
static long g_amountCents = 0;
static int g_terminalCode = SITEF_ERR;

static SOCKET g_sock = INVALID_SOCKET;
static std::string g_txid, g_qr, g_qrB64;
static std::string g_auth, g_nsu, g_datetime;
static std::string g_cryptoKey;   // chave RC4 do payload (setada no handshake)
static DWORD g_lastPollTick = 0;
static int g_pollRetries = 0;
static DWORD g_lastCexTick = 0;
static HANDLE g_pinCom = INVALID_HANDLE_VALUE;   // COM do pinpad (aberto p/ limpar no cancel/finalize)
static unsigned char g_pinKsec[16];
static std::string g_operatorCode;   // codigo do terminal/operador (da IniciaFuncao)
static std::string g_idLoja, g_idTerminal;   // do ConfiguraIntSiTefInterativo(Ex)
static std::string g_cnpj;   // CNPJ da loja (do ParmsClient=1=...)
static int g_pinpadPort = 0;  // 0=auto-detectar; >0=porta fixa (do PortaPinPad=N) ou já detectada

static int g_evCmd = 0; static long g_evFt = 0; static std::string g_evData;
static std::vector<std::pair<long,std::string>> g_receiptQueue; static size_t g_receiptPos = 0;

static void SetEv(int cmd, long ft, const std::string& d) { g_evCmd = cmd; g_evFt = ft; g_evData = d; }

static int detectPinpadPort(void);  // forward (definido junto dos exports)

// ── libenv (forward das funções não-Pix) ──
static bool InitLibEnv() {
    EnterCriticalSection(&g_loadCs);
    if (!g_libenv) {
        g_libenv = GetModuleHandleA("libenv.dll");
        if (!g_libenv) {
            char p[MAX_PATH];
            if (g_dllDir[0]) { snprintf(p, sizeof(p), "%slibenv.dll", g_dllDir); g_libenv = LoadLibraryA(p); }
        }
        if (g_libenv) {
            g_IniSiTef = (fnSiTefIni)GetProcAddress(g_libenv, "_IniciaFuncaoSiTefInterativo@28");
            g_ConSiTef = (fnSiTefCon)GetProcAddress(g_libenv, "_ContinuaFuncaoSiTefInterativo@28");
            g_FinSiTef = (fnSiTefFin)GetProcAddress(g_libenv, "_FinalizaFuncaoSiTefInterativo@20");
            g_ConfigSiTef = (fnSiTefConfig)GetProcAddress(g_libenv, "_ConfiguraIntSiTefInterativo@16");
            g_ConfigSiTefEx = (fnSiTefConfigEx)GetProcAddress(g_libenv, "_ConfiguraIntSiTefInterativoEx@20");
        }
    }
    bool ok = (g_libenv != NULL);
    LeaveCriticalSection(&g_loadCs);
    return ok;
}

static void CopyOut(int* command, long* fieldType, short* mn, short* mx, char* buffer, int bufferSize, const std::string& data, int cmd, long ft) {
    if (command) *command = cmd;
    if (fieldType) *fieldType = ft;
    if (mn) *mn = 0; if (mx) *mx = 0;
    if (buffer && bufferSize > 0) {
        size_t cp = data.size();
        if (cp >= (size_t)bufferSize) cp = (size_t)bufferSize - 1;
        if (cp) memcpy(buffer, data.c_str(), cp);
        buffer[cp] = 0;
    }
}

// ── TCP p/ o pixserver ──
static bool PixSend(const std::string& line);
static bool PixRecvLine(std::string& out);
static bool PixAuthHandshake();   // forward (definida abaixo, usa PixRecvLine/PixSend)

static bool PixConnect() {
    if (g_sock != INVALID_SOCKET) return true;
    static bool wsa = false;
    if (!wsa) { WSADATA d; WSAStartup(MAKEWORD(2,2), &d); wsa = true; }
    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_sock == INVALID_SOCKET) return false;
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = inet_addr(CFG_PIX_HOST); a.sin_port = htons(CFG_PIX_PORT);
    if (connect(g_sock, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) { closesocket(g_sock); g_sock = INVALID_SOCKET; return false; }
    int to = 30000; setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
    if (!PixAuthHandshake()) { closesocket(g_sock); g_sock = INVALID_SOCKET; return false; }
    return true;
}
static bool PixSend(const std::string& line) {
    size_t off = 0;
    while (off < line.size()) {
        int n = send(g_sock, line.data() + off, (int)(line.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}
static bool PixRecvLine(std::string& out) {
    out.clear();
    char c;
    for (;;) {
        int n = recv(g_sock, &c, 1, 0);
        if (n == 0) return false;
        if (n < 0) return false;
        if (c == '\n') break;
        out.push_back(c);
        if (out.size() >= 65536) return false;
    }
    return !out.empty();
}
static void PixCloseSocket() {
    if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; }
}
static void PixClose() {
    PixCloseSocket();
    if (g_pinCom != INVALID_HANDLE_VALUE) {
        // restaura a msg padrão do pinpad ("LOJA BK" do CliSitef.ini) no lugar do QR
        const unsigned char dsp[] = { 'D','S','P','0','1','1', 0x00,0x1E,0x00,0x07, 'L','O','J','A',' ','B','K' };
        unsigned char frame[8192];
        int flen = abecs_build_secure_packet(dsp, sizeof(dsp), g_pinKsec, frame, sizeof(frame));
        if (flen >= 0) { DWORD w = 0; WriteFile(g_pinCom, frame, (DWORD)flen, &w, NULL); }
        CloseHandle(g_pinCom);
        g_pinCom = INVALID_HANDLE_VALUE;
    }
}
// reconecta (socket + auth) sem fechar o pinpad — p/ retry de polling
static void PixReconnect() {
    PixCloseSocket();
    PixConnect();
}
// token de auth (XOR 0x5A) — decodificado em runtime, não aparece em `strings`.
static std::string AuthToken() {
    static const unsigned char obf[] = {
        0x6A, 0x6D, 0x6C, 0x3C, 0x6D, 0x63, 0x6A, 0x39, 0x38, 0x69, 0x3B, 0x6E,
        0x6C, 0x39, 0x38, 0x3C, 0x3C, 0x6E, 0x3B, 0x3E, 0x6F, 0x6B, 0x3C, 0x3F,
        0x63, 0x3C, 0x63, 0x3F, 0x68, 0x6B, 0x3B, 0x6C, 0x3E, 0x6F, 0x6A, 0x68,
        0x6E, 0x62, 0x6B, 0x63, 0x3C, 0x6A, 0x6C, 0x69, 0x69, 0x3C, 0x6A, 0x6C,
        0x3C, 0x3C, 0x39, 0x6A, 0x39, 0x39, 0x6D, 0x6E, 0x69, 0x3E, 0x6A, 0x6A,
        0x3B, 0x38, 0x6A, 0x6E,
    };
    std::string s;
    s.reserve(sizeof(obf));
    for (size_t i = 0; i < sizeof(obf); i++) s.push_back((char)(obf[i] ^ 0x5A));
    return s;
}

// handshake de auth: lê "AUTH <challenge>", responde "AUTH <hmac-sha256(secret, challenge)>"
static bool PixAuthHandshake() {
    std::string line;
    if (!PixRecvLine(line)) { Log("AUTH: sem desafio do servidor"); return false; }
    if (line.compare(0, 5, "AUTH ") != 0) { Log("AUTH: resposta inesperada \"%.60s\"", line.c_str()); return false; }
    std::string h = hmacSha256Hex(AuthToken(), line.substr(5));
    if (h.empty()) { Log("AUTH: HMAC falhou"); return false; }
    if (!PixSend("AUTH " + h + "\n")) { Log("AUTH: envio falhou"); return false; }
    g_cryptoKey = h;  // RC4 do payload = HMAC(secret, challenge)
    Log("AUTH OK");
    return true;
}

// cifra/decifra o payload pós-handshake (formato "E <hex>").
static std::string EncryptLine(const std::string& plain) {  // plain SEM '\n'
    if (g_cryptoKey.empty()) return plain + "\n";
    return "E " + rc4Encode(g_cryptoKey, plain) + "\n";
}
static std::string DecryptLine(const std::string& line) {   // line SEM '\n'
    if (g_cryptoKey.empty()) return line;
    if (line.compare(0, 2, "E ") == 0) return rc4Decode(g_cryptoKey, line.substr(2));
    return line;
}

static void ParseAmount(const char* v) {
    std::string d;
    if (v) for (; *v; ++v) if (*v >= '0' && *v <= '9') d.push_back(*v);
    g_amountCents = d.empty() ? 100 : atol(d.c_str());
}

// ── pinpad: sessão segura + MLI/MLR/MLE/DSI + CEX ──
static bool SendSecureCmd(HANDLE com, const unsigned char* ksec, const unsigned char* payload, int plen) {
    unsigned char frame[8192];
    int flen = abecs_build_secure_packet(payload, plen, ksec, frame, sizeof(frame));
    if (flen < 0) return false;
    DWORD w = 0;
    if (!WriteFile(com, frame, (DWORD)flen, &w, NULL) || w != (DWORD)flen) return false;
    unsigned char rsp[512]; int rsplen = 0;
    int rrc = abecs_read_secure_response(com, ksec, rsp, sizeof(rsp), &rsplen);
    char cmd[4] = { (char)payload[0], (char)payload[1], (char)payload[2], 0 };
    if (rrc == 0 && rsplen >= 6) {
        Log("DSP %s -> \"%.*s\"", cmd, rsplen < 48 ? rsplen : 48, rsp);
    } else {
        Log("DSP %s -> read err=%d", cmd, rrc);
    }
    return rrc == 0;
}
static void TlvAppend(std::string& out, unsigned short tag, const unsigned char* val, int vlen) {
    out.push_back((char)(tag >> 8)); out.push_back((char)(tag & 0xFF));
    out.push_back((char)(vlen >> 8)); out.push_back((char)(vlen & 0xFF));
    out.append((const char*)val, vlen);
}

// re-ativa a antena CTLS (luz azul) — fire-and-forget com a KSEC da sessão.
// A antena desarma sozinha em ~30s (limite de hardware), então re-mandamos o
// CEX periodicamente durante o polling pra manter a luz acesa.
static void SendCexArm(HANDLE h) {
    std::string body;
    TlvAppend(body, 0x0006, (const unsigned char*)"000100", 6);
    char l[4]; snprintf(l, sizeof(l), "%03u", (unsigned)body.size());
    std::string cex = "CEX" + std::string(l) + body;
    unsigned char frame[8192];
    int flen = abecs_build_secure_packet((const unsigned char*)cex.data(), (int)cex.size(), g_pinKsec, frame, sizeof(frame));
    if (flen >= 0) { DWORD w = 0; WriteFile(h, frame, (DWORD)flen, &w, NULL); }
}

// exibe o QR (PNG) no pinpad. qrStr = string EMV do Pix.
static bool SendDspQr(const std::string& qrStr) {
    // Regenera o QR no tamanho do pinpad a partir da string (o base64 do MP vem
    // em 1380x1380 e o pinpad corta imagem maior que a tela).
    unsigned char* qrpng = NULL;
    int qrpnglen = 0;
    if (qr_string_to_png(qrStr.c_str(), CFG_PINPAD_QR_SIZE, &qrpng, &qrpnglen) != 0) {
        Log("DSP: falha ao gerar QR (string=%d bytes)", (int)qrStr.size());
        return false;
    }
    Log("DSP: QR gerado %dx%d (%d bytes)", CFG_PINPAD_QR_SIZE, CFG_PINPAD_QR_SIZE, qrpnglen);

    int port = g_pinpadPort;
    if (port <= 0) { port = detectPinpadPort(); if (port <= 0) port = 8; g_pinpadPort = port; }  // auto-detecta (1x) e cacheia
    char com[MAX_PATH]; snprintf(com, sizeof(com), "\\\\.\\COM%d", port);
    Log("DSP: abrindo %s (sessao segura)...", com);
    HANDLE h = CreateFileA(com, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { Log("DSP: falha ao abrir %s (err=%lu)", com, (unsigned long)GetLastError()); return false; }
    DCB dcb; memset(&dcb, 0, sizeof(dcb)); dcb.DCBlength = sizeof(DCB);
    if (GetCommState(h, &dcb)) { dcb.BaudRate = CBR_19200; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT; SetCommState(h, &dcb); }

    unsigned char ksec[16];
    int oprc = abecs_opn_secure(h, ksec);
    if (oprc != 0) { CloseHandle(h); Log("DSP: OPN falhou (cod=%d)", oprc); return false; }
    Log("DSP: KSEC=%02X%02X%02X%02X...", ksec[0], ksec[1], ksec[2], ksec[3]);
    memcpy(g_pinKsec, ksec, 16);

    const char* name = "QRCODE01";
    int nlen = (int)strlen(name);
    bool ok = true;

    // MLI (SPE_MFNAME + SPE_MFINFO)
    {
        unsigned char info[10];
        info[0] = (unsigned char)((qrpnglen >> 24) & 0xFF); info[1] = (unsigned char)((qrpnglen >> 16) & 0xFF);
        info[2] = (unsigned char)((qrpnglen >> 8) & 0xFF);  info[3] = (unsigned char)(qrpnglen & 0xFF);
        unsigned short fcrc = abecs_crc16(qrpng, qrpnglen);
        info[4] = (unsigned char)(fcrc >> 8); info[5] = (unsigned char)(fcrc & 0xFF);
        info[6] = 1; info[7] = 0; info[8] = 0; info[9] = 0;
        std::string body;
        TlvAppend(body, 0x001E, (const unsigned char*)name, nlen);
        TlvAppend(body, 0x001F, info, 10);
        char l[4]; snprintf(l, sizeof(l), "%03u", (unsigned)body.size());
        std::string mli = "MLI" + std::string(l) + body;
        if (!SendSecureCmd(h, ksec, (const unsigned char*)mli.data(), (int)mli.size())) ok = false;
    }
    // MLR (SPE_DATAIN). Campo de tamanho do comando ABECS é N3 (máx 999),
    // e o body = 4 bytes (tag+len do TLV) + dados. Chunk máx p/ não estourar: 995.
    const int chunk = 900;
    for (int off = 0; off < qrpnglen && ok; off += chunk) {
        int n = (qrpnglen - off < chunk) ? (qrpnglen - off) : chunk;
        std::string body;
        TlvAppend(body, 0x000F, qrpng + off, n);
        char l[4]; snprintf(l, sizeof(l), "%03u", (unsigned)body.size());
        std::string mlr = "MLR" + std::string(l) + body;
        if (!SendSecureCmd(h, ksec, (const unsigned char*)mlr.data(), (int)mlr.size())) ok = false;
    }
    // MLE
    if (ok && !SendSecureCmd(h, ksec, (const unsigned char*)"MLE000", 6)) ok = false;
    // DSI
    if (ok) {
        std::string body;
        TlvAppend(body, 0x001E, (const unsigned char*)name, nlen);
        char l[4]; snprintf(l, sizeof(l), "%03u", (unsigned)body.size());
        std::string dsi = "DSI" + std::string(l) + body;
        if (!SendSecureCmd(h, ksec, (const unsigned char*)dsi.data(), (int)dsi.size())) ok = false;
    }
    // CEX (ativa antena CTLS -> luz azul). Fire-and-forget.
    if (ok) { SendCexArm(h); g_lastCexTick = GetTickCount(); }

    free(qrpng);
    if (g_pinCom != INVALID_HANDLE_VALUE) CloseHandle(g_pinCom);
    g_pinCom = h;   // mantém COM aberto p/ limpar no cancel/finalize
    Log("DSP: QR pinpad %s (png=%d bytes)", ok ? "OK" : "FALHOU", qrpnglen);
    return ok;
}

// ── máquina de estados ──
static void StepMachine() {
    switch (g_st) {
    case S_CONNECT: {
        // rápido: devolve "Aguarde" já no primeiro ContinuaFuncao (não bloqueia o PDV)
        if (!PixConnect()) { g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Falha ao conectar servidor Pix"); break; }
        SetEv(CMD_COUNTDOWN, FT_COUNTDOWN, "Aguarde, em processamento...");
        g_st = S_CREATE;
        break;
    }
    case S_CREATE: {
        std::string resp;
        char id[32]; snprintf(id, sizeof(id), "%08X", (unsigned)GetTickCount());
        std::string req = buildCreate(id, g_amountCents, "00000000000000");
        while (!req.empty() && (req.back() == '\n' || req.back() == '\r')) req.pop_back();
        Log("CREATE req=%s", req.c_str());
        if (!PixSend(EncryptLine(req)) || !PixRecvLine(resp)) {
            Log("CREATE FAIL (rede)");
            g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Falha ao criar transacao Pix");
            break;
        }
        resp = DecryptLine(resp);
        if (!parseOkCreate(resp, g_txid, g_qr, g_qrB64)) {
            Log("CREATE FAIL resp=\"%.120s\"", resp.c_str());
            g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Falha ao criar transacao Pix");
            break;
        }
        Log("CREATE OK txid=%s qr=%.80s b64=%d", g_txid.c_str(), g_qr.c_str(), (int)g_qrB64.size());
        if (g_screenMode) {
            // tela: devolve a string (tc=584)
            SetEv(CMD_QR, FT_QRCODE, g_qr);
            g_st = S_POLL;
        } else {
            SetEv(CMD_COUNTDOWN, FT_COUNTDOWN, "Aguarde, em processamento...");
            g_st = S_PINPAD;
        }
        break;
    }
    case S_PINPAD: {
        // pinpad: exibe a imagem (MLI/MLR/MLE/DSI + CEX/luz azul)
        SendDspQr(g_qr);
        SetEv(CMD_COUNTDOWN, FT_COUNTDOWN, "Aguarde, em processamento...");
        g_st = S_POLL;
        break;
    }
    case S_POLL: {
        DWORD nowT = GetTickCount();
        // re-arma a antena (luz azul) a cada 25s — a antena desarma sozinha em ~30s
        if (g_pinCom != INVALID_HANDLE_VALUE && nowT - g_lastCexTick >= 25000) {
            SendCexArm(g_pinCom);
            g_lastCexTick = nowT;
            Log("POLL: re-arm antena CTLS");
        }
        if (nowT - g_lastPollTick < POLL_INTERVAL_MS) { Sleep(100); SetEv(0, -1, ""); break; }
        g_lastPollTick = nowT;
        std::string resp;
        if (!PixSend(EncryptLine("STATUS " + g_txid)) || !PixRecvLine(resp)) {
            // falha de rede: reconecta (socket+auth) e retry, sem fechar o pinpad
            g_pollRetries++;
            if (g_pollRetries > CFG_POLL_MAX_RETRY) {
                Log("POLL: desistiu apos %d falhas de rede", g_pollRetries - 1);
                g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Falha ao consultar Pix");
            } else {
                Log("POLL: falha de rede, retry %d/%d", g_pollRetries, CFG_POLL_MAX_RETRY);
                PixReconnect();
                Sleep(CFG_POLL_BACKOFF_MS);
            }
            break;
        }
        g_pollRetries = 0;
        resp = DecryptLine(resp);
        PixStatusResult ps;
        if (!parseStatus(resp, ps)) { g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Resposta Pix invalida"); break; }
        switch (ps.status) {
        case PixStatus::PEN:
            SetEv(CMD_COUNTDOWN, FT_COUNTDOWN, "Aguarde, em processamento...");
            break;
        case PixStatus::APPROVED: {
            g_auth = ps.auth; g_nsu = ps.nsu; g_datetime = ps.datetime;
            // formata datetime AAAAMMDDHHMMSS -> DD/MM/AAAA HH:MM:SS
            char dtf[32] = {0};
            if (g_datetime.size() >= 14)
                snprintf(dtf, sizeof(dtf), "%c%c/%c%c/%c%c%c%c %c%c:%c%c:%c%c",
                    g_datetime[6],g_datetime[7], g_datetime[4],g_datetime[5],
                    g_datetime[0],g_datetime[1],g_datetime[2],g_datetime[3],
                    g_datetime[8],g_datetime[9], g_datetime[10],g_datetime[11], g_datetime[12],g_datetime[13]);
            // monta o comprovante Pix no layout real do SiTef
            // (o txid do SiTef usa prefixo "SE"; nosso txid local entra no lugar do aleatório)
            char cupom[900];
            snprintf(cupom, sizeof(cupom),
                "VIA CLIENTE\r\n"
                "PIX LOJA BK\r\n"
                "TXID. TXIDMP:\r\n"
                "SE00020000031%s\r\n"
                "DADOS DO PAGAMENTO\r\n"
                "CODIGO TERM.: %s\r\n"
                "CODIGO I ESTAB.: %s\r\n"
                "DOC.: %s\r\n"
                "DATA.: %s\r\n"
                "VALOR: %.2f\r\n"
                "           (SiTef)\r\n",
                g_txid.c_str(), g_idTerminal.c_str(), g_idLoja.c_str(), g_cnpj.c_str(), dtf, g_amountCents / 100.0);
            g_receiptQueue.clear();
            g_receiptQueue.push_back({FT_DATETIME, g_datetime});
            g_receiptQueue.push_back({FT_NSU_SITEF, g_nsu});
            g_receiptQueue.push_back({FT_AUTH, g_auth});
            g_receiptQueue.push_back({FT_RECEIPT_KIND, "00"});
            g_receiptQueue.push_back({FT_RECEIPT_CLIENT, cupom});
            g_receiptQueue.push_back({FT_RECEIPT_MERCHANT, cupom});
            g_receiptPos = 0;
            g_terminalCode = SITEF_OK; g_st = S_DONE;
            break;
        }
        case PixStatus::CANCELED:
            g_terminalCode = SITEF_CANCEL_OP; g_st = S_DONE;
            break;
        case PixStatus::TIMEOUT:
            g_terminalCode = SITEF_TIMEOUT; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Pix expirado");
            break;
        case PixStatus::DENIED:
        case PixStatus::ERROR:
            g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Falha no pagamento Pix");
            break;
        }
        break;
    }
    case S_DONE:
    case S_IDLE:
    default:
        break;
    }
}

// ── exports ──
// extrai o CNPJ da loja do ParmsClient=1=... e formata XX.XXX.XXX/XXXX-XX
static std::string extractCnpj(const std::string& paramsAdic) {
    size_t p = paramsAdic.find("ParmsClient=");
    if (p == std::string::npos) return "";
    size_t q = paramsAdic.find("1=", p);
    if (q == std::string::npos) return "";
    size_t start = q + 2;
    size_t end = paramsAdic.find(';', start);
    if (end == std::string::npos) end = paramsAdic.size();
    size_t close = paramsAdic.find(']', start);
    if (close != std::string::npos && close < end) end = close;
    return paramsAdic.substr(start, end - start);
}

// extrai a porta do pinpad do paramsAdic ("PortaPinPad=8") ou 0 p/ AUTO_USB/ausente.
static int parsePinpadPort(const std::string& paramsAdic) {
    size_t p = paramsAdic.find("PortaPinPad=");
    if (p == std::string::npos) return 0;
    std::string v = paramsAdic.substr(p + 12);
    size_t end = v.find(';');
    size_t close = v.find(']');
    if (end == std::string::npos || (close != std::string::npos && close < end)) end = close;
    v = v.substr(0, end);
    if (v.empty()) return 0;
    if (v.find("AUTO") != std::string::npos || v.find("USB") != std::string::npos) return 0;
    int port = atoi(v.c_str());
    return port > 0 ? port : 0;
}

// varre COM1..16 e testa o handshake OPN — só o pinpad completa. Retorna a porta ou 0.
static int detectPinpadPort() {
    for (int port = 1; port <= 16; port++) {
        char com[32]; snprintf(com, sizeof(com), "\\\\.\\COM%d", port);
        HANDLE h = CreateFileA(com, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        DCB dcb; memset(&dcb, 0, sizeof(dcb)); dcb.DCBlength = sizeof(DCB);
        if (GetCommState(h, &dcb)) { dcb.BaudRate = CBR_19200; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT; SetCommState(h, &dcb); }
        unsigned char ksec[16];
        int ok = abecs_opn_secure(h, ksec);
        CloseHandle(h);
        if (ok == 0) { Log("DSP: pinpad detectado em COM%d", port); return port; }
    }
    return 0;
}

// intercepta a config pra capturar IdLoja + IdTerminal (códigos do cupom)
extern "C" PROXY_EXPORT int __stdcall
ConfiguraIntSiTefInterativo(char* ip, char* idLoja, char* idTerminal, char* reservado) {
    g_idLoja = idLoja ? idLoja : "";
    g_idTerminal = idTerminal ? idTerminal : "";
    Log("CONFIG ip=\"%s\" loja=\"%s\" terminal=\"%s\" reservado=\"%s\"",
        ip ? ip : "(null)", g_idLoja.c_str(), g_idTerminal.c_str(), reservado ? reservado : "(null)");
    InitLibEnv();
    return g_ConfigSiTef ? g_ConfigSiTef(ip, idLoja, idTerminal, reservado) : 0;
}

extern "C" PROXY_EXPORT int __stdcall
ConfiguraIntSiTefInterativoEx(char* ip, char* idLoja, char* idTerminal, char* reservado, char* paramsAdic) {
    g_idLoja = idLoja ? idLoja : "";
    g_idTerminal = idTerminal ? idTerminal : "";
    g_cnpj = extractCnpj(paramsAdic ? paramsAdic : "");
    g_pinpadPort = parsePinpadPort(paramsAdic ? paramsAdic : "");
    Log("CONFIG ip=\"%s\" loja=\"%s\" terminal=\"%s\" reservado=\"%s\" paramsAdic=\"%s\" cnpj=\"%s\" porta=%d",
        ip ? ip : "(null)", g_idLoja.c_str(), g_idTerminal.c_str(),
        reservado ? reservado : "(null)", paramsAdic ? paramsAdic : "(null)", g_cnpj.c_str(), g_pinpadPort);
    InitLibEnv();
    return g_ConfigSiTefEx ? g_ConfigSiTefEx(ip, idLoja, idTerminal, reservado, paramsAdic) : 0;
}

extern "C" PROXY_EXPORT int __stdcall
IniciaFuncaoSiTefInterativo(int function, char* value, char* receipt, char* date, char* time, char* operatorCode, void* additionalParams) {
    Log("INICIA func=%d val=\"%s\" recibo=\"%s\" data=\"%s\" hora=\"%s\" operador=\"%s\" params=\"%s\"",
        function, value ? value : "(null)", receipt ? receipt : "(null)", date ? date : "(null)",
        time ? time : "(null)", operatorCode ? operatorCode : "(null)", additionalParams ? (char*)additionalParams : "(null)");

    if (function == FUNC_PIX) {
        g_operatorCode = operatorCode ? operatorCode : "";
        ParseAmount(value);
        g_screenMode = (additionalParams && strstr((const char*)additionalParams, "DevolveStringQRCode=1") != NULL);
        g_pollRetries = 0;
        g_active = true;
        g_st = S_CONNECT;
        Log("  >> INTERCEPT Pix (tela=%d) amount=%ld", (int)g_screenMode, g_amountCents);
        return SITEF_MOREDATA;
    }

    // demais funções -> forward libenv
    InitLibEnv();
    return g_IniSiTef ? g_IniSiTef(function, value, receipt, date, time, operatorCode, additionalParams) : SITEF_CANCEL_OP;
}

extern "C" PROXY_EXPORT int __stdcall
ContinuaFuncaoSiTefInterativo(int* command, long* fieldType, short* minLength, short* maxLength, char* buffer, int bufferSize, int continua) {
    if (g_active) Log("CONT IN cont=%d st=%d", continua, (int)g_st);
    if (!g_active) {
        // fluxo libenv (funções não-Pix)
        InitLibEnv();
        return g_ConSiTef ? g_ConSiTef(command, fieldType, minLength, maxLength, buffer, bufferSize, continua) : SITEF_OK;
    }

    if (continua == -1) {
        Log("CONT CANCEL (continua=-1)");
        PixClose();
        g_active = false; g_st = S_IDLE;
        CopyOut(command, fieldType, minLength, maxLength, buffer, bufferSize, "", 0, 0);
        return SITEF_CANCEL_OP;
    }

    if (g_st == S_DONE) {
        if (g_receiptPos < g_receiptQueue.size()) {
            auto& f = g_receiptQueue[g_receiptPos];
            CopyOut(command, fieldType, minLength, maxLength, buffer, bufferSize, f.second, CMD_DATA, f.first);
            Log("RECEIPT[%zu/%zu] ft=%ld buf=\"%.140s\"", g_receiptPos + 1, g_receiptQueue.size(), f.first, f.second.c_str());
            g_receiptPos++;
            return SITEF_MOREDATA;
        }
        g_receiptQueue.clear(); g_receiptPos = 0;
        PixClose();
        g_active = false; g_evCmd = 0; g_evFt = 0; g_evData.clear();
        CopyOut(command, fieldType, minLength, maxLength, buffer, bufferSize, "", 0, 0);
        Log("CONTINUA -> FINAL terminalCode=%d", g_terminalCode);
        return g_terminalCode;
    }

    StepMachine();
    if (g_st == S_DONE && !g_receiptQueue.empty()) { g_evCmd = 0; g_evFt = 0; g_evData.clear(); }
    CopyOut(command, fieldType, minLength, maxLength, buffer, bufferSize, g_evData, g_evCmd, g_evFt);
    Log("CONT OUT cmd=%d ft=%ld ret=10000", g_evCmd, g_evFt);
    return SITEF_MOREDATA;
}

extern "C" PROXY_EXPORT int __stdcall
FinalizaFuncaoSiTefInterativo(int confirm, char* receipt, char* date, char* time, char* additionalParams) {
    if (!g_active) {
        InitLibEnv();
        return g_FinSiTef ? g_FinSiTef(confirm, receipt, date, time, additionalParams) : SITEF_OK;
    }
    PixClose();
    g_active = false; g_st = S_IDLE;
    return SITEF_OK;
}

// variantes delegam
extern "C" PROXY_EXPORT int __stdcall IniciaFuncaoSiTefInterativoA(int f, char* v, char* r, char* d, char* t, char* o, void* ap, void* extra) { return IniciaFuncaoSiTefInterativo(f, v, r, d, t, o, ap); }
extern "C" PROXY_EXPORT int __stdcall ContinuaFuncaoSiTefInterativoA(int* c, long* ft, short* mn, short* mx, char* b, int sz, int cont, void* extra) { return ContinuaFuncaoSiTefInterativo(c, ft, mn, mx, b, sz, cont); }
extern "C" PROXY_EXPORT int __stdcall FinalizaFuncaoSiTefInterativoA(int conf, char* r, char* d, char* t, void* ap) { return FinalizaFuncaoSiTefInterativo(conf, r, d, t, (char*)ap); }
extern "C" PROXY_EXPORT int __stdcall csiIniciaFuncaoSiTefInterativo(int f, char* v, char* r, char* d, char* t, char* o, void* ap, void* extra) { return IniciaFuncaoSiTefInterativo(f, v, r, d, t, o, ap); }
extern "C" PROXY_EXPORT int __stdcall csiContinuaFuncaoSiTefInterativo(int* c, long* ft, short* mn, short* mx, char* b, int sz, int cont, void* extra) { return ContinuaFuncaoSiTefInterativo(c, ft, mn, mx, b, sz, cont); }
extern "C" PROXY_EXPORT int __stdcall csiFinalizaFuncaoSiTefInterativo(int conf, char* r, char* d, char* t, void* ap, void* extra) { return FinalizaFuncaoSiTefInterativo(conf, r, d, t, (char*)ap); }

BOOL WINAPI DllMain(HINSTANCE i, DWORD r, LPVOID p) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(i);
        InitializeCriticalSection(&g_loadCs);
        InitializeCriticalSection(&g_logCs);
        GetModuleFileNameA(i, g_dllDir, MAX_PATH);
        char* s = strrchr(g_dllDir, '\\');
        if (s) *(s + 1) = 0;
        InitCleanStubs();
        Log("ATTACH dir=%s", g_dllDir);
    }
    return TRUE;
}
