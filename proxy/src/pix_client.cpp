#include "pix_client.h"
#include <cstdio>
#include <vector>
#include <windows.h>
#include <bcrypt.h>
#undef ERROR  // windows.h define ERROR=0, conflita com PixStatus::ERROR

// RC4 stream (mesma chave do pixserver).
static void rc4(const unsigned char* key, int klen, const unsigned char* in, int n, unsigned char* out) {
    unsigned char S[256];
    for (int i = 0; i < 256; i++) S[i] = (unsigned char)i;
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % klen]) & 0xFF;
        unsigned char t = S[i]; S[i] = S[j]; S[j] = t;
    }
    int i = 0; j = 0;
    for (int k = 0; k < n; k++) {
        i = (i + 1) & 0xFF;
        j = (j + S[i]) & 0xFF;
        unsigned char t = S[i]; S[i] = S[j]; S[j] = t;
        out[k] = in[k] ^ S[(S[i] + S[j]) & 0xFF];
    }
}

static int hxval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string rc4Encode(const std::string& key, const std::string& plain) {
    if (plain.empty()) return "";
    std::vector<unsigned char> tmp(plain.size());
    rc4((const unsigned char*)key.data(), (int)key.size(), (const unsigned char*)plain.data(), (int)plain.size(), tmp.data());
    static const char HX[] = "0123456789abcdef";
    std::string out;
    out.reserve(plain.size() * 2);
    for (size_t i = 0; i < tmp.size(); i++) { out.push_back(HX[tmp[i] >> 4]); out.push_back(HX[tmp[i] & 0xF]); }
    return out;
}

std::string rc4Decode(const std::string& key, const std::string& hexstr) {
    if (hexstr.empty() || (hexstr.size() & 1)) return "";
    std::vector<unsigned char> tmp(hexstr.size() / 2);
    for (size_t i = 0; i < tmp.size(); i++) {
        int hi = hxval(hexstr[i * 2]), lo = hxval(hexstr[i * 2 + 1]);
        if (hi < 0 || lo < 0) return "";
        tmp[i] = (unsigned char)((hi << 4) | lo);
    }
    std::vector<unsigned char> out(tmp.size());
    rc4((const unsigned char*)key.data(), (int)key.size(), tmp.data(), (int)tmp.size(), out.data());
    return std::string((const char*)out.data(), out.size());
}

// HMAC-SHA256 (hex, 64 chars) via BCrypt CNG — bcrypt.lib já linkado no build.bat.
std::string hmacSha256Hex(const std::string& key, const std::string& msg) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    unsigned char digest[32];
    std::string out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) return out;
    if (BCryptCreateHash(alg, &h, NULL, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0) { BCryptCloseAlgorithmProvider(alg, 0); return out; }
    if (BCryptHashData(h, (PUCHAR)msg.data(), (ULONG)msg.size(), 0) != 0) { BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0); return out; }
    if (BCryptFinishHash(h, digest, sizeof(digest), 0) != 0) { BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0); return out; }
    BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0);
    static const char hexd[] = "0123456789abcdef";
    out.reserve(64);
    for (int i = 0; i < 32; i++) { out.push_back(hexd[digest[i] >> 4]); out.push_back(hexd[digest[i] & 0xF]); }
    return out;
}

std::string buildCreate(const std::string& id, long cents, const std::string& cnpj) {
    char host[128] = "";
    DWORD hl = sizeof(host);
    GetComputerNameA(host, &hl);
    char b[640];
    snprintf(b, sizeof(b), "CREATE %s %ld %s %s\n", id.c_str(), cents, cnpj.c_str(), host);
    return std::string(b);
}

static std::string stripCrlf(std::string r) {
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    return r;
}

bool parseOkCreate(const std::string& resp, std::string& txid, std::string& qr, std::string& qrB64) {
    std::string r = stripCrlf(resp);
    if (r.compare(0, 3, "OK|") != 0) return false;
    size_t p1 = r.find('|', 3);
    if (p1 == std::string::npos) return false;
    txid = r.substr(3, p1 - 3);
    size_t p2 = r.find('|', p1 + 1);
    if (p2 == std::string::npos) return false;
    qr = r.substr(p1 + 1, p2 - p1 - 1);
    qrB64 = r.substr(p2 + 1);
    return !txid.empty() && !qr.empty();
}

bool parseStatus(const std::string& resp, PixStatusResult& out) {
    out = PixStatusResult();
    std::string r = stripCrlf(resp);
    if (r == "PEN") { out.status = PixStatus::PEN; return true; }
    if (r == "CANCELED") { out.status = PixStatus::CANCELED; return true; }
    if (r == "TIMEOUT") { out.status = PixStatus::TIMEOUT; return true; }
    if (r.compare(0, 7, "DENIED ") == 0) { out.status = PixStatus::DENIED; return true; }
    if (r.compare(0, 4, "ERR ") == 0) { out.status = PixStatus::ERROR; return true; }
    if (r.compare(0, 9, "APPROVED ") == 0) {
        out.status = PixStatus::APPROVED;
        size_t p1 = 9, p2 = r.find(' ', p1);
        if (p2 == std::string::npos) return false;
        out.auth = r.substr(p1, p2 - p1);
        size_t p3 = r.find(' ', p2 + 1);
        if (p3 == std::string::npos) return false;
        out.nsu = r.substr(p2 + 1, p3 - p2 - 1);
        // datetime até '|', depois orderId|pixTxid (extras opcionais)
        size_t p4 = r.find('|', p3 + 1);
        if (p4 == std::string::npos) {
            out.datetime = r.substr(p3 + 1);
        } else {
            out.datetime = r.substr(p3 + 1, p4 - p3 - 1);
            size_t p5 = r.find('|', p4 + 1);
            if (p5 == std::string::npos) {
                out.orderId = r.substr(p4 + 1);
            } else {
                out.orderId = r.substr(p4 + 1, p5 - p4 - 1);
                out.pixTxid = r.substr(p5 + 1);
            }
        }
        return !out.auth.empty() && !out.nsu.empty() && !out.datetime.empty();
    }
    return false;
}
