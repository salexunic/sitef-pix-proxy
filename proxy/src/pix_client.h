#pragma once
#include <string>

// monta "CREATE <id> <cents> <cnpj>\n"
std::string buildCreate(const std::string& id, long cents, const std::string& cnpj);

// parseia "OK <txid> <qr>\n" (qr = resto da linha, pode ter espaco)
bool parseOkCreate(const std::string& resp, std::string& txid, std::string& qr);

enum class PixStatus { PEN, APPROVED, CANCELED, TIMEOUT, DENIED, ERROR };

struct PixStatusResult {
    PixStatus status = PixStatus::ERROR;
    std::string auth, nsu, datetime;
};

// parseia "PEN" | "APPROVED <auth> <nsu> <datetime>" | "CANCELED" | "TIMEOUT" | "DENIED <r>" | "ERR <c> <m>"
bool parseStatus(const std::string& resp, PixStatusResult& out);
