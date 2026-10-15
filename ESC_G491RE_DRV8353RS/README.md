# ESC Eco Octano UFPR — NUCLEO-G491RE + DRV8353RS

Projeto de firmware do ESC. Placas: **Artemis** (adaptadora sobre a
NUCLEO-G491RE) + **Ouroboros** (potência, DRV8353RS, sem MCU).

Estado em 2026-10-15: a cadeia de build e gravação está resolvida e
validada. A comunicação SPI com o DRV8353RS **ainda não** funciona — a
investigação está no fim, com uma hipótese dominante. Veja
[Onde a investigação está](#onde-a-investigação-está).

---

## Como restaurar

1. Descompacte a pasta em `Documents/PlatformIO/Projects/`.
2. Abra a pasta no VS Code (não o arquivo — a **pasta**).
3. `pio run -t upload`.

O ambiente se chama `spi_lab` no `platformio.ini`; o nome é livre, mas se
você renomear, os comandos passam a precisar de `-e <nome>`.

Na primeira compilação o PlatformIO baixa o toolchain e o
`tools/install_variant.py` copia o header de variante para dentro do
pacote do framework. Isso é automático e precisa acontecer **a cada
computador novo e a cada reinstalação do PlatformIO**.

---

## Mapa dos arquivos

### Raiz — a cadeia que faz a G491 funcionar

Estes cinco arquivos existem por causa de armadilhas específicas da
NUCLEO-G491RE no PlatformIO. Cada uma delas custou uma sessão de
depuração. **Não mexa sem ler `tools/SETUP.md`.**

| Arquivo | Resolve |
|---|---|
| `platformio.ini` | board customizado, variante genérica, RAM de 112 KB, gravação por OpenOCD com `.elf` |
| `boards/nucleo_g491re_arduino.json` | o board oficial não declara o framework Arduino, só stm32cube |
| `tools/variant_NUCLEO_G491RE_ARDUINO.h` | redireciona para `variant_generic.h`, que a variante genérica não traz |
| `tools/install_variant.py` | copia esse header para dentro do pacote do framework antes de cada build |
| `src/clock_config.cpp` | **obrigatório** — sobrescreve o `SystemClock_Config` fraco; sem ele o clock não vai a 170 MHz |

### `src/` — o que compila hoje

| Arquivo | |
|---|---|
| `main.cpp` | teste de validação do SPI (o mesmo que `testes/05`) |
| `clock_config.cpp` | configuração de clock, HSI → PLL → 170 MHz |

### `include/` — headers do projeto

| Arquivo | |
|---|---|
| `board_pins.h` | pinagem completa + constantes elétricas (shunt 3 mΩ, ganho 20 V/V, 19 A) |
| `drv8353_regs.h` | mapa de registradores conferido linha a linha contra o SLVSDY6A Rev A, com os valores de reset |
| `current_backend.h` | os três backends de leitura de corrente atrás de uma interface |

### `testes/` — um por vez vira `src/main.cpp`

O PlatformIO **não** compila esta pasta, então eles podem conviver aqui
sem conflito de `setup()`/`loop()`. Para usar um, copie sobre
`src/main.cpp`.

Estão em ordem de profundidade de diagnóstico: do que não depende de nada
ao que depende de tudo.

| # | Arquivo | Responde |
|---|---|---|
| 00 | `blink_com_delay.cpp` | o básico com `delay()` |
| 01 | `blink_sem_delay.cpp` | o básico com `millis()` |
| 02 | `piso_blink.cpp` | o núcleo executa? o SysTick vive? (espera ocupada, não depende de clock) |
| 03 | `sonda_serial.cpp` | por qual UART sai a serial? (transmite em USART1 e USART2 ao mesmo tempo) |
| 04 | `spi_lab_didatico.cpp` | bancada de estudo de SPI, com exercícios de osciloscópio |
| 05 | `teste_spi_completo.cpp` | **o teste principal** — mapeamento, dump interpretado, bit-bang, loopback, varredura de modos |
| 06 | `estimulo_dc.cpp` | os sinais chegam ao DRV? (ciclo de trabalho conhecido, medível com multímetro) |
| 07 | `teste_escrita_via_adc.cpp` | as escritas chegam? (escreve `VREF_DIV`, mede SOx pelo próprio ADC) |

### `firmware_foc/` — escrito, não compila na build de hoje

O firmware de controle propriamente dito. Fica fora de `src/` porque
depende da SimpleFOC, que está desabilitada enquanto o SPI não fecha.

| Arquivo | |
|---|---|
| `drv8353.h` / `drv8353.cpp` | driver SPI do DRV8353RS: framing, escrita-com-releitura, `applyConfig`, decodificação de falhas |
| `drv8353_bringup.cpp` | bring-up completo: acorda → configura → COAST → autocalibração → calibração manual |
| `inline_bringup.cpp` | bring-up da leitura de corrente inline, em duas etapas |
| `platformio_duas_envs.ini` | o `platformio.ini` com as envs `inline` e `lowside` para o ensaio comparativo |

### `docs/`

| Arquivo | |
|---|---|
| `CHECKPOINT.md` / `ESC Checkpoint.md` | estado do projeto (a segunda é a versão Obsidian) |
| `GUIA_DO_CODIGO.md` / `ESC Guia do Código.md` | explicação didática do código |
| `plano_execucao_firmware.md` | roadmap M1–M10 com critério de aprovação por módulo |
| `roteiro_foc_simplefoc_g491.md` | roteiro de FOC com pseudocódigo |
| `plano_inline_simplefoc_g491.md` | análise de viabilidade de ADC e pinos |
| `port_f411_para_g491.md` | o que mudou na portabilidade do firmware antigo |
| `modelo-platformio-nucleo-g491re.md` | o template genérico, documentado |
| `nucleo-g491re-jumpers-e-solder-bridges.md` | cada jumper e solder bridge, e o efeito de fechar ou abrir |
| `Setup PlatformIO Arduino NUCLEO-G491RE.md` | criação do projeto passo a passo |
| `2026-09-11-estudo-spi-drv8353rs.md` | estudo do protocolo SPI do driver |
| `2026-09-21-review-drv8353-init.md` | revisão do código de init e calibração |

---

## Onde a investigação está

**Sintoma:** todos os 8 registradores do DRV8353RS leem `0x7FF`. O MISO
está preso em 1 — não é valor estranho, é ausência de resposta.

### Eliminado, com evidência

| Hipótese | Como caiu |
|---|---|
| mapeamento de pino (PC10/11/12 não serem SPI3) | `pinmap_peripheral` confirma SPI3 nos três; a instância do objeto é SPI3 |
| bug de software (`SPI` no lugar de `SPI_3`) | era real, foi corrigido, o sintoma não mudou |
| configuração do periférico SPI, modo, AF, biblioteca | bit-bang por GPIO puro dá resultado **idêntico** |
| driver não alimentado | na bancada: 13 mA (= `IVM` de operação), DVDD 5,0 V, VGLS 15,2 V, VCP 46 V, VREF 3,3 V — tudo em especificação |
| defeito de componente | segunda Ouroboros soldada, sintoma idêntico |
| `nFAULT = 0 V` indicar falha | artefato: o pull-up vai ao 3,3 V do micro, ausente com a Artemis fora |

### Hipótese dominante: **o driver está dormindo**

O datasheet, §8.4.1.1, sobre o sleep mode:

> *In sleep mode, all gate drivers are disabled, (…) the sense amplifiers
> are disabled, and **the SPI bus is disabled**. In sleep mode all the
> device registers will reset to their default values.*

Em sleep o barramento SPI não existe: o SDO fica em alta impedância, o
pull-up de 10 k segura a linha em 1, e **todo registrador lê `0x7FF`**.

Encaixa em tudo:

| Observação | Compatível com sleep |
|---|---|
| 8 registradores em `0x7FF` | sim — SPI desabilitado |
| `nFAULT` lido alto com a placa montada | sim — o pino é liberado em sleep |
| hardware SPI ≡ bit-bang | sim — irrelevante, o chip não escuta |
| chip saudável com ENABLE jumpeado na bancada | sim — ali ele estava acordado |
| *"o enable liga e desliga bem rápido ao ligar a placa"* | sim |

### Próximos dois testes

1. **Continuidade PD2 → pino 33 (ENABLE)**, fonte desligada. Medida
   direta. Fazer junto: nSCS→32, SCLK→31, SDI→30, SDO→29, todas < 1 Ω, e
   29↔30↔31↔32 abertas entre si.

2. **Degrau de corrente**: sistema montado, 36 V na fonte,
   `testes/07` rodando, apertar `e` para alternar o ENABLE. Acordado são
   13 mA, dormindo são 20 µA. Se o degrau de ~13 mA não aparecer, o
   ENABLE não chega ao chip.

Se o ENABLE estiver bom, `testes/07` separa "só o SDO quebrado" de "nada
chega": ele escreve o bit `VREF_DIV` (reg 0x06 bit 9) e mede a saída SOx
pelo próprio ADC do MCU — caminho de volta analógico, que não passa pelo
SDO.

---

## Restrições de bancada descobertas

Valem para qualquer medida futura:

- A **Ouroboros não pode ser energizada sem a Artemis**: o 5 V vem de um
  buck na Artemis, e o 3,3 V de um LDO alimentado por esse 5 V. Para
  medir com a Artemis fora, injete 5 V no conector e jumpeie o ENABLE à
  saída do LDO.
- Com a **Artemis encaixada, os pinos do DRV ficam inacessíveis** (ela
  fica em cima). Medida em pino do driver exige a Ouroboros sozinha, ou
  headers de extensão entre as duas placas.
- **`nFAULT` e `SDO` dependem do 3,3 V do micro** para ter nível alto.
  Nenhum dos dois pode ser avaliado com a Artemis desconectada.
- VM opera de **9 a 75 V** — use 12 V ou 36 V de bancada com limite de
  corrente em 200 mA em vez do pacote de 42 V.
- INHx/INLx têm **pull-down interno de 100 kΩ**: com a Artemis fora as
  entradas de PWM ficam seguras em 0, sem risco de acionar MOSFET.

---

## Pendências de hardware

Antes de energizar o conjunto completo, confira em
`docs/nucleo-g491re-jumpers-e-solder-bridges.md`:

- SB17/SB23 **abertos**, SB18/SB22 **fechados** (tira PA2/PA3 do VCP do
  ST-Link e leva ao morpho)
- SB13/SB19 **abertos** (senão LSCSA e HSCS ficam em curto)
- JP8 na posição **[2-3]** (VREF+ do ADC por VDD, não pelo TL1431)
- Option bytes: `nSWBOOT0 = 0`, `nBOOT0 = 1`
- 12 solder bridges ainda marcadas `[PENDENTE]` no documento
