# Design — Proxy CliSiTef32I.dll para Pix

Data: 2026-09-08
Status: rascunho para revisão

## Contexto e objetivo

Drop-in de `CliSiTef32I.dll` que intercepta a função Pix (7 = venda carteira
digital, 8 = cancelamento) e roteia para o **servidor Pix próprio** (o
`pixserver.exe` fake desta entrega). Todo o resto do contrato SiTef segue
íntegro via forward para `libenv.dll` (SiTef nativo). Motivo: não usar o Pix
do SiTef/CardSE (caro).

Este documento é o spec da **DLL proxy** — o servidor fake tem spec próprio.

## Não-objetivos

- **Sem PGWebLib**, sem pinpad, sem leitura de cartão. Pix é TCP puro.
- Não integra PSP real; o destino é o pix server (fake por hora).
- Não renderiza QR em imagem (nem PinPad, nem tela) — devolve a **string** do
  QR e quem renderiza é a automação do PDV.
- `cnpj_automacao`: obrigatório no fluxo real, mas **fora de escopo** no fake.
  Ajustar quando integrar PSP real.

## Arquitetura

Reuso integral do forwarder do `paygo-proxy`: `exports.def` (712) +
`stubs_clean.cpp` idênticos. O delta é o `proxy.cpp` (intercept core) + um
cliente TCP pequeno.

```
PDV ──chama──▶ CliSiTef32I.dll (proxy Pix)
                  ├── func 7/8 (Pix) ──▶ TCP ──▶ pixserver.exe (CREATE/STATUS/CANCEL)
                  └── qualquer outro  ──▶ libenv.dll (SiTef nativo)
```

| Componente | Papel |
|---|---|
| `exports.def` + `stubs_clean.cpp` | copiados do paygo-proxy (712 exports, forwarders, 18 locals) |
| `proxy.cpp` | intercept core: Inicia/Continua/Finaliza + variantes A/csi |
| `pix_client.h/.cpp` | cliente TCP: builda comando + parseia resposta (testável sem socket) |
| `config.h` | `CFG_PIX_HOST`/`CFG_PIX_PORT`/merchant (compile-time) |
| `build.bat` | build DLL MSVC x86 |

## Interceptação

- `IniciaFuncaoSiTefInterativo`: `function == 7 || function == 8` → intercepta.
  Qualquer outro (2/3, 770/771/772, tudo) → forward pra `libenv.dll`.
- `ContinuaFuncaoSiTefInterativo`: máquina de estados (abaixo).
- `FinalizaFuncaoSiTefInterativo`: encerra socket; se PDV abortou, `CANCEL`.

## Máquina de estados (Continua)

Cada `Continua` avança UM passo (padrão paygo).

| estado | ação |
|---|---|
| `S_CONNECT` | `tcpConnect` → `CREATE` → parseia `OK <txid> <qr>` → guarda txid/qr → emite QR (`CMD_DATA`, `fieldType=584`, buffer=qr) → `S_POLL`. Retorna MOREDATA. |
| `S_POLL` | `STATUS <txid>`: `PEN` → display "Aguardando pagamento..." + MOREDATA; `APPROVED` → guarda auth/nsu/datetime → monta fila de comprovante → `S_DONE`; `CANCELED` → terminalCode negativo → `S_DONE`; `TIMEOUT` → terminalCode negativo → `S_DONE`. |
| `S_DONE` | entrega 1 comprovante por `Continua` (CMD_DATA) e, no fim, retorna `terminalCode`. |

Retornos (convenção SiTef, igual paygo):
- `SITEF_MOREDATA = 10000` enquanto há eventos.
- `SITEF_OK = 0` na aprovação.
- negativo = cancelado/timeout/erro (`-2` cancelado, `-100` erro genérico).

## Apresentação do QR

- Detecta `{DevolveStringQRCode=1}` no `additionalParams` (ParamAdic) do
  `IniciaFuncaoSiTefInterativo`.
- **Ruling:** em ambos os modos devolve a string via `CMD_DATA` + `fieldType=584`
  (o proxy não tem PinPad QR). O modo PinPad é limitação documentada; a
  automação sempre renderiza.

## Cliente TCP (`pix_client`)

Funções puras, testáveis sem socket:

```cpp
std::string buildCreate(const std::string& id, long cents, const std::string& cnpj);
bool parseOkCreate(const std::string& resp, std::string& txid, std::string& qr);
enum class PixStatus { PEN, APPROVED, CANCELED, TIMEOUT, DENIED, ERROR };
struct PixStatusResult { PixStatus status; std::string auth, nsu, datetime; };
bool parseStatus(const std::string& resp, PixStatusResult& out);
```

Protocolo idêntico ao spec do servidor fake (`OK <txid> <qr>`, `PEN`,
`APPROVED <auth> <nsu> <datetime>`, `CANCELED`, `TIMEOUT`, `DENIED`, `ERR`).

## Valor

`IniciaFuncaoSiTefInterativo` recebe valor no formato `"1,00"` (reais, vírgula).
Extrai só dígitos → centavos (`"1,00"` → `100`) antes do `CREATE`.

## Config (compile-time, `config.h`)

| chave | default |
|---|---|
| `CFG_PIX_HOST` | `127.0.0.1` |
| `CFG_PIX_PORT` | `31736` |

## Build

`build.bat` — MSVC x86, mesmo vcvarsall do paygo. `link /DLL` com `exports.def`,
gera `build\CliSiTef32I.dll`. A DLL vai pra `C:\SORIODEV\SORGES\BIN` no deploy.

## Teste

1. **Unit (`pix_client`)**: `buildCreate`, `parseOkCreate`, `parseStatus`
   (vetores de string, sem socket).
2. **Integração manual**: `vendas_teste.exe` (func 7, `{DevolveStringQRCode=1}`)
   contra a DLL proxy apontando pro `pixserver.exe`. Fluxo: QR no TypeField 584
   → `PAY` no server → `STATUS` aprovado → comprovante + retorno 0.

## Critérios de conclusão

- [ ] func 7/8 interceptado; demais funções forward pra libenv.
- [ ] QR devolvido via `fieldType=584` (string copia-e-cola).
- [ ] Polling `STATUS` até `APPROVED`/`CANCELED`/`TIMEOUT`.
- [ ] Comprovante (NSU/auth/datetime) em CMD_DATA + terminalCode 0 na aprovação.
- [ ] Aborto do PDV fecha socket e manda `CANCEL`.
- [ ] build DLL passa; testes unit do `pix_client` passam.
