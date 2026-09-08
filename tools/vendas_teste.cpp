// vendas_teste.cpp — PDV de teste p/ o fluxo Pix (func 7) contra a proxy
// CliSiTef32I.dll. Self-contained: LoadLibrary + Configura + Inicia func=7
// (carteira digital/Pix) + loop de Continua. Sem sGestao.
//
// Ajustes vs o vendas_teste original (crédito):
//   - func 2 -> 7  (venda carteira digital/Pix)
//   - ParamAdic "" -> "{DevolveStringQRCode=1}"  => QR volta como STRING no
//     TypeField 584 (em vez de exibir no PinPad). Automação renderiza.
//   - loop detecta o QR (tc==584 ou payload "000201...") e imprime em destaque.
//
// Uso: vendas_teste.exe [segundos]   (default 120 — tempo pro cliente pagar)
// DLL carregada de C:\SORIODEV\SORGES\BIN (a proxy buildada vai pra lá).
//
// TODO(confirmar doc CliSiTef func 7): onde exatamente vai o cnpj_automacao
// (campo obrigatório Pix). Hoje o cfg já leva CNPJs no ParmsClient; validar se
// o Pix exige campo próprio no Configura ou no ParamAdic antes do go-live.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (__stdcall *F_CFGEX)(const char*, const char*, const char*, const char*, const char*);
typedef int (__stdcall *F_INI)(int, const char*, const char*, const char*, const char*, const char*, const char*);
typedef int (__stdcall *F_CON)(int*, long*, short*, short*, char*, int, int);
typedef int (__stdcall *F_FIN)(short, const char*, const char*, const char*);

// Loja/terminal REGISTRADOS no gateway (mesmos do test_capture validado).
#define LOJA     "00002390"
#define TERMINAL "SC000002"

// Func Pix: 7 = venda carteira digital, 8 = cancelamento carteira digital.
#define FUNC_PIX 7

static int is_qr(const char* buf, long tc) {
    if (!buf || !buf[0]) return 0;
    if (tc == 584) return 1;                                   // TypeField 584 = string do QR
    return strncmp(buf, "000201", 6) == 0;                     // payload Pix (copia-e-cola)
}

int main(int argc, char** argv){
    int secs = argc > 1 ? atoi(argv[1]) : 120;
    printf("[TEST] carregando C:\\SORIODEV\\SORGES\\BIN\\CliSiTef32I.dll\n");
    HMODULE dll = LoadLibraryA("C:\\SORIODEV\\SORGES\\BIN\\CliSiTef32I.dll");
    if(!dll){ printf("[ERRO] DLL nao carregou (%lu) — sGestao aberto? path errado?\n", GetLastError()); return 1; }
    F_CFGEX cfg = (F_CFGEX)GetProcAddress(dll, "ConfiguraIntSiTefInterativoEx");
    F_INI   ini = (F_INI)GetProcAddress(dll, "IniciaFuncaoSiTefInterativo");
    F_CON   con = (F_CON)GetProcAddress(dll, "ContinuaFuncaoSiTefInterativo");
    F_FIN   fin = (F_FIN)GetProcAddress(dll, "FinalizaTransacaoSiTefInterativo");
    if(!cfg || !ini || !con || !fin){ printf("[ERRO] exports faltando\n"); return 1; }

    int rc = cfg("tls-prod.fiservapp.com", LOJA, TERMINAL, NULL,
        "[PortaPinPad=8;MultiplosCupons=1;VersaoAutomacaoCielo=SORGE2023;TipoComunicacaoExterna=TLSGWP];[ParmsClient=1=10387986000158;2=05068435000191]");
    printf("[CFG] ret=%d\n", rc);

    SYSTEMTIME st; GetLocalTime(&st);
    char data[16], hora[16], cupom[32];
    sprintf_s(data,  sizeof(data),  "%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
    sprintf_s(hora,  sizeof(hora),  "%02d%02d%02d", st.wHour, st.wMinute, st.wSecond);
    sprintf_s(cupom, sizeof(cupom), "%04d%02d%02d%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    // func=PIX, valor "1,00", ParamAdic={DevolveStringQRCode=1}
    rc = ini(FUNC_PIX, "1,00", cupom, data, hora, "TESTE", "{DevolveStringQRCode=1}");
    printf("[INI] pix func=%d ret=%d\n", FUNC_PIX, rc);
    if(rc != 10000){ fin(0, cupom, data, hora); printf("[FIM] Inicia falhou (func 7 habilitado? TransacoesAdicionaisHabilitadas=7;8 no ini?)\n"); return 1; }

    printf("[PIX] aguardando QR / status do fluxo...\n");
    int cmd = 0; long tc = 0; short mn = 0, mx = 0;
    char buf[32768] = {0};
    char prev[128] = "";
    DWORD t0 = GetTickCount();
    while(GetTickCount() - t0 < (DWORD)secs * 1000){
        int st = con(&cmd, &tc, &mn, &mx, buf, 32768, 0);
        if(st != 10000){
            printf("[CONT] FIM st=%d cmd=%d tc=%ld buf='%.120s'\n", st, cmd, tc, buf);
            break;
        }
        if(buf[0] && strncmp(buf, prev, sizeof(prev)-1) != 0){
            strncpy_s(prev, sizeof(prev), buf, _TRUNCATE);
            if(is_qr(buf, tc)){
                printf("\n════════════════════════════════════════════════\n");
                printf("[QR] tc=%ld — RENDERIZE este copia-e-cola:\n%s\n", tc, buf);
                printf("════════════════════════════════════════════════\n\n");
            } else {
                printf("[RESP] cmd=%d tc=%ld buf='%.120s'\n", cmd, tc, buf);
            }
        }
        // Menu residual (defensivo): responde "1" se aparecer opção (à vista).
        if((cmd == 4 || cmd == 21) && buf[0]){
            strcpy_s(buf, sizeof(buf), "1");
            printf("[RESP] menu respondido: 1\n");
        }
        Sleep(30);
    }
    fin(0, cupom, data, hora);
    printf("[FIM] simulacao encerrada — confere painel/coletas.log/flow_trace\n");
    return 0;
}
