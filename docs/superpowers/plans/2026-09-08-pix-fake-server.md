# Servidor Pix Fake — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Construir `pixserver.exe` — servidor TCP (C++ Winsock) que simula um PSP Pix: cria transação, gera QR copia-e-cola (EMV), e responde polling de status (PEN → APPROVED/CANCELED/TIMEOUT).

**Architecture:** Texto puro sobre TCP (`\n`-delimited). Camadas puras (`protocol`, `pix_core`) testáveis sem socket + uma camada Winsock (`server`). Zero dependência externa, zero pinpad, zero PGWebLib. Thread por conexão + `CRITICAL_SECTION` no mapa de transações.

**Tech Stack:** C++20, MSVC x86, Winsock2, build via `build.bat` (vcvarsall, igual paygo-proxy). Teste = harness próprio (`CHECK` macro) em exe separado, sem framework.

**Spec:** `docs/superpowers/specs/2026-09-08-pix-fake-server-design.md`

## Global Constraints

- C++20, compilado com `/std:c++20 /O2 /MT /W3 /EHsc` (padrão paygo-proxy).
- x86 (MACHINE:X86), console subsystem.
- Porta default `31736` (compile-time `CFG_PORT`), bind `0.0.0.0`.
- `CFG_AUTO_APPROVE_MS` = 5000, `CFG_TIMEOUT_MS` = 180000 (compile-time).
- CRC16-CCITT-FALSE (poly `0x1021`, init `0xFFFF`, sem reflexão, sem XOR final).
- Protocolo: linha de texto terminada em `\n`, `\r` tolerado no fim.
- **Sem git repo** — sem steps de commit. Cada task termina em checkpoint de verificação (compilar + rodar teste + conferir saída). Commit só se o usuário pedir.
- Strings de resposta sempre terminam em `\n`.

---

### Task 1: Scaffold + CRC16 (primeiro teste)

**Files:**
- Create: `src/config.h`
- Create: `src/pix_core.h`
- Create: `src/pix_core.cpp`
- Create: `tests/test_pix.cpp`
- Create: `build.bat`

**Interfaces:**
- Produces: `unsigned short crc16Ccitt(const std::string& data);` (em `pix_core.h`)

- [ ] **Step 1: `src/config.h`**

```cpp
#pragma once
#ifndef CFG_PORT
#define CFG_PORT 31736
#endif
#ifndef CFG_AUTO_APPROVE_MS
#define CFG_AUTO_APPROVE_MS 5000
#endif
#ifndef CFG_TIMEOUT_MS
#define CFG_TIMEOUT_MS 180000
#endif
#ifndef CFG_MERCHANT_NAME
#define CFG_MERCHANT_NAME "PIX FAKE TEST"
#endif
#ifndef CFG_MERCHANT_CITY
#define CFG_MERCHANT_CITY "SAO PAULO"
#endif
```

- [ ] **Step 2: `src/pix_core.h` (só o crc por enquanto)**

```cpp
#pragma once
#include <string>

// CRC16-CCITT-FALSE (polinomio 0x1021, init 0xFFFF, sem reflexao, sem XOR final).
// Padrao usado no CRC do BR Code do Pix.
unsigned short crc16Ccitt(const std::string& data);
```

- [ ] **Step 3: `tests/test_pix.cpp` (harness + teste CRC)**

```cpp
#include <cstdio>
#include <string>
#include "pix_core.h"

static int g_failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static void testCrc() {
    // Vetor conhecido: CRC-16/CCITT-FALSE("123456789") == 0x29B1
    CHECK(crc16Ccitt("123456789") == 0x29B1);
    // Caso base vazio
    CHECK(crc16Ccitt("") == 0xFFFF);
}

int main() {
    testCrc();
    if (g_failures == 0) { printf("ALL TESTS PASSED\n"); return 0; }
    printf("%d FAILURES\n", g_failures);
    return 1;
}
```

- [ ] **Step 4: `src/pix_core.cpp` (implementa crc)**

```cpp
#include "pix_core.h"

unsigned short crc16Ccitt(const std::string& data) {
    unsigned short crc = 0xFFFF;
    for (unsigned char c : data) {
        crc ^= (unsigned short)(c << 8);
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000) crc = (unsigned short)((crc << 1) ^ 0x1021);
            else crc = (unsigned short)(crc << 1);
        }
    }
    return crc;
}
```

- [ ] **Step 5: `build.bat` (target de teste)**

```bat
@echo off
setlocal EnableExtensions DisableDelayedExpansion

if /I "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :toolchain_ready

set "VCVARS="
if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%I"
    if defined VSROOT set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat"
)
if not defined VCVARS (
    echo ERRO: vcvarsall.bat nao encontrado.
    exit /b 2
)
call "%VCVARS%" x86
if errorlevel 1 ( echo ERRO: vcvarsall.bat falhou. & exit /b 2 )

:toolchain_ready
set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "TEST=%ROOT%tests"
set "OUT=%ROOT%build"
if not exist "%OUT%" mkdir "%OUT%"

echo [1/2] Compilando testes...
cl.exe /nologo /std:c++20 /O2 /MT /W3 /EHsc /I"%SRC%" "%SRC%\pix_core.cpp" "%TEST%\test_pix.cpp" /Fe:"%OUT%\pixcore_test.exe" /Fo:"%OUT%\\" /link /SUBSYSTEM:CONSOLE kernel32.lib
if errorlevel 1 ( echo ERRO: compilacao teste falhou. & exit /b 1 )

echo [2/2] Rodando testes...
"%OUT%\pixcore_test.exe"
exit /b %ERRORLEVEL%
```

- [ ] **Step 6: Compilar + rodar**

Run: `build.bat`
Expected: `ALL TESTS PASSED`, exit 0.

- [ ] **Step 7: Checkpoint** — confirma que `build\pixcore_test.exe` foi gerado e rodou.

---

### Task 2: Geração do QR EMV

**Files:**
- Modify: `src/pix_core.h` (adiciona declaração)
- Modify: `src/pix_core.cpp` (implementa)
- Modify: `tests/test_pix.cpp` (adiciona teste)

**Interfaces:**
- Consumes: `crc16Ccitt` (Task 1).
- Produces: `std::string buildQrCode(const std::string& txid, long amountCents, const std::string& merchantName, const std::string& city);`

- [ ] **Step 1: adiciona teste em `tests/test_pix.cpp` (antes do `main`)**

```cpp
#include <cstring>

static void testQr() {
    std::string qr = buildQrCode("0123456789ABCDEF", 1000, "PIX FAKE TEST", "SAO PAULO");
    // comeca com 00 02 01 (payload format indicator)
    CHECK(qr.rfind("000201", 0) == 0);
    // contem o GUI do BCB e a chave (txid)
    CHECK(qr.find("BR.GOV.BCB.PIX") != std::string::npos);
    CHECK(qr.find("0123456789ABCDEF") != std::string::npos);
    // termina com "6304" + 4 hex, e o CRC confere
    CHECK(qr.size() > 8);
    std::string payload = qr.substr(0, qr.size() - 4);
    std::string crcStr  = qr.substr(qr.size() - 4);
    unsigned short crc = crc16Ccitt(payload);
    char want[8];
    snprintf(want, sizeof(want), "%04X", crc);
    CHECK(crcStr == std::string(want));
}
```

E em `main()`, adicione `testQr();` após `testCrc();`.

- [ ] **Step 2: rodar pra ver falhar (link não resolve `buildQrCode`)**

Run: `build.bat`
Expected: erro de link `unresolved external symbol buildQrCode`.

- [ ] **Step 3: declara em `src/pix_core.h`** (após a declaração de crc)

```cpp
// Monta o BR Code (copia-e-cola) de um QR Pix estatico.
std::string buildQrCode(const std::string& txid, long amountCents,
                        const std::string& merchantName, const std::string& city);
```

- [ ] **Step 4: implementa em `src/pix_core.cpp`** (após crc16Ccitt)

```cpp
#include <cstdio>

static std::string emvField(const std::string& id, const std::string& value) {
    char len[4];
    snprintf(len, sizeof(len), "%02u", (unsigned)value.size());
    return id + len + value;
}

std::string buildQrCode(const std::string& txid, long amountCents,
                        const std::string& merchantName, const std::string& city) {
    std::string gui = emvField("00", "BR.GOV.BCB.PIX") + emvField("01", txid);
    std::string qr;
    qr += emvField("00", "01");
    qr += emvField("26", gui);
    qr += emvField("52", "0000");
    qr += emvField("53", "986");
    if (amountCents > 0) {
        char amt[32];
        snprintf(amt, sizeof(amt), "%ld.%02ld", amountCents / 100, amountCents % 100);
        qr += emvField("54", amt);
    }
    qr += emvField("58", "BR");
    qr += emvField("59", merchantName);
    qr += emvField("60", city);
    qr += emvField("62", emvField("05", txid));
    unsigned short crc = crc16Ccitt(qr + "6304");
    char crcHex[8];
    snprintf(crcHex, sizeof(crcHex), "%04X", crc);
    qr += "6304";
    qr += crcHex;
    return qr;
}
```

- [ ] **Step 5: rodar pra ver passar**

Run: `build.bat`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 6: Checkpoint** — QR gerado tem CRC válido.

---

### Task 3: Protocolo (parser + responses)

**Files:**
- Create: `src/protocol.h`
- Create: `src/protocol.cpp`
- Modify: `tests/test_pix.cpp` (adiciona teste)
- Modify: `build.bat` (adiciona `protocol.cpp` na linha do cl)

**Interfaces:**
- Produces:
  - `bool parseCommand(const std::string& line, Command& out);`
  - `std::string respOkCreate(const std::string& txid, const std::string& qr);`
  - `std::string respErr(int code, const std::string& msg);`
  - `std::string respPen();`
  - `std::string respApproved(const std::string& auth, const std::string& nsu, const std::string& datetime);`
  - `std::string respCanceled(); std::string respDenied(const std::string& reason); std::string respTimeout(); std::string respOk();`

- [ ] **Step 1: `src/protocol.h`**

```cpp
#pragma once
#include <string>

enum class CmdType { UNKNOWN, CREATE, STATUS, CANCEL, PAY };

struct Command {
    CmdType type = CmdType::UNKNOWN;
    std::string id;        // CREATE: id do cliente (opcional)
    long amountCents = 0;  // CREATE
    std::string cnpj;      // CREATE
    std::string txid;      // STATUS/CANCEL/PAY
};

// Parseia uma linha (sem '\n'/'\r'). Retorna false se malformado.
// Comando desconhecido => true com out.type == UNKNOWN.
bool parseCommand(const std::string& line, Command& out);

// Builders de resposta (todas terminam em '\n').
std::string respOkCreate(const std::string& txid, const std::string& qr);
std::string respErr(int code, const std::string& msg);
std::string respPen();
std::string respApproved(const std::string& auth, const std::string& nsu, const std::string& datetime);
std::string respCanceled();
std::string respDenied(const std::string& reason);
std::string respTimeout();
std::string respOk();
```

- [ ] **Step 2: `src/protocol.cpp`**

```cpp
#include "protocol.h"
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace {
std::vector<std::string> splitWs(const std::string& s) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) { v.push_back(cur); cur.clear(); } }
        else cur.push_back(c);
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}
std::string upper(std::string s) {
    for (char& c : s) c = (char)::toupper((unsigned char)c);
    return s;
}
}

bool parseCommand(const std::string& line, Command& out) {
    out = Command();
    auto tok = splitWs(line);
    if (tok.empty()) return true; // linha vazia => UNKNOWN (server ignora)
    std::string c = upper(tok[0]);
    if (c == "CREATE") {
        if (tok.size() < 4) return false;
        out.type = CmdType::CREATE;
        out.id = tok[1];
        out.amountCents = strtol(tok[2].c_str(), nullptr, 10);
        out.cnpj = tok[3];
        return out.amountCents > 0;
    }
    if (c == "STATUS") { if (tok.size() < 2) return false; out.type = CmdType::STATUS; out.txid = tok[1]; return true; }
    if (c == "CANCEL") { if (tok.size() < 2) return false; out.type = CmdType::CANCEL; out.txid = tok[1]; return true; }
    if (c == "PAY")   { if (tok.size() < 2) return false; out.type = CmdType::PAY;   out.txid = tok[1]; return true; }
    out.type = CmdType::UNKNOWN;
    return true;
}

std::string respOkCreate(const std::string& txid, const std::string& qr) {
    char b[4096]; int n = snprintf(b, sizeof(b), "OK %s %s\n", txid.c_str(), qr.c_str());
    return std::string(b, n < 0 ? 0 : (size_t)n);
}
std::string respErr(int code, const std::string& msg) {
    char b[512]; int n = snprintf(b, sizeof(b), "ERR %d %s\n", code, msg.c_str());
    return std::string(b, n < 0 ? 0 : (size_t)n);
}
std::string respPen() { return "PEN\n"; }
std::string respApproved(const std::string& auth, const std::string& nsu, const std::string& datetime) {
    char b[256]; int n = snprintf(b, sizeof(b), "APPROVED %s %s %s\n", auth.c_str(), nsu.c_str(), datetime.c_str());
    return std::string(b, n < 0 ? 0 : (size_t)n);
}
std::string respCanceled() { return "CANCELED\n"; }
std::string respDenied(const std::string& reason) {
    char b[256]; int n = snprintf(b, sizeof(b), "DENIED %s\n", reason.c_str());
    return std::string(b, n < 0 ? 0 : (size_t)n);
}
std::string respTimeout() { return "TIMEOUT\n"; }
std::string respOk() { return "OK\n"; }
```

- [ ] **Step 3: adiciona teste em `tests/test_pix.cpp`**

```cpp
#include "protocol.h"

static void testParse() {
    Command c;
    CHECK(parseCommand("CREATE pedido1 1000 12345678000199", c));
    CHECK(c.type == CmdType::CREATE);
    CHECK(c.amountCents == 1000);
    CHECK(c.cnpj == "12345678000199");
    CHECK(parseCommand("STATUS 0123456789ABCDEF", c));
    CHECK(c.type == CmdType::STATUS && c.txid == "0123456789ABCDEF");
    CHECK(parseCommand("PAY 0123456789ABCDEF", c));
    CHECK(c.type == CmdType::PAY);
    CHECK(parseCommand("CANCEL 0123456789ABCDEF", c));
    CHECK(c.type == CmdType::CANCEL);
    // malformado: CREATE sem cnpj
    CHECK(!parseCommand("CREATE so 1000", c));
    // desconhecido
    CHECK(parseCommand("FOO bar", c));
    CHECK(c.type == CmdType::UNKNOWN);
}

static void testResp() {
    CHECK(respOkCreate("0123456789ABCDEF", "00020126QR") == "OK 0123456789ABCDEF 00020126QR\n");
    CHECK(respPen() == "PEN\n");
    CHECK(respApproved("123456", "000000000001", "20260908103000") == "APPROVED 123456 000000000001 20260908103000\n");
    CHECK(respErr(3, "not found") == "ERR 3 not found\n");
    CHECK(respCanceled() == "CANCELED\n");
    CHECK(respTimeout() == "TIMEOUT\n");
    CHECK(respOk() == "OK\n");
}
```

Em `main()`, adicione `testParse(); testResp();`.

- [ ] **Step 4: editar `build.bat`** — na linha do `cl.exe`, adicionar `"%SRC%\protocol.cpp"` antes de `"%SRC%\pix_core.cpp"`:

```
cl.exe /nologo /std:c++20 /O2 /MT /W3 /EHsc /I"%SRC%" "%SRC%\protocol.cpp" "%SRC%\pix_core.cpp" "%TEST%\test_pix.cpp" /Fe:"%OUT%\pixcore_test.exe" /Fo:"%OUT%\\" /link /SUBSYSTEM:CONSOLE kernel32.lib
```

- [ ] **Step 5: compilar + rodar**

Run: `build.bat`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 6: Checkpoint** — parser e builders corretos.

---

### Task 4: PixCore (estado + transações)

**Files:**
- Modify: `src/pix_core.h` (adiciona tipos + classe)
- Modify: `src/pix_core.cpp` (implementa)
- Modify: `tests/test_pix.cpp` (adiciona teste)

**Interfaces:**
- Consumes: `crc16Ccitt`, `buildQrCode` (Tasks 1–2), `CFG_AUTO_APPROVE_MS`/`CFG_TIMEOUT_MS`/`CFG_MERCHANT_NAME`/`CFG_MERCHANT_CITY` (`config.h`).
- Produces:
  - `enum class PixState { PEN, APPROVED, CANCELED, TIMEOUT, DENIED };`
  - `enum class OpResult { OK, NOT_FOUND, FINALIZED };`
  - `struct Transaction { ... };` (campos: txid, clientId, amountCents, cnpj, qr, state, auth, nsu, datetime, createdAtMs)
  - `class PixCore { PixCore(); ~PixCore(); std::string create(const std::string& clientId, long amountCents, const std::string& cnpj, DWORD nowMs); bool status(const std::string& txid, DWORD nowMs, Transaction* out); OpResult cancel(const std::string& txid); OpResult pay(const std::string& txid); };`

- [ ] **Step 1: adiciona teste em `tests/test_pix.cpp`**

```cpp
#include "config.h"

static void testState() {
    PixCore core;
    DWORD t0 = 1000;

    std::string tx = core.create("id1", 500, "12345678000199", t0);
    Transaction o;
    CHECK(core.status(tx, t0, &o));
    CHECK(o.state == PixState::PEN);
    CHECK(!o.qr.empty());

    // auto-approve quando elapsed >= CFG_AUTO_APPROVE_MS
    CHECK(core.status(tx, t0 + CFG_AUTO_APPROVE_MS, &o));
    CHECK(o.state == PixState::APPROVED);
    CHECK(o.auth.size() == 6);
    CHECK(o.nsu.size() == 12);
    CHECK(o.datetime.size() == 14);

    // cancel
    std::string tx2 = core.create("id2", 100, "12345678000199", t0);
    CHECK(core.cancel(tx2) == OpResult::OK);
    CHECK(core.status(tx2, t0, &o));
    CHECK(o.state == PixState::CANCELED);

    // pay manual (gatilho de teste)
    std::string tx3 = core.create("id3", 200, "12345678000199", t0);
    CHECK(core.pay(tx3) == OpResult::OK);
    CHECK(core.status(tx3, t0, &o));
    CHECK(o.state == PixState::APPROVED);

    // timeout (ordem: timeout vence auto-approve)
    std::string tx4 = core.create("id4", 300, "12345678000199", t0);
    CHECK(core.status(tx4, t0 + CFG_TIMEOUT_MS, &o));
    CHECK(o.state == PixState::TIMEOUT);

    // txid inexistente
    CHECK(!core.status("FFFFFFFFFFFFFFFF", t0, &o));
    // pay em finalizada
    CHECK(core.pay(tx) == OpResult::FINALIZED);
    // cancel em finalizada
    CHECK(core.cancel(tx3) == OpResult::FINALIZED);
}
```

Em `main()`, adicione `testState();`.

- [ ] **Step 2: rodar pra ver falhar (tipos inexistentes)**

Run: `build.bat`
Expected: erro de compilação `PixCore`/`PixState`/`OpResult` não declarados.

- [ ] **Step 3: `src/pix_core.h`** — substituir o conteúdo inteiro por:

```cpp
#pragma once
#include <string>
#include <map>
#include <windows.h>

// CRC16-CCITT-FALSE (polinomio 0x1021, init 0xFFFF, sem reflexao, sem XOR final).
unsigned short crc16Ccitt(const std::string& data);

// Monta o BR Code (copia-e-cola) de um QR Pix estatico.
std::string buildQrCode(const std::string& txid, long amountCents,
                        const std::string& merchantName, const std::string& city);

enum class PixState { PEN, APPROVED, CANCELED, TIMEOUT, DENIED };
enum class OpResult { OK, NOT_FOUND, FINALIZED };

struct Transaction {
    std::string txid;
    std::string clientId;
    long amountCents = 0;
    std::string cnpj;
    std::string qr;
    PixState state = PixState::PEN;
    std::string auth;
    std::string nsu;
    std::string datetime;
    DWORD createdAtMs = 0;
};

// Mapa de transacoes em memoria + maquina de estado. Thread-safe (CRITICAL_SECTION).
// Sem socket. nowMs = GetTickCount() injetado para testes deterministicos.
class PixCore {
public:
    PixCore();
    ~PixCore();
    std::string create(const std::string& clientId, long amountCents,
                       const std::string& cnpj, DWORD nowMs);
    bool status(const std::string& txid, DWORD nowMs, Transaction* out);
    OpResult cancel(const std::string& txid);
    OpResult pay(const std::string& txid);
private:
    std::map<std::string, Transaction> txs_;
    CRITICAL_SECTION cs_;
    unsigned long long nextId_ = 0;
};
```

- [ ] **Step 4: `src/pix_core.cpp`** — adicionar no topo `#include "config.h"`, e ao final:

```cpp
#include <map>

namespace {
void fillApproval(Transaction& t) {
    unsigned long long v = 0;
    for (char c : t.txid) {
        int d = (c >= '0' && c <= '9') ? (c - '0') : ((c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 0);
        v = v * 16 + (unsigned long long)d;
    }
    char auth[8]; snprintf(auth, sizeof(auth), "%06u", (unsigned)(v % 1000000));
    t.auth = auth;
    char nsu[16]; snprintf(nsu, sizeof(nsu), "%012llu", v % 1000000000000ULL);
    t.nsu = nsu;
    SYSTEMTIME st; GetLocalTime(&st);
    char dt[16]; snprintf(dt, sizeof(dt), "%04d%02d%02d%02d%02d%02d",
                          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    t.datetime = dt;
}
}

PixCore::PixCore()  { InitializeCriticalSection(&cs_); }
PixCore::~PixCore() { DeleteCriticalSection(&cs_); }

std::string PixCore::create(const std::string& clientId, long amountCents,
                            const std::string& cnpj, DWORD nowMs) {
    EnterCriticalSection(&cs_);
    ++nextId_;
    char txbuf[17]; snprintf(txbuf, sizeof(txbuf), "%016llX", nextId_);
    std::string txid(txbuf);
    Transaction t;
    t.txid = txid;
    t.clientId = clientId;
    t.amountCents = amountCents;
    t.cnpj = cnpj;
    t.qr = buildQrCode(txid, amountCents, CFG_MERCHANT_NAME, CFG_MERCHANT_CITY);
    t.state = PixState::PEN;
    t.createdAtMs = nowMs;
    txs_[txid] = t;
    LeaveCriticalSection(&cs_);
    return txid;
}

bool PixCore::status(const std::string& txid, DWORD nowMs, Transaction* out) {
    EnterCriticalSection(&cs_);
    auto it = txs_.find(txid);
    if (it == txs_.end()) { LeaveCriticalSection(&cs_); return false; }
    Transaction& t = it->second;
    if (t.state == PixState::PEN) {
        DWORD elapsed = nowMs - t.createdAtMs;
        if (elapsed >= (DWORD)CFG_TIMEOUT_MS) {
            t.state = PixState::TIMEOUT;
        } else if (elapsed >= (DWORD)CFG_AUTO_APPROVE_MS) {
            t.state = PixState::APPROVED;
            fillApproval(t);
        }
    }
    if (out) *out = t;
    LeaveCriticalSection(&cs_);
    return true;
}

OpResult PixCore::cancel(const std::string& txid) {
    EnterCriticalSection(&cs_);
    auto it = txs_.find(txid);
    if (it == txs_.end()) { LeaveCriticalSection(&cs_); return OpResult::NOT_FOUND; }
    if (it->second.state != PixState::PEN) { LeaveCriticalSection(&cs_); return OpResult::FINALIZED; }
    it->second.state = PixState::CANCELED;
    LeaveCriticalSection(&cs_);
    return OpResult::OK;
}

OpResult PixCore::pay(const std::string& txid) {
    EnterCriticalSection(&cs_);
    auto it = txs_.find(txid);
    if (it == txs_.end()) { LeaveCriticalSection(&cs_); return OpResult::NOT_FOUND; }
    if (it->second.state != PixState::PEN) { LeaveCriticalSection(&cs_); return OpResult::FINALIZED; }
    it->second.state = PixState::APPROVED;
    fillApproval(it->second);
    LeaveCriticalSection(&cs_);
    return OpResult::OK;
}
```

- [ ] **Step 5: compilar + rodar**

Run: `build.bat`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 6: Checkpoint** — máquina de estado cobre PEN→APPROVED (auto + manual), CANCELED, TIMEOUT, NOT_FOUND, FINALIZED.

---

### Task 5: Servidor Winsock + entry

**Files:**
- Create: `src/server.cpp`
- Create: `src/main.cpp`
- Modify: `build.bat` (adiciona target do exe servidor)

**Interfaces:**
- Consumes: `PixCore` (Task 4), `protocol` (Task 3), `CFG_PORT` (config.h).
- Produces: `pixserver.exe` (console).

- [ ] **Step 1: `src/server.cpp`**

```cpp
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <cstdio>
#include "config.h"
#include "protocol.h"
#include "pix_core.h"

static PixCore* g_core = nullptr;

static bool sendAll(SOCKET s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        int n = send(s, data.data() + off, (int)(data.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

static DWORD WINAPI handleClient(LPVOID param) {
    SOCKET s = (SOCKET)(uintptr_t)param;
    std::string buf;
    char chunk[512];
    for (;;) {
        int n = recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        buf.append(chunk, (size_t)n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            Command cmd;
            std::string resp;
            DWORD now = GetTickCount();
            if (!parseCommand(line, cmd)) {
                resp = respErr(2, "malformed");
            } else if (cmd.type == CmdType::UNKNOWN) {
                resp = respErr(1, "unknown command");
            } else if (cmd.type == CmdType::CREATE) {
                std::string txid = g_core->create(cmd.id, cmd.amountCents, cmd.cnpj, now);
                Transaction t;
                g_core->status(txid, now, &t);
                resp = respOkCreate(txid, t.qr);
            } else if (cmd.type == CmdType::STATUS) {
                Transaction t;
                if (!g_core->status(cmd.txid, now, &t)) {
                    resp = respErr(3, "not found");
                } else {
                    switch (t.state) {
                        case PixState::PEN:      resp = respPen(); break;
                        case PixState::APPROVED: resp = respApproved(t.auth, t.nsu, t.datetime); break;
                        case PixState::CANCELED: resp = respCanceled(); break;
                        case PixState::TIMEOUT:  resp = respTimeout(); break;
                        case PixState::DENIED:   resp = respDenied("denied"); break;
                    }
                }
            } else if (cmd.type == CmdType::PAY) {
                OpResult r = g_core->pay(cmd.txid);
                if (r == OpResult::NOT_FOUND) resp = respErr(3, "not found");
                else if (r == OpResult::FINALIZED) resp = respErr(4, "already finalized");
                else resp = respOk();
            } else if (cmd.type == CmdType::CANCEL) {
                OpResult r = g_core->cancel(cmd.txid);
                if (r == OpResult::NOT_FOUND) resp = respErr(3, "not found");
                else if (r == OpResult::FINALIZED) resp = respErr(4, "already finalized");
                else resp = respOk();
            }
            if (!resp.empty()) sendAll(s, resp);
        }
    }
    closesocket(s);
    return 0;
}

int runServer(PixCore& core) {
    g_core = &core;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WSAStartup fail\n"); return 1; }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { fprintf(stderr, "socket fail\n"); return 1; }
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CFG_PORT);
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind fail (porta %d em uso?)\n", CFG_PORT); return 1;
    }
    listen(ls, SOMAXCONN);
    printf("pixserver listening on 0.0.0.0:%d\n", CFG_PORT);
    fflush(stdout);
    for (;;) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        HANDLE h = CreateThread(nullptr, 0, handleClient, (LPVOID)(uintptr_t)c, 0, nullptr);
        if (h) CloseHandle(h);
    }
}
```

- [ ] **Step 2: `src/main.cpp`**

```cpp
#include "pix_core.h"

int runServer(PixCore& core); // definida em server.cpp

int main() {
    PixCore core;
    return runServer(core);
}
```

- [ ] **Step 3: editar `build.bat`** — manter o target de teste e adicionar o target do servidor após a execução do teste:

```bat
echo [3/4] Compilando servidor...
cl.exe /nologo /std:c++20 /O2 /MT /W3 /EHsc /I"%SRC%" "%SRC%\protocol.cpp" "%SRC%\pix_core.cpp" "%SRC%\server.cpp" "%SRC%\main.cpp" /Fe:"%OUT%\pixserver.exe" /Fo:"%OUT%\\" /link /SUBSYSTEM:CONSOLE ws2_32.lib kernel32.lib
if errorlevel 1 ( echo ERRO: compilacao servidor falhou. & exit /b 1 )

echo [4/4] OK.
exit /b 0
```

(Insira entre o `"%OUT%\pixcore_test.exe"` e o `exit /b %ERRORLEVEL%` final; ajuste os labels `[n/4]`.)

- [ ] **Step 4: compilar**

Run: `build.bat`
Expected: `ALL TESTS PASSED` e `build\pixserver.exe` gerado.

- [ ] **Step 5: Checkpoint** — servidor compila e linka com ws2_32.

---

### Task 6: Verificação end-to-end (manual)

**Files:**
- Create: `tests/test_client.cpp` (cliente de teste opcional — simula o loop do proxy)

**Interfaces:**
- Consumes: protocolo do servidor (Task 5). Nada novo é produzido.

- [ ] **Step 1: sobe o servidor**

Run (background): `build\pixserver.exe`
Expected: imprime `pixserver listening on 0.0.0.0:31736`.

- [ ] **Step 2: fluxo via netcat**

```
> CREATE pedido1 1000 12345678000199
< OK <txid16hex> 00020126...6304XXXX
> STATUS <txid>
< PEN
> PAY <txid>
< OK
> STATUS <txid>
< APPROVED 123456 000000000001 20260908103000
```

Run: `echo CREATE pedido1 1000 12345678000199 | nc localhost 31736` (ou telnet; no Windows, `ncat`/PowerShell `Test-NetConnection`).

Expected: respostas acima, na ordem.

- [ ] **Step 3: testa erros**

```
> FOO bar          → ERR 1 unknown command
> CREATE so 1000   → ERR 2 malformed
> STATUS FFFFFFFFFFFFFFFF → ERR 3 not found
> CANCEL <txid aprovada>  → ERR 4 already finalized
```

- [ ] **Step 4: `tests/test_client.cpp` (cliente de teste, opcional)**

```cpp
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <string>
#include <cstring>

static std::string sendLine(SOCKET s, const std::string& line) {
    std::string all = line + "\n";
    send(s, all.data(), (int)all.size(), 0);
    std::string resp;
    char c;
    while (recv(s, &c, 1, 0) == 1) { if (c == '\n') break; resp.push_back(c); }
    return resp;
}

int main() {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = inet_addr("127.0.0.1"); a.sin_port = htons(31736);
    if (connect(s, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) { printf("connect fail\n"); return 1; }
    std::string r = sendLine(s, "CREATE t1 1000 12345678000199");
    printf("CREATE -> %s\n", r.c_str());
    // extrai txid (token 2)
    size_t sp1 = r.find(' '); size_t sp2 = r.find(' ', sp1 + 1);
    std::string txid = r.substr(sp1 + 1, sp2 - sp1 - 1);
    printf("PAY -> %s\n", sendLine(s, "PAY " + txid).c_str());
    printf("STATUS -> %s\n", sendLine(s, "STATUS " + txid).c_str());
    closesocket(s); WSACleanup();
    return 0;
}
```

Compilar (uma vez): `cl.exe /nologo /std:c++20 /EHsc tests\test_client.cpp /Fe:build\test_client.exe /link /SUBSYSTEM:CONSOLE ws2_32.lib`
Run: `build\test_client.exe`
Expected: `CREATE -> OK ...`, `PAY -> OK`, `STATUS -> APPROVED ...`.

- [ ] **Step 5: Checkpoint final** — fluxo completo CREATE → PEN → PAY → APPROVED funciona; erros mapeados certo.
