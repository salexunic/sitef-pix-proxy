#pragma once
#include <string>

// CRC16-CCITT-FALSE (polinomio 0x1021, init 0xFFFF, sem reflexao, sem XOR final).
// Padrao usado no CRC do BR Code do Pix.
unsigned short crc16Ccitt(const std::string& data);
