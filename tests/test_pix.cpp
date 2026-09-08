#include <cstdio>
#include <cstring>
#include <string>
#include "pix_core.h"
#include "protocol.h"
#include "config.h"

static int g_failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static void testCrc() {
    // Vetor conhecido: CRC-16/CCITT-FALSE("123456789") == 0x29B1
    CHECK(crc16Ccitt("123456789") == 0x29B1);
    // Caso base vazio
    CHECK(crc16Ccitt("") == 0xFFFF);
}

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

int main() {
    testCrc();
    testQr();
    testParse();
    testResp();
    testState();
    if (g_failures == 0) { printf("ALL TESTS PASSED\n"); return 0; }
    printf("%d FAILURES\n", g_failures);
    return 1;
}
