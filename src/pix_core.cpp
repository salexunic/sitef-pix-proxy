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
