---
data: 2026-09-11
tags:
  - ferramenta/platformio
  - plataforma/stm32g4
  - framework/arduino
  - classe/setup
  - projeto/eco-octano
aliases:
  - setup G491RE arduino
  - projeto novo NUCLEO-G491RE
  - receita platformio G491
status: validado
---

# Setup PlatformIO Arduino NUCLEO-G491RE

> [!abstract] O que esta nota resolve
> Criar do zero um projeto PlatformIO com framework **Arduino (stm32duino)**
> para a **NUCLEO-G491RE**, num computador novo ou existente, sem cair nas
> seis armadilhas que foram descobertas por eliminação no projeto do ESC.
>
> Relacionado: [[ESC Checkpoint]] · [[ESC Guia do Código]] ·
> [[2026-09-11-estudo-spi-drv8353rs]]

## Causa raiz comum

> [!important] Uma frase explica todas as armadilhas
> O stm32duino conhece o **chip** STM32G491, mas não conhece a **placa**
> NUCLEO-G491RE. Não existe variante de placa — só a variante genérica do chip,
> que ainda por cima é **compartilhada** com o G491RC e o G4A1RE.

Consequências, em ordem de aparição:

| # | Sintoma | Causa | Correção |
| - | ------- | ----- | -------- |
| 1 | O wizard só oferece STM32Cube | O board oficial não declara `arduino` | Board customizado em `boards/` |
| 2 | `variant_NUCLEO_G491RE_ARDUINO.h: No such file` | Nome do header é derivado do ID do board | `variant_h` + header de redirecionamento |
| 3 | HardFault antes do `main()`, nem blink roda | Variante declara 128 KB (valor do G4A1); o G491 tem 112 KB contíguos | `maximum_ram_size = 114688` |
| 4 | `Debug adapter doesn't support 'hla_swd'` | A placa tem ST-Link **V3E** | Upload customizado via OpenOCD |
| 5 | `delay()` trava; laço de contagem funciona | Clock da variante não bate com o declarado | `clock_config.cpp` próprio |
| 6 | Serial transmite mas não recebe | `HardwareSerial(rx, tx)` deduz periférico e falha no RX | `HardwareSerial(USART1)` + `setRx/setTx` |

> [!note]- Por que nenhuma delas dá mensagem útil
> As armadilhas 3 e 5 não geram erro de build: o firmware compila, grava,
> verifica e simplesmente não roda. A 6 compila e transmite normalmente. Só dá
> para achar com SWD (ver [[#Diagnóstico por SWD]]) e testes de eliminação.

## Estrutura de pastas

```
meu_projeto/
├── platformio.ini                         ← configuração (arquivo 1)
├── boards/
│   └── nucleo_g491re_arduino.json         ← board customizado (arquivo 2)
├── tools/
│   ├── install_variant.py                 ← script de pré-build (arquivo 3)
│   └── variant_NUCLEO_G491RE_ARDUINO.h    ← header de redirecionamento (arquivo 4)
├── include/                               ← headers do projeto (vazio no início)
└── src/
    ├── clock_config.cpp                   ← clock de 170 MHz (arquivo 5)
    └── main.cpp                           ← sua aplicação (arquivo 6)
```

> [!warning] Três regras de posição que já causaram erro
> - `boards/` fica na **raiz**, no mesmo nível do `platformio.ini`. Nome exato,
>   no plural, minúsculo.
> - `clock_config.cpp` e `main.cpp` são **dois arquivos separados** em `src/`.
>   Colar um dentro do outro dá `undefined reference to 'setup'`.
> - Arquivos de teste **não vão em `test/`**. Essa pasta é do framework de
>   testes unitários e **não é compilada** por `pio run` — o build ignora o
>   arquivo em silêncio e grava o firmware antigo.

## Passo a passo

### 1. Criar o projeto

- [ ] VS Code → PlatformIO Home → **New Project**
- [ ] Board: **Nucleo G491RE (ST)**
- [ ] Framework: **STM32Cube** — é o único que aparece; o passo 3 troca
- [ ] Location: de preferência um caminho **curto**, como `C:\pio\meu_projeto`

> [!tip] Por que caminho curto
> O Windows limita caminhos a 260 caracteres. Bibliotecas com exemplos muito
> aninhados (a SimpleFOC é uma) estouram esse limite dentro de
> `Documents\PlatformIO\Projects\nome_longo\.pio\libdeps\...` e a instalação
> falha com `PackageException`. Ver [[#Projetos com SimpleFOC]].

### 2. Criar as pastas

- [ ] Criar `boards/` na raiz do projeto
- [ ] Criar `tools/` na raiz do projeto
- [ ] Conferir que `src/` e `include/` já existem (o wizard cria)

### 3. Colocar os arquivos

- [ ] Substituir o `platformio.ini` gerado pelo **arquivo 1** — apagar tudo e colar
- [ ] Criar `boards/nucleo_g491re_arduino.json` com o **arquivo 2**
- [ ] Criar `tools/install_variant.py` com o **arquivo 3**
- [ ] Criar `tools/variant_NUCLEO_G491RE_ARDUINO.h` com o **arquivo 4**
- [ ] Criar `src/clock_config.cpp` com o **arquivo 5**
- [ ] Substituir `src/main.cpp` pelo **arquivo 6** (teste de fumaça)

> [!warning] Extensões ocultas no Windows
> Ative *Exibir → Extensões de nomes de arquivos* no Explorer. Arquivo criado no
> Bloco de Notas vira `variant_...h.txt` e o build não o encontra. Prefira criar
> os arquivos pelo próprio VS Code.

### 4. Primeiro build

- [ ] Rodar `pio run` (ou o ícone ✓ na barra inferior)

Num **computador novo** esse primeiro build baixa a plataforma `ststm32`, o
toolchain ARM e o `framework-arduinoststm32`. Leva alguns minutos.

> [!note] O primeiro build pode falhar — e está tudo bem
> Se ele reclamar de `variant_NUCLEO_G491RE_ARDUINO.h: No such file`, **rode
> de novo**. Na primeira execução o pacote do framework pode ainda não existir
> quando o script roda; na segunda ele existe, e o log mostra:
> ```
> [G491] header de variante instalado em ...
> ```

### 5. Conferir o log

O build está correto quando aparecem **todas** estas linhas:

```
HARDWARE: STM32G491RET6 170MHz, 112KB RAM, 512KB Flash
Compiling .pio\build\g491\src\clock_config.cpp.o
Compiling .pio\build\g491\src\main.cpp.o
RAM:   [          ]  ... (used ... bytes from 114688 bytes)
```

| Se aparecer | Significa |
| ----------- | --------- |
| `128KB RAM` ou `from 131072 bytes` | o `maximum_ram_size` não pegou — armadilha 3 |
| sem `clock_config.cpp.o` | o arquivo não está em `src/` — armadilha 5 |

### 6. Gravar

- [ ] Placa conectada pelo USB do ST-Link, jumper de alimentação em **5V_STLK**
- [ ] `pio run -t upload`

Sucesso é:

```
** Verified OK **
** Resetting Target **
```

### 7. Teste de fumaça

- [ ] Conversor USB-serial em PB6/PB7 (ver arquivo 6), 115200 baud
- [ ] Apertar o **reset preto** com o monitor já aberto
- [ ] Conferir `SYSCLK = 170000000` na saída
- [ ] Digitar qualquer caractere e ver o eco — prova que o RX funciona
- [ ] Medir PA0: deve alternar 0 / 3,3 V a cada 500 ms

> [!success] Critério de aprovação
> SYSCLK em 170 MHz, eco do RX funcionando e PA0 alternando. Com os três,
> todas as seis armadilhas estão resolvidas e o ambiente está pronto.

## Arquivos completos

### Arquivo 1 — `platformio.ini`

```ini
; NUCLEO-G491RE + framework Arduino (stm32duino)
; As linhas [G491] existem por causa de armadilhas especificas desta placa.
; Ver a nota "Setup PlatformIO Arduino NUCLEO-G491RE" no Obsidian.

[platformio]
default_envs = g491
boards_dir   = boards

[env:g491]
platform  = ststm32
; [G491] 1 - o board oficial nao declara o framework arduino
board     = nucleo_g491re_arduino
framework = arduino

; [G491] 2 - a variante generica nao tem header de placa
board_build.variant_h = variant_generic.h

; [G491] 2b - instala o header de redirecionamento antes de cada build
extra_scripts = pre:tools/install_variant.py

; [G491] 3 - o G491 tem 112 KB de RAM contigua, nao 128.
; O JSON sozinho NAO basta - esta linha e' obrigatoria.
board_upload.maximum_ram_size = 114688

; [G491] 4 - ST-Link V3E: upload pelo OpenOCD, gravando o .ELF
upload_protocol = custom
upload_command = openocd -s "$PROJECT_PACKAGES_DIR/tool-openocd/openocd/scripts" -f interface/stlink.cfg -c "transport select swd" -f target/stm32g4x.cfg -c "program {$BUILD_DIR/firmware.elf} verify reset; shutdown"

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
```

> [!warning] Uma chave, uma vez
> `upload_protocol` e `debug_tool` só podem aparecer **uma vez** por seção. Se
> ficar o `upload_protocol = stlink` antigo junto com o `custom`, o PlatformIO
> recusa o arquivo com `DuplicateOptionError`.

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
> Os campos `board`, `product_line`, `variant` e o tamanho de flash vêm do
> `boards_entry.txt` da própria variante, dentro de
> `framework-arduinoststm32/variants/STM32G4xx/G491RC(I-T)_.../`.
>
> A **exceção** é a RAM: o `boards_entry.txt` declara 131072, que é o valor do
> G4A1RE. O G491 tem 80 KB de SRAM1 + 16 KB de SRAM2 contíguos (até
> `0x2001C000`) e 16 KB de CCM em endereço separado. Só os 112 KB contíguos
> servem para o linker.

### Arquivo 3 — `tools/install_variant.py`

```python
# install_variant.py - [G491] instala o header de variante automaticamente
#
# Roda antes de cada build (extra_scripts = pre:...). Copia
# tools/variant_NUCLEO_G491RE_ARDUINO.h para dentro do pacote do framework,
# SE ele ainda nao estiver la'.
#
# O header precisa morar DENTRO do pacote, porque os fontes do framework sao
# compilados num ambiente que nao enxerga a pasta include/ do projeto.

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
  Redirecionamento. O PlatformIO deriva o nome do header de variante a partir
  do ID do board (nucleo_g491re_arduino -> NUCLEO_G491RE_ARDUINO). A variante
  generica do G491 so' tem variant_generic.h.

  Este arquivo e' copiado para dentro do pacote do framework pelo
  install_variant.py. Nao precisa ser movido a mao.
*/
#include "variant_generic.h"
```

### Arquivo 5 — `src/clock_config.cpp`

```cpp
/*
  [G491] Configuracao de clock - obrigatoria. Nao remova.

  A variante generica nao entrega os 170 MHz declarados. O SysTick fica
  configurado a partir de um SystemCoreClock que nao corresponde ao clock
  real, e o delay() trava ou fica muito mais lento.

  O SystemClock_Config() do core e' WEAK: esta definicao tem precedencia.
  Nada precisa ser chamado.

  CADEIA:
    HSI16 -> PLLM /4 -> 4 MHz -> PLLN x85 -> 340 MHz -> PLLR /2 -> 170 MHz
    AHB /1, APB1 /1, APB2 /1

  Usa HSI (interno, ~1%), nao HSE - o HSE da Nucleo depende de solder bridges.
*/

#include <Arduino.h>

extern "C" void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {};
    RCC_ClkInitTypeDef clk = {};

    // 170 MHz exige o regulador em BOOST.
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM            = RCC_PLLM_DIV4;
    osc.PLL.PLLN            = 85;
    osc.PLL.PLLP            = RCC_PLLP_DIV2;
    osc.PLL.PLLQ            = RCC_PLLQ_DIV2;
    osc.PLL.PLLR            = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { while (1) { } }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK  | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    // 4 wait states: obrigatorio em 170 MHz com VOS1 BOOST.
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) { while (1) { } }
}
```

### Arquivo 6 — `src/main.cpp` (teste de fumaça)

```cpp
/*
  Teste de fumaca - valida as seis correcoes de uma vez.

  Console: conversor USB-serial em PB6 (TX do MCU) e PB7 (RX do MCU),
  115200 baud, GND em comum, SEM ligar o VCC do conversor.

  Criterio de aprovacao:
    - SYSCLK = 170000000        (clock_config.cpp funcionando)
    - eco de cada tecla         (RX funcionando)
    - PA0 alternando a 1 Hz     (delay() funcionando)
*/

#include <Arduino.h>

// [G491] 6 - NAO use HardwareSerial(rx, tx): na variante generica a deducao
// do periferico falha no RX. Passe o periferico direto.
HardwareSerial SerialDbg(USART1);

void setup()
{
    // setRx/setTx TEM que vir antes do begin().
    SerialDbg.setRx(PB7);
    SerialDbg.setTx(PB6);
    SerialDbg.begin(115200);
    delay(300);

    // Use so' nomes PXn. A variante generica nao mapeia LED_BUILTIN, D13, A0.
    pinMode(PA0, OUTPUT);

    SerialDbg.println();
    SerialDbg.println(F("=== teste de fumaca NUCLEO-G491RE ==="));
    SerialDbg.print(F("SYSCLK = "));
    SerialDbg.println(HAL_RCC_GetSysClockFreq());
    SerialDbg.println(F("(deve ser 170000000)"));
    SerialDbg.println(F("digite algo - deve voltar em eco"));
}

void loop()
{
    if (SerialDbg.available()) {
        char c = SerialDbg.read();
        SerialDbg.print(F("eco: "));
        SerialDbg.println(c);
    }

    digitalWrite(PA0, (millis() / 500) & 1);
}
```

## Hardware da NUCLEO

> [!info] Onde ficam os pinos
> | Sinal | Pino | Morpho | Observação |
> | ----- | ---- | ------ | ---------- |
> | TX do MCU | PB6 | CN10-17 | vai no **RX** do conversor |
> | RX do MCU | PB7 | CN7-21 | vai no **TX** do conversor |
> | Teste | PA0 | CN7-28 | também é `A0` no header Arduino |
>
> PB6 e PB7 estão em **headers diferentes**. É fácil acertar um e errar o outro.

> [!note]- Sobre o VCP do ST-Link
> Por padrão o VCP do ST-Link usa PA2/PA3 (LPUART1), ligado pelos SB17/SB23.
> Nesse estado o console pode sair pelo próprio USB do ST-Link, sem conversor —
> desde que PA2/PA3 não sejam usados por mais nada.
>
> Na placa do ESC ([[ESC Checkpoint]]) esses bridges foram abertos, porque
> PA2/PA3 são sinais do shield. Aí o console **precisa** do conversor em PB6/PB7.

> [!warning] O PC13 não é LED
> Na NUCLEO-64, PC13 é o **botão B1**. O LED LD2 fica em PA5, e só funciona com
> o SB6 fechado. No firmware do ESC o SB6 foi aberto.

## Diagnóstico por SWD

Quando o firmware grava, verifica e **nada acontece**, o SWD diz onde o
processador está. No PowerShell:

```powershell
$ocd = "$env:USERPROFILE\.platformio\packages\tool-openocd"
& "$ocd\bin\openocd.exe" -s "$ocd\openocd\scripts" -f interface/stlink.cfg `
  -c "transport select swd" -f target/stm32g4x.cfg `
  -c "init; reset run; sleep 500; halt; reg pc; reg msp; shutdown"
```

| Resultado | Significado |
| --------- | ----------- |
| `current mode: Thread` | executando normalmente |
| `current mode: Handler HardFault` | travou — suspeite de RAM (armadilha 3) ou pino não mapeado |
| `msp` acima de `0x2001C000` | o tamanho de RAM não pegou |
| PC fixo em duas leituras seguidas | laço infinito — suspeite de `delay()` (armadilha 5) |

> [!tip] Ler o nível de um pino sem multímetro
> `mdw 0x48000014` lê o registrador ODR do GPIOA. O bit 0 é o PA0. Leia duas
> vezes com intervalo: se o bit alternar, o código está acionando o pino e o
> problema é a medida, não o firmware.

## Projetos com SimpleFOC

Para projetos que usam a SimpleFOC, acrescente ao `platformio.ini`:

```ini
[platformio]
; caminho curto - a SimpleFOC estoura o limite de 260 caracteres do Windows
libdeps_dir = C:/pio/lib

[env:g491]
; OBRIGATORIO: sem isto o linker descarta os simbolos especificos do STM32
; e o current sense por canal injetado nao funciona
lib_archive = no

lib_deps =
    askuric/Simple FOC@^2.4.0
    simplefoc/SimpleFOCDrivers@^1.0.8
```

Confirmação de que o `lib_archive` pegou: o log mostra
`stm32g4_hal.cpp` sendo compilado.

## Checklist de computador novo

- [ ] VS Code + extensão PlatformIO instalados
- [ ] Projeto criado em caminho curto
- [ ] Os seis arquivos nos lugares certos
- [ ] Primeiro `pio run` — se falhar no header, rodar de novo
- [ ] Log mostra `112KB RAM` e `clock_config.cpp.o`
- [ ] `pio run -t upload` termina com `Verified OK`
- [ ] Teste de fumaça: SYSCLK 170 MHz, eco no RX, PA0 piscando
