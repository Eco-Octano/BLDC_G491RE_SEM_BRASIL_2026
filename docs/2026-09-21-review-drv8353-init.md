---
data: 2026-09-21
tags:
  - review/c-cpp
  - alvo/bare-metal
  - plataforma/stm32g4
  - subsistema/gate-driver
  - modulo/drv8353
  - defeito/inicializacao
aliases:
  - review init DRV8353
  - revisao calibracao CSA
status: aberto
---

# Review — inicialização e calibração do DRV8353RS

> [!abstract] Síntese
> **3 P0, 4 P1.** O achado dominante é o objeto SPI: ele é local à função de
> transferência e nunca recebe `begin()`, então nenhuma escrita chega ao chip.
> Os outros dois P0 deixam o estado final do driver inutilizável para medir
> corrente.
>
> Contexto: [[ESC Checkpoint]] · [[2026-09-11-estudo-spi-drv8353rs]]

## Correções de premissa

> [!important] Duas afirmações a corrigir antes de usar este código
> **O registrador `0x07` existe.** É o *Driver Configuration Register*
> (§8.6.2.6 do SLVSDY6A), disponível apenas nas variantes DRV8353 e DRV8353R.
> Bit 0 = `CAL_MODE`: `0b` manual, `1b` autocalibração interna. Afirmação
> anterior de que ele não existia estava errada.
>
> **Não há registro de recomendação de "auto primeiro, manual depois".** A
> sequência é defensável e tem base no datasheet, mas não veio de análise
> anterior deste projeto — tratar como decisão a validar, não como premissa.

## O que cada modo de calibração faz

Base: §8.3.4.3 do SLVSDY6A.

| | Autocalibração (`CAL_MODE = 1`) | Manual (`CAL_MODE = 0`) |
| --- | --- | --- |
| O que acontece | rotina interna de trim do amplificador | entradas curto-circuitadas, carga desconectada |
| Quem mede | o próprio DRV | **o MCU**, pelo ADC |
| O que corrige | offset de entrada do amplificador | resíduo total: amplificador + ADC + placa |
| Disparo | bits `CSA_CAL_x`, com `CAL_MODE = 1` | bits `CSA_CAL_x`, com `CAL_MODE = 0` |
| Ganho durante a rotina | forçado ao máximo | o configurado |

Ocorre também **no power-up**, automaticamente, nas duas variantes: 50 µs após
o VREF cruzar a tensão mínima de operação.

> [!note] Os dois usam o mesmo gatilho
> `CSA_CAL_x` é o comando nos dois modos. O que muda é o que o firmware precisa
> fazer enquanto o bit está em 1, e o que precisa acontecer para o bit voltar a
> zero.

## Achados

### P0

**[P0] `DRV8353_SPI_Transfer` — objeto SPI local, nunca inicializado**
Mecanismo: `SPIClass SPI(...)` é declarado dentro da função; é construído e
destruído a cada chamada e nunca recebe `begin()`. O `SPI.begin()` dos inits
atua sobre o objeto global da biblioteca, que é outro. O quarto argumento
(`PIN_SPI_CS`) entrega o chip select ao hardware e conflita com o
`digitalWrite` manual.
Condição de disparo: toda transferência.
Correção: um `SPIClass` global, sem o quarto argumento, com `begin()` único.
Confiança: alta.

**[P0] `DRV8353_Init` — amplificadores ficam em calibração permanente**
Mecanismo: `0x329C` liga `CSA_CAL_A/B/C` e nada os limpa. Entradas
curto-circuitadas e carga desconectada tornam toda leitura de corrente
inválida.
Condição de disparo: qualquer leitura de corrente após o boot.
Correção: os bits são pulso, não estado.
Confiança: alta — §8.3.4.3 e Tabela 18.

**[P0] `DRV8353_Calibration_Init` — LOCK antes das escritas seguintes**
Mecanismo: `0x1E44` põe LOCK em `110b`. Escritas passam a ser ignoradas exceto
no próprio LOCK e nos bits 0–2 do `0x02`. O quadro `0x1B44` do `Init` destrava,
mas o IDRIVE do mesmo quadro é descartado.
Correção: destravar e configurar são duas escritas. Se o LOCK for intencional,
aplicá-lo só no fim de toda a sequência.
Confiança: alta — Tabela 15.

### P1

**[P1] A autocalibração nunca é comandada.** Em `Calibration_Init` o CSA recebe
`0x3280` (bits `CSA_CAL` em zero) e o `CAL_MODE` só é escrito depois. A rotina
é disparada pelos bits `CSA_CAL_x`; sem eles, nada roda além da calibração de
power-up. Confiança: alta.

**[P1] Nenhuma escrita é verificada.** Com o LOCK ativo e a ordem atual,
escritas estão sendo descartadas em silêncio. Correção: relê e compara os 11
bits de dados. Confiança: alta.

**[P1] `delay(1000)` arbitrário.** O datasheet especifica 50 µs para a rotina
de power-up após o VREF cruzar o mínimo; para o rerun em runtime não declara
tempo. Confiança: média — verificar se há nota de aplicação da TI com o número.

**[P1] `SPI.end()` ao fim de cada init**, seguido de novo `begin()`.
Desnecessário e mais um ponto de falha. Confiança: alta.

### Menção positiva

`COAST = 1` durante a calibração é coerente com a recomendação do datasheet de
calibrar fora do período de chaveamento. Vale preservar em alterações futuras.

### P2

Agrupados: pinos `PIN_UART_TX/RX` e `PIN_LED` definidos e não usados;
`SPI_ADRESS_MASK` sem o segundo D; quadros completos passados no parâmetro
`dados` (funciona só pela máscara `0x7FF`).

## Verificações pendentes

- [ ] Modo SPI: `SPI_MODE1` ainda não foi confirmado contra o diagrama de temporização do datasheet — ver [[2026-09-11-estudo-spi-drv8353rs]], bloco 3
- [ ] Tempo da rotina de autocalibração em runtime (o datasheet só declara os 50 µs do power-up)
- [ ] Se os bits `CSA_CAL_x` se autolimpam ao fim da rotina automática ou exigem limpeza pelo firmware — Tabela 18 descreve o bit como estado, não como pulso
- [ ] `OCP_MODE` em `0x265` é **retry**, não latch. Confirmar se é a escolha desejada (latch = `0x225`)
- [ ] Se PA2/PA3 nesta montagem são UART ou os sinais do shield

## Não revisado

- Presença e efeito do `clock_config.cpp`
- Estado das solder bridges da Nucleo
- Comportamento com a ponte energizada — este review é estático

## Tarefa em aberto

- [ ] Projetar a sequência de calibração: ordem das escritas, o que o firmware faz enquanto `CSA_CAL_x` está em 1 em cada modo, e onde o offset medido é guardado
