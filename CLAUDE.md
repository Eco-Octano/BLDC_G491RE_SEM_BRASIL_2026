# CLAUDE.md — ESC Eco Octano (NUCLEO-G491RE + DRV8353RS)

Firmware do ESC do veículo elétrico do Eco Octano UFPR. Este arquivo vale
para qualquer sessão do Claude Code neste repositório.

## Hardware

- **MCU:** STM32G491RE na NUCLEO-G491RE, 170 MHz, 112 KB de RAM contígua.
- **Artemis:** adaptador que fica sobre a Nucleo.
- **Ouroboros:** placa de potência com o DRV8353RS (RGZ-48), sem MCU.
- **Motor:** BLDC com halls, tensão de barramento de 48 V. Modelo 43 do
  catálogo 129H43T, **5 pares de polos** (informado pelo usuário em
  2026-10-17). O `POLE_PAIRS = 88` do `firmware_foc/inline_bringup.cpp`
  está errado. Catálogo Koford: 10 polos, ranhurado; halls a 120°,
  alimentados em **+5 V**, saída em coletor aberto (dependem do pull-up para
  3,3 V; já usados assim pelo usuário). Cabos: fase A azul, B branco, C marrom; +5 V
  vermelho, terra preto; hall A amarelo, B laranja, C verde.
- Há dois grupos de sensores de corrente: amplificadores externos *inline*
  (PC0/PA7/PB13) e CSAs internos do driver *low-side* (PA2/PA6/PB0). O
  objetivo final é comparar os dois.

## Comandos

```bash
pio run                       # compila
pio run -t upload             # grava (OpenOCD, ST-Link V3E)
pio device monitor -p COM11   # console; use a porta da FTDI
```

A porta do console é a **FTDI**, nunca a VCP do ST-Link: a USART2 da VCP usa
PA2, que aqui é o `LSCS_A`.

## Regras fixas

- **Console:** USART1, `RX = PB7`, `TX = PB6`, 115200. Nunca inverter nem
  variar esses pinos.
- **ERRATA 001:** MOSI e MISO estão trocados na Ouroboros. O SPI de
  hardware não funciona; o driver usa bit-bang (`DRV_PINO_SDI = PC11`,
  `DRV_PINO_SDO = PC12`). Ao corrigir a placa, só inverte essas duas linhas em
  `include/board_pins.h`.
- **Não mexer no `platformio.ini`, em `boards/` nem em `tools/`** sem ler a
  tabela de armadilhas do README. Cada linha marcada `[G491]` evita uma
  falha distinta, e uma delas (RAM de 112 KB) só aparece como HardFault
  antes do `main()`.
- **`src/clock_config.cpp` não é opcional:** configura os 170 MHz e o clock de
  kernel do ADC. Sem o segundo, `analogRead` devolve zero.
- Caminho do projeto com acento quebra o linker no Windows, e o caminho longo
  somado às pastas da SimpleFOC passa de 260 caracteres. Por isso `build_dir`
  e `libdeps_dir` apontam para `C:/pio/`. Não remover.
- SimpleFOC 2.4.0 instalada (`lib_deps`, com `lib_archive = no`). Instalar
  biblioteca pelo PlatformIO Home apaga os comentários do `platformio.ini`:
  edite `lib_deps` à mão.
- Dois ambientes: `ouroboros` (aplicação, `src/main.cpp`) e `validacao`
  (`src/validacao/`, menu de modos de teste da placa). Cada um ignora os
  arquivos do outro por `build_src_filter`. Teste de bancada novo entra como
  modo do firmware de validação, não como cópia por cima do `main.cpp`.
- Releases: tag `validacao-vX.Y` dispara `.github/workflows/release-validacao.yml`,
  que compila no GitHub e anexa `.elf`/`.bin`. No CI, `PLATFORMIO_BUILD_DIR` e
  `PLATFORMIO_LIBDEPS_DIR` sobrescrevem os caminhos `C:/pio/`. A plataforma
  está fixa em `ststm32@19.6.0` para o release igualar a bancada.
- Pinos de PWM (TIM1), halls (TIM3) e BEMF no `board_pins.h` vêm da tabela
  de projeto da Artemis (2026-10-17), ainda sem verificação com ponteira.

## Convenções de código

- Comentário só onde o código não se explica sozinho: o *porquê*, nunca o
  *o quê*. Uma linha quase sempre basta.
- Comentários e documentação em português, com acentuação correta. Strings
  de console ficam em ASCII de propósito (terminais serial não renderizam
  acento de forma confiável).
- Referir-se ao Eco Octano com artigo masculino: "do Eco Octano".
- Identificadores em português seguindo o que já existe (`acordar`,
  `dormir`, `escreverEConferir`); pinos com prefixo `DRV_PINO_` ou `PINO_`.
- Código completo e pronto para colar, não trechos para o usuário montar.
- **O `src/main.cpp` é do usuário e fica em branco** até ele escrevê-lo. Não
  preencher nem reescrever sem pedido explícito; código de teste vai para
  `testes/` (o console de bancada é `testes/08_bancada_drv8353.cpp`).
- O resto fica em bibliotecas em `lib/`. O driver do DRV8353RS é
  `lib/DRV8353`, orientado a registradores: o usuário passa o valor em hex
  (`escreverRegistrador`) e a biblioteca guarda, confere e explica em texto.
  O usuário rejeitou a versão com struct de configuração e enums. Pinos
  ficam no `include/board_pins.h` e entram pelo construtor.
- Uma mudança por vez e um teste por mudança. Não reorganizar arquivos que
  não fazem parte do pedido.

## Estado validado em bancada

- Leitura e escrita SPI do DRV8353RS funcionam, com o SDO lido como `INPUT`
  (pull-up externo de 4,7 k no pino 29).
- `d` devolve os valores de reset: `0x02=0x000` (ou `0x100` com GDF
  mascarado), `0x03=0x3FF`, `0x04=0x7FF`, `0x05=0x16D`, `0x06=0x283`,
  `0x07=0x000`.
- `0x04 = 0x7FF` é o único registrador em que todos os bits em 1 é o valor
  correto; não use essa leitura como teste de "barramento morto".
- Despertar: 300 ciclos dormir → acordar (`r`) sem nenhuma falha de
  conferência com `delayMicroseconds(1500)` no lugar do `delay(1)`
  (2026-10-16). O `FALHOU` intermitente era o tWAKE.
- Offsets dos amplificadores em torno de 1,65 V com a rotina de
  `despertarDriver()` (2026-10-17, relato do usuário sem os valores
  individuais). O 0,41 V antigo era o `analogRead` em 10 bits convertido
  com /4095; `analogReadResolution(12)` resolveu.
- Halls (M3, `testes/09_halls.cpp`, 2026-10-17): **uma volta física da
  roda = exatamente 30 trocas**, confirmando 5 pares de polos, sem estado
  inválido nem pulo; reversão de sentido acompanhada. Sequência no sentido
  positivo (ABC): 011 → 001 → 101 → 100 → 110 → 010. Os 11 estados
  inválidos (000/111) de uma corrida anterior eram **mau contato no cabo dos
  halls** (confirmado mexendo no cabo com o motor parado). Contador de
  inválidos crescendo = conferir esse cabo primeiro.
- Faltas `GDF` e `VGS_Lx` aparecem enquanto MOSFETs e shunts do CSA interno
  não estão soldados. É o comportamento correto do driver, não defeito.

## Pendente / não validado

- Biblioteca `lib/DRV8353` reescrita em 2026-10-16 (acordar, passar e
  explicar registradores, falhas com causa, reset de falha, modo planar por
  ENABLE ou `COAST`). Transporte SPI e despertar são os validados; o resto
  falta testar com o console de bancada, em especial `e`/`k`/`v` (planar) e
  `c`/`p` (reset de falha).
- Rotina de calibração do usuário, hoje em `testes/11_despertar_calibracao.cpp`
  (saiu do `main.cpp` sem commit e foi recriada; o `main.cpp` recebe os testes
  de bancada da vez). `despertarDriver()` configura,
  acorda, calibra os offsets dos amplificadores internos e externos com a
  ponte em COAST, sai do COAST e trava os registradores. Use-o em todo
  despertar, inclusive na volta do planar pelo ENABLE (não `drv.retomar()`
  direto). Não testado na placa; a janela de offset (1,65 ± 0,20 V) é chute
  inicial.
- PWM sem motor (modo 4 / `testes/10_pwm.cpp`, 2026-10-18): o usuário viu no
  osciloscópio e acredita que os sinais estão corretos, mas sem registro de
  frequência, larguras e tempo morto medidos. Medir antes da malha aberta.
- `PLACA_SEM_ESTAGIO_DE_POTENCIA` no console de bancada está em `1`. Colocar
  em `0` só depois de soldar o estágio de potência.
- Gate drive (`0x03`, `0x04`) e OCP (`0x05`) estão nos valores de reset, não
  dimensionados. **Não ligar PWM** antes de dimensioná-los contra o SLVSDY6 e
  a carga de gate dos MOSFETs (CSD18535KCS, segundo o
  `firmware_foc/drv8353_bringup.cpp`, que traz valores não revalidados).
- O build com a toolchain ARM real só foi verificado na bancada do usuário;
  mudanças em HAL e clocks exigem um `pio run` real, não só checagem
  de sintaxe.

## Regras de segurança do driver

- Desligar o PWM **antes** de baixar o `ENABLE`; ligar o PWM por **último**
  no despertar.
- O sono apaga todos os registradores. Todo despertar reescreve e confere
  os seis (`DRV8353::acordar`). Se a conferência falhar, não ligar o PWM.
- Zerar os integradores da malha de corrente ao acordar.
- Limpar falha sem perder configuração: pulso `tRST` de 5 a 40 µs no
  `ENABLE`. Acima de 1 ms o chip dorme.
- Nunca chamar SPI dentro do laço de corrente de 20 kHz. Falha rápida é pelo
  `nFAULT` como GPIO/EXTI.

## Roadmap

M3 halls → M4 malha aberta → M6 alinhamento → M7 malha de corrente →
M8 aplicação → M9 segurança → M10 experimento inline × low-side.

## Referências

- Datasheet DRV835x: SLVSDY6 (TI), §8.5.1.1 para o quadro SPI.
- `README.md` tem a errata completa, o procedimento de bring-up e a tabela
  de armadilhas do PlatformIO.
- `docs/drv8353-biblioteca.md` documenta a biblioteca `lib/DRV8353` (funções,
  campos de cada registrador, regras de segurança e reaproveitamento da
  `firmware_foc/`). É um
  documento versionado (`versao` no frontmatter e histórico de revisões):
  ao mudar a biblioteca, atualize o guia e incremente a versão.
