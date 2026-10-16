# CLAUDE.md — ESC Eco Octano (NUCLEO-G491RE + DRV8353RS)

Firmware do ESC do veículo elétrico do Eco Octano UFPR. Este arquivo vale
para qualquer sessão do Claude Code neste repositório.

## Hardware

- **MCU:** STM32G491RE na NUCLEO-G491RE, 170 MHz, 112 KB de RAM contígua.
- **Artemis:** adaptador que fica sobre a Nucleo.
- **Ouroboros:** placa de potência com o DRV8353RS (RGZ-48), sem MCU.
- **Motor:** BLDC com halls, tensão de barramento de 48 V.
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
- Caminho do projeto com acento quebra o linker no Windows.

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
  O usuário configura parâmetros no `main.cpp`; o resto fica em bibliotecas
  em `lib/`. O driver do DRV8353RS é `lib/DRV8353`; pinos ficam no
  `include/board_pins.h` e entram pelo construtor.
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
- Faltas `GDF` e `VGS_Lx` aparecem enquanto MOSFETs e shunts do CSA interno
  não estão soldados. É o comportamento correto do driver, não defeito.

## Pendente / não validado

- Biblioteca `lib/DRV8353` (2026-10-16): mesma lógica validada, agora com a
  configuração por enums em `configurarDriver()` no `main.cpp`. Falta conferir
  em bancada que `d` continua batendo com o reset e que `r` dá zero falhas.
- Auto-zero (`a`) lia ~0,41 V em vez de 1,65 V. Causa provável: `analogRead`
  em 10 bits convertido com /4095 (512 contagens = 0,413 V). Corrigido com
  `analogReadResolution(12)`; falta confirmar em bancada.
- `PLACA_SEM_ESTAGIO_DE_POTENCIA` em `main.cpp` está em `1`. Colocar em `0`
  só depois de soldar o estágio de potência.
- Gate drive (`idrivep_*`, `idriven_*`, `tdrive`) e OCP (`tempo_morto`,
  `nivel_vds`, `deglitch_ocp`) estão nos valores de reset em
  `configurarDriver()`, não dimensionados. **Não ligar PWM** antes de
  dimensioná-los contra o SLVSDY6 e a carga de gate dos MOSFETs.
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
