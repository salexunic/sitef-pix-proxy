#include "pix_core.h"
#include <cstdio>

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
