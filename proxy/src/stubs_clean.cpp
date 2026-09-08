// stubs_clean.cpp — forwarders p/ funcoes "clean-only" da libenv.
// ---------------------------------------------------------------------------
// MSVC link.exe NAO aceita forwarder (.def) p/ nome LIMPO (so alvo decorado
// _Foo@N). Estas 10 funcoes nao tem forma decorada na libenv (exportadas so
// pelo nome limpo), entao usamos stub NAKED com jmp: passa TODOS os argumentos
// transparentemente, sem saber a assinatura (funciona p/ cdecl E stdcall).
// ---------------------------------------------------------------------------
#include <windows.h>

// exporta nome LIMPO -> simbolo cdecl do naked stub (linker aceita via pragma)
#pragma comment(linker, "/EXPORT:ConfiguraIntSiTefInterativoCdecl=_ConfiguraIntSiTefInterativoCdecl")
#pragma comment(linker, "/EXPORT:ContinuaFuncaoSiTefInterativoCdecl=_ContinuaFuncaoSiTefInterativoCdecl")
#pragma comment(linker, "/EXPORT:IniciaFuncaoSiTefInterativoCdecl=_IniciaFuncaoSiTefInterativoCdecl")
#pragma comment(linker, "/EXPORT:FinalizaTransacaoSiTefInterativoCdecl=_FinalizaTransacaoSiTefInterativoCdecl")
#pragma comment(linker, "/EXPORT:EscreveMensagemPermanentePinPadCdecl=_EscreveMensagemPermanentePinPadCdecl")
#pragma comment(linker, "/EXPORT:ObtemQuantidadeTransacoesPendentesCdecl=_ObtemQuantidadeTransacoesPendentesCdecl")
#pragma comment(linker, "/EXPORT:VerificaPresencaPinPadCdecl=_VerificaPresencaPinPadCdecl")
#pragma comment(linker, "/EXPORT:EfetuaPagamentoAASiTefInterativo=_EfetuaPagamentoAASiTefInterativo")
#pragma comment(linker, "/EXPORT:EfetuaPagamentoAASiTefInterativoA=_EfetuaPagamentoAASiTefInterativoA")
#pragma comment(linker, "/EXPORT:EfetuaPagamentoSiTefInterativoA=_EfetuaPagamentoSiTefInterativoA")

static FARPROC fp_ConfiguraIntSiTefInterativoCdecl;
static FARPROC fp_ContinuaFuncaoSiTefInterativoCdecl;
static FARPROC fp_IniciaFuncaoSiTefInterativoCdecl;
static FARPROC fp_FinalizaTransacaoSiTefInterativoCdecl;
static FARPROC fp_EscreveMensagemPermanentePinPadCdecl;
static FARPROC fp_ObtemQuantidadeTransacoesPendentesCdecl;
static FARPROC fp_VerificaPresencaPinPadCdecl;
static FARPROC fp_EfetuaPagamentoAASiTefInterativo;
static FARPROC fp_EfetuaPagamentoAASiTefInterativoA;
static FARPROC fp_EfetuaPagamentoSiTefInterativoA;

// resolve todas de uma vez (chamado do DllMain do proxy.cpp)
extern "C" void InitCleanStubs(void) {
    HMODULE h = GetModuleHandleA("libenv.dll");
    if (!h) h = LoadLibraryA("libenv.dll");
    if (!h) return;
    #define R(p, n) fp_##p = GetProcAddress(h, n)
    R(ConfiguraIntSiTefInterativoCdecl, "ConfiguraIntSiTefInterativoCdecl");
    R(ContinuaFuncaoSiTefInterativoCdecl, "ContinuaFuncaoSiTefInterativoCdecl");
    R(IniciaFuncaoSiTefInterativoCdecl, "IniciaFuncaoSiTefInterativoCdecl");
    R(FinalizaTransacaoSiTefInterativoCdecl, "FinalizaTransacaoSiTefInterativoCdecl");
    R(EscreveMensagemPermanentePinPadCdecl, "EscreveMensagemPermanentePinPadCdecl");
    R(ObtemQuantidadeTransacoesPendentesCdecl, "ObtemQuantidadeTransacoesPendentesCdecl");
    R(VerificaPresencaPinPadCdecl, "VerificaPresencaPinPadCdecl");
    R(EfetuaPagamentoAASiTefInterativo, "EfetuaPagamentoAASiTefInterativo");
    R(EfetuaPagamentoAASiTefInterativoA, "EfetuaPagamentoAASiTefInterativoA");
    R(EfetuaPagamentoSiTefInterativoA, "EfetuaPagamentoSiTefInterativoA");
    #undef R
}

extern "C" __declspec(naked) void ConfiguraIntSiTefInterativoCdecl(void){
__asm{ mov eax, fp_ConfiguraIntSiTefInterativoCdecl
       jmp eax }
}
extern "C" __declspec(naked) void ContinuaFuncaoSiTefInterativoCdecl(void){
__asm{ mov eax, fp_ContinuaFuncaoSiTefInterativoCdecl
       jmp eax }
}
extern "C" __declspec(naked) void IniciaFuncaoSiTefInterativoCdecl(void){
__asm{ mov eax, fp_IniciaFuncaoSiTefInterativoCdecl
       jmp eax }
}
extern "C" __declspec(naked) void FinalizaTransacaoSiTefInterativoCdecl(void){
__asm{ mov eax, fp_FinalizaTransacaoSiTefInterativoCdecl
       jmp eax }
}
extern "C" __declspec(naked) void EscreveMensagemPermanentePinPadCdecl(void){
__asm{ mov eax, fp_EscreveMensagemPermanentePinPadCdecl
       jmp eax }
}
extern "C" __declspec(naked) void ObtemQuantidadeTransacoesPendentesCdecl(void){
__asm{ mov eax, fp_ObtemQuantidadeTransacoesPendentesCdecl
       jmp eax }
}
extern "C" __declspec(naked) void VerificaPresencaPinPadCdecl(void){
__asm{ mov eax, fp_VerificaPresencaPinPadCdecl
       jmp eax }
}
extern "C" __declspec(naked) void EfetuaPagamentoAASiTefInterativo(void){
__asm{ mov eax, fp_EfetuaPagamentoAASiTefInterativo
       jmp eax }
}
extern "C" __declspec(naked) void EfetuaPagamentoAASiTefInterativoA(void){
__asm{ mov eax, fp_EfetuaPagamentoAASiTefInterativoA
       jmp eax }
}
extern "C" __declspec(naked) void EfetuaPagamentoSiTefInterativoA(void){
__asm{ mov eax, fp_EfetuaPagamentoSiTefInterativoA
       jmp eax }
}
