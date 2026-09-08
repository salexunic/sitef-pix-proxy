#include "pix_core.h"
#include <cstdio>
#include "config.h"

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
