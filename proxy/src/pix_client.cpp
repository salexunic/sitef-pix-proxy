#include "pix_client.h"
#include <cstdio>

std::string buildCreate(const std::string& id, long cents, const std::string& cnpj) {
    char b[512];
    snprintf(b, sizeof(b), "CREATE %s %ld %s\n", id.c_str(), cents, cnpj.c_str());
    return std::string(b);
}

static std::string stripCrlf(std::string r) {
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    return r;
}

bool parseOkCreate(const std::string& resp, std::string& txid, std::string& qr) {
    std::string r = stripCrlf(resp);
    if (r.compare(0, 3, "OK ") != 0) return false;
    size_t p2 = r.find(' ', 3);
    if (p2 == std::string::npos) return false;
    txid = r.substr(3, p2 - 3);
    qr = r.substr(p2 + 1);
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
        out.datetime = r.substr(p3 + 1);
        return !out.auth.empty() && !out.nsu.empty() && !out.datetime.empty();
    }
    return false;
}
