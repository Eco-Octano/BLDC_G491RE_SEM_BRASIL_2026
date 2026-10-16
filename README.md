# ESC Eco Octano — DRV8353RS sobre NUCLEO-G491RE

Firmware de bring-up e biblioteca de driver para o ESC do veículo elétrico
da **Eco Octano UFPR**. Alvo: NUCLEO-G491RE + placa adaptadora *Artemis* +
placa de potência *Ouroboros* (DRV8353RS).

Este repositório cobre a camada de driver e a validação de bancada. O
controle de motor (FOC, halls, malha de corrente) vem em cima disto.

---

## ERRATA 001 — MOSI e MISO trocados

**Esta revisão da Ouroboros tem o barramento SPI do DRV8353RS invertido.**

Medido com ponteira no pino do MCU e no pino do CI:

| MCU | chega no pino | função real (SLVSDY6, RGZ-48) |
|---|---|---|
| PC11 — MISO, *entrada* | 30 | **SDI**, *entrada* do driver |
| PC12 — MOSI, *saída* | 29 | **SDO**, *saída* do driver |

Entrada contra entrada, saída contra saída. O net do SDI não é dirigido por
ninguém e flutua; o SDO recebe push-pull do MCU.

**Consequência.** O SPI de hardware não funciona nesta placa. O mapeamento de
função alternada do SPI3 no G491 é fixo — PC11 só pode ser `SPI3_MISO` e PC12
só pode ser `SPI3_MOSI`, e nenhuma configuração inverte isso (confirmado no
CubeMX).

**Contorno.** Bit-bang por GPIO, onde a atribuição é nossa. Custa cerca de
130 µs por quadro de 16 bits contra 16 µs do periférico a 1 MHz. Isso é
irrelevante para este projeto: o SPI do DRV8353 serve para configurar no boot
e diagnosticar falha, nunca para o laço de controle. O caminho rápido de falha
é o **nFAULT**, que é um GPIO e pode ser EXTI.

**Correção.** Trocar as trilhas dos pinos 29 e 30 na próxima revisão do
esquemático. Feito isso, basta inverter `DRV_PINO_SDI` e `DRV_PINO_SDO` em
`include/board_pins.h`.

**Não fazer rework nesta placa.** QFN de 0,5 mm, risco alto, e o ganho seria
velocidade que o projeto não precisa.

### Errata menores

- **Pull-ups do SPI.** O SDO do driver (pino 29, open-drain) tem pull-up de
  4,7 k na placa; o SDI (pino 30) tem 10 k na Artemis, que apenas carrega o
  PC11 e é inofensivo. O firmware lê o SDO com `INPUT`, sem pull-up interno.
  Para SPI mais rápido, trocar o do pino 29 por 2,2 kΩ.
- **Jumper J7 do nFAULT.** Seleciona entre o pull-up da Artemis (4,7 k,
  populado) e o da Ouroboros (**não populado**). Tem que estar na posição da
  Artemis. O LED indicador é ligado *antes* do jumper e acende mesmo com o
  pino do driver flutuando — **não serve de prova de nível**.
- **nSCS com dois pull-ups** (4,7 k na Artemis e 10 k na Ouroboros, 3,2 kΩ em
  paralelo). Benigno: 1 mA de afundamento contra os 20 mA que o GPIO entrega.

---

## Estrutura

```
include/
  board_pins.h      pinagem verificada + errata
  drv8353_regs.h    mapa de registradores e bits
  drv8353.h         API do driver
src/
  drv8353.cpp       transporte, configuração, falha, auto-zero
  clock_config.cpp  170 MHz + clock de kernel do ADC
  main.cpp          aplicação de bring-up
boards/
  nucleo_g491re_arduino.json      board customizado (ver Armadilhas)
tools/
  install_variant.py              roda antes de cada build
  variant_NUCLEO_G491RE_ARDUINO.h
```

### `clock_config.cpp` não é opcional

Ele corrige duas coisas que a variante genérica do stm32duino não entrega:

1. **Os 170 MHz declarados.** Sem isso o `SystemCoreClock` diverge do clock
   real e o `delay()` trava ou fica lento.
2. **O clock de kernel do ADC.** No G4 o ADC tem multiplexador próprio, cujo
   estado de reset é *nenhuma fonte selecionada*. Nesse estado toda conversão
   devolve zero — sem erro, sem travar, só zero. Como o objetivo do projeto é
   comparar sensoriamento inline contra low-side, um ADC morto invalida tudo.

---

## Compilar e rodar

```bash
pio run -t upload
pio device monitor -p COM11 -b 115200
```

Use a porta da **FTDI**, não a VCP do ST-Link: a USART2 que alimenta a VCP tem
o TX em PA2, e PA2 aqui é o `LSCS_A`. O console é USART1 — **PB6 é TX, PB7 é
RX**, não inverter.

### Armadilhas da NUCLEO-G491RE no PlatformIO

As cinco linhas marcadas `[G491]` no `platformio.ini` não são opcionais. Cada
uma corresponde a um build que falha de um jeito diferente:

| # | sintoma sem a linha | causa |
|---|---|---|
| 1 | `Error: This board doesn't support arduino framework!` | o board oficial `nucleo_g491re` só declara `stm32cube`. Daí o JSON em `boards/` |
| 2 | `variant_NUCLEO_G491RE_ARDUINO.h: No such file or directory` | o PlatformIO deriva o nome do header do ID do board, e a variante genérica só tem `variant_generic.h`. O `install_variant.py` copia um redirecionamento para dentro do pacote do framework — ele some a cada reinstalação, por isso é script e não cópia manual |
| 3 | **HardFault antes do `main()`**, sem mensagem nenhuma | o core declara 128 KB de RAM porque a variante é compartilhada com o G4A1. O G491 tem 112 KB contíguos (a CCM fica em endereço separado). O linker põe a pilha em memória inexistente |
| 4 | `Debug adapter doesn't support 'hla_swd' transport` | a placa tem ST-Link **V3E**, que fala `dapdirect_swd`; o `upload_protocol=stlink` padrão assume V2 |
| 5 | firmware grava e não roda | gravar o `.bin` em vez do `.elf`: o `.bin` não carrega endereço e vai parar em `0x00000000` |

A 3 é a pior: compila, grava, e o firmware morre em silêncio antes da primeira
linha do seu código.

No Windows há ainda uma sexta: o GNU `ld` não lida com acento no caminho do
projeto e o build falha com `cannot open map file`. Use um caminho ASCII ou as
linhas `build_dir` / `libdeps_dir` comentadas no fim do `platformio.ini`.

### Comandos do console

| tecla | ação |
|---|---|
| `d` | dump dos 8 registradores |
| `s` | status de falha decodificado |
| `c` | limpa falha por SPI (CLR_FLT) |
| `p` | pulso `tRST` no ENABLE — limpa falha **sem** perder configuração |
| `w` | acorda e configura |
| `z` | dorme (ENABLE = 0) |
| `t` | cronometra 100 transações |
| `a` | auto-zero dos CSAs |

---

## Procedimento de bring-up

1. **Antes de soldar o estágio de potência**, mantenha
   `PLACA_SEM_ESTAGIO_DE_POTENCIA` em `1` no `main.cpp`. Sem carga de gate o
   driver declara **GDF corretamente e sem parar** — não é defeito.
2. Aperte `d`. Os registradores de configuração (`0x02` a `0x07`) têm que
   bater com os valores de reset. É o gabarito do barramento nos dois
   sentidos. Note que `0x04` reseta em `0x7FF`: é o único registrador em que
   "todos os bits em um" é o valor **correto** — por isso o dump dos oito é
   diagnóstico e a leitura de um só não é.
3. Solde MOSFETs e shunts. Ponha `PLACA_SEM_ESTAGIO_DE_POTENCIA` em `0`.
4. Aperte `s`. `0x00` e `0x01` têm que vir limpos. Se só os bits `VGS_Lx`
   acusarem, olhe os shunts: o monitor de low-side se referencia em SPx.
5. **Só depois disso** dimensione `gate_drive_hs`, `gate_drive_ls` e
   `ocp_control` contra o SLVSDY6 e a carga de gate dos seus MOSFETs, e
   comece o PWM.

---

## Arquitetura de energia (acelerador solto)

O veículo tem cubo de roda-livre, então o motor para quando não é acionado —
não há força contra-eletromotriz para administrar. Isso permite **dormir o
driver durante a inércia**:

| estado | corrente | a 48 V |
|---|---|---|
| acordado | 10,4 mA | 500 mW |
| dormindo | 20 µA | 1 mW |

O sono **apaga todos os registradores**, então todo despertar reconfigura.
`DRV8353::acordar()` faz a sequência completa:

```
ENABLE alto → espera tWAKE (1 ms) → escreve 6 registradores
            → confere o que escreveu  ≈ 2,6 ms no total
```

Duas regras que não podem ser quebradas:

- **O PWM desliga antes do ENABLE e liga por último.** Entre acordar e
  configurar, `0x02` está em reset, o que significa `PWM_MODE = 6x`. Se o
  timer estiver gerando outro esquema nessa janela, os dois MOSFETs do mesmo
  braço conduzem.
- **Zerar os integradores da malha de corrente no despertar.** Durante o sono
  os CSAs ficam desligados e as saídas vão a zero; integrar essas leituras dá
  coice de torque quando o PWM volta.

Para apenas limpar uma falha sem perder a configuração, use o pulso `tRST`
(5 a 40 µs no ENABLE). Acima de 1 ms o chip dorme.

---

## Pendências

- [ ] Dimensionar `GATE_DRIVE_HS`, `GATE_DRIVE_LS` e `OCP_CONTROL` contra a
      carga de gate dos MOSFETs escolhidos. Os defaults são os valores de
      reset — seguros para bancada, não dimensionados para potência.
- [ ] Corrigir a ERRATA 001 no esquemático da próxima revisão.
- [ ] M3 halls, M4 malha aberta, M6 alinhamento, M7 malha de corrente,
      M8 aplicação, M9 segurança, M10 experimento comparativo inline × low-side.

---

## Referência

[DRV835x datasheet, SLVSDY6 — Texas Instruments](https://www.ti.com/lit/ds/symlink/drv8353.pdf)
