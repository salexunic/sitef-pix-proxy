// test_cupom.cpp — imprime um comprovante Pix FAKE na impressora térmica (ESC/POS).
// Uso:
//   test_cupom.exe              -> impressora padrão do Windows
//   test_cupom.exe COM3         -> porta serial/USB (raw)
//   test_cupom.exe "Minha Imp." -> impressora pelo nome (spooler raw)
// O cupom usa os MESMOS campos da proxy (FT 105/121/122/123/133/135), com \n.
#include <windows.h>
#include <winspool.h>
#include <stdio.h>
#include <string>

#pragma comment(lib, "winspool.lib")

static std::string buildCupom() {
    // mesmo formato do buildCupom da proxy (com dados fake p/ conferir o layout)
    auto center = [](const std::string& s, int w) {
        if ((int)s.size() >= w) return s;
        int pad = (w - (int)s.size()) / 2;
        return std::string(pad, ' ') + s;
    };
    std::string c;
    c += center("VENDA PIX COMPRA", 42) + "\n";
    c += center("VIA - ESTABELECIMENTO", 42) + "\n";
    c += "\n";
    c += center("12.892.415/0006-00", 42) + "\n";
    c += "ESTAB: 012892415000600  TERM: 00000102\n";
    c += "AUT-SE000120006000HDOYBT750X23XOB95ZWQ\n";
    c += "CV-000858369920    DOC-000858369920\n";
    c += "09/09/26            18:19:34\n";
    c += "VALOR TOTAL         R$ 285.42\n";
    c += "\n";
    c += center("Transacao Pix Autorizada", 42) + "\n";
    c += center("SiTef from Fiserv", 42) + "\n";
    return c;
}

static std::string escpos(const std::string& text) {
    // ESC @ (init) + texto + \n\n\n + GS V (corte parcial)
    std::string out;
    out.push_back(0x1B); out.push_back(0x40);           // ESC @
    out += text;
    out += "\n\n\n";
    out.push_back(0x1D); out.push_back(0x56); out.push_back(0x42); out.push_back(0x00); // GS V B 0
    return out;
}

// imprime cru na porta COM (USB-serial). Retorna 0 ok.
static int printCom(const char* port, const std::string& data) {
    std::string path = "\\\\.\\" + std::string(port);
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { fprintf(stderr, "ERRO: nao abriu %s (err=%lu)\n", path.c_str(), GetLastError()); return 1; }
    DCB dcb; memset(&dcb, 0, sizeof(dcb)); dcb.DCBlength = sizeof(DCB);
    if (GetCommState(h, &dcb)) { dcb.BaudRate = CBR_9600; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT; SetCommState(h, &dcb); }
    DWORD w = 0;
    WriteFile(h, data.data(), (DWORD)data.size(), &w, NULL);
    CloseHandle(h);
    printf("COM %s: %lu bytes enviados.\n", port, w);
    return 0;
}

// imprime cru via spooler (impressora Windows). Retorna 0 ok.
static int printPrinter(const char* name, const std::string& data) {
    fprintf(stderr, "debug: nome=\"%s\" (len=%zu)\n", name, strlen(name));
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    std::wstring wname(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, name, -1, &wname[0], wlen);
    HANDLE h;
    if (!OpenPrinterW((LPWSTR)wname.c_str(), &h, NULL)) { fprintf(stderr, "ERRO: nao abriu impressora \"%s\" (err=%lu)\n", name, GetLastError()); return 1; }
    DOC_INFO_1W di; memset(&di, 0, sizeof(di));
    di.pDocName = (LPWSTR)L"Cupom Pix Teste";
    di.pDatatype = (LPWSTR)L"RAW";
    DWORD job = StartDocPrinterW(h, 1, (LPBYTE)&di);
    if (!job) { fprintf(stderr, "ERRO: StartDocPrinter falhou (err=%lu)\n", GetLastError()); ClosePrinter(h); return 1; }
    StartPagePrinter(h);
    DWORD w = 0;
    WritePrinter(h, (void*)data.data(), (DWORD)data.size(), &w);
    EndPagePrinter(h);
    EndDocPrinter(h);
    ClosePrinter(h);
    printf("Impressora \"%s\": %lu bytes enviados.\n", name, w);
    return 0;
}

int main(int argc, char** argv) {
    std::string cupom = escpos(buildCupom());

    if (argc >= 2) {
        std::string target = argv[1];
        std::string up = target;
        for (size_t i = 0; i < up.size(); i++) if (up[i] >= 'a' && up[i] <= 'z') up[i] = up[i] - 'a' + 'A';
        // detecta COM: "COM3" / "3" / "\\.\COM3"
        size_t p = up.find_last_of("\\");
        std::string tail = (p != std::string::npos) ? up.substr(p + 1) : up;
        if (tail.find("COM") == 0 || (tail.size() >= 1 && tail.size() <= 3 && tail[0] >= '0' && tail[0] <= '9')) {
            std::string port = tail;
            if (port.find("COM") != 0) port = "COM" + port;
            return printCom(port.c_str(), cupom);
        }
        return printPrinter(target.c_str(), cupom);
    }

    // sem arg: impressora padrão
    char defName[256] = "";
    DWORD sz = sizeof(defName);
    if (!GetDefaultPrinterA(defName, &sz) || defName[0] == 0) {
        fprintf(stderr, "Nenhuma impressora padrao. Passe a porta COM ou o nome da impressora.\n");
        return 1;
    }
    printf("Impressora padrao: %s\n", defName);
    return printPrinter(defName, cupom);
}
