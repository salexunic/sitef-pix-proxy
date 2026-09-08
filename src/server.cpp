#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <cstdio>
#include "config.h"
#include "protocol.h"
#include "pix_core.h"

static PixCore* g_core = nullptr;

static bool sendAll(SOCKET s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        int n = send(s, data.data() + off, (int)(data.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

static DWORD WINAPI handleClient(LPVOID param) {
    SOCKET s = (SOCKET)(uintptr_t)param;
    std::string buf;
    char chunk[512];
    for (;;) {
        int n = recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        buf.append(chunk, (size_t)n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            Command cmd;
            std::string resp;
            DWORD now = GetTickCount();
            if (!parseCommand(line, cmd)) {
                resp = respErr(2, "malformed");
            } else if (cmd.type == CmdType::UNKNOWN) {
                resp = respErr(1, "unknown command");
            } else if (cmd.type == CmdType::CREATE) {
                std::string txid = g_core->create(cmd.id, cmd.amountCents, cmd.cnpj, now);
                Transaction t;
                g_core->status(txid, now, &t);
                resp = respOkCreate(txid, t.qr);
            } else if (cmd.type == CmdType::STATUS) {
                Transaction t;
                if (!g_core->status(cmd.txid, now, &t)) {
                    resp = respErr(3, "not found");
                } else {
                    switch (t.state) {
                        case PixState::PEN:      resp = respPen(); break;
                        case PixState::APPROVED: resp = respApproved(t.auth, t.nsu, t.datetime); break;
                        case PixState::CANCELED: resp = respCanceled(); break;
                        case PixState::TIMEOUT:  resp = respTimeout(); break;
                        case PixState::DENIED:   resp = respDenied("denied"); break;
                    }
                }
            } else if (cmd.type == CmdType::PAY) {
                OpResult r = g_core->pay(cmd.txid);
                if (r == OpResult::NOT_FOUND) resp = respErr(3, "not found");
                else if (r == OpResult::FINALIZED) resp = respErr(4, "already finalized");
                else resp = respOk();
            } else if (cmd.type == CmdType::CANCEL) {
                OpResult r = g_core->cancel(cmd.txid);
                if (r == OpResult::NOT_FOUND) resp = respErr(3, "not found");
                else if (r == OpResult::FINALIZED) resp = respErr(4, "already finalized");
                else resp = respOk();
            }
            if (!resp.empty()) sendAll(s, resp);
        }
    }
    closesocket(s);
    return 0;
}

int runServer(PixCore& core) {
    g_core = &core;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WSAStartup fail\n"); return 1; }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { fprintf(stderr, "socket fail\n"); return 1; }
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CFG_PORT);
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind fail (porta %d em uso?)\n", CFG_PORT); return 1;
    }
    listen(ls, SOMAXCONN);
    printf("pixserver listening on 0.0.0.0:%d\n", CFG_PORT);
    fflush(stdout);
    for (;;) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        HANDLE h = CreateThread(nullptr, 0, handleClient, (LPVOID)(uintptr_t)c, 0, nullptr);
        if (h) CloseHandle(h);
    }
}
