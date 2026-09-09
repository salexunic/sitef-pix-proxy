#pragma once
#ifndef CFG_PIX_HOST
#define CFG_PIX_HOST "107.189.26.194"   // VPS do pixserver (produção)
#endif
#ifndef CFG_PIX_PORT
#define CFG_PIX_PORT 31736
#endif
#ifndef CFG_PINPAD_COM
#define CFG_PINPAD_COM "COM8"   // porta serial do pinpad (mesma do CliSiTef.ini)
#endif
#ifndef CFG_PINPAD_QR_SIZE
#define CFG_PINPAD_QR_SIZE 240  // tamanho (px) do QR no pinpad — display 320x248 corta maior
#endif
// token de auth NÃO fica em plaintext aqui — ver AuthToken() em proxy.cpp
// (XOR 0x5A, decodificado em runtime; não aparece em `strings`).
#ifndef CFG_POLL_MAX_RETRY
#define CFG_POLL_MAX_RETRY 3    // falhas de rede toleradas antes de devolver -100
#endif
#ifndef CFG_POLL_BACKOFF_MS
#define CFG_POLL_BACKOFF_MS 1000
#endif
