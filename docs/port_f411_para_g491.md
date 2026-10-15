# Port ESC_BLDC_2024: BlackPill F411 + DRV8323H → NUCLEO-G491RE + DRV8353RS (inline)

São três mudanças independentes acontecendo ao mesmo tempo: **MCU**, **gate driver** e
**topologia de medição de corrente**. Vale tratá-las em ordem, porque se você trocar tudo
de uma vez e o motor não girar, não vai saber em qual das três está o problema.

---

## 1. Toolchain e build

| Item | Antes | Depois |
|---|---|---|
| `board` | `genericSTM32F411CE` | `nucleo_g491re` |
| Upload | DFU (BOOT0 + botão) | ST-Link on-board |
| Debug | só `printf` | **SWD real, com breakpoint** |
| Clock | 100 MHz | 170 MHz |
| USB CDC | habilitado | não é mais o caminho do serial |
| `lib_archive = no` | obrigatório | continua obrigatório |

O ganho de fluxo aqui é maior do que parece: você sai de "compila, aperta BOOT0, torce" para
step-debug com o ST-Link embutido. Vale gastar meia hora configurando o debugger antes de
qualquer outra coisa — o resto do port fica muito mais barato.

Confirme que a variante do stm32duino está configurando o PLL para 170 MHz. Isso muda a
resolução do PWM em 20 kHz e o tempo de ciclo do FOC.

---

## 2. Pinagem — tabela completa de substituição

| Função | F411 (antes) | G491 (depois) | Observação |
|---|---|---|---|
| PWM A alto / baixo | PA8 / PB13 | PA8 / **PA11** | TIM1_CH1/CH1N, **AF6** |
| PWM B alto / baixo | PA9 / PB14 | PA9 / **PA12** | TIM1_CH2/CH2N, AF6 |
| PWM C alto / baixo | PA10 / PB15 | PA10 / **PB1** | TIM1_CH3/CH3N, AF6 |
| Hall A / B / C | PA15 / PB3 / PB4 | **PC6 / PC7 / PC8** | TIM3_CH1/2/3, EXTI 6/7/8 |
| Corrente A / B / C | PA7 / PB0 / PB1 (low-side) | **PC0 / PA7 / PB13** (inline) | ADC1_IN6 / ADC2_IN4 / ADC3_IN5 |
| ENABLE do driver | PA4 | **PD2** | pull-down de 100 k na placa |
| nFAULT | PB10 | **PB10** | agora também é TIM1_BKIN |
| CAL do driver | PB8 | **não existe mais** | virou registrador SPI |
| Botão sobe / desce | PA3 / PA6 | **PB14 / PB12** | PA6 agora é low-side B |
| LED de status | PC13 | **PC14** | PC13 é o botão B1 da Nucleo |
| Serial | USB CDC | **PB6 / PB7** (USART1) | ver seção 3 |
| SPI do driver | — | PC10/PC11/PC12, nSCS PC9 | SPI3 |

**Atenção ao PB3.** Era o Hall B; agora é `SPI1_SCK` do Teensy. Qualquer referência velha a
PB3 no código vai colidir com a comunicação do supervisório.

**Confira as linhas EXTI.** Halls em PC6/7/8 usam EXTI 6, 7 e 8; nFAULT em PB10 usa EXTI 10.
Sem conflito no arranjo atual, mas se você adicionar mais alguma interrupção externa,
lembre que o STM32 compartilha a linha pelo *número* do pino, não pela porta.

---

## 3. Serial — mudança silenciosa que quebra tudo

Na NUCLEO-G491RE o VCP do ST-Link vai para **PA2/PA3**, que na sua placa são a low-side A e a
temperatura do motor. Duas consequências:

1. **Abra os solder bridges do VCP.** Se ficarem fechados, o TX do ST-Link disputa o pino com
   a saída do amplificador de shunt. Confirme quais são no manual da Nucleo.
2. **O `Serial` padrão do stm32duino deixa de servir.** Instancie o seu:

```cpp
HardwareSerial SerialDbg(PB7, PB6);   // USART1: RX, TX
```

E então troque **todas** as referências: `SimpleFOCDebug::enable(&SerialDbg)`,
`Commander command = Commander(SerialDbg)`, `motor.useMonitoring(&SerialDbg)`, e cada
`Serial.print` da telemetria. É fácil esquecer um e passar meia hora sem entender por que
nada aparece.

O bloco `while (!Serial)` do `WAIT_FOR_SERIAL` não faz sentido numa UART de hardware —
ele nunca bloqueia. Remova.

Você vai precisar de um conversor USB-serial externo, já que não é mais USB CDC.

---

## 4. Gate driver: DRV8323H → DRV8353RS

Esta é a maior mudança de firmware, e a que não tem atalho.

O sufixo **H** era a variante *hardware*: modo de PWM, ganho do CSA, dead time e OCP vinham
de resistor ou pino. O **RS** é a variante SPI — nada disso existe em pino.

### O que some do código

```cpp
// tudo isso deixa de existir
const int CAL_PIN = PB8;
digitalWrite(CAL_PIN, HIGH); delay(100); digitalWrite(CAL_PIN, LOW);
```

### O que precisa ser escrito

Uma camada mínima de SPI3, antes de qualquer coisa energizar:

- **Escrita e leitura de registradores.** Quadros de 16 bits, MSB primeiro, com o bit de
  leitura/escrita no topo. Confirme polaridade e fase do clock no datasheet.
- **Modo de PWM.** Precisa ser 6x. Confirme qual é o valor de *reset* — se não for 6x, o
  motor não gira e você vai procurar o erro no lugar errado.
- **Dead time.** Agora é registrador. Decida onde colocar o tempo morto: no DRV ou no
  `driver.dead_zone` da SimpleFOC. Os dois somam, e somar sem querer distorce a corrente.
- **Ganho do CSA e calibração de offset.** Só importa quando você for usar as low-side.
  Para a etapa inline, ignore.
- **OCP / limiar de VDS e IDRIVE.** Configure antes de subir tensão. É a proteção que
  salva a placa nova.
- **Leitura dos registradores de falha.** Dois registradores de status contam *qual* falha
  ocorreu. Hoje seu código só sabe que "houve falha".

### Sequência de partida

Há um tempo de wake-up depois de subir o ENABLE antes de o SPI responder. Os seus
`delay(DRV_WAKEUP_MS)` de 2 ms provavelmente cobrem, mas confirme o `t_WAKE`.

**Estudar:** datasheet do DRV8353RS — seção do protocolo SPI, mapa de registradores, e a
tabela de valores de reset.

---

## 5. Low-side → inline: o que muda de verdade

Não é só trocar o nome da classe.

### Mudança de classe

```cpp
// antes
LowsideCurrentSense current_sense = LowsideCurrentSense(0.002f, 40.0f, PA7, PB0, PB1);
// depois
InlineCurrentSense  current_sense = InlineCurrentSense(SHUNT, GAIN, PC0, PA7, PB13);
```

### Consequências práticas

**Shunt e ganho são outros.** Os 0,002 Ω e 40 V/V vinham do CSA do DRV8323H medindo as
low-side. As inline passam por amplificadores externos próprios, com shunt próprio. Tire os
dois do esquemático.

**O teto de corrente muda.** O antigo era `(3,3/2)/(0,002 × 40) ≈ 20,6 A`, e o `MAX_TORQUE`
de 19 A saía dali. Refaça a conta com os valores novos — pode dar bem diferente, e nos dois
sentidos.

**Some a exigência de sincronismo, some o sincronismo.** A `LowsideCurrentSense` amostrava
por canal injetado disparado pelo TIM1, na janela em que os FETs inferiores conduzem. A
`InlineCurrentSense` faz `analogRead` quando o loop chama, num ponto arbitrário do período de
PWM. Você ganha liberdade de pinagem e perde a limpeza do sinal: o ripple de chaveamento
entra inteiro na medida.

**O tempo de ciclo sobe.** `analogRead` no stm32duino reconfigura e faz polling a cada
chamada. Três chamadas por iteração custam caro. **Meça isso** — com 88 pares de polos e
alvo de dezenas de kHz de loop, esse número decide se o caminho inline serve ou não para a
prova.

**Tensão sobre a medida.** O ruído extra empurra você a aumentar o `LPF_current_*.Tf`, mas o
filtro come margem de fase da malha. É o aperto central deste caminho, e não tem solução
elegante — só compromisso medido.

**Vantagem real:** a shunt inline conduz em qualquer estado de chaveamento. Some a janela
cega em razão cíclica alta, que é o calcanhar da low-side justamente em plena potência.

---

## 6. Constantes que precisam ser refeitas

| Constante | Situação |
|---|---|
| `ZERO_ELECTRIC_ANGLE` | **Inválida.** Foi medida no chicote e na ordem de fases antigos. Refaça com `SKIP_STARTUP_ALIGN = 0` e roda livre. O `3.14f` atual tem cara de placeholder. |
| `SENSOR_DIRECTION` | Idem — sai do mesmo procedimento. |
| `MOTOR_LIMIT` | Está em 30 V, acima do teto do SVPWM (42/√3 ≈ 24,2 V). Corrija, ou o integrador acumula numa região inalcançável. |
| `CURRENT_TF` | 500 µs põe um polo em ~318 Hz, dentro da banda da malha. Reduza para a faixa de 30–50 µs e reavalie com o ruído do inline. |
| `CURRENT_P` / `CURRENT_I` | Recalcule depois de medir a taxa real do `loopFOC()`. Kp = L·ωc, Ki = R·ωc, com ωc no máximo 1/10 da frequência de amostragem. |
| `TORQUE_SLEW` | Está em 1000 A/s (valor de calibração). Volte para 1,0. |
| `MAX_TORQUE` | Refazer pelo novo shunt e ganho. |
| `POLE_PAIRS` | Confirme os 88. |

---

## 7. Bugs do código atual, para corrigir durante o port

- O `BLDCDriver6PWM` é instanciado com `PWM_UH_PIN…PWM_WL_PIN`, que só existem no bloco
  comentado. **Não compila.**
- `Serial.println(tempo_foc_us, 3)` — o segundo argumento de um `uint32_t` é a **base**, não
  casas decimais. Imprime em base 3.
- `tempo_foc_us` só é atribuído dentro do ramo de falha; no loop normal a medição está
  comentada, então a telemetria mostra zero.
- `driver_armed` é lido e escrito na ISR mas não é `volatile`.
- A interrupção do nFAULT é por **borda de descida**, mas nFAULT é sinal de **nível**. Uma
  falha durante os 2 ms de wake-up nunca é vista. Leia o pino depois de armar.
- `delay()` dentro de `ArmDriver`/`DisarmDriver` bloqueia o FOC por 2 ms a cada transição.
- Instrumentação de LED esquecida em volta do `loopFOC()` no loop principal.

---

## 8. Oportunidades novas que a placa abre

- **nFAULT em TIM1_BKIN.** Você pode fazer o hardware desligar a ponte sem passar por
  software. A SimpleFOC não configura isso; dá para fazer com LL depois do `driver.init()`.
  É a proteção mais rápida que existe.
- **VBAT em PC5 e temperatura em PA3.** Duas medidas que o firmware antigo não tinha e que
  valem muito num veículo: cutoff por subtensão e proteção térmica.
- **170 MHz, FPU, CORDIC e FMAC.** Folga de sobra para o FOC.

---

## 9. O que ler

| Documento | Para quê |
|---|---|
| Datasheet DRV8353RS | SPI, mapa de registradores, valores de reset, `t_WAKE` |
| RM0440 (ref. manual G4) | TIM1 complementar e break, ADC |
| Manual de usuário da NUCLEO-G491RE | solder bridges do VCP, PC13, PA5 |
| `PeripheralPins.c` da variante `NUCLEO_G491RE` | qual AF cada pino aceita, de verdade |
| `stm32_mcu.cpp` da SimpleFOC | `_ADC_RESOLUTION` — tem que bater com o `analogReadResolution()` |
| Docs SimpleFOC: *Inline Current Sense* e *Choosing ADC pins* | comportamento e restrições da classe |
| AN5346 | boas práticas de ADC no G4 |

---

## 10. Ordem sugerida

1. Build vazio no `nucleo_g491re`, SWD funcionando, blink no PC14.
2. Serial em PB6/PB7 imprimindo, com os solder bridges do PA2/PA3 abertos.
3. Camada SPI do DRV8353RS: ler os registradores de status e confirmar que responde.
4. Halls: girar a roda com a mão e ver o ângulo mudar. Sem potência ainda.
5. Leitura inline com o driver desarmado — offset e ruído de fundo.
6. Modo tensão, tensão baixa, roda livre: as três correntes senoidais, soma em zero.
7. Alinhamento com `SKIP_STARTUP_ALIGN = 0`, anotar os valores.
8. Só então fechar a malha de corrente e retomar o controle de torque por degraus.

Cada passo isola uma variável. Pular do 1 para o 8 é a receita para não saber qual das três
mudanças quebrou.
