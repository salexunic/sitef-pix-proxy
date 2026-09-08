# Design — Servidor Pix Fake (TCP nativo, C++ Winsock)

Data: 2026-09-08
Status: rascunho para revisão

## Contexto e objetivo

O projeto `sitef-pix-proxy` substituirá, no futuro, o fluxo Pix do SiTef por um
servidor próprio. Motivo: o Pix do SiTef (CardSE/PSP) é caro; a intenção é o
proxy interceptar a função Pix no PDV e rotear para uma API/TCP própria.

**Este documento cobre somente a primeira entrega**: o **servidor Pix fake**,
usado para testar a integração antes de existir o servidor real.

Objetivo do fake: simular um PSP de Pix completo o suficiente para o proxy (ou
um cliente de teste) percorrer o fluxo inteiro — criar transação, receber QR
copia-e-cola, fazer polling de status e receber aprovação/cancelamento/timeout.

## Não-objetivos (fora de escopo)

- **Não usa PGWebLib** nem qualquer dependência PayGo/SiTef. O proxy falará com
  este servidor via TCP puro; não há pinpad, não há leitura de cartão.
- O proxy DLL (`CliSiTef32I.dll` drop-in) é **outra entrega**, posterior. Aqui só
  o servidor.
- Não integra banco/PSP real, não gera Pix dinâmico no Banco Central, não faz
  liquidação. É fake para teste.
- Sem criptografia/TLS nesta fase.

## Arquitetura

```
proxy DLL (depois) ──TCP raw──▶ pixserver.exe (fake)
   CREATE <id> <cents> <cnpj>      │ mapa txid→estado em memória
   STATUS <txid>                   │ gera QR EMV (copia-e-cola)
   CANCEL <txid>                   │ PEN→APPROVED/CANCELED/TIMEOUT
   PAY <txid>  (gatilho de teste)  │
```

Unidades (isoladas, testáveis):

| Arquivo | Papel | Depende de |
|---|---|---|
| `protocol.h/.cpp` | framing de linha (`\n`) + parse/serialize dos comandos. Zero socket. | nada |
| `pix_core.h/.cpp` | mapa de transações + geração QR EMV (CRC16) + máquina de estado. Zero socket. | protocol |
| `server.cpp` | Winsock listen/accept, dispatch comando→core, responde. | pix_core, protocol |
| `config.h` | porta, delay auto-approve, timeout, merchant/city do QR (compile-time). | nada |
| `main.cpp` | entry point. | server, config |
| `build.bat` | build MSVC x86 (padrão paygo-proxy). | — |

Separação de responsabilidades: `pix_core` e `protocol` são puros e testáveis
sem rede; `server.cpp` é a única camada que toca Winsock.

## Protocolo (texto, linha terminada em `\n`)

Sem JSON, sem header binário. Linha de texto — debugável com netcat/telnet.

### Pedidos (cliente → servidor)

```
CREATE <id> <amount_cents> <cnpj_automacao>\n
STATUS <txid>\n
CANCEL <txid>\n
PAY   <txid>\n      # gatilho manual de teste (simula o cliente pagando)
```

- `<id>`: identificador da transação no lado do cliente (opcional, livre). O
  servidor não o usa para lookup; a referência canônica é o `<txid>` que o
  próprio servidor devolve no `CREATE`.
- `<amount_cents>`: valor inteiro em centavos.
- `<cnpj_automacao>`: 14 dígitos (campo obrigatório no fluxo Pix real).
- `<txid>`: 16 caracteres hex, gerados pelo servidor no CREATE.

### Respostas (servidor → cliente)

```
CREATE → OK <txid> <qr>\n
       | ERR <code> <msg>\n

STATUS → PEN\n
       | APPROVED <auth> <nsu> <datetime>\n
       | CANCELED\n
       | DENIED <reason>\n
       | TIMEOUT\n

CANCEL → OK\n
       | ERR <code> <msg>\n

PAY   → OK\n
       | ERR <code> <msg>\n
```

- `<txid>`: 16 hex. `<qr>`: resto-da-linha após o txid (pode conter espaço —
  campo 59 do EMV). `<auth>`: 6 dígitos. `<nsu>`: numérico. `<datetime>`:
  `YYYYMMDDHHMMSS`. `<code>`: inteiro. `<msg>`: texto livre, resto-da-linha.

### Códigos de erro (ERR)

| code | significado |
|---|---|
| 1 | comando desconhecido |
| 2 | argumentos inválidos / linha malformada |
| 3 | txid inexistente |
| 4 | transação já finalizada (não aceita PAY/CANCEL) |

## QR EMV (copia-e-cola)

Gera BR Code válido. Formato de cada campo: `ID(2) LEN(2) VALOR`.

| ID | valor | notas |
|---|---|---|
| 00 | `01` | payload format indicator |
| 26 | GUI `BR.GOV.BCB.PIX` + chave (txid) | merchant account info |
| 52 | `0000` | MCC |
| 53 | `986` | BRL |
| 54 | amount (se > 0) | Pix de valor fixo |
| 58 | `BR` | país |
| 59 | merchant name (config) | |
| 60 | city (config) | |
| 62 | `05` + txid | additional data (txid) |
| 63 | CRC16 (4 hex, maiúsculo) | calculado sobre todos os campos exceto 63 |

CRC16-CCITT-FALSE: polinômio `0x1021`, valor inicial `0xFFFF`, sem reflexão,
sem XOR final. Calculado sobre a string completa (campos 00..62, já com seus
`ID+LEN+VALOR`), então concatenado `6304<CRC>`.

## Máquina de estado + simulação de pagamento

Estados: `PEN` → `APPROVED | CANCELED | TIMEOUT`.

- `PEN` → `APPROVED` quando:
  - (a) `PAY <txid>` manual, ou
  - (b) auto-approve: `STATUS` detecta `elapsed >= AUTO_APPROVE_MS` (default
    5000) → aprova e devolve `APPROVED`.
- `PEN` → `TIMEOUT` quando `elapsed >= TIMEOUT_MS` (default 180000 = 3 min,
  espelhando o timeout do e-SiTef).
- `CANCEL <txid>` → `CANCELED`.

**Avaliação lazy no `STATUS`** — a transição por tempo é calculada na hora do
polling, sem thread de timer dedicada. Determinístico e sem race com o mutex.

`APPROVED` devolve `auth` (6 díg.), `nsu` e `datetime` para o proxy montar o
comprovante em "língua SiTef" (CMD_DATA + terminalCode 0).

## Concorrência

- Thread por conexão.
- Mapa de transações protegido por `CRITICAL_SECTION`.
- txid gerado com `rand()` (não criptográfico — é fake) ou contador; unicidade
  garantida pelo mapa.

## Configuração (compile-time, `config.h`)

| chave | default | notas |
|---|---|---|
| `CFG_PORT` | `31736` | porta TCP |
| `CFG_AUTO_APPROVE_MS` | `5000` | delay auto-approve |
| `CFG_TIMEOUT_MS` | `180000` | timeout Pix |
| `CFG_MERCHANT_NAME` | `PIX FAKE TEST` | campo 59 do QR |
| `CFG_MERCHANT_CITY` | `SAO PAULO` | campo 60 do QR |

Bind em `0.0.0.0`. Sem arquivo de config em runtime (coerente com a preferência
compile-time do paygo-proxy).

## Erros e robustez

- Linha malformada → `ERR 2`.
- txid desconhecido → `ERR 3`.
- PAY/CANCEL em tx finalizada → `ERR 4`.
- Conexão cai no meio → servidor descarta, sem afetar o mapa.
- Log em stderr (opcional, via env `PIXSERVER_LOG=1`).

## Teste

1. **Unit (sem socket)**: CRC16 do QR (vetor conhecido), parse de comandos,
   transição de estado (PEN→APPROVED via PAY e via auto-approve, PEN→TIMEOUT,
   CANCEL).
2. **Manual**: `netcat` manda `CREATE`/`STATUS`/`PAY` e valida o fluxo.
3. **Cliente de teste** (opcional): simula o loop `CONTINUA` do proxy — conecta,
   `CREATE`, imprime QR, faz `STATUS` em loop até sair de `PEN`.

## Build

`build.bat` — MSVC x86, mesmo padrão de detecção de vcvarsall do paygo-proxy.
Gera `build\pixserver.exe`.

## Contrato do proxy (fase seguinte)

Registrado para não perder ao construir o `CliSiTef32I.dll` drop-in. O servidor
fake não muda com isto — só emite a string do QR; a apresentação é do proxy.

1. Proxy intercepta **função 7 (venda carteira digital) e 8 (cancelamento)** no
   `IniciaFuncaoSiTefInterativo`; demais funções → forward pra `libenv.dll`
   (mesmo esquema de forwarders do paygo-proxy, **sem PGWebLib**).
2. Proxy **detecta o modo do QR** no `additionalParams` (ParamAdic):
   - `{DevolveStringQRCode=1}` presente → devolve a **string do QR via
     TypeField 584** (`CMD_DATA`, `fieldType=584`); a automação do PDV renderiza.
   - ausente (padrão) → caminho PinPad. **Decisão aberta**: o proxy fake não tem
     PinPad QR — forçar a string mesmo assim ou desenhar no PinPad.
3. Proxy chama `CREATE`, recebe `OK <txid> <qr>`, apresenta o QR conforme (2).
4. Proxy faz `STATUS` em loop a cada `CONTINUA` até `APPROVED` / `CANCELED` /
   `TIMEOUT` (o PDV fica aguardando no `CONTINUA`).
5. `APPROVED` → proxy devolve comprovante em "língua SiTef" (CMD_DATA com NSU,
   auth, datetime) + `terminalCode 0`; `CANCELED`/`TIMEOUT` → código de retorno
   negativo apropriado.

## Critérios de conclusão

- [ ] `CREATE` devolve `OK <txid> <qr>` com QR EMV válido (CRC confere).
- [ ] `STATUS` devolve `PEN` até `AUTO_APPROVE_MS`, depois `APPROVED` com auth/nsu/datetime.
- [ ] `PAY` força `APPROVED` imediato.
- [ ] `CANCEL` → `CANCELED`.
- [ ] timeout → `TIMEOUT` após `CFG_TIMEOUT_MS`.
- [ ] erros (comando/txid/args) devolvem `ERR <code>` correto.
- [ ] build passa; testes unit rodam.
