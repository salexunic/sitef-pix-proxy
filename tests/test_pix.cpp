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
