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
    if (n < 0) n = 0; else if ((size_t)n >= sizeof(b)) n = (int)sizeof(b) - 1;
    return std::string(b, (size_t)n);
}
std::string respErr(int code, const std::string& msg) {
    char b[512]; int n = snprintf(b, sizeof(b), "ERR %d %s\n", code, msg.c_str());
    if (n < 0) n = 0; else if ((size_t)n >= sizeof(b)) n = (int)sizeof(b) - 1;
    return std::string(b, (size_t)n);
}
std::string respPen() { return "PEN\n"; }
std::string respApproved(const std::string& auth, const std::string& nsu, const std::string& datetime) {
    char b[256]; int n = snprintf(b, sizeof(b), "APPROVED %s %s %s\n", auth.c_str(), nsu.c_str(), datetime.c_str());
    if (n < 0) n = 0; else if ((size_t)n >= sizeof(b)) n = (int)sizeof(b) - 1;
    return std::string(b, (size_t)n);
}
std::string respCanceled() { return "CANCELED\n"; }
std::string respDenied(const std::string& reason) {
    char b[256]; int n = snprintf(b, sizeof(b), "DENIED %s\n", reason.c_str());
    if (n < 0) n = 0; else if ((size_t)n >= sizeof(b)) n = (int)sizeof(b) - 1;
    return std::string(b, (size_t)n);
}
std::string respTimeout() { return "TIMEOUT\n"; }
std::string respOk() { return "OK\n"; }
