# GUIA DO CÓDIGO — como o firmware funciona e como usar

Documento didático. Lê-se de cima para baixo; a §1 explica a ideia geral, as
§2–§7 percorrem arquivo por arquivo, e a §8 em diante é o manual de operação.

---

## 1. A ideia geral

O firmware tem que provar uma coisa antes de fazer qualquer outra: **que a
placa lê corrente corretamente**. Todo o resto do FOC depende disso. Por isso a
arquitetura é organizada em camadas, de baixo para cima:

```
        ┌─────────────────────────────────┐
        │  main.cpp - bancada e console   │   você digita comandos
        └────────────┬────────────────────┘
                     │
     ┌───────────────┼───────────────┐
     ▼               ▼               ▼
┌──────────┐  ┌─────────────┐  ┌──────────────┐
│ drv8353  │  │ current_    │  │ BLDCDriver   │
│  (SPI)   │  │ backend     │  │  6PWM        │
└──────────┘  └─────────────┘  └──────────────┘
     │               │                │
     └───────────────┴────────────────┘
                     │
              ┌──────────────┐
              │ board_pins.h │   um lugar só para pinagem
              └──────────────┘
```

Duas regras que valem para o projeto inteiro:

**Nenhum número de pino fora do `board_pins.h`.** Se você se pegar escrevendo
`PA8` em outro arquivo, pare e traga para lá. Pino espalhado pelo código é a
causa mais comum de bug depois de uma revisão de placa.

**O FOC não sabe como a corrente foi lida.** Existe uma camada de abstração
justamente porque o objetivo do projeto é comparar duas topologias de medida.
Se o controle soubesse a diferença, você estaria comparando dois firmwares em
vez de duas medidas.

---

## 2. `platformio.ini` — três builds do mesmo código

O arquivo define **três ambientes**. Todos compilam exatamente o mesmo
código-fonte; muda só um `#define`:

| Env | O que muda | Quando usar |
|---|---|---|
| `inline` | lê as 3 correntes inline (INA240) | produção |
| `inline_align` | ferramenta descartável | uma vez, para achar os sinais |
| `lowside` | lê 2 correntes low-side (CSA do DRV) | produção |

```
pio run -e inline  -t upload
pio run -e lowside -t upload
```

### Três linhas que não são óbvias

**`lib_archive = no`** — obrigatório. Sem isso o linker descarta os símbolos
específicos do STM32 e a SimpleFOC cai num fallback genérico: o current sense
por canal injetado simplesmente não funciona. Você sabe que está certo quando o
log mostra `stm32g4_hal.cpp` sendo compilado.

**`board = nucleo_g491re_arduino`** — o board oficial do PlatformIO para essa
placa **não declara o framework Arduino**, só STM32Cube. O JSON em `boards/`
resolve isso.

**`libdeps_dir = C:/pio/lib`** — a SimpleFOC tem exemplos com aninhamento
profundo que estouram o limite de 260 caracteres de caminho do Windows.

---

## 3. `board_pins.h` — a pinagem e as constantes

Só definições. Nenhuma lógica. Três blocos:

**Pinos**, agrupados por função (ponte, gate driver, halls, corrente, IO).

**Constantes elétricas** — barramento, PWM, shunt, ganho, limite de corrente.

**Parâmetros do motor** — pares de polos, R, L, e a constante de tempo elétrica
`τ = L/R = 678 µs`, que o projeto do PI vai usar depois.

O cabeçalho do arquivo também documenta o **rework de hardware obrigatório**
(solder bridges, JP8, option bytes). Está lá de propósito: é a informação que
some primeiro quando outra pessoa pega o projeto.

### Por que `MAX_CURRENT_A = 19` e não 27,5

A cadeia de medida satura em ±27,5 A. Operar em 19 A deixa ~30% de margem, para
o transitório de um degrau de corrente não ceifar a medida — se a medida ceifa,
o PI enxerga um valor menor que o real e empurra ainda mais corrente.

---

## 4. `drv8353_regs.h` — o mapa de registradores

Só `#define`s, um por campo, **verificados linha a linha contra o datasheet**
(DRV835x Rev A, seção 8.6). Serve para o resto do código nunca escrever números
mágicos.

### O que é um "registrador" aqui

O DRV8353RS é a variante **SPI**: modo de PWM, tempo morto, corrente de gate,
proteção e ganho do amplificador são todos configuráveis por software. (A
variante `H`, usada na placa antiga, fazia tudo por resistor e pino — daí o
`CAL_PIN` ter desaparecido no port.)

Cada transação é um quadro de 16 bits:

```
 bit 15    bits 14-11        bits 10-0
┌───────┬──────────────┬──────────────────┐
│  R/W  │  endereço    │      dados       │
└───────┴──────────────┴──────────────────┘
```

### Os registradores que importam

| Endereço | Nome | Para quê |
|---|---|---|
| 0x00 | Fault Status 1 | *qual* falha ocorreu (VDS, UVLO, temperatura…) |
| 0x01 | Fault Status 2 | falhas de VGS e do amplificador |
| 0x02 | Driver Control | modo 6x PWM, limpar falhas |
| 0x03 | Gate Drive HS | corrente de gate do lado alto + bits LOCK |
| 0x04 | Gate Drive LS | corrente de gate do lado baixo |
| 0x05 | OCP Control | tempo morto, proteção de sobrecorrente |
| 0x06 | CSA Control | ganho, modo bidirecional, calibração de offset |

No fim do arquivo estão os **valores de reset**. Eles são a sua ferramenta de
diagnóstico número um: se o dump mostrar exatamente aqueles números, a
configuração não foi aplicada.

---

## 5. `drv8353.h` / `drv8353.cpp` — o driver SPI

### O detalhe que organiza a classe inteira

> O SPI só responde com o **ENABLE alto**, e a configuração dos registradores
> **é perdida** quando o ENABLE cai.

Por isso o `enable()` não só acorda o chip — ele reconfigura e verifica. Se você
"otimizar" isso um dia, achando que já configurou no `setup()`, o pulse-and-glide
vai rodar com ganho de CSA de reset e a corrente sai escalada, sem nenhum sinal
de erro.

### O ciclo de vida

```
begin()      configura GPIOs e SPI. NÃO acorda o driver.
   │
   ▼
enable()     ENABLE alto → espera wake-up → applyConfig() → verifica
   │         retorna false se algo não bateu
   ▼
(operação)   readFaults(), calibrateCSA(), dumpRegisters()
   │
   ▼
disable()    ENABLE baixo. Configuração morre aqui.
```

### `writeReg()` escreve e relê

```cpp
bool DRV8353::writeReg(uint8_t addr, uint16_t data) {
    transfer16_(frame);                    // escreve
    return readReg(addr) == data;          // confere
}
```

Custa uma transação a mais. Paga por si na primeira vez que um bit não pegar.

### `applyConfig_()` — a ordem importa

1. **Destrava** (bits LOCK). Se alguém deixou travado, todas as escritas
   seguintes seriam ignoradas em silêncio.
2. **Limpa falhas** herdadas do ciclo anterior.
3. Modo 6x PWM.
4. Corrente de gate (HS e LS).
5. Tempo morto e OCP.
6. Ganho do CSA e modo bidirecional.
7. **Verifica** os campos críticos.

### Por que os valores de configuração são os que são

Foram calculados a partir do MOSFET da placa (CSD18535KCS):

**Corrente de gate — 300 mA na subida, 600 mA na descida.** O tempo de comutação
é `Qgd / I = 10,4 nC / 300 mA = 35 ns`. O risco clássico de acelerar a comutação
é o FET oposto ligar sozinho pelo acoplamento de Miller, mas:

```
Vgs induzido = (Crss/Ciss) × ΔVds = (24/5090) × 42 V ≈ 0,2 V
```

contra um `Vgs(th)` de 1,4 V no pior caso. Margem de 7×. Este MOSFET é
excepcionalmente imune — é o que "Ultra-Low Qg and Qgd" significa na prática.

**Tempo morto — 200 ns.** Tem que cobrir o **desligamento**:

```
t_off = (Qg − Qgd − Qgs)/I_sink + Qgd/I_sink
      = 36,9 nC/600 mA + 10,4 nC/600 mA = 79 ns
```

⚠️ **Contra-intuitivo e importante:** corrente de gate baixa NÃO é o lado
seguro. Com 100 mA de dreno, `t_off` seria 473 ns — maior que o tempo morto, ou
seja, shoot-through. Foi um erro real cometido na primeira versão deste código.

**Proteção — duas camadas.** A primária é pelo shunt: `SEN_LVL = 0,25 V` sobre
3 mΩ dispara em **83 A**, e é estável com a temperatura. A secundária é por VDS
em 0,2 V, que dispara entre 78 A (quente) e 125 A (frio) — a variação vem do
`Rds(on)` dobrar com a temperatura.

---

## 6. `current_backend.h` — o coração do projeto

Três implementações atrás de uma interface só. É o arquivo mais denso, e vale
entender por quê.

### O que descobrimos lendo o código da SimpleFOC

Duas coisas mudaram o projeto:

**1. O `driverAlign()` da `GenericCurrentSense` é um stub.** Ele retorna 1 e não
faz nada. A classe não descobre o sinal nem a ordem das fases.

**2. O `getPhaseCurrents()` ignora os ganhos:**

```cpp
current.a = (current.a - offset_ia);   // só subtrai o offset
```

Os campos `gain_a/gain_b/gain_c` **não são usados**. Escrever
`cs.gain_a *= -1` não teria efeito nenhum — e esse é o pior tipo de bug: compila,
roda, e produz sinal errado em silêncio.

**Conclusão:** no modo inline, o callback é o único lugar onde o sinal pode ser
corrigido. Daí os `CS_SIGN_A/B/C`.

### E como descobrir o sinal certo?

Rodando **uma vez** a `InlineCurrentSense` convencional, que tem o alinhamento
de verdade, nos mesmos pinos. É exatamente para isso que existe a env
`inline_align`. Ferramenta descartável.

### Os três modos

**`CS_MODE_INLINE`** — produção.

```cpp
c.a = CS_SIGN_A * _readRegularADCVoltage(PIN_ILCS_A) * VREF_SCALE / CS_VOLTS_PER_AMP;
```

Sem restrição de ADC (os três pinos estão em ADCs diferentes). Assíncrono ao
PWM. Usa `_readRegularADCVoltage()` em vez de `analogRead()` — a documentação da
SimpleFOC adverte que o `analogRead` reconfigura o ADC a cada chamada e
introduz jitter na malha.

**`CS_MODE_INLINE_ALIGN`** — ferramenta, não produção.

**`CS_MODE_LOWSIDE`** — produção.

```cpp
LowsideCurrentSense(SHUNT_OHM, CS_GAIN_VpV, PIN_LSCS_A, _NC, PIN_LSCS_C);
```

Conversão injetada, sincronizada com o TIM1. PA2 e PB0 estão ambos no ADC1; PA6
só existe no ADC2, então a fase B fica de fora e é reconstruída por
`Ia+Ib+Ic=0`. **A ordem importa:** são as fases A e C, então o `_NC` vai no meio.

### Não se preocupe com o 1,65 V

A `calibrateOffsets()` chama o callback 1000 vezes e tira a média, já em
ampères. Devolva `tensão / (shunt × ganho)` e deixe a biblioteca medir e remover
o resíduo — isso absorve de uma vez o repouso do INA240, o offset do
amplificador e a incerteza do VREF+.

Duas consequências:

- **O `init()` bloqueia por ~1 s.** São 1000 amostras com `_delay(1)`. Não é travo.
- **Nada pode estar acionado.** Corrente circulando durante a calibração vira
  offset permanente, e o sintoma depois é "o motor puxa corrente parado".

### Uma assimetria importante

O fator `VREF_SCALE` só funciona no modo **inline**, porque lá a conversão é
nossa. No modo low-side ela é interna à biblioteca e não há onde corrigir — por
isso, se você for usar low-side, o JP8 precisa ir para VDD. Não é opcional lá.

---

## 7. `main.cpp` — a bancada

Nesta etapa **o motor não gira**. Não existe objeto `BLDCMotor` nem malha
fechada. O arquivo tem três partes:

**A configuração do gate driver** (`drv_cfg`), com cada campo comentado com a
conta que o justifica.

**O console serial**, que é como você opera tudo.

**O tratamento de falha**, que faz algo aparentemente estranho: quando o nFAULT
dispara, ele desliga o driver e **reacorda só para ler o motivo**. É porque a
leitura por SPI exige ENABLE alto — o próprio ato de proteger cega o
diagnóstico.

### Por que o driver de PWM sobe mesmo no modo inline

A `LowsideCurrentSense` precisa dele para achar o TIM1 e montar a sequência
injetada; a `InlineCurrentSense` precisa dele para o alinhamento. Com o DRV
desarmado (ENABLE baixo), nada chega aos MOSFETs — é seguro.

---

## 8. Manual de operação — o console

Conecte um **conversor USB-serial** em PB6 (TX) e PB7 (RX), 115200 baud. O VCP
do ST-Link não serve mais depois do rework.

### Comandos do gate driver

| Tecla | O que faz |
|---|---|
| `?` | ajuda |
| `d` | dump dos 7 registradores + falhas decodificadas |
| `e` | acorda o driver, configura e **verifica** |
| `x` | desliga o driver (a configuração é perdida) |
| `f` | lê e decodifica as falhas |
| `c` | limpa as falhas (não resolve a causa) |
| `k` | calibra o offset dos CSA (só faz sentido no modo low-side) |
| `r AA` | lê o registrador `AA` (hex) |
| `w AA DDD` | escreve `DDD` (hex) no registrador `AA` |

### Comandos de corrente

| Tecla | O que faz |
|---|---|
| `i` | inicializa o driver de PWM e a camada de corrente |
| `s` | liga/desliga o streaming (`Ia,Ib,Ic,soma`) |
| `z` | 200 amostras: média e desvio-padrão de cada fase |

---

## 9. Roteiro de uso, passo a passo

### Etapa A — o gate driver responde (M2)

**Só 3,3 V. Sem barramento de potência.**

1. `d` → o dump tem que responder. Se vier tudo `0x000` ou `0x7FF`, o SPI não
   está falando: confira o modo SPI, o CS e se o ENABLE está alto.
2. `e` → deve responder `enable OK`.
3. `d` de novo → confira:
   - `0x02` com `PWM_MODE` em 6x
   - `0x05` com dead time 200 ns e `VDS_LVL` 0,2 V
   - `0x06` com `CSA_GAIN` 20 V/V e `VREF_DIV` = 1
4. `x`, depois `e`, depois `d` → **a configuração tem que voltar sozinha.**

   ✅ *Este é o critério de aprovação do M2.* Ele prova que o rearme
   reconfigura, que é o que sustenta o pulse-and-glide mais tarde.

> Se o dump mostrar `0x000 / 0x3FF / 0x7FF / 0x15D / 0x283`, esses são os
> valores de **reset**: o `applyConfig()` não rodou.

### Etapa B — ruído de fundo (M5, parte 1)

**Ainda sem potência, driver desarmado.**

1. `i` → aguarde ~1 s (a calibração de offset).
2. `z` → as três médias devem dar ~0 A.

O **desvio-padrão** que sobrar é o seu piso de ruído. **Anote** — é uma das
métricas do trabalho comparativo, e a única que dá para medir sem energizar nada.

Repita com a build `lowside` e compare.

### Etapa C — as senoides (M5, parte 2)

**Requer o M4 (malha aberta) escrito. Roda livre, tensão baixa, fonte com
limite de corrente apertado.**

1. Motor girando devagar em malha aberta.
2. `s` → mande os quatro canais para o SerialPlot.
3. Confira: três senoides defasadas de 120°, e **`Ia+Ib+Ic` colado no zero**.

O canal `soma` é o teste que pega quase tudo: ganho errado numa fase, sinal
invertido, pino trocado.

### Etapa D — os sinais das fases (M5, parte 3)

1. Compile a env `inline_align`.
2. Roda livre. O alinhamento energiza as fases e revela o sinal de cada uma.
3. Traga os sinais para `CS_SIGN_A/B/C` no `current_backend.h`.
4. Volte para a env `inline` e repita a Etapa C.

---

## 10. Diagnóstico — sintoma para causa

| Sintoma | Suspeitos, na ordem |
|---|---|
| Serial mudo | solder bridge; objeto `Serial` errado; baud |
| Dump todo `0x000` ou `0x7FF` | modo SPI; CS; ENABLE baixo |
| Dump com os valores de reset | `applyConfig()` não rodou; ENABLE caiu |
| `current_sense.init()` falha (lowside) | PA2 e PB0 não caíram no mesmo ADC |
| Corrente errada por fator inteiro | resolução do ADC; VREF+; ganho |
| Corrente com offset constante | calibração rodou com corrente circulando |
| `Ia+Ib+Ic` não fecha em zero | sinal de uma fase invertido; pino trocado |
| nFAULT dispara ao armar | tempo morto insuficiente; OCP baixo; shoot-through |
| Motor puxa corrente e não gira | `zero_electric_angle`; pares de polos; ordem de fases |
| Iq oscila e ganho não resolve | `Tf` dentro da banda; tempo de ciclo menor que o suposto |
| Reset aleatório | watchdog; BOOT0 em PB8; brownout |

---

## 11. O que vem depois

Os módulos ainda não escritos, na ordem:

**M3 — halls.** Sem potência. Girar a roda com a mão e contar as transições:
devem ser `6 × pares de polos` = 528 por volta mecânica. Se não fechar, os 88
pares estão errados e nada depois vai funcionar.

**M4 — malha aberta.** Primeira energização, `voltage_limit = 2 V`, fonte com
limite de corrente apertado.

**M6 — alinhamento.** Descobrir `zero_electric_angle` e `sensor_direction`.
Rodar três vezes com reset entre elas; os valores têm que repetir.

**M7 — malha de corrente.** Medir o tempo de ciclo **primeiro**; os ganhos saem
dele: `Kp = L·ωc`, `Ki = R·ωc`, com `ωc ≤ 2π·(fs/10)` e `Kp/Ki = L/R = 678 µs`.

**M8 — aplicação.** Degraus de torque, botões, pulse-and-glide — com máquina de
estados, sem `delay()` no caminho de controle.

**M9 — segurança.** IWDG, `TIM1_BKIN` no PB10, leitura de nível do nFAULT após
armar, cortes por subtensão e temperatura.

**M10 — telemetria e o ensaio comparativo.**
