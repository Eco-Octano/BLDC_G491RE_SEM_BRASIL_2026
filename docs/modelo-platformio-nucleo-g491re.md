---
data: 2026-09-21
atualizado: 2026-09-21
versao: 1.1
status: vigente
tags:
  - doc/procedimento
  - plataforma/stm32g4
  - ferramenta/platformio
  - framework/arduino
aliases:
  - modelo projeto G491RE
  - template NUCLEO-G491RE
  - projeto novo platformio G491
---

# Modelo de projeto PlatformIO para NUCLEO-G491RE

> [!abstract] Resumo
> Passo a passo para **criar do zero** um projeto PlatformIO com framework
> **Arduino (stm32duino)** para a **NUCLEO-G491RE**, com as seis correções
> específicas da placa. O programa de validação pisca o LED LD2 ao apertar o
> botão azul B1 e usa o console pelo USB do ST-LINK.
>
> Configuração física da placa: [[nucleo-g491re-jumpers-e-solder-bridges]]

> [!warning] Correções em relação à nota anterior
> Esta nota substitui [[Setup PlatformIO Arduino NUCLEO-G491RE]]. Dois pontos
> daquela nota estavam errados:
> - **"PA5 causa HardFault na variante genérica"** — hipótese não confirmada.
>   O HardFault vinha do tamanho de RAM; PA5 funciona normalmente pelo nome
>   `PA5`. Este modelo usa PA5 para o LD2.
> - **"A CCM fica em endereço separado"** — explicação sem base. O fato
>   verificado é empírico: a RAM é acessível até `0x2001C000` e falha acima
>   disso. Ver [[#Pendências]].

## Escopo

**Cobre:** criação de projeto novo pelo wizard do PlatformIO, criação de cada
arquivo, gravação e validação na NUCLEO-G491RE em configuração de fábrica, num
computador novo ou existente.

**Não cobre:** placas-shield e alterações de solder bridge (ver
[[nucleo-g491re-jumpers-e-solder-bridges]]), bibliotecas específicas além da
nota sobre SimpleFOC, e o framework STM32Cube.

## Causa comum das armadilhas

> [!important] Uma frase explica todas
> O stm32duino conhece o **chip** STM32G491, não a **placa** NUCLEO-G491RE. Só
> existe a variante genérica do chip, compartilhada com o G491RC e o G4A1RE.

| # | Sintoma | Correção | Onde |
| - | ------- | -------- | ---- |
| 1 | O wizard só oferece o framework STM32Cube | board customizado | `boards/nucleo_g491re_arduino.json` |
| 2 | `variant_NUCLEO_G491RE_ARDUINO.h: No such file` | header de redirecionamento, instalado por script | `tools/` + `platformio.ini` |
| 3 | HardFault antes do `main()`; nem blink roda | RAM de 112 KB, não 128 | `platformio.ini` + JSON |
| 4 | `Debug adapter doesn't support 'hla_swd'` | gravação via OpenOCD, pelo `.elf` | `platformio.ini` |
| 5 | `delay()` trava; laço de contagem funciona | clock de 170 MHz próprio | `src/clock_config.cpp` |
| 6 | Serial transmite mas não recebe | `HardwareSerial(LPUART1)` + `setRx/setTx` | `src/main.cpp` |

## Estrutura final

É isto que você terá ao fim do passo 5:

```
meu_projeto/
├── platformio.ini                        ← arquivo 1 (substitui o do wizard)
├── boards/
│   └── nucleo_g491re_arduino.json        ← arquivo 2
├── tools/
│   ├── install_variant.py                ← arquivo 3
│   └── variant_NUCLEO_G491RE_ARDUINO.h   ← arquivo 4
├── include/                              ← criada pelo wizard
├── lib/                                  ← criada pelo wizard
├── test/                                 ← criada pelo wizard (não use)
└── src/
    ├── clock_config.cpp                  ← arquivo 5
    └── main.cpp                          ← arquivo 6
```

> [!warning] Regras de posição
> - `boards/` e `tools/` ficam na **raiz**, ao lado do `platformio.ini`.
>   Nomes exatos, minúsculos, `boards` no plural.
> - `clock_config.cpp` e `main.cpp` são **arquivos separados** em `src/`.
>   Juntar os dois dá `undefined reference to 'setup'`.
> - Código **nunca** em `test/`: essa pasta não é compilada por `pio run`, e o
>   build grava o firmware antigo sem avisar.

## Procedimento

### Passo 1 — Pré-requisitos

- [ ] VS Code com a extensão **PlatformIO IDE** instalada
- [ ] Placa em configuração de fábrica: **JP5 em [1-2]**, **JP6 ON**,
      **JP8 em [1-2]**, nada ligado nos headers
- [ ] Cabo USB **de dados** (cabo só de carga não enumera) no conector CN1

### Passo 2 — Criar o projeto no wizard

- [ ] Barra lateral → ícone do PlatformIO (cabeça de formiga) → **Home**
- [ ] **New Project**
- [ ] **Name:** o nome do projeto, sem espaços
- [ ] **Board:** `Nucleo G491RE (ST)`
- [ ] **Framework:** `STM32Cube`
- [ ] **Location:** desmarcar *Use default location* e escolher um caminho
      **curto**, por exemplo `C:\pio`
- [ ] **Finish** e aguardar a criação da pasta

> [!question]- Por que STM32Cube, se o projeto é Arduino?
> O board oficial `nucleo_g491re` só declara o framework STM32Cube, então é o
> único que o wizard oferece. O passo 4 substitui o `platformio.ini` e troca
> tanto o board quanto o framework. O wizard serve só para criar a pasta.

> [!tip] Por que caminho curto
> O Windows limita caminhos a 260 caracteres. Bibliotecas com exemplos muito
> aninhados estouram esse limite dentro de
> `Documents\PlatformIO\Projects\...\.pio\libdeps\` e a instalação falha com
> `PackageException`.

Se o wizard criar algum arquivo dentro de `src/` (`main.c` ou `main.cpp`),
**apague-o**: o projeto terá só os dois arquivos do passo 5.

### Passo 3 — Criar as pastas

No explorador de arquivos do VS Code, com o botão direito sobre o **nome do
projeto** (a raiz, não sobre `src/`):

- [ ] **New Folder** → `boards`
- [ ] **New Folder** → `tools`

### Passo 4 — Substituir o `platformio.ini`

- [ ] Abrir o `platformio.ini` que o wizard criou na raiz
- [ ] **Apagar todo o conteúdo** e colar o **arquivo 1**
- [ ] Salvar (Ctrl+S)

> [!warning] Uma chave, uma vez
> Não junte o conteúdo novo ao antigo. `upload_protocol`, `board` e
> `framework` só podem aparecer uma vez por seção; se sobrar o
> `upload_protocol` do wizard, o PlatformIO recusa o arquivo com
> `DuplicateOptionError`.

Ao salvar, o VS Code pode reindexar o projeto e reclamar do board. É esperado:
os arquivos seguintes resolvem.

### Passo 5 — Criar os demais arquivos

Para cada arquivo: botão direito sobre a **pasta de destino** → **New File** →
digitar o nome exato → colar o conteúdo → salvar.

- [ ] `boards/nucleo_g491re_arduino.json` ← **arquivo 2**
- [ ] `tools/install_variant.py` ← **arquivo 3**
- [ ] `tools/variant_NUCLEO_G491RE_ARDUINO.h` ← **arquivo 4**
- [ ] `src/clock_config.cpp` ← **arquivo 5**
- [ ] `src/main.cpp` ← **arquivo 6**

> [!warning] Crie os arquivos pelo VS Code, não pelo Bloco de Notas
> O Bloco de Notas acrescenta `.txt` escondido: `install_variant.py` vira
> `install_variant.py.txt` e o build não o encontra. Se precisar usar o
> Explorer do Windows, ative *Exibir → Extensões de nomes de arquivos*.

- [ ] Conferir a árvore contra a [[#Estrutura final]]

### Passo 6 — Primeiro build

- [ ] Terminal do PlatformIO: `pio run` (ou o ícone ✓ na barra inferior)

Num computador novo, esse build baixa a plataforma `ststm32`, o toolchain ARM e
o `framework-arduinoststm32`. Leva alguns minutos.

> [!note] O primeiro build pode falhar uma vez
> Se reclamar de `variant_NUCLEO_G491RE_ARDUINO.h: No such file`, **rode de
> novo**. Na primeira execução o pacote do framework pode ainda não existir
> quando o script roda. Na segunda, o log mostra:
> ```
> [G491] header de variante instalado em ...
> ```

O build está correto quando o log mostra **todas** estas linhas:

```
HARDWARE: STM32G491RET6 170MHz, 112KB RAM, 512KB Flash
Compiling .pio\build\nucleo_g491re\src\clock_config.cpp.o
Compiling .pio\build\nucleo_g491re\src\main.cpp.o
RAM:   [          ]  ... (used ... bytes from 114688 bytes)
```

| Se aparecer | Causa | Correção |
| ----------- | ----- | -------- |
| `128KB RAM` ou `from 131072 bytes` | falta `board_upload.maximum_ram_size` | conferir o arquivo 1 |
| sem `clock_config.cpp.o` | arquivo fora de `src/` | mover para `src/` |
| `Unknown board ID` | JSON fora de `boards/` ou nome diferente | conferir o passo 5 |
| `undefined reference to 'setup'` | `main.cpp` vazio ou com o conteúdo errado | conferir o arquivo 6 |

### Passo 7 — Gravar

- [ ] Placa ligada pelo USB do ST-LINK
- [ ] `pio run -t upload` (ou o ícone → na barra inferior)

Sucesso é:

```
** Verified OK **
** Resetting Target **
```

### Passo 8 — Validar

- [ ] Abrir o monitor: `pio device monitor -b 115200`
      (ou o ícone 🔌 na barra inferior)
- [ ] Apertar o botão **preto** (reset), **sem** segurar o azul
- [ ] Conferir as quatro evidências

| Evidência | Prova |
| --------- | ----- |
| LD2 (verde) pisca 3 vezes no boot | gravação e `delay()` |
| Monitor mostra `SYSCLK : 170000000 Hz` | clock de 170 MHz ativo |
| Botão azul liga e desliga um pisca de 2 Hz | GPIO de entrada e de saída |
| Tecla digitada volta como `eco: x` | RX e TX do console |

> [!success] Critério de aprovação
> As quatro evidências presentes. O ambiente está pronto para a aplicação.

> [!tip] Porta COM
> `pio device list` mostra as portas. O ST-LINK aparece como
> *STMicroelectronics STLink Virtual COM Port*. Se a tela ficar vazia, aperte o
> reset com o monitor já aberto: o banner sai uma vez só, 300 ms após o boot.

## Arquivos

### Arquivo 1 — `platformio.ini`

```ini
; ============================================================================
;  MODELO DE PROJETO - NUCLEO-G491RE + framework Arduino (stm32duino)
; ============================================================================
;  Placa em configuracao de FABRICA (nenhuma solder bridge alterada).
;  As linhas marcadas [G491] corrigem armadilhas especificas desta placa no
;  PlatformIO. Nao remova nenhuma delas - ver docs/ no pacote.
; ============================================================================

[platformio]
default_envs = nucleo_g491re
boards_dir   = boards

[env:nucleo_g491re]
platform  = ststm32
framework = arduino

; [G491] 1 - o board oficial "nucleo_g491re" nao declara o framework arduino.
; Este e' o board customizado da pasta boards/.
board = nucleo_g491re_arduino

; [G491] 2 - a variante do stm32duino e' generica (do chip, nao da placa) e
; so' tem variant_generic.h.
board_build.variant_h = variant_generic.h

; [G491] 2b - instala o header de redirecionamento dentro do pacote do
; framework antes de cada build (necessario em todo computador novo).
extra_scripts = pre:tools/install_variant.py

; [G491] 3 - RAM contigua real do G491: 112 KB (acessivel ate' 0x2001C000).
; A variante declara 128 KB, o que poe a pilha em memoria inexistente e causa
; HardFault antes do main(). O JSON sozinho nao basta: esta linha e' obrigatoria.
board_upload.maximum_ram_size = 114688

; [G491] 4 - a placa tem ST-LINK V3E. O upload padrao do PlatformIO assume
; ST-LINK V2 e falha com "Debug adapter doesn't support 'hla_swd' transport".
; Grava o .ELF (o .bin nao carrega endereco e vai para 0x00000000).
upload_protocol = custom
upload_command  = openocd -s "$PROJECT_PACKAGES_DIR/tool-openocd/openocd/scripts" -f interface/stlink.cfg -c "transport select swd" -f target/stm32g4x.cfg -c "program {$BUILD_DIR/firmware.elf} verify reset; shutdown"

debug_tool   = custom
debug_server =
    openocd
    -s
    $PROJECT_PACKAGES_DIR/tool-openocd/openocd/scripts
    -f
    interface/stlink.cfg
    -c
    transport select swd
    -f
    target/stm32g4x.cfg

monitor_speed = 115200
build_flags   = -O2

; ----------------------------------------------------------------------------
;  OPCIONAL - projetos com SimpleFOC. Descomente as linhas abaixo e
;  acrescente, na secao [platformio] acima:
;      libdeps_dir = C:/pio/lib
;  (a SimpleFOC estoura o limite de 260 caracteres de caminho do Windows)
; ----------------------------------------------------------------------------
; lib_archive = no
; lib_deps =
;     askuric/Simple FOC@^2.4.0
;     simplefoc/SimpleFOCDrivers@^1.0.8
```

### Arquivo 2 — `boards/nucleo_g491re_arduino.json`

```json
{
  "build": {
    "board": "GENERIC_G491RETX",
    "core": "stm32",
    "cpu": "cortex-m4",
    "extra_flags": "-DSTM32G4xx -DSTM32G491xx -DARDUINO_GENERIC_G491RETX",
    "f_cpu": "170000000L",
    "framework_extra_flags": {
      "arduino": "-DHAL_UART_MODULE_ENABLED"
    },
    "mcu": "stm32g491ret6",
    "product_line": "STM32G491xx",
    "variant": "STM32G4xx/G491RC(I-T)_G491RE(I-T-Y)x(Z)_G4A1RE(I-T-Y)",
    "variant_h": "variant_generic.h"
  },
  "connectivity": ["can"],
  "debug": {
    "default_tools": ["stlink"],
    "jlink_device": "STM32G491RE",
    "onboard_tools": ["stlink"],
    "openocd_target": "stm32g4x",
    "svd_path": "STM32G491.svd"
  },
  "frameworks": ["arduino", "cmsis", "stm32cube"],
  "name": "Nucleo G491RE (Arduino / stm32duino generic)",
  "upload": {
    "maximum_ram_size": 114688,
    "maximum_size": 524288,
    "protocol": "stlink",
    "protocols": ["blackmagic", "cmsis-dap", "jlink", "stlink"]
  },
  "url": "https://www.st.com/en/evaluation-tools/nucleo-g491re.html",
  "vendor": "ST"
}
```

> [!note]- De onde vieram os valores
> `board`, `product_line`, `variant` e o tamanho de flash vêm do
> `boards_entry.txt` da própria variante, dentro de
> `framework-arduinoststm32/variants/STM32G4xx/G491RC(I-T)_.../`. A exceção é a
> RAM: o `boards_entry.txt` declara 131072, e a placa só funciona com 114688.

### Arquivo 3 — `tools/install_variant.py`

```python
# install_variant.py - [G491] instala o header de variante automaticamente
#
# Roda antes de cada build (extra_scripts = pre:...). Copia
# tools/variant_NUCLEO_G491RE_ARDUINO.h para dentro do pacote do framework,
# SE ele ainda nao estiver la'.
#
# Por que existe: o PlatformIO deriva o nome do header de variante a partir
# do ID do board (nucleo_g491re_arduino -> variant_NUCLEO_G491RE_ARDUINO.h).
# A variante generica do G491 so' tem variant_generic.h, e o build falha.
# O header precisa morar DENTRO do pacote, porque os fontes do framework sao
# compilados num ambiente que nao enxerga a pasta include/ do projeto.
#
# Colocar a copia a mao funciona, mas some em cada computador novo, em cada
# reinstalacao e em cada atualizacao do framework. Este script resolve isso.

Import("env")
import os
import shutil

HEADER  = "variant_NUCLEO_G491RE_ARDUINO.h"
VARIANT = os.path.join("variants", "STM32G4xx",
                       "G491RC(I-T)_G491RE(I-T-Y)x(Z)_G4A1RE(I-T-Y)")

pkg = env.PioPlatform().get_package_dir("framework-arduinoststm32")
src = os.path.join(env.subst("$PROJECT_DIR"), "tools", HEADER)

if not pkg:
    print("[G491] framework-arduinoststm32 ainda nao instalado - rode o build de novo")
elif not os.path.isfile(src):
    print("[G491] ERRO: nao achei %s" % src)
else:
    dst_dir = os.path.join(pkg, VARIANT)
    dst     = os.path.join(dst_dir, HEADER)
    if not os.path.isdir(dst_dir):
        print("[G491] ERRO: pasta da variante nao existe: %s" % dst_dir)
        print("[G491] a versao do framework mudou o nome da pasta - confira")
    elif not os.path.isfile(dst):
        shutil.copyfile(src, dst)
        print("[G491] header de variante instalado em %s" % dst)
```

### Arquivo 4 — `tools/variant_NUCLEO_G491RE_ARDUINO.h`

```c
/*
  [G491] Header de redirecionamento.

  O PlatformIO deriva o nome do header de variante a partir do ID do board
  (nucleo_g491re_arduino -> variant_NUCLEO_G491RE_ARDUINO.h). A variante
  generica do G491 so' tem variant_generic.h.

  NAO precisa ser copiado a mao: o tools/install_variant.py coloca este
  arquivo dentro do pacote framework-arduinoststm32 antes de cada build.
*/
#include "variant_generic.h"
```

### Arquivo 5 — `src/clock_config.cpp`

```cpp
/*
  clock_config.cpp - clock de 170 MHz para o STM32G491RE
  [G491] Obrigatorio. Nao remova.

  POR QUE EXISTE
  A variante generica do stm32duino nao entrega os 170 MHz declarados no
  board. O SysTick fica configurado a partir de um SystemCoreClock que nao
  corresponde ao clock real, e o delay() trava ou fica muito mais lento.
  Sintoma: blink com delay() parado; o mesmo blink com laco de contagem pisca.

  O SystemClock_Config() do core e' declarado WEAK, entao esta definicao tem
  precedencia automaticamente. Nada precisa ser chamado.

  CADEIA (validada em bancada)
    HSI16 -> PLLM /4 -> 4 MHz -> PLLN x85 -> 340 MHz (VCO) -> PLLR /2 -> 170 MHz
    AHB /1, APB1 /1, APB2 /1  ->  todos em 170 MHz
  170 MHz exige regulador em SCALE1 BOOST e 4 wait states na flash.

  SOBRE A FONTE DE CLOCK
  Usa o HSI (RC interno, ~1% de precisao). A NUCLEO-G491RE tem, de fabrica,
  um cristal HSE de 24 MHz (X3, com SB25/SB26 ON - UM2505 sec. 7.5.1), que e'
  mais preciso. A alternativa com HSE esta' comentada no fim do arquivo, mas
  NAO foi validada em bancada.
*/

#include <Arduino.h>

extern "C" void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {};
    RCC_ClkInitTypeDef clk = {};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM            = RCC_PLLM_DIV4;   // 16 MHz / 4  =   4 MHz
    osc.PLL.PLLN            = 85;              //  4 MHz * 85 = 340 MHz
    osc.PLL.PLLP            = RCC_PLLP_DIV2;
    osc.PLL.PLLQ            = RCC_PLLQ_DIV2;
    osc.PLL.PLLR            = RCC_PLLR_DIV2;   // 340 MHz / 2 = 170 MHz

    // Sem clock nao ha' o que fazer: trava de proposito.
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { while (1) { } }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK  | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) { while (1) { } }
}

/*
  ALTERNATIVA COM HSE 24 MHz (NAO VALIDADA EM BANCADA)
  Trocar o bloco do oscilador por:

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = RCC_PLLM_DIV6;   // 24 MHz / 6  =   4 MHz
    osc.PLL.PLLN       = 85;              //  4 MHz * 85 = 340 MHz
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = RCC_PLLQ_DIV2;
    osc.PLL.PLLR       = RCC_PLLR_DIV2;   // 170 MHz

  E conferir que o core compila com HSE_VALUE = 24000000 (acrescentar
  -DHSE_VALUE=24000000 ao build_flags do platformio.ini). Validar com o
  SYSCLK impresso no boot e medindo o periodo do LED.
*/
```

### Arquivo 6 — `src/main.cpp`

```cpp
/*
  MODELO NUCLEO-G491RE - validacao de build, gravacao e placa
  ===========================================================

  Placa em configuracao de FABRICA: nenhuma solder bridge alterada, JP5 em
  [1-2] (5V_STLK), nada conectado nos headers. So' o cabo USB do ST-LINK.

  O QUE ESTE PROGRAMA VALIDA
    - build e gravacao            -> se o LED piscou 3x no boot, gravou
    - clock de 170 MHz            -> SYSCLK impresso no monitor serial
    - delay()/millis()            -> LED pisca no ritmo certo (2 Hz)
    - GPIO de saida               -> LED verde LD2 (PA5)
    - GPIO de entrada             -> botao azul B1 USER (PC13)
    - console pelo USB do ST-LINK -> Virtual COM Port (LPUART1, PA2/PA3)

  COMO USAR
    1. Grave (pio run -t upload)
    2. Abra o monitor serial:  pio device monitor -b 115200
    3. Aperte o botao PRETO (reset): o LD2 pisca 3x e o banner aparece
    4. Aperte o botao AZUL (B1): o LD2 comeca a piscar. Aperte de novo: para
    5. Digite qualquer tecla no monitor: ela volta em eco

  NOTAS DE PROJETO
    - Nao use os aliases Arduino (LED_BUILTIN, D13, A0...): a variante e'
      generica e nao traz o mapeamento de placa da Nucleo. Use sempre PXn.
    - HardwareSerial e' construido com o PERIFERICO (LPUART1), nao com os
      pinos: HardwareSerial(rx, tx) deduz o periferico e, na variante
      generica, o RX nao funciona (TX transmite, available() sempre falso).
    - A polaridade do botao nao e' presumida: o nivel com o botao SOLTO e'
      lido no boot, e "apertado" e' o nivel oposto. Por isso nao segure o
      botao azul durante o reset.
*/

#include <Arduino.h>

// ------------------------------------------------------------------ pinos
// Mapeamento de fabrica, UM2505 sec. 7.6.1, 7.6.2 e 7.6.5.
#define PIN_LED       PA5    // LD2 verde, ativo em alto (SB6 ON)
#define PIN_BUTTON    PC13   // B1 azul USER (SB16 ON, SB21 OFF)
#define PIN_VCP_TX    PA2    // LPUART1_TX -> ST-LINK VCP (SB17 ON)
#define PIN_VCP_RX    PA3    // LPUART1_RX <- ST-LINK VCP (SB23 ON)

// ------------------------------------------------------------ parametros
static const uint32_t DEBOUNCE_MS    = 30;
static const uint32_t BLINK_HALF_MS  = 250;   // 2 Hz

// --------------------------------------------------------------- objetos
HardwareSerial SerialVcp(LPUART1);

// ---------------------------------------------------------------- estado
static int      btn_idle;          // nivel do botao solto, lido no boot
static int      btn_raw_last;
static int      btn_stable;
static uint32_t btn_t_change = 0;
static bool     blinking     = false;
static uint32_t presses      = 0;

// ================================================================= SETUP
void setup()
{
    // setRx/setTx TEM que vir antes do begin().
    SerialVcp.setRx(PIN_VCP_RX);
    SerialVcp.setTx(PIN_VCP_TX);
    SerialVcp.begin(115200);

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    // O botao da Nucleo tem circuito proprio de polarizacao na placa.
    pinMode(PIN_BUTTON, INPUT);

    // Tres piscadas de 100 ms: prova visual de que gravou e o delay() anda,
    // mesmo sem monitor serial aberto.
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, HIGH); delay(100);
        digitalWrite(PIN_LED, LOW);  delay(100);
    }

    btn_idle     = digitalRead(PIN_BUTTON);
    btn_raw_last = btn_idle;
    btn_stable   = btn_idle;

    SerialVcp.println();
    SerialVcp.println(F("=== MODELO NUCLEO-G491RE ==="));
    SerialVcp.print(F("SYSCLK            : "));
    SerialVcp.print(HAL_RCC_GetSysClockFreq());
    SerialVcp.println(F(" Hz (esperado 170000000)"));
    SerialVcp.print(F("botao B1 solto le : "));
    SerialVcp.println(btn_idle == HIGH ? F("HIGH") : F("LOW"));
    SerialVcp.println(F("aperte o botao azul para ligar/desligar o pisca"));
    SerialVcp.println(F("digite algo no monitor para testar o eco"));
    SerialVcp.println();
}

// ================================================================== LOOP
void loop()
{
    const uint32_t now = millis();

    // --- botao, com debounce -------------------------------------------
    const int raw = digitalRead(PIN_BUTTON);
    if (raw != btn_raw_last) {
        btn_raw_last = raw;
        btn_t_change = now;
    }
    if ((now - btn_t_change) > DEBOUNCE_MS && raw != btn_stable) {
        btn_stable = raw;
        if (btn_stable != btn_idle) {          // borda de APERTAR
            presses++;
            blinking = !blinking;
            if (!blinking) digitalWrite(PIN_LED, LOW);
            SerialVcp.print(F("B1 #"));
            SerialVcp.print(presses);
            SerialVcp.println(blinking ? F(" -> pisca LIGADO") : F(" -> pisca DESLIGADO"));
        }
    }

    // --- LED -------------------------------------------------------------
    if (blinking) {
        digitalWrite(PIN_LED, (now / BLINK_HALF_MS) & 1);
    }

    // --- eco serial ------------------------------------------------------
    if (SerialVcp.available()) {
        const char c = SerialVcp.read();
        if (c != '\r' && c != '\n') {
            SerialVcp.print(F("eco: "));
            SerialVcp.println(c);
        }
    }
}
```

## Começar a aplicação

1. Substituir o conteúdo de `src/main.cpp`
2. **Manter** `src/clock_config.cpp`
3. Para console, manter o padrão `HardwareSerial(LPUART1)` com
   `setRx(PA3)` / `setTx(PA2)` antes do `begin()`
4. Referir pinos sempre por `PXn`, nunca por aliases Arduino

> [!warning] PA2/PA3 são do console por padrão
> Com as solder bridges de fábrica, PA2/PA3 vão ao ST-LINK e não chegam ao
> header. Para usá-los num shield e mover o console, ver
> [[nucleo-g491re-jumpers-e-solder-bridges#Receitas de configuração]].

> [!tip] Atalho para os próximos projetos
> O `template-nucleo-g491re.zip` contém esta mesma estrutura pronta. Depois de
> fazer o procedimento completo uma vez, basta descompactar num caminho curto e
> usar **Open Project** no PlatformIO Home.

## Diagnóstico por SWD

Quando o firmware grava, verifica e nada acontece, no PowerShell:

```powershell
$ocd = "$env:USERPROFILE\.platformio\packages\tool-openocd"
& "$ocd\bin\openocd.exe" -s "$ocd\openocd\scripts" -f interface/stlink.cfg `
  -c "transport select swd" -f target/stm32g4x.cfg `
  -c "init; reset run; sleep 500; halt; reg pc; reg msp; shutdown"
```

| Resultado | Diagnóstico |
| --------- | ----------- |
| `current mode: Thread` | firmware executando |
| `current mode: Handler HardFault` | travou — conferir a correção 3 |
| `msp` acima de `0x2001C000` | `maximum_ram_size` não pegou |
| PC igual em duas leituras | laço infinito — conferir a correção 5 |

Para ler o nível de um pino sem multímetro: `mdw 0x48000014` lê o ODR do
GPIOA; o bit 5 é o PA5 (LD2).

## Projetos com SimpleFOC

O arquivo 1 traz o bloco comentado. Ao ativar:

- descomentar `lib_archive = no` e `lib_deps`
- acrescentar `libdeps_dir = C:/pio/lib` em `[platformio]`

`lib_archive = no` é obrigatório: sem ele o linker descarta o código
específico do STM32 e o current sense por canal injetado não funciona.
Confirmação no log: `stm32g4_hal.cpp` sendo compilado.

## Pendências

- [ ] Marcar [[Setup PlatformIO Arduino NUCLEO-G491RE]] como `status: substituido`, com link para esta nota
- [ ] Validar em bancada o console por `HardwareSerial(LPUART1)` — no projeto do ESC foi validado USART1, não LPUART1
- [ ] [PENDENTE: tamanho de SRAM do G491RET6 — o UM2505 Rev 7 lista 96 KB, mas a memória foi acessível até `0x2001C000` (112 KB) e o firmware roda com a pilha lá. Conferir o mapa de SRAM no datasheet do STM32G491xx e no RM0440]
- [ ] [PENDENTE: polaridade elétrica do botão B1 — conferir no esquemático do MB1367. O firmware detecta o nível no boot e não depende disso]
- [ ] [PENDENTE: se o wizard, com framework STM32Cube, cria algum arquivo em `src/` — confirmar na primeira criação e ajustar o passo 2]
- [ ] Validar em bancada a alternativa de clock com HSE 24 MHz comentada no arquivo 5

## Histórico de revisões

| Versão | Data | Alteração |
| ------ | ---- | --------- |
| 1.1 | 2026-09-21 | Procedimento reescrito para criação do projeto do zero pelo wizard, com criação de pastas e de cada arquivo, conteúdo completo dos seis arquivos e tabela de erros do primeiro build. O zip passa a ser atalho, não o caminho principal |
| 1.0 | 2026-09-21 | Versão inicial. Pacote genérico com programa de validação por botão e LED, console pelo VCP do ST-LINK, script de instalação automática do header de variante, e correção de duas afirmações da nota anterior |
