// CliSiTef32I proxy p/ Pix — intercepta func 7/8 e roteia pro pix server (TCP).
// Todo o resto -> forward libenv. Sem PGWebLib, sem pinpad.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "config.h"
#undef ERROR  // windows.h define ERROR=0 (wingdi.h); colide com PixStatus::ERROR
#include "pix_client.h"
#define PROXY_EXPORT

extern "C" void InitCleanStubs(void);

#define SITEF_OK 0
#define SITEF_MOREDATA 10000
#define SITEF_CANCEL_OP -2
#define SITEF_ERR -100

#define CMD_DATA 0
#define CMD_DISPLAY_BOTH 3
#define FT_DATETIME 105L
#define FT_NSU_SITEF 133L
#define FT_AUTH 135L
#define FT_QRCODE 584L

typedef int (__stdcall *fnSiTefIni)(int,char*,char*,char*,char*,char*,void*);
typedef int (__stdcall *fnSiTefCon)(int*,long*,short*,short*,char*,int,int);
typedef int (__stdcall *fnSiTefFin)(int,char*,char*,char*,char*);

static HMODULE g_libenv = NULL;
static fnSiTefIni g_IniSiTef = NULL;
static fnSiTefCon g_ConSiTef = NULL;
static fnSiTefFin g_FinSiTef = NULL;
static CRITICAL_SECTION g_loadCs;
static char g_dllDir[MAX_PATH] = "";

enum St { S_IDLE, S_CONNECT, S_POLL, S_DONE };
static St g_st = S_IDLE;
static bool g_active = false, g_forwardMode = false, g_handledTransaction = false;
static int g_lastFunc = 0;
static long g_amountCents = 0;
static int g_terminalCode = SITEF_ERR;

static SOCKET g_sock = INVALID_SOCKET;
static std::string g_txid, g_qr;
static std::string g_auth, g_nsu, g_datetime;

static int g_evCmd = 0; static long g_evFt = 0; static std::string g_evData;
static std::vector<std::pair<long,std::string>> g_receiptQueue; static size_t g_receiptPos = 0;

static void SetEv(int cmd, long ft, const std::string& d) { g_evCmd = cmd; g_evFt = ft; g_evData = d; }

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

// ── TCP pix server ──
static bool PixConnect() {
    if (g_sock != INVALID_SOCKET) return true;
    static bool wsa = false;
    if (!wsa) { WSADATA d; WSAStartup(MAKEWORD(2,2), &d); wsa = true; }
    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_sock == INVALID_SOCKET) return false;
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = inet_addr(CFG_PIX_HOST); a.sin_port = htons(CFG_PIX_PORT);
    if (connect(g_sock, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) { closesocket(g_sock); g_sock = INVALID_SOCKET; return false; }
    int to = 5000; setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
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
        if (n == 0) return false;         // EOF
        if (n < 0) { if (WSAGetLastError() == WSAETIMEDOUT) return false; continue; }
        if (c == '\n') break;
        out.push_back(c);
    }
    return !out.empty();
}

static void PixClose() { if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; } }

static void ParseAmount(const char* v) {
    std::string d;
    if (v) for (; *v; ++v) if (*v >= '0' && *v <= '9') d.push_back(*v);
    g_amountCents = d.empty() ? 100 : atol(d.c_str());
}

// ── maquina de estados ──
static void StepMachine() {
    switch (g_st) {
    case S_CONNECT: {
        std::string resp;
        if (!PixConnect()) { g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Falha ao conectar servidor Pix"); break; }
        char id[32]; snprintf(id, sizeof(id), "%08X", (unsigned)GetTickCount());
        std::string req = buildCreate(id, g_amountCents, "00000000000000");
        if (!PixSend(req) || !PixRecvLine(resp) || !parseOkCreate(resp, g_txid, g_qr)) {
            g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Falha ao criar transacao Pix");
            break;
        }
        SetEv(CMD_DATA, FT_QRCODE, g_qr);   // QR via TypeField 584
        g_st = S_POLL;
        break;
    }
    case S_POLL: {
        std::string resp;
        if (!PixSend("STATUS " + g_txid + "\n") || !PixRecvLine(resp)) {
            g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Falha ao consultar Pix");
            break;
        }
        PixStatusResult ps;
        if (!parseStatus(resp, ps)) { g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Resposta Pix invalida"); break; }
        switch (ps.status) {
        case PixStatus::PEN:
            SetEv(CMD_DISPLAY_BOTH, 0, "Aguardando pagamento Pix...");
            break;
        case PixStatus::APPROVED:
            g_auth = ps.auth; g_nsu = ps.nsu; g_datetime = ps.datetime;
            g_receiptQueue.clear();
            g_receiptQueue.push_back({FT_DATETIME, g_datetime});
            g_receiptQueue.push_back({FT_NSU_SITEF, g_nsu});
            g_receiptQueue.push_back({FT_AUTH, g_auth});
            g_receiptPos = 0;
            g_terminalCode = SITEF_OK; g_st = S_DONE;
            break;
        case PixStatus::CANCELED:
            g_terminalCode = SITEF_CANCEL_OP; g_st = S_DONE;
            break;
        case PixStatus::TIMEOUT:
            g_terminalCode = SITEF_ERR; g_st = S_DONE; SetEv(CMD_DISPLAY_BOTH, 0, "Pix expirado");
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
extern "C" PROXY_EXPORT int __stdcall
IniciaFuncaoSiTefInterativo(int function, char* value, char* receipt, char* date, char* time, char* operatorCode, void* additionalParams) {
    if (function != 7 && function != 8) {
        InitLibEnv();
        int ret = g_IniSiTef ? g_IniSiTef(function, value, receipt, date, time, operatorCode, additionalParams) : SITEF_CANCEL_OP;
        g_forwardMode = (ret == SITEF_MOREDATA);
        return ret;
    }
    g_handledTransaction = true; g_active = true; g_forwardMode = false; g_lastFunc = function;
    ParseAmount(value);
    g_terminalCode = SITEF_ERR; g_st = S_CONNECT;
    g_evCmd = 0; g_evFt = 0; g_evData.clear();
    g_receiptQueue.clear(); g_receiptPos = 0;
    g_txid.clear(); g_qr.clear();
    PixClose();
    return SITEF_MOREDATA;
}

extern "C" PROXY_EXPORT int __stdcall
ContinuaFuncaoSiTefInterativo(int* command, long* fieldType, short* minLength, short* maxLength, char* buffer, int bufferSize, int continua) {
    if (!g_active) {
        if (g_forwardMode) {
            InitLibEnv();
            int ret = g_ConSiTef ? g_ConSiTef(command, fieldType, minLength, maxLength, buffer, bufferSize, continua) : SITEF_OK;
            if (ret != SITEF_MOREDATA) g_forwardMode = false;
            return ret;
        }
        if (g_handledTransaction) {
            CopyOut(command, fieldType, minLength, maxLength, buffer, bufferSize, "", 0, 0);
            return SITEF_OK;
        }
        InitLibEnv();
        return g_ConSiTef ? g_ConSiTef(command, fieldType, minLength, maxLength, buffer, bufferSize, continua) : SITEF_OK;
    }

    if (continua == -1) {
        PixClose();
        g_active = false; g_st = S_IDLE;
        CopyOut(command, fieldType, minLength, maxLength, buffer, bufferSize, "", 0, 0);
        return SITEF_CANCEL_OP;
    }

    if (g_st == S_DONE) {
        if (g_receiptPos < g_receiptQueue.size()) {
            auto& f = g_receiptQueue[g_receiptPos];
            CopyOut(command, fieldType, minLength, maxLength, buffer, bufferSize, f.second, CMD_DATA, f.first);
            g_receiptPos++;
            return SITEF_MOREDATA;
        }
        g_receiptQueue.clear(); g_receiptPos = 0;
        PixClose();
        g_active = false; g_evCmd = 0; g_evFt = 0; g_evData.clear();
        CopyOut(command, fieldType, minLength, maxLength, buffer, bufferSize, "", 0, 0);
        return g_terminalCode;
    }

    StepMachine();

    if (g_st == S_DONE && !g_receiptQueue.empty()) {
        // aprovação: não há evento de display; deixa o PDV chamar de novo p/ drenar comprovante
        g_evCmd = 0; g_evFt = 0; g_evData.clear();
    }
    CopyOut(command, fieldType, minLength, maxLength, buffer, bufferSize, g_evData, g_evCmd, g_evFt);
    return SITEF_MOREDATA;
}

extern "C" PROXY_EXPORT int __stdcall
FinalizaFuncaoSiTefInterativo(int confirm, char* receipt, char* date, char* time, char* additionalParams) {
    if (!g_active && !g_handledTransaction) {
        if (g_forwardMode) {
            InitLibEnv();
            int ret = g_FinSiTef ? g_FinSiTef(confirm, receipt, date, time, additionalParams) : SITEF_OK;
            g_forwardMode = false;
            return ret;
        }
        InitLibEnv();
        return g_FinSiTef ? g_FinSiTef(confirm, receipt, date, time, additionalParams) : SITEF_OK;
    }
    if (g_handledTransaction) {
        PixClose();
        g_active = false; g_handledTransaction = false; g_st = S_IDLE;
        return SITEF_OK;
    }
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
        GetModuleFileNameA(i, g_dllDir, MAX_PATH);
        char* s = strrchr(g_dllDir, '\\');
        if (s) *(s + 1) = 0;
        InitCleanStubs();
    }
    return TRUE;
}
