#pragma once
#include <string>

// monta "CREATE <id> <cents> <cnpj>\n"
std::string buildCreate(const std::string& id, long cents, const std::string& cnpj);

// parseia "OK|txid|qr|qrB64\n" (qr = string Pix, qrB64 = PNG em base64)
bool parseOkCreate(const std::string& resp, std::string& txid, std::string& qr, std::string& qrB64);

enum class PixStatus { PEN, APPROVED, CANCELED, TIMEOUT, DENIED, ERROR };

struct PixStatusResult {
    PixStatus status = PixStatus::ERROR;
    std::string auth, nsu, datetime;
    std::string orderId, pixTxid;   // extras (id do pedido MP + txid do Pix)
};

// parseia "PEN" | "APPROVED <auth> <nsu> <datetime>" | "CANCELED" | "TIMEOUT" | "DENIED <r>" | "ERR <c> <m>"
bool parseStatus(const std::string& resp, PixStatusResult& out);

// HMAC-SHA256 (hex, minúsculo) via BCrypt. Retorna "" em falha.
std::string hmacSha256Hex(const std::string& key, const std::string& msg);

// RC4 stream + hex (mesma chave/derivação do pixserver). Retorna "" em falha.
std::string rc4Encode(const std::string& key, const std::string& plain);
std::string rc4Decode(const std::string& key, const std::string& hexstr);
