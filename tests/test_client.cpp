#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <string>
#include <cstring>

static std::string sendLine(SOCKET s, const std::string& line) {
    std::string all = line + "\n";
    send(s, all.data(), (int)all.size(), 0);
    std::string resp;
    char c;
    while (recv(s, &c, 1, 0) == 1) { if (c == '\n') break; resp.push_back(c); }
    return resp;
}

int main() {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = inet_addr("127.0.0.1"); a.sin_port = htons(31736);
    if (connect(s, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) { printf("connect fail\n"); return 1; }
    std::string r = sendLine(s, "CREATE t1 1000 12345678000199");
    printf("CREATE -> %s\n", r.c_str());
    // extrai txid (token 2)
    size_t sp1 = r.find(' '); size_t sp2 = r.find(' ', sp1 + 1);
    std::string txid = r.substr(sp1 + 1, sp2 - sp1 - 1);
    printf("PAY -> %s\n", sendLine(s, "PAY " + txid).c_str());
    printf("STATUS -> %s\n", sendLine(s, "STATUS " + txid).c_str());
    closesocket(s); WSACleanup();
    return 0;
}
