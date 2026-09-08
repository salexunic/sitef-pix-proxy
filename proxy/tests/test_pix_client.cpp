#include <cstdio>
#include <string>
#include "pix_client.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static void testBuildCreate() {
    std::string s = buildCreate("pedido1", 1000, "12345678000199");
    CHECK(s == "CREATE pedido1 1000 12345678000199\n");
}

static void testParseOkCreate() {
    std::string txid, qr;
    CHECK(parseOkCreate("OK 0123456789ABCDEF 00020126QR\n", txid, qr));
    CHECK(txid == "0123456789ABCDEF");
    CHECK(qr == "00020126QR");
    // qr com espaco (resto da linha)
    CHECK(parseOkCreate("OK 0123456789ABCDEF 00020126 PIX FAKE\n", txid, qr));
    CHECK(qr == "00020126 PIX FAKE");
    CHECK(!parseOkCreate("ERR 3 not found\n", txid, qr));
}

static void testParseStatus() {
    PixStatusResult p;
    CHECK(parseStatus("PEN\n", p) && p.status == PixStatus::PEN);
    CHECK(parseStatus("CANCELED\n", p) && p.status == PixStatus::CANCELED);
    CHECK(parseStatus("TIMEOUT\n", p) && p.status == PixStatus::TIMEOUT);
    CHECK(parseStatus("DENIED x\n", p) && p.status == PixStatus::DENIED);
    CHECK(parseStatus("ERR 3 not found\n", p) && p.status == PixStatus::ERROR);
    CHECK(parseStatus("APPROVED 123456 000000000001 20260908103000\n", p));
    CHECK(p.status == PixStatus::APPROVED);
    CHECK(p.auth == "123456" && p.nsu == "000000000001" && p.datetime == "20260908103000");
}

int main() {
    testBuildCreate();
    testParseOkCreate();
    testParseStatus();
    if (g_failures == 0) { printf("ALL TESTS PASSED\n"); return 0; }
    printf("%d FAILURES\n", g_failures);
    return 1;
}
