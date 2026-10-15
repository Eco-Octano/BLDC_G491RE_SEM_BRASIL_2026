# Plano validado: leitura inline com SimpleFOC no NUCLEO-G491RE

Documento baseado em: UM2505 Rev 7 (mai/2026), documentação SimpleFOC v2.4.0,
release notes da biblioteca, datasheets DRV8353 e INA240, e a planilha de pinagem
da placa. A seção 9 lista explicitamente o que **não** consegui confirmar.

---

## 0. Veredito honesto antes de começar

O caminho inline com SimpleFOC é o mais rápido para **validar a placa**. Mas para
o requisito que você colocou — confiável, 100% funcional, eficiente — encontrei um
obstáculo documentado que precisa ser tratado explicitamente:

> A própria documentação da SimpleFOC diz que, no STM32, `analogRead()` durante o
> FOC pode reconfigurar o ADC a cada chamada, adicionar latência e introduzir
> jitter na malha de controle.

E a `InlineCurrentSense` no STM32 usa exatamente `analogRead()`. Isso foi
confirmado pelo próprio mantenedor da biblioteca em fevereiro de 2026, ao
comentar o novo helper `_readRegularADCVoltage()` da v2.4.0: ele disse que não
implementou uma abstração unificada por enquanto e que **a biblioteca nem usa esse
helper para a leitura inline no stm32**.

Ou seja: a `InlineCurrentSense` pura tem um teto de desempenho e determinismo que
não combina com "firmware de competição". A boa notícia é que existe uma saída
limpa, dentro da própria biblioteca, sem escrever driver do zero — a seção 3
detalha.

**Conclusão prática:** siga o caminho inline, mas planeje desde já a arquitetura
da seção 3 (Opção B), não a `InlineCurrentSense` direta.

---

## 1. Solder bridges da Nucleo — resolver ANTES de energizar

Esta é a parte mais crítica do documento. Sua placa **não vai funcionar como
está** e há risco de dano.

### 1.1 O conflito PA2/PA3 vs PC4/PC5

Do UM2505, Tabela 11, configuração **padrão** (SB17 e SB23 ON):

> LPUART1 (PA2/PA3) conectado ao Virtual COM port do STLINK-V3E.

E da Tabela 10, configuração **padrão** (SB13 e SB19 ON):

> USART1 (PC4/PC5) conectado ao ARDUINO (D1 e D0) e ao morpho CN10 pinos 35 e 37.

O manual reforça em texto corrido: por padrão, a comunicação com o STLINK-V3E é
feita pela LPUART1, e a comunicação com os conectores ARDUINO/morpho pela USART1.

Agora cruze com a sua planilha:

| Seu sinal | Pino | Você espera em | Na placa como vem de fábrica |
|---|---|---|---|
| LSCSA (low-side A) | PA2 | CN10-35 | CN10-35 carrega **PC4**; PA2 vai para o ST-LINK |
| MOTOR_TEMP | PA3 | CN10-37 | CN10-37 carrega **PC5**; PA3 vai para o ST-LINK |
| HSCS (high-side) | PC4 | CN10-34 | também presente em CN10-35 |
| VBAT | PC5 | CN10-6 | também presente em CN10-37 |

O resultado é grave: o pino onde seu shield injeta o sinal LSCSA está ligado a
PC4, que é onde o shield injeta HSCS. **Duas saídas de amplificador em curto.**
O mesmo para MOTOR_TEMP e VBAT.

### 1.2 Configuração necessária

| SB | Estado necessário | Efeito |
|---|---|---|
| SB17 | **OFF** | desliga o TX do ST-LINK de PA2 |
| SB23 | **OFF** | desliga o RX do ST-LINK de PA3 |
| SB18 | **ON** | leva PA2 para ARD_D1 / CN10-35 |
| SB22 | **ON** | leva PA3 para ARD_D0 / CN10-37 |
| SB13 | **OFF** | tira PC4 da rede ARD_D1 / CN10-35 |
| SB19 | **OFF** | tira PC5 da rede ARD_D0 / CN10-37 |
| SB33 | conferir ON | PC4 no morpho CN10-34 |
| SB3 | conferir ON | PC5 no morpho CN10-6 |

**Consequência: você perde o Virtual COM port do ST-LINK.** O SWD continua
funcionando normalmente (PA13/PA14), então o debug com breakpoint permanece. O
serial passa a ser a sua USART1 em PB6/PB7, com conversor USB-serial externo.

### 1.3 Outros bridges que a sua pinagem exige

| Pino seu | Uso | SB envolvido | Ação |
|---|---|---|---|
| PC13 (GPIO_5) | GPIO | SB16 (botão USER em PC13) | **OFF** — senão o botão B1 disputa o pino |
| PA5 (ANALOG2) | entrada analógica | SB6 (LED2 LD2 em PA5) | **OFF** — o transistor do LED carrega a entrada |
| PB3 (SPI1_SCK) | SPI do Teensy | SB15 (T_SWO em PB3) | **OFF** — senão o trace SWO disputa o clock |
| PC15 (MOTOR_VEL), PC14 | GPIO | SB29, SB32 ON / SB30, SB31 OFF | por padrão esses pinos estão no cristal LSE, **não** no morpho |
| PC0 (inline A, CN7-38) | ADC | SB36 ON, SB37 OFF | CN7-38 é compartilhado entre PC0 e PA15 |
| PA15 (SPI1_NSS, CN7-17) | GPIO | SB7 ON | |
| PC1 (ANALOG1, CN7-36) | ADC | SB35 ON, SB34 OFF | CN7-36 é compartilhado entre PC1 e PB9 |

Repare no item PC14/PC15: no sketch de bring-up anterior eu sugeri PC14 como LED
de status. **Isso só funciona depois de mexer nos bridges do LSE.** Se não quiser
mexer, use outro pino livre.

### 1.4 VREF+ não é 3,3 V por padrão — isso quebra toda a conversão de corrente

Do UM2505, Tabela 4, configuração de jumpers:

> JP8 (seleção de VREF+): ON [1-2] **(padrão)** VREF+ alimentado por VREF;
> ON [2-3] (opcional) VREF+ alimentado por VDD.

E na seção 7.6.3: o VREF é gerado por um TL1431CL5T, e vale **2,5 V** com SB14
fechado, ou **3,25 V** com SB14 aberto e R34 = 33 kΩ montado.

Portanto, de fábrica, o fundo de escala do ADC é 2,5 V ou 3,25 V — **não 3,3 V**.
A SimpleFOC assume 3,3 V nas suas constantes de conversão. Sem tratar isso, toda
leitura de corrente sai com erro de escala de 24% ou 1,5%, dependendo do SB14.

**Duas saídas:**
1. Mover JP8 para [2-3], alimentando VREF+ com VDD (3,3 V). Simples, e alinha com
   a suposição da biblioteca.
2. Manter o VREF preciso (é melhor para medida!) e ajustar a escala no seu código.
   Se você for pela Opção B da seção 3, isso é trivial — a conversão é sua.

Para um projeto de eficiência energética, a opção 2 é tecnicamente superior: o
TL1431 tem estabilidade térmica garantida, enquanto o VDD flutua. Mas ela **exige**
a arquitetura da Opção B.

**Meça o VREF+ com multímetro antes de qualquer coisa.** Não deduza pelo SB.

### 1.5 PB8 é o BOOT0

Sua planilha usa PB8 como `CANRX_EXT`. O pino 61 do G491 é `PB8-BOOT0`. Se a
option byte `nSWBOOT0` estiver no padrão, o nível do pino no reset decide se o MCU
entra no bootloader. Um transceiver CAN segurando essa linha alta no reset trava a
partida do carro.

**Ação:** programar `nSWBOOT0 = 0` e `nBOOT0 = 1` nas option bytes (via
STM32CubeProgrammer). Faça isso uma vez e documente — é o tipo de coisa que some
quando alguém troca o MCU.

---

## 2. Build

O board existe oficialmente no PlatformIO — foi adicionado na versão 19.2.0 do
`platform-ststm32`, com 170 MHz e `stlink` como protocolo padrão de upload.

```ini
[env:nucleo_g491re]
platform         = ststm32
board            = nucleo_g491re
framework        = arduino
board_build.f_cpu = 170000000L
upload_protocol  = stlink
debug_tool       = stlink
lib_archive      = no
lib_deps         = askuric/Simple FOC@^2.4.0
monitor_speed    = 115200
```

### Atualize da 2.3.4 para a 2.4.0 — há motivos concretos

- **A 2.3.5 corrigiu um bug de copy/paste no current sense do STM32G4**
  (`stm32g4_utils.cpp`, seleção do trigger externo quando o timer é o TIM8). Você
  usa TIM1, então não te afeta diretamente, mas indica que a camada G4 recebeu
  manutenção depois da sua versão.
- **A 2.4.0 trouxe o `_readRegularADCVoltage()`**, que é a base da Opção B abaixo.
- Termos de feed-forward de corrente e tensão, e retrabalho no cálculo de
  velocidade — ambos relevantes para malha de torque com hall.
- Exemplo novo `align_current_sense.ino` em `examples/utils/current_sense_test`,
  para verificar o alinhamento entre driver e fases do current sense.
- Atenção na migração: a 2.4.0 lista *"correção no current sensing quando só duas
  correntes de fase estão disponíveis — reconfira o tuning do PID de corrente"*.

---

## 3. A decisão de arquitetura da leitura de corrente

### Opção A — `InlineCurrentSense` direta

```cpp
InlineCurrentSense current_sense = InlineCurrentSense(SHUNT, GAIN, PC0, PA7, PB13);
```

Funciona, sem restrição de ADC. Mas usa `analogRead()`, contra o qual a própria
documentação adverte. Use **apenas** para o bring-up (seção 6, passos 5–6).

### Opção B — `GenericCurrentSense` + `_readRegularADCVoltage()` — recomendada

A `GenericCurrentSense` deixa você fornecer o callback de leitura, e a biblioteca
cuida de offsets, filtragem e integração com o FOC. Combinada com o helper novo do
STM32, você troca `analogRead` por uma conversão regular de uma via só, que
segundo a documentação é *mais lenta que a injetada (tipicamente >10 µs) mas
muito mais rápida e segura que `analogRead()`*.

```cpp
#include "current_sense/hardware_specific/stm32/stm32_mcu.h"

PhaseCurrent_s readInline(){
  PhaseCurrent_s c;
  c.a = (_readRegularADCVoltage(PC0)  - V_ZERO) / (SHUNT * GAIN);
  c.b = (_readRegularADCVoltage(PA7)  - V_ZERO) / (SHUNT * GAIN);
  c.c = (_readRegularADCVoltage(PB13) - V_ZERO) / (SHUNT * GAIN);
  return c;
}
GenericCurrentSense current_sense = GenericCurrentSense(readInline);
```

Três coisas importantes sobre a `GenericCurrentSense`:

1. O `init()` roda **automaticamente** a calibração de offset amostrando seu
   callback várias vezes — o motor não pode estar acionado nesse momento.
2. O `initFOC()` **não** roda o alinhamento do current sense para esse tipo. Você
   determina os sinais na mão e corrige com `current_sense.gain_a *= -1` (ou b/c).
3. Como a conversão é sua, você resolve o problema do VREF+ da seção 1.4 aqui
   mesmo, e mantém o VREF preciso do TL1431.

### Opção C — amostragem injetada própria, síncrona ao TIM1 — alvo final

O INA240 lê continuamente, então você pode amostrar em **qualquer** instante fixo
do período de PWM. Isso torna viável configurar, com HAL/LL, três conversões
injetadas em ADC1/ADC2/ADC3 disparadas pelo mesmo evento do TIM1, e simplesmente
ler os registradores no callback.

O ponto bonito: **essa opção também usa a `GenericCurrentSense`**. Você troca só o
corpo do callback. Ou seja, indo pela Opção B agora, a migração para C depois é
localizada, não uma reescrita.

É a Opção C que aproveita de verdade o que você pagou no INA240, e é ela que
torna a comparação com o código do CubeIDE justa — senão você vai estar comparando
FOC síncrono contra FOC assíncrono, não SimpleFOC contra MCSDK.

---

## 4. DRV8353RS — o que confirmei

- **Modo 6x PWM corresponde a `PWM_MODE = 00b`** (no DRV835x, 6x é a condição
  `PWM_MODE = 00b` ou pino MODE aterrado). Como campo de reset zerado, é
  praticamente certo que seja o padrão — mas **confirme na tabela de valores de
  reset do datasheet** antes de energizar.
- **A calibração de offset dos CSA é por registrador SPI (`CSA_CAL_A/B/C`), e a TI
  diz explicitamente que essa opção não existe nas variantes de interface por
  hardware.** É a confirmação de que o seu `CAL_PIN` some. Só importa quando você
  for usar as low-side.
- **O SPI só responde com o ENABLE alto.** Relato de campo no fórum da TI com a
  própria DRV8353RS-EVM: leitura e escrita funcionam desde que o ENABLE esteja
  alto, e os dados escritos se perdem quando ele cai. Isso significa que **toda a
  configuração precisa ser reaplicada depois de cada ciclo de ENABLE** — e o seu
  `DisarmDriver()` baixa o ENABLE. Consequência direta de projeto: reconfigurar o
  DRV dentro do `ArmDriver()`, não só no `setup()`.

Existe implementação aberta de referência para esse chip: `drv8353rs.c` no projeto
`patosai/esc-chibios` no GitHub. Não é para copiar (é ChibiOS e usa 3x PWM), mas
serve para conferir formato de quadro e ordem de escrita.

---

## 5. Eficiência energética — os pontos que realmente pesam

Como o objetivo é comparar com a versão CubeIDE, vale saber onde estão as perdas
controláveis por software:

**Tempo morto.** É a maior fonte de distorção e perda controlável. O DRV8353RS tem
dead time por registrador, e a SimpleFOC tem `driver.dead_zone`. **Os dois somam.**
Escolha um dos dois e zere o outro. Rodar com 1,25 µs de software mais o do
hardware é jogar energia fora e distorcer a corrente em baixa modulação.

**IDRIVE / slew rate.** Corrente de acionamento de porta configurável por SPI.
Chaveamento mais rápido reduz perda de comutação, ao custo de EMI e ringing. É uma
alavanca de eficiência que só existe porque você usou um smart gate driver — e que
o firmware antigo não explorava.

**Frequência de PWM.** Perda de comutação escala com ela. Os 20 kHz atuais são
herança do projeto antigo; com 88 pares de polos você precisa de razão suficiente
entre PWM e frequência elétrica, mas vale medir se 16 kHz não entrega o mesmo
controle com menos perda.

**SVPWM.** Já está configurado (`FOCModulationType::SpaceVectorPWM`) e dá cerca de
15% mais aproveitamento do barramento que a modulação senoidal. Mantenha.

**Id = 0.** É o argumento central do FOC sobre controle por tensão: para um dado
torque, manter Id em zero minimiza a corrente total e portanto a perda no cobre.
Isso só funciona se a medida de corrente for confiável — o que fecha o círculo com
a seção 3.

**Pulse-and-glide de verdade.** O `motor.move(0)` do código atual mantém as chaves
comutando regulando Iq para zero. Num evento de eficiência, planeio significa
**desarmar**: `motor.disable()` e ENABLE baixo. Só cuidado com a seção 4 — ao
rearmar, reconfigure o DRV por SPI.

**Taxa da malha.** Malha lenta gera ripple de corrente, e ripple é perda. É mais um
motivo para não deixar `analogRead` no caminho crítico.

---

## 6. Passo a passo

Cada passo tem um critério de aprovação. Não avance sem ele.

**1. Bridges e jumpers.** Aplique a seção 1 inteira. Meça VREF+ com multímetro.
*Critério: VREF+ conhecido e anotado; continuidade confirmada entre os pinos do
shield e os pinos certos do MCU.*

**2. Option bytes.** `nSWBOOT0 = 0`, `nBOOT0 = 1` via CubeProgrammer.
*Critério: a placa dá boot com o CAN conectado.*

**3. Build vazio.** `nucleo_g491re`, 170 MHz, SWD funcionando, um LED piscando.
*Critério: breakpoint para na função de blink.*

**4. Serial em PB6/PB7.** `HardwareSerial SerialDbg(PB7, PB6)`, USB-serial externo.
*Critério: texto legível no terminal.*

**5. SPI do DRV8353RS.** Com ENABLE alto, leia os registradores de status.
Configure modo 6x, dead time, IDRIVE e OCP. Reaplique dentro do `ArmDriver()`.
*Critério: leitura de volta bate com o que você escreveu.*

**6. Halls.** Gire a roda com a mão, sem potência. Confira 88 pares de polos.
*Critério: ângulo elétrico percorre 0–2π e a contagem por volta mecânica fecha.*

**7. Inline sem energizar.** `InlineCurrentSense`, driver desarmado.
*Critério: três correntes em ~0 A; anote o desvio-padrão — é seu piso de ruído.*

**8. Modo tensão, tensão baixa, roda livre.** Três senoides a 120°.
*Critério: Ia+Ib+Ic colado no zero em toda a volta.*

**9. Meça o tempo de ciclo.** Toggle de GPIO em volta do `loopFOC()`, no
osciloscópio. Faça com a Opção A e depois com a Opção B.
*Critério: você tem um número. Ele decide se para na B ou vai para a C.*

**10. Migre para `GenericCurrentSense` (Opção B).** Ajuste os sinais na mão com
`gain_a/b/c` — o `initFOC()` não faz isso por você aqui.
*Critério: mesmas senoides do passo 8, com tempo de ciclo menor.*

**11. Alinhamento.** `SKIP_STARTUP_ALIGN = 0`, roda livre. Anote
`zero_electric_angle` e `sensor_direction` com 4 casas.
*Critério: valores estáveis entre partidas sucessivas.*

**12. Feche a malha.** `foc_current`, ganhos recalculados pela taxa medida no
passo 9, `Tf` na casa de 30–50 µs.
*Critério: degrau de corrente sem sobressinal excessivo nem oscilação sustentada.*

**13. Endurecimento.** IWDG, `TIM1_BKIN` no PB10, releitura de nível do nFAULT
após armar, `volatile` no `driver_armed`, remoção dos `delay()` do caminho de
controle, corte por subtensão via VBAT (PC5) e por temperatura (PA3).

**14. Só então** pulse-and-glide com desarme real, e as medições comparativas
contra a versão CubeIDE.

---

## 7. Constantes a refazer

| Constante | Situação |
|---|---|
| `GAIN_INLINE` | 20, 50, 100 ou 200 V/V conforme o sufixo do INA240 (A1/A2/A3/A4) |
| `SHUNT_INLINE` | do esquemático |
| fundo de escala | `V_REF_bias / (SHUNT × GAIN)`, com o VREF+ real da seção 1.4 |
| `MOTOR_LIMIT` | está em 30 V; o teto do SVPWM com 42 V é 42/√3 ≈ 24,2 V |
| `CURRENT_TF` | 500 µs põe polo em 318 Hz, dentro da banda; alvo 30–50 µs |
| `CURRENT_P` / `CURRENT_I` | Kp = L·ωc, Ki = R·ωc, com ωc ≤ 1/10 da taxa medida no passo 9 |
| `TORQUE_SLEW` | está em 1000 A/s (valor de calibração); voltar para 1,0 |
| `ZERO_ELECTRIC_ANGLE` | inválida — refazer no passo 11 |
| `POLE_PAIRS` | confirmar os 88 no passo 6 |

---

## 8. Leitura

| Documento | Seção |
|---|---|
| UM2505 Rev 7 | 7.6.3 (VREF+), 7.6.5 (VCP), Tabelas 10, 11, 12, 16 |
| Datasheet DRV8353 | protocolo SPI, mapa de registradores, valores de reset, `t_WAKE` |
| Datasheet INA240 | ganho por sufixo, faixa de saída, polarização do REF |
| RM0440 | TIM1 complementar e break, ADC injetado, option bytes |
| docs.simplefoc.com/regular_adc_read | o helper da Opção B |
| docs.simplefoc.com/generic_current_sense | a classe da Opção B |
| docs.simplefoc.com/real_time_loop | determinismo da malha |
| docs.simplefoc.com/openloop_efficiency | considerações de eficiência |

---

## 9. O que eu NÃO consegui confirmar

Listado aqui de propósito. Nada disso deve ser assumido.

1. **Estado padrão dos SBs 33, 3, 36, 37, 7, 35, 34 e 14.** A tabela do UM2505
   marca os padrões em negrito e a extração de texto perdeu essa formatação.
   Abra o PDF original na Tabela 12 e confira visualmente a placa.
2. **O valor real do VREF+ da sua unidade.** Meça. É 2,5 V ou 3,25 V, e a diferença
   entre os dois é 30% de erro na corrente.
3. **Valor do shunt inline e sufixo do INA240.** Não estão na planilha que você
   mandou.
4. **Tensão de alimentação e polarização do REF do INA240.** Se ele estiver em 5 V,
   a saída pode exceder o que o ADC do G491 tolera.
5. **Valores de reset dos registradores do DRV8353RS.** O modo 6x corresponde a
   `PWM_MODE = 00b`, mas se ele é o valor de reset você precisa ler na tabela.
6. **Tempo real do `loopFOC()` na sua placa.** A documentação diz ">10 µs" para o
   helper regular e adverte contra `analogRead`, mas o número da sua montagem só
   sai medindo (passo 9).
7. **Se os 88 pares de polos estão corretos.** O `MOTOR_KV` no código traz um
   comentário "ou é 10.5" ao lado de 6.064, o que sugere que os parâmetros do motor
   ainda não foram fechados.
