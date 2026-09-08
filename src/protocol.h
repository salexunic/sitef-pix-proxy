#pragma once
#include <string>

enum class CmdType { UNKNOWN, CREATE, STATUS, CANCEL, PAY };

struct Command {
    CmdType type = CmdType::UNKNOWN;
    std::string id;        // CREATE: id do cliente (opcional)
    long amountCents = 0;  // CREATE
    std::string cnpj;      // CREATE
    std::string txid;      // STATUS/CANCEL/PAY
};

// Parseia uma linha (sem '\n'/'\r'). Retorna false se malformado.
// Comando desconhecido => true com out.type == UNKNOWN.
bool parseCommand(const std::string& line, Command& out);

// Builders de resposta (todas terminam em '\n').
std::string respOkCreate(const std::string& txid, const std::string& qr);
std::string respErr(int code, const std::string& msg);
std::string respPen();
std::string respApproved(const std::string& auth, const std::string& nsu, const std::string& datetime);
std::string respCanceled();
std::string respDenied(const std::string& reason);
std::string respTimeout();
std::string respOk();
