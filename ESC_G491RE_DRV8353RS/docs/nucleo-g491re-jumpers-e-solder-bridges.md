---
data: 2026-09-21
atualizado: 2026-09-21
versao: 1.0
status: vigente
tags:
  - doc/especificacao
  - plataforma/stm32g4
  - hardware/nucleo
aliases:
  - jumpers NUCLEO-G491RE
  - solder bridges G491RE
  - SB da Nucleo G4
  - MB1367 configuracao
---

# NUCLEO-G491RE — jumpers e solder bridges

> [!abstract] Resumo
> Função de cada jumper (JP) e de cada solder bridge (SB) da NUCLEO-G491RE
> (placa MB1367), o efeito de cada estado, as combinações que causam curto e
> receitas prontas de configuração.
>
> Fonte principal: **UM2505 Rev 7 (maio de 2026)**, seções 6.1, 7.4 a 7.7 e 8.
> Modelo de projeto: [[modelo-platformio-nucleo-g491re]]

## Escopo

**Cobre:** os jumpers JP1, JP3, JP5, JP6, JP7 e JP8, e as solder bridges
listadas na Tabela 12 do UM2505, válidas para NUCLEO-G431RB, G474RE e G491RE
(mesma placa MB1367).

**Não cobre:** componentes de configuração que o UM2505 não documenta
individualmente ("demais solder bridges usadas para compatibilidade de pinout
entre os alvos"), nem o esquemático — ver [[#Pendências]].

## Convenções

| Termo | Significado |
| ----- | ----------- |
| Jumper **ON** | jumper encaixado |
| Jumper **OFF** | jumper retirado |
| Jumper **[1-2]** | jumper entre os pinos 1 e 2 |
| SB **ON** | ponte fechada (resistor de 0 Ω ou gota de solda) |
| SB **OFF** | ponte aberta |

> [!info] Onde ficam
> Todos os jumpers são removíveis, na face de cima. As **41 solder bridges
> ficam na face de baixo** e exigem ferro de solda para mudar.

> [!note] Sobre a coluna "Padrão"
> O UM2505 marca o estado de fábrica em **negrito**, e essa marcação se perde na
> extração do PDF. Por isso cada padrão abaixo indica a fonte:
> - **UM §x.y** — o texto do manual declara explicitamente o padrão
> - **observado** — medido por continuidade numa placa sem aquela SB alterada
> - **[PENDENTE]** — não confirmado; conferir na placa ou no PDF original

## Jumpers

| Jumper | Função | Padrão | Efeito de cada estado |
| ------ | ------ | ------ | --------------------- |
| **JP1** | reset do ST-LINK | OFF | **ON:** mantém o ST-LINK V3E em reset, com todos os seus GPIOs em alta impedância. Usado para ligar um depurador externo no CN4 sem conflito |
| **JP3** | T_RST — reset do alvo | ON | Serigrafia "Target reset". **ON** é o estado de uso normal. [PENDENTE: efeito exato do OFF no esquemático] |
| **JP5** | seleção da fonte de 5 V | [1-2] | ver tabela abaixo |
| **JP6** | IDD — alimentação do MCU | ON | **ON:** MCU alimentado pelo 3V3. **OFF:** o 3V3 do MCU passa por um amperímetro ligado nos pinos; **sem amperímetro, o MCU fica sem alimentação** |
| **JP7** | BOOT0 | OFF | Só tem efeito se as option bytes mandarem o boot seguir o pino. [PENDENTE: a que JP7 liga o PB8-BOOT0 no esquemático] |
| **JP8** | fonte do VREF+ do MCU | [1-2] | **[1-2]:** VREF+ pela referência TL1431 (2,5 V ou 3,25 V, ver SB14). **[2-3]:** VREF+ pelo VDD (3,3 V) |

### JP5 — fonte de 5 V

| Posição | Fonte | Entrada | Limite |
| ------- | ----- | ------- | ------ |
| **[1-2]** (padrão) | 5V_USB_STLK — USB do ST-LINK | CN1 | 500 mA, pedido na enumeração |
| [3-4] | 5V_VIN — regulador LD1117 | CN6-8 ou CN7-24 | 7–12 V; 800 mA a 7 V, 450 mA até 9 V, 250 mA até 12 V |
| [5-6] | E5V — 5 V externo | CN7-6 | 4,75–5,25 V, 500 mA |
| [7-8] | 5V_USB_CHGR — carregador USB | CN1 | sem depuração |

> [!danger] Um único jumper no JP5
> Duas posições ocupadas ao mesmo tempo ligam duas fontes de 5 V entre si.

> [!warning] Limite de corrente pelo USB do ST-LINK
> Se a placa **mais os shields** passarem de **300 mA**, o UM2505 exige fonte
> externa (E5V, VIN ou 3V3). Acima de 500 mA o chaveador U4 corta e o LED
> vermelho **LD4 (OC)** acende. Se a enumeração USB falhar por falta de
> corrente do PC, o MCU simplesmente não é alimentado e o LD3 fica apagado.

> [!note] Ordem de ligação com fonte externa (E5V ou VIN)
> 1. JP5 na posição da fonte
> 2. Ligar a fonte externa e conferir o **LD3 aceso**
> 3. **Só então** conectar o USB do ST-LINK
>
> Na ordem inversa, a placa pode puxar mais de 300 mA do PC ou falhar a
> enumeração. Alimentação direta por **3V3** (CN6-4 ou CN7-16, 3–3,6 V) deixa o
> ST-LINK sem energia: **sem gravação e sem depuração**.

> [!bug] Inconsistência no UM2505
> As seções de VIN e de carregador USB citam "jumper **JP2**". A tabela de
> jumpers e o layout mostram que a seleção de 5 V é o **JP5**. Tratar como
> erro de digitação do manual.

## Solder bridges

### Console (Virtual COM Port) e pinos D0/D1

O mesmo par de pinos do conector Arduino (D1/D0, que também saem no morpho
CN10-35/37) pode receber **LPUART1** (PA2/PA3) ou **USART1** (PC4/PC5), e o
ST-LINK pode receber o outro.

| SB | ON | OFF | Padrão |
| -- | -- | --- | ------ |
| **SB17** | PA2 (LPUART1_TX) ligado ao TX do VCP do ST-LINK | desligado | ON — UM §7.6.5 |
| **SB23** | PA3 (LPUART1_RX) ligado ao RX do VCP | desligado | ON — UM §7.6.5 |
| **SB18** | PA2 ligado a ARD_D1 / CN10-35 | desligado | OFF — UM §7.6.5 |
| **SB22** | PA3 ligado a ARD_D0 / CN10-37 | desligado | OFF — UM §7.6.5 |
| **SB12** | PC4 (USART1_TX) ligado ao TX do VCP | desligado | OFF — UM §7.6.5 |
| **SB20** | PC5 (USART1_RX) ligado ao RX do VCP | desligado | OFF — UM §7.6.5 |
| **SB13** | PC4 ligado a ARD_D1 / CN10-35 | desligado | ON — UM §7.6.5 |
| **SB19** | PC5 ligado a ARD_D0 / CN10-37 | desligado | ON — UM §7.6.5 |

De fábrica: **console = LPUART1 em PA2/PA3**, e **D1/D0 = USART1 em PC4/PC5**.

### Botão, LED e depuração

| SB | ON | OFF | Padrão |
| -- | -- | --- | ------ |
| **SB6** | LED LD2 acionado por PA5 (ARD_D13) | LD2 desligado; PA5 livre | ON — UM §7.6.1 |
| **SB16** | botão B1 ligado a PC13 | desligado | ON — UM §7.6.2 |
| **SB21** | botão B1 ligado a PA0 (suporte a wakeup) | desligado | OFF — UM §7.6.2 |
| **SB40** | SWDIO do ST-LINK ligado a PA13 | sem SWDIO — **sem gravação** | ON — a placa grava de fábrica |
| **SB41** | SWCLK do ST-LINK ligado a PA14 | sem SWCLK — **sem gravação** | ON — a placa grava de fábrica |
| **SB15** | SWO do ST-LINK ligado a PB3 | PB3 isolado do SWO e de ARD_D3 | [PENDENTE] |
| **SB9** | JTDI (PA15) ligado ao CN4 pino 10 | desligado | [PENDENTE] |
| **SB1** | capacitor de 100 nF do NRST para o GND | capacitor desligado | [PENDENTE] |

> [!warning] PA13 e PA14
> São os pinos do SWD. O UM2505 desaconselha usá-los como I/O: fazer isso
> impede a gravação e a depuração pelo ST-LINK.

### Clock HSE — cristal de 24 MHz ou pinos PF0/PF1

| SB | ON | OFF | Padrão |
| -- | -- | --- | ------ |
| **SB25 + SB26** | cristal X3 de 24 MHz ligado ao HSE | cristal desligado | ON — UM §7.5.1 |
| **SB27** | MCO do ST-LINK (8 MHz fixo) como HSE | não usado | OFF — UM §7.5.1 |
| **SB24** | pino do oscilador no morpho CN7-31, como I/O | desligado | OFF — UM §7.5.1 |
| **SB28** | pino do oscilador no morpho CN7-29, como I/O ou entrada de oscilador externo | desligado | OFF — UM §7.5.1 |

Configurações válidas, segundo o UM §7.5.1:

| Uso | SB25/SB26 | SB27 | SB24 | SB28 |
| --- | --------- | ---- | ---- | ---- |
| Cristal X3 24 MHz (padrão) | ON | OFF | OFF | OFF |
| MCO do ST-LINK, 8 MHz | OFF | ON | OFF | OFF |
| Oscilador externo pelo CN7-29 | OFF | OFF | OFF | ON |
| HSE não usado; PF0/PF1 como GPIO | OFF | OFF | ON | ON |

### Clock LSE — cristal de 32,768 kHz ou pinos PC14/PC15

| SB | ON | OFF | Padrão |
| -- | -- | --- | ------ |
| **SB30 + SB31** | cristal X2 de 32,768 kHz ligado ao LSE | cristal desligado | ON — UM §7.5.2 |
| **SB32** | PC14 no morpho CN7-25 | desligado | OFF — UM §7.5.2 |
| **SB29** | PC15 no morpho CN7-27 | desligado | OFF — UM §7.5.2 |

> [!warning] PC14 e PC15 não chegam ao header de fábrica
> Para usá-los como GPIO: **SB29 e SB32 ON, SB30 e SB31 OFF**. O cristal deixa
> de funcionar.

### Pinos compartilhados no conector

Dois pinos do MCU disputam o mesmo pino do conector; a SB escolhe qual chega.

| SB | ON | OFF | Padrão |
| -- | -- | --- | ------ |
| **SB35** | PC1 em ARD_A4 / CN7-36 | desligado | ON — observado |
| **SB34** | PB9 em ARD_A4 / CN7-36 | desligado | OFF — observado |
| **SB36** | PC0 em ARD_A5 / CN7-38 | desligado | ON — observado |
| **SB37** | PA15 em ARD_A5 / CN7-38 | desligado | OFF — observado |
| **SB2** | PB8 em ARD_D14 | desligado | [PENDENTE] |
| **SB4** | PB8 no morpho CN7-7 (pino rotulado BOOT0) | desligado | [PENDENTE] |
| **SB7** | PA15 no morpho CN7-17 | desligado | [PENDENTE] |
| **SB3** | PC5 no morpho CN10-6 | desligado | [PENDENTE] |
| **SB33** | PC4 no morpho CN10-34 | desligado | [PENDENTE] |

### Alimentação e referência analógica

| SB | ON | OFF | Padrão |
| -- | -- | --- | ------ |
| **SB5** | regulador U12 fornece o 3V3 | sem 3V3; exige fonte externa de 3,3 V | ON — sem ele a placa não funciona de fábrica |
| **SB8** | ferrite L1 em curto; VDDA direto no VDD | VDDA filtrado pela ferrite L1 | [PENDENTE] |
| **SB10** | ferrite L2 em curto no VREF+ | VREF+ filtrado pela ferrite L2 | [PENDENTE] |
| **SB11** | AGND ligado ao GND | AGND separado | ON — UM: "reservado, não modificar" |
| **SB14** | VREF = 2,5 V (e R34 deve ser retirado) | VREF = 3,25 V (com R34 de 33 kΩ montado) | OFF — observado: VREF+ medido em ~3,25 V |
| **SB38** | VBAT (pino 1) alimentado pelo VDD | VBAT pelo morpho CN7-33 | [PENDENTE] |
| **SB39** | IOREF ligado ao 3V3 | IOREF desligado | [PENDENTE] |

> [!warning] VREF+ de fábrica não é 3,3 V
> Com JP8 em [1-2], o fundo de escala do ADC é o VREF (2,5 V ou 3,25 V), não
> 3,3 V. Bibliotecas que assumem 3,3 V leem tensões com erro proporcional.
> Para ADC com fundo de escala de 3,3 V: **JP8 em [2-3]**. Para medida de
> precisão, manter [1-2] e usar o valor real do VREF na conversão.

## Combinações que causam curto

> [!danger] Nunca feche os dois ao mesmo tempo
> Cada par abaixo liga **dois pinos do MCU no mesmo fio**. Se qualquer um dos
> dois for configurado como saída, há curto entre saídas.

| Par | O que fica ligado junto |
| --- | ----------------------- |
| SB13 + SB18 | PC4 e PA2, em ARD_D1 / CN10-35 |
| SB19 + SB22 | PC5 e PA3, em ARD_D0 / CN10-37 |
| SB12 + SB17 | PC4 e PA2, no TX do VCP |
| SB20 + SB23 | PC5 e PA3, no RX do VCP |
| SB16 + SB21 | PC13 e PA0, através do botão |
| SB34 + SB35 | PB9 e PC1, em ARD_A4 / CN7-36 |
| SB36 + SB37 | PC0 e PA15, em ARD_A5 / CN7-38 |

> [!danger] Fontes de clock
> Não combine duas fontes no mesmo pino: cristal X3 (SB25/SB26) com MCO do
> ST-LINK (SB27) ou com saída para o morpho (SB24/SB28); cristal X2 (SB30/SB31)
> com saída para o morpho (SB29/SB32).

> [!danger] Alimentação
> - Mais de um jumper no JP5
> - SB5 OFF sem fonte externa de 3,3 V: MCU sem alimentação
> - JP6 OFF sem amperímetro: MCU sem alimentação

## Receitas de configuração

### Fábrica

Console pelo USB do ST-LINK em PA2/PA3; D0/D1 do Arduino em PC4/PC5; LD2 em
PA5; B1 em PC13; cristais HSE e LSE ativos; JP5 [1-2], JP6 ON, JP8 [1-2].
É o estado que o [[modelo-platformio-nucleo-g491re]] assume.

### Liberar PA2/PA3 para o header e manter o console no USB

Move o console para USART1 (PC4/PC5) e leva PA2/PA3 ao conector
(UM Tabelas 10 e 11):

| SB | Estado |
| -- | ------ |
| SB18, SB22 | ON |
| SB12, SB20 | ON |
| SB17, SB23 | OFF |
| SB13, SB19 | OFF |

No firmware, o console passa a ser `HardwareSerial(USART1)` com
`setRx(PC5)` / `setTx(PC4)`.

### Liberar PA2/PA3 e abrir mão do console no USB

Quando PC4/PC5 também são necessários no header:

| SB | Estado |
| -- | ------ |
| SB18, SB22 | ON |
| SB17, SB23 | OFF |
| SB13, SB19 | OFF |
| SB12, SB20 | OFF |

O console precisa então de um conversor USB-serial externo em outra UART.

### Outras

| Objetivo | Alteração |
| -------- | --------- |
| PC14/PC15 como GPIO | SB29, SB32 ON · SB30, SB31 OFF |
| PF0/PF1 como GPIO | SB24, SB28 ON · SB25, SB26, SB27 OFF |
| Botão B1 em PA0 (wakeup) | SB21 ON · SB16 OFF |
| PA5 livre, sem o LED | SB6 OFF |
| PB3 livre do SWO | SB15 OFF |
| ADC com fundo de escala de 3,3 V | JP8 em [2-3] |
| Medir o consumo do MCU | JP6 OFF com amperímetro nos pinos; JP8 em [2-3] para incluir o VREF+ na medida |
| Depurador externo no CN4 | JP1 ON (ST-LINK interno em alta impedância) |

## Boot e option bytes

O pino BOOT0 é o **PB8**, e o UM2505 informa que ele **não é ligado por
padrão**: a função de boot é feita por software, pelas option bytes.

| Option byte | Valor recomendado | Efeito |
| ----------- | ----------------- | ------ |
| `nSWBOOT0` | 0 | o boot ignora o pino PB8 e usa o bit `nBOOT0` |
| `nBOOT0` | 1 | boot sempre pela flash principal |

Com essa configuração, qualquer circuito ligado ao PB8 (um transceptor CAN,
por exemplo) não interfere no boot.

> [!bug] Inconsistência no UM2505
> A nota de rodapé da Tabela 16 diz primeiro que, com `nSWBOOT0 = 0`, o BOOT0
> vem da option byte, e na frase seguinte que, com `nSWBOOT0 = 0`, ele vem do
> pino PB8. As duas afirmações se contradizem. Conferir no RM0440, seção de
> option bytes, antes de alterar.

## Pendências

- [ ] [PENDENTE: estado de fábrica de SB1, SB2, SB3, SB4, SB7, SB8, SB9, SB10, SB15, SB33, SB38 e SB39 — abrir o UM2505 Rev 7, Tabela 12, e conferir o negrito; ou verificar visualmente a face de baixo da placa]
- [ ] [PENDENTE: efeito do JP3 OFF e ligação do JP7 — conferir no esquemático do MB1367, disponível na página do produto em st.com]
- [ ] [PENDENTE: semântica exata de `nSWBOOT0`, dada a contradição da nota de rodapé do UM2505 — conferir no RM0440]
- [ ] Confirmar os padrões marcados como "observado" numa segunda placa sem alterações

## Histórico de revisões

| Versão | Data | Alteração |
| ------ | ---- | --------- |
| 1.0 | 2026-09-21 | Versão inicial, a partir do UM2505 Rev 7: jumpers, solder bridges da Tabela 12, combinações proibidas, receitas de configuração e option bytes de boot |
