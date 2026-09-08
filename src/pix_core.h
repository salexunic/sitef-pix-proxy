#pragma once
#include <string>
#include <map>
#include <windows.h>

// CRC16-CCITT-FALSE (polinomio 0x1021, init 0xFFFF, sem reflexao, sem XOR final).
unsigned short crc16Ccitt(const std::string& data);

// Monta o BR Code (copia-e-cola) de um QR Pix estatico.
std::string buildQrCode(const std::string& txid, long amountCents,
                        const std::string& merchantName, const std::string& city);

enum class PixState { PEN, APPROVED, CANCELED, TIMEOUT, DENIED };
enum class OpResult { OK, NOT_FOUND, FINALIZED };

struct Transaction {
    std::string txid;
    std::string clientId;
    long amountCents = 0;
    std::string cnpj;
    std::string qr;
    PixState state = PixState::PEN;
    std::string auth;
    std::string nsu;
    std::string datetime;
    DWORD createdAtMs = 0;
};

// Mapa de transacoes em memoria + maquina de estado. Thread-safe (CRITICAL_SECTION).
// Sem socket. nowMs = GetTickCount() injetado para testes deterministicos.
class PixCore {
public:
    PixCore();
    ~PixCore();
    std::string create(const std::string& clientId, long amountCents,
                       const std::string& cnpj, DWORD nowMs);
    bool status(const std::string& txid, DWORD nowMs, Transaction* out);
    OpResult cancel(const std::string& txid);
    OpResult pay(const std::string& txid);
private:
    std::map<std::string, Transaction> txs_;
    CRITICAL_SECTION cs_;
    unsigned long long nextId_ = 0;
};
