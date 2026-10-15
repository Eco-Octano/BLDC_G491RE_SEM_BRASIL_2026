# CHECKPOINT — Firmware FOC da ESC Eco Octano

**Data:** agosto de 2026
**Alvo:** NUCLEO-G491RE + shield ESC custom (DRV8353RS)
**Biblioteca:** SimpleFOC 2.4.0 (Arduino / stm32duino)
**Objetivo:** firmware de competição, a ser comparado com uma versão paralela
feita em STM32CubeIDE/MCSDK

---

## 1. Onde estamos

| Módulo | Descrição | Estado |
|---|---|---|
| **M1** | Esqueleto do projeto, toolchain, build | ✅ compila |
| **M2** | Driver SPI do DRV8353RS | ✅ escrito, não testado |
| **M3** | Sensores hall | ⬜ não iniciado |
| **M4** | Driver de PWM e malha aberta | ⬜ não iniciado |
| **M5** | Camada de corrente (3 backends) | ✅ escrito, não testado |
| **M6** | Alinhamento (zero elétrico) | ⬜ bloqueado por hardware |
| **M7** | Malha de corrente fechada | ⬜ bloqueado por M5 |
| **M8** | Aplicação (torque, botões, pulse-and-glide) | ⬜ não iniciado |
| **M9** | Segurança (IWDG, BKIN, cortes) | ⬜ não iniciado |
| **M10** | Telemetria e ensaio comparativo | ⬜ não iniciado |

**A fase de escrita sem hardware terminou.** Tudo que resta precisa medir algo
na placa para ter parâmetro.

---

## 2. Arquivos do projeto

```
projeto/
├── platformio.ini                          3 envs: inline, inline_align, lowside
├── boards/
│   └── nucleo_g491re_arduino.json          board customizado (o oficial não tem Arduino)
├── include/
│   ├── board_pins.h                        TODA a pinagem + constantes elétricas
│   ├── drv8353_regs.h                      mapa de registradores (verificado)
│   ├── drv8353.h                           interface do driver SPI
│   └── current_backend.h                   3 implementações de leitura de corrente
├── src/
│   ├── drv8353.cpp                         implementação do driver SPI
│   └── main.cpp                            bancada + console serial
└── tools/
    └── variant_NUCLEO_G491RE_ARDUINO.h     cópia de segurança (ver §6)
```

---

## 3. Constantes fechadas

### Cadeia de medida de corrente — idêntica nas duas topologias

```
shunt                3 mΩ          (inline e low-side)
ganho                20 V/V        INA240A1 fixo | CSA do DRV por SPI
transimpedância      60 mV/A
repouso              1,65 V        (bidirecional)
fundo de escala      ±27,5 A       (um pouco menos: o INA240 não chega aos trilhos)
resolução            ~13 mA/LSB    12 bits com VREF+ = 3,3 V
limite de operação   19 A          (~30% de margem até saturar)
```

### Potência e motor

```
barramento           42 V
teto do SVPWM        42/√3 ≈ 24,25 V   ← MOTOR_LIMIT não pode passar disso
PWM                  20 kHz            (reavaliar no M10 por eficiência)
pares de polos       88                (CONFIRMAR no M3)
R de fase            0,095 Ω
L de fase            64,4 µH
τ elétrica (L/R)     678 µs            ← Kp/Ki do PI tem que dar isto
```

### Configuração do gate driver (calculada para o CSD18535KCS)

| Campo | Código | Valor | Justificativa |
|---|---|---|---|
| `PWM_MODE` | `00b` | 6x | exigido pela SimpleFOC (é o reset) |
| `IDRIVEP` | `0100b` | 300 mA | Miller em 35 ns; dV/dt 1,2 V/ns |
| `IDRIVEN` | `0100b` | 600 mA | t_off = 79 ns |
| `TDRIVE` | `00b` | 500 ns | > 210 ns para entregar Qg |
| `DEAD_TIME` | `10b` | 200 ns | 2,5× de margem sobre 79 ns |
| `OCP_MODE` | `00b` | latch | retry mascara defeito de hardware |
| `OCP_DEG` | `10b` | 4 µs | conservador contra disparo espúrio |
| `VDS_LVL` | `0101b` | 0,2 V | 78 A quente / 125 A frio (secundária) |
| `CSA_GAIN` | `10b` | 20 V/V | 60 mV/A, igual à inline |
| `SEN_LVL` | `00b` | 0,25 V | **83 A — proteção primária** |
| `VREF_DIV` | `1b` | VREF/2 | bidirecional, obrigatório para FOC |

### Valores de RESET do DRV8353RS (ferramenta de diagnóstico)

```
0x02 = 0x000    PWM_MODE 6x
0x03 = 0x3FF    LOCK destravado, IDRIVEP 1000 mA, IDRIVEN 2000 mA
0x04 = 0x7FF
0x05 = 0x15D    dead time 100 ns, OCP retry, deglitch 4 µs, VDS 1 V
0x06 = 0x283    VREF_DIV 1, CSA_GAIN 20 V/V, SEN_LVL 1 V
```

Se o dump mostrar **exatamente** esses valores, o `applyConfig()` não rodou —
ou o ENABLE caiu e levou a configuração junto.

### Constantes que ainda não têm valor

| Constante | Sai de |
|---|---|
| `CS_SIGN_A/B/C` | env `inline_align` (M5, passo 2) |
| `ZERO_ELECTRIC_ANGLE` | alinhamento (M6) |
| `SENSOR_DIRECTION` | idem |
| `CURRENT_P` / `CURRENT_I` | tempo de ciclo medido (M7) |
| `CURRENT_TF` | idem — tem que ficar FORA da banda |
| `VREF_PLUS_V` | medir com multímetro |

---

## 4. Decisões de projeto e por quê

**Três envs do mesmo código-fonte.** O FOC não sabe como a corrente foi lida.
Se soubesse, comparar as duas topologias viraria comparar dois firmwares.

**Low-side em duas fases (A e C), não três.** PA2 (ADC1_IN3) e PB0 (ADC1_IN15)
estão no mesmo ADC; PA6 só existe no ADC2. A SimpleFOC exige ADC comum para a
sequência injetada. A fase B é reconstruída por `Ia+Ib+Ic=0` — não é concessão,
o FOC só precisa de duas.

**Inline pela `GenericCurrentSense`, não pela `InlineCurrentSense`.** A
documentação da SimpleFOC adverte contra `analogRead()` durante o FOC
(reconfigura o ADC a cada chamada, latência e jitter). A Generic com
`_readRegularADCVoltage()` é mais rápida e deixa a conversão sob nosso
controle — o que permite corrigir o VREF+ sem editar a biblioteca.

**O sinal de cada fase vive no callback.** Descoberta na leitura do
`GenericCurrentSense.cpp`: o `getPhaseCurrents()` só subtrai o offset e
**ignora** os campos `gain_a/b/c`. Escrever `cs.gain_a *= -1` não teria efeito
nenhum — bug silencioso. E o `driverAlign()` da Generic é um stub que retorna 1.
Daí a env `inline_align`, que roda a classe convencional só para revelar os
sinais.

**O offset não é calculado, é medido.** A `calibrateOffsets()` chama o callback
1000 vezes e tira a média em ampères. Absorve de uma vez o repouso do INA240, o
offset do amplificador e a incerteza do VREF+.

**O `enable()` reconfigura o driver sempre.** Os registradores do DRV8353RS se
perdem quando o ENABLE cai. Em pulse-and-glide isso acontece o tempo todo.

**Toda escrita SPI é relida e comparada.** Um bit de modo que não pegou
transforma 6x em 3x, e o sintoma é "o motor não gira", sem mensagem de erro.

**OCP primária pelo shunt, não por VDS.** Com 3 mΩ, `SEN_LVL = 0,25 V` dispara
em 83 A e é estável com temperatura. O VDS varia por um fator 1,6 entre 25 °C e
125 °C — serve como rede secundária.

**Tempo morto no DRV, `driver.dead_zone = 0` na SimpleFOC.** Os dois somam.

---

## 5. Erros cometidos e corrigidos (para não repetir)

**Corrente de gate baixa NÃO é o lado seguro.** A configuração provisória
inicial (`idrive = 0x00` = 50/100 mA) dava t_off = 473 ns contra um tempo morto
de 400 ns — shoot-through de 42 V. A corrente de dreno alonga o desligamento, e
é o desligamento que o tempo morto tem que cobrir.

**Os pinos de ADC foram otimizados para o alvo errado.** PA2/PA6/PB0, um em cada
ADC, é ideal para amostragem simultânea — e incompatível tanto com a SimpleFOC
(exige ADC comum) quanto com o MCSDK (usa 2 ADCs com fase compartilhada).

**O board `nucleo_g491re` do PlatformIO não declara o framework Arduino.**
Verificado na documentação oficial. Resolvido com board customizado.

---

## 6. Pendências de hardware — ANTES de encaixar o shield

### Solder bridges (UM2505)

| SB | Estado | Motivo |
|---|---|---|
| SB17, SB23 | **abrir** | tira PA2/PA3 do VCP do ST-Link |
| SB18, SB22 | **fechar** | leva PA2/PA3 ao CN10-35/37 |
| SB13, SB19 | **abrir** | tira PC4/PC5 do CN10-35/37 |
| SB16 | abrir | libera PC13 do botão B1 |
| SB6 | abrir | libera PA5 do LED LD2 |
| SB15 | abrir | libera PB3 do TRACESWO |
| SB29–SB32 | conferir | PC14/PC15 estão no cristal LSE por padrão |

⚠️ **Com SB13/SB19 fechados, LSCSA (PA2) e HSCS (PC4) ficam EM CURTO no
CN10-35** — duas saídas de amplificador ligadas uma na outra.

O VCP do ST-Link é perdido. O SWD continua funcionando. Serial passa a ser
USART1 em PB6/PB7, com conversor USB-serial externo.

### Outros

- **JP8 → posição [2-3]**: VREF+ alimentado por VDD (3,3 V). No padrão o TL1431
  dá 2,5 V ou 3,25 V; com 2,5 V a leitura satura em ~14 A, e só no positivo.
- **Option bytes**: `nSWBOOT0 = 0` (PB8 é BOOT0 e é usado como CAN RX).
- **Guardar cópia** do `variant_NUCLEO_G491RE_ARDUINO.h`: ele vive dentro de
  `framework-arduinoststm32` e some numa reinstalação do pacote.

### Verificações de esquemático ainda abertas

- Polaridade das shunts nos pinos SPx/SNx do DRV (SP/SN trocado inverte o sinal)
- `ANALOG_3V3` é a mesma rede do VDD do MCU?
- Pull-ups dos halls (5–10 kΩ): montados ou usar os internos?
- Potência dos resistores de shunt: a 19 A, 3 mΩ dissipa ~1,1 W → usar 2 W

### Medições para o dia em que a placa voltar

1. VREF+ no pino da Nucleo (deve dar 3,3 V depois do JP8)
2. Saída de um INA240 em repouso (deve dar ~1,65 V)
3. Continuidade CN10-35 → PA2, e **não** para PC4

---

## 7. Roteiro de bring-up

Cada passo tem critério de aprovação. Não avance sem ele.

1. **Rework**, Nucleo sozinha. *Continuidade confirmada.*
2. **Meça VREF+.** *Valor anotado.*
3. **Shield + 3,3 V, sem potência.** Comando `d`. *O dump responde.*
4. **`e` e depois `d`.** *Os valores são os da §3, não os de reset.*
5. **`x`, `e`, `d`.** *A configuração volta sozinha — valida o rearme.*
6. **`i` e depois `z`,** sem potência. *Correntes em ~0 A; anote o desvio.*
7. — só a partir daqui o barramento de 42 V —
8. **M3**: halls. *528 transições por volta mecânica.*
9. **M4**: malha aberta, 2 V. *Gira suave, correntes equilibradas.*
10. **M5 passo 2**: env `inline_align`. *Sinais anotados em `CS_SIGN_A/B/C`.*
11. **M6**: alinhamento. *Três medições consistentes.*
12. **M7**: medir tempo de ciclo, projetar o PI, fechar a malha.
13. **M8, M9, M10.**

---

## 8. Documentos de referência

| Documento | Código | Para quê |
|---|---|---|
| DRV8350/8353 datasheet | SLVSDY6A Rev A | SPI, registradores, tabelas 14–18 |
| CSD18535KCS datasheet | SLPS531 | Qg, Qgd, Crss, Rds(on) vs temperatura |
| INA240 datasheet | SBOS678 | ganho por sufixo, faixa de saída, REF |
| NUCLEO-64 G4 user manual | UM2505 Rev 7 | solder bridges, JP8, VREF+ |
| STM32G4 reference manual | RM0440 | TIM1, ADC, option bytes |
| SimpleFOC — Choosing ADC pins | docs.simplefoc.com | regra do ADC comum |
| SimpleFOC — Regular ADC read | docs.simplefoc.com | `_readRegularADCVoltage()` |
| SimpleFOC — Generic current sense | docs.simplefoc.com | a classe usada no inline |

### Trechos de código da biblioteca que importam

```
C:\pio\lib\inline\Simple FOC\src\
├── current_sense\GenericCurrentSense.cpp        offset e getPhaseCurrents
├── current_sense\hardware_specific\stm32\
│   ├── stm32_mcu.cpp                            _ADC_VOLTAGE_G4 3.3f
│   │                                            _ADC_RESOLUTION_G4 4096.0f
│   └── stm32g4\stm32g4_hal.cpp                  sequência injetada, _adc_init()
```

---

## 9. Alavancas de eficiência (para o comparativo)

- **Tempo morto**: DRV **ou** `dead_zone`, nunca os dois. 400→100 ns devolve
  1,2% do período em 20 kHz.
- **IDRIVE**: comutação mais rápida reduz perda; o Crss de 24 pF do
  CSD18535KCS torna isso seguro (Vgs induzido de 0,2 V contra Vth de 1,4 V).
- **Frequência de PWM**: perda de comutação escala com ela; testar 16 kHz.
- **SVPWM**: ~15% a mais de barramento que senoidal. Já configurado.
- **Id = 0**: minimiza corrente total para um dado torque.
- **Pulse-and-glide com desarme real**, não `motor.move(0)`.
- **Malha rápida**: ripple é perda.

---

## 10. O experimento comparativo (M10)

A placa tem as duas tríades de medida — condição rara e a oportunidade central
deste trabalho.

**Arranjo:** low-side injetada fecha a malha; inline lida por
`_readRegularADCVoltage()` a cada N ciclos como referência independente.

**Métricas:** desvio-padrão de Iq; erro contra medida externa; tempo de ciclo;
e **potência no barramento para o mesmo torque e velocidade na roda** — a
métrica que decide a prova.

**Varra o índice de modulação.** As duas falham em pontas opostas: o erro da
inline assíncrona é máximo perto de 50% de modulação; a janela cega da low-side
aparece perto de 100%. Ensaio num ponto só favorece arbitrariamente uma delas.

⚠️ **Não compare amostra por amostra.** A low-side injetada amostra a média do
período; a inline assíncrona amostra um ponto do triângulo de ripple. A
diferença instantânea entre elas é o ripple, não erro de medida. Compare médias
ou os Iq/Id filtrados.
