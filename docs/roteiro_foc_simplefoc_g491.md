# Roteiro de construção: FOC com SimpleFOC na ESC custom (NUCLEO-G491RE + DRV8353RS)

Formato: dez módulos, construídos de baixo para cima. Cada módulo é testável
isoladamente e tem critério de aprovação. **Não avance sem passar no critério** —
em controle de motor, bug acumulado não se soma, se multiplica.

---

## Regras de trabalho

**Uma variável por vez.** Se você mudar biblioteca, pinagem e topologia de medida
no mesmo commit, e o motor não girar, você tem três suspeitos e nenhum método.

**Caderno de bancada.** Anote com data: estado de cada solder bridge, VREF+ medido,
offsets de cada canal, `zero_electric_angle`, ganhos do PID, tempo de ciclo. Metade
dos bugs de firmware embarcado de motor são "alguém mexeu num valor e não anotou".

**Um branch git por módulo.** Quando o módulo 7 quebrar, você precisa conseguir
voltar ao módulo 6 funcionando em trinta segundos.

**Bancada antes de veículo.** Roda livre, corrente baixa, fonte com limite de
corrente ajustado bem abaixo do que você acha que precisa. A fonte é seu primeiro
sistema de proteção.

---

## Módulo 1 — Esqueleto do projeto e arquitetura

**Objetivo:** estrutura que permita as duas versões de leitura de corrente
conviverem, já que você vai compará-las.

**Conceito a aprender:** separar *política* de *mecanismo*. O FOC não deve saber
como a corrente foi lida. Se ele souber, você vai duplicar o firmware inteiro para
comparar duas medidas — e aí não estará comparando medidas, estará comparando dois
firmwares.

Duas builds do mesmo código, selecionadas por flag:

```ini
[common]
platform = ststm32
board    = nucleo_g491re
framework = arduino
lib_archive = no
lib_deps = askuric/Simple FOC@^2.4.0

[env:inline]
extends = common
build_flags = -D CS_MODE_INLINE

[env:lowside]
extends = common
build_flags = -D CS_MODE_LOWSIDE
```

Estrutura sugerida:

```
src/
  main.cpp            aplicação: botões, degraus, pulse-and-glide
  board_pins.h        TODA a pinagem, um lugar só
  drv8353.h/.cpp      driver SPI do gate driver
  current_backend.h   escolhe a implementação por #ifdef
  safety.h/.cpp       falhas, watchdog, limites
  telemetry.h/.cpp    saída serial
```

**Armadilha:** seu projeto atual tem a `BLDCCustomMotor` duplicada em `src/BLDC/`
e em `lib/BLDCCustomMotor/`. Resolva isso agora, antes de crescer. Duas cópias de
uma classe é um bug esperando o dia em que você editar a errada.

**Critério:** as duas builds compilam com um `main.cpp` vazio e um blink.

---

## Módulo 2 — Driver do DRV8353RS por SPI

**Objetivo:** falar com o gate driver e confirmar que ele responde.

Este é o maior bloco de programação original do projeto, e é pré-requisito de
tudo. Nada de potência antes dele.

**Ler antes:** seção de interface SPI do datasheet do DRV8353, mapa de
registradores, e a tabela de valores de reset.

**Conceitos:**
- Quadro de 16 bits: bit de leitura/escrita, endereço, dados.
- Padrão *escreve e relê*: nunca confie numa escrita sem ler de volta. Em gate
  driver isso não é paranoia, é procedimento.
- Registradores são voláteis: **o SPI só responde com ENABLE alto, e a
  configuração se perde quando ele cai.**

```
classe DRV8353:
    begin(spi, cs_pin, en_pin):
        configura CS como saída, nível alto
        inicializa SPI       // conferir modo e clock no datasheet
        // não assume que o driver está acordado ainda

    enable():
        ENABLE = alto
        espera t_WAKE                    // do datasheet
        applyConfig()                    // OBRIGATÓRIO aqui, não só no setup
        retorna verificaConfig()

    disable():
        ENABLE = baixo
        marca configuração como perdida

    escreveReg(addr, valor):
        monta quadro de 16 bits
        CS baixo -> transfere -> CS alto
    leReg(addr) -> valor

    applyConfig():
        modo 6x PWM                      // PWM_MODE = 00b
        dead time                        // decidir: aqui OU no dead_zone. Nunca os dois
        IDRIVE / slew rate
        limiar de VDS / OCP
        ganho dos CSA                    // só relevante no modo low-side
        calibração de offset dos CSA     // bits CSA_CAL_x, só no modo low-side

    verificaConfig() -> bool:
        relê cada registrador escrito, compara

    leFalhas() -> struct:
        lê os dois registradores de status, decodifica em campos nomeados
```

**Armadilhas:**

- Chamar `applyConfig()` só no `setup()`. Seu `DisarmDriver()` baixa o ENABLE; ao
  rearmar, o driver volta em configuração de reset. Isso é especialmente perigoso
  no pulse-and-glide, onde arma e desarma o tempo todo.
- Não verificar a escrita. Um bit de modo errado transforma 6x em 3x e o motor
  simplesmente não gira, sem mensagem de erro.
- Somar tempo morto do DRV com o `dead_zone` da SimpleFOC. Escolha um, zere o outro.
- Deixar o CS flutuando. Configure como saída em nível alto **antes** de qualquer
  outra coisa.

**Ferramenta de teste:** um comando serial que despeja todos os registradores.
Você vai usar isso a cada módulo daqui pra frente.

**Referência:** existe um `drv8353rs.c` aberto no projeto `patosai/esc-chibios`.
É ChibiOS e usa 3x PWM, então não copie — mas serve para conferir formato de quadro
e ordem de escrita.

**Critério:** você lê os registradores de status, escreve o modo 6x, relê e bate.
Desliga e religa o ENABLE, e a releitura mostra que a configuração foi reaplicada.

---

## Módulo 3 — Sensores hall

**Objetivo:** ângulo confiável, sem tocar em potência.

**Conceitos:** hall dá 6 estados por período elétrico, ou seja, resolução de 60°
elétricos. A velocidade derivada disso é ruidosa em baixa rotação — é para isso que
existe o `SmoothingSensor` que você já usa.

```
sensor = HallSensor(PC6, PC7, PC8, POLE_PAIRS)
sensor.init()
sensor.enableInterrupts(doA, doB, doC)

loop:
    sensor.update()
    imprime sensor.getAngle(), sensor.getVelocity()
```

**Procedimento de verificação dos pares de polos** — faça, não confie no número:
marque a roda, gire exatamente uma volta mecânica devagar, conte as transições de
estado hall. Devem ser `6 × POLE_PAIRS`. Com 88, são 528. Se der outro número,
`POLE_PAIRS` está errado e **nada** depois disso vai funcionar.

**Armadilhas:**
- Pull-ups: sua planilha anota que os halls precisam de 5–10 kΩ. Confirme se estão
  na placa ou se você precisa habilitar os internos.
- Linhas EXTI: PC6/PC7/PC8 usam EXTI 6, 7 e 8. O nFAULT em PB10 usa EXTI 10. Sem
  conflito hoje — mas lembre que o STM32 compartilha a linha pelo *número* do pino.
- Ordem das fases dos halls trocada em relação às fases de potência: não dá para
  detectar aqui, só no módulo 6. Se lá der errado, volte a suspeitar daqui.

**Critério:** ângulo cresce monotonicamente girando num sentido, decresce no outro,
e a contagem por volta mecânica fecha.

---

## Módulo 4 — Driver e malha aberta

**Objetivo:** primeira energização. Motor gira sem realimentação de corrente.

```
driver.pwm_frequency = 20000
driver.voltage_power_supply = 42
driver.voltage_limit = 2         // COMEÇA BAIXO
driver.dead_zone = ...           // zero se o DRV faz o tempo morto
driver.init()

motor.controller = MotionControlType::velocity_openloop
motor.voltage_limit = 2
loop: motor.move(5)              // rad/s baixo
```

**Armadilhas:**
- Fonte com limite de corrente apertado. Se houver shoot-through, você quer que a
  fonte segure, não que o MOSFET decida.
- `voltage_limit` acima de `42/√3 ≈ 24,2 V` satura o SVPWM. Não é o caso agora
  (você está em 2 V), mas anote para o módulo 7.
- Alicate amperímetro na fase: confirme que a corrente é senoidal e simétrica.
- Termografia ou o dedo (com cuidado): MOSFET aquecendo assimetricamente indica
  problema de acionamento numa fase.

**Critério:** motor gira suave nos dois sentidos, corrente equilibrada nas três
fases, nada esquenta anormalmente.

---

## Módulo 5 — Camada de corrente, com duas implementações

**Objetivo:** medir corrente corretamente, pelas duas topologias, atrás de uma
interface só.

**Conceito central:** offset e ganho são coisas separadas. O *offset* é o que o
ADC lê com corrente zero — a biblioteca mede isso sozinha na inicialização, desde
que nada esteja acionado. O *ganho* converte volts em ampères, e vem do seu
hardware. Errar offset dá corrente deslocada; errar ganho dá corrente escalada.
Os sintomas são diferentes e você precisa saber distinguir.

```
// current_backend.h

#ifdef CS_MODE_LOWSIDE
    // A e C no ADC1, B reconstruída. Injetada, síncrona ao TIM1.
    LowsideCurrentSense cs(SHUNT_LS, GAIN_LS, PA2, _NC, PB0);

#else  // CS_MODE_INLINE
    PhaseCurrent_s leInline():
        // _readRegularADCVoltage() em vez de analogRead()
        // conversão feita por você -> aqui você resolve o VREF+ real
        c.a = (le(PC0)  - v_zero) / (SHUNT_IL * GAIN_IL)
        c.b = (le(PA7)  - v_zero) / (SHUNT_IL * GAIN_IL)
        c.c = (le(PB13) - v_zero) / (SHUNT_IL * GAIN_IL)
    GenericCurrentSense cs(leInline);
#endif
```

**A armadilha grande da versão inline:** o `initFOC()` **não** roda o alinhamento
para a `GenericCurrentSense`. Você determina os sinais na mão e corrige com
`cs.gain_a *= -1`.

Como obter esses sinais sem tentativa e erro: rode **uma vez** a
`InlineCurrentSense` convencional nos mesmos pinos, com o alinhamento ativo, e
anote o que ela descobriu. Depois cole esses sinais na versão Generic. Você usa a
classe simples como ferramenta de calibração da classe boa.

**Ordem de teste, em três etapas:**
1. Driver desarmado. As correntes têm que ler ~0 A. O desvio que sobrar é seu piso
   de ruído — anote, é uma métrica do trabalho comparativo.
2. Malha aberta do módulo 4, tensão baixa. Três senoides a 120°.
3. Confira `Ia+Ib+Ic ≈ 0` (só faz sentido na versão inline; na low-side de duas
   fases a soma é zero por construção).

**Armadilhas:**
- VREF+ da Nucleo não é 3,3 V por padrão. Meça. Na versão inline você compensa no
  seu código; na low-side, avalie mover o JP8.
- Resolução do ADC: `analogReadResolution(12)` e conferir o `_ADC_RESOLUTION` da
  biblioteca. Erro por fator inteiro parece defeito de hardware.
- Saída do INA240: se ele estiver alimentado em 5 V, confirme que a saída não
  excede o que o pino do G491 tolera.
- Na low-side, o ganho vem dos CSAs do DRV — ou seja, do módulo 2. Se o SPI não
  estiver certo, o ganho é chute.

**Critério:** as duas builds leem corrente coerente com um alicate amperímetro,
dentro de uns poucos por cento, no mesmo ponto de operação.

---

## Módulo 6 — Alinhamento

**Objetivo:** descobrir `zero_electric_angle` e `sensor_direction` reais.

**Conceito:** o FOC precisa saber onde está o zero elétrico do rotor em relação ao
zero dos halls. O `initFOC()` descobre isso energizando as fases e observando para
onde o rotor puxa. É o número mais crítico do firmware inteiro.

```
#define SKIP_STARTUP_ALIGN 0     // roda livre, tensão de alinhamento baixa
motor.voltage_sensor_align = 1.0
motor.initFOC()
imprime motor.zero_electric_angle com 4 casas
imprime motor.sensor_direction
```

Rode **três vezes** com reset entre elas. Os valores têm que repetir. Se variarem
muito, algo está solto: chicote de hall, ordem de fases, ou pares de polos errados.

**Armadilha:** esse número é válido para esta combinação de motor, chicote e ordem
de fases. Trocar qualquer um dos três invalida. Anote no caderno com data e com o
que estava conectado.

**Sintoma de alinhamento errado:** o motor puxa corrente e não gira, ou gira ao
contrário, ou gira com torque muito abaixo do esperado. Se você vir isso mais
adiante, volte aqui antes de mexer em PID.

**Critério:** três medições consistentes, anotadas.

---

## Módulo 7 — Fechar a malha de corrente

**Objetivo:** `foc_current` funcionando com resposta limpa.

**Antes de tudo, meça o tempo de ciclo.** Toggle de GPIO em volta do `loopFOC()`,
osciloscópio. Esse número define o teto da sua banda.

**Projeto do PI**, na ordem certa:

```
fs   = 1 / tempo_de_ciclo_medido
ωc  ≤ 2π × (fs / 10)              // regra prática: banda ≤ 1/10 da amostragem
Kp   = L × ωc
Ki   = R × ωc
Tf  ≪ 1/ωc                        // filtro FORA da banda, não dentro
```

Com L = 64,4 µH e R = 0,095 Ω. Note que `Kp/Ki = L/R = 678 µs` — se essa razão não
bater, você não está cancelando o polo elétrico do motor.

**Armadilhas:**
- `CURRENT_TF = 500 µs` põe um polo em 318 Hz. Se sua banda é 1,5 kHz, o filtro
  está *dentro* dela e come toda a margem de fase. É a causa nº 1 de oscilação que
  não se resolve mexendo em ganho.
- `PID.limit` e `motor.voltage_limit` acima de 24,2 V com barramento de 42 V: o
  integrador acumula numa região inalcançável e você ganha windup.
- Testar degrau com o motor travado é diferente de testar girando. Faça os dois.

**Ferramenta:** use o `Commander` para ajustar ganhos em tempo real sem
recompilar, e o SerialPlot para ver o degrau. Não tente sintonizar por
recompilação — você vai desistir.

**Critério:** degrau de corrente com sobressinal aceitável e sem oscilação
sustentada, nos dois modos de leitura.

---

## Módulo 8 — Camada de aplicação

**Objetivo:** trazer de volta os degraus de torque, os botões e o pulse-and-glide,
agora sem os defeitos do código antigo.

**Conceito a aprender: máquina de estados em vez de `delay()`.** Todo `delay()` no
caminho de controle é uma janela em que o FOC está cego. Seu `ArmDriver()` bloqueia
2 ms — inaceitável em pulse-and-glide, que arma e desarma constantemente.

```
estados: DESARMADO, ACORDANDO, ARMADO, FALHA

passo():
    caso DESARMADO:
        se pedido_de_arme:
            ENABLE = alto; t0 = agora; estado = ACORDANDO
    caso ACORDANDO:
        se agora - t0 >= T_WAKE:
            drv.applyConfig()            // registradores foram perdidos
            se nFAULT está BAIXO: estado = FALHA   // ler NÍVEL, não borda
            senão: motor.enable(); estado = ARMADO
    caso ARMADO:
        se flag_de_falha: estado = FALHA
    caso FALHA:
        motor.disable(); ENABLE = baixo
        alvo = 0
        sai só com liberação explícita
```

**Pulse-and-glide de verdade:** no código antigo, o planeio chama `motor.move(0)` —
as chaves continuam comutando regulando Iq para zero. Para um evento de eficiência,
planeio é **desarmar**. Mas atenção ao acoplamento com o módulo 2: cada rearme
precisa reconfigurar o DRV.

Meça se o desarme compensa. Você troca perda de comutação por um transiente de
rearme; a resposta depende da frequência de ciclo do pulse-and-glide. Isso é um
experimento, não uma dedução.

**Armadilhas:**
- `TORQUE_SLEW` está em 1000 A/s no código atual (valor de calibração de PID).
  Volte para a rampa de operação.
- Botões: borda de subida com debounce, sem repetição ao segurar. A lógica atual
  está correta — preserve.
- `driver_armed` é lido e escrito na ISR de falha: tem que ser `volatile`.

**Critério:** degraus suaves, sem solavanco; pulse-and-glide entra e sai sem falha
de driver.

---

## Módulo 9 — Segurança

**Objetivo:** o firmware falha de forma segura, não de forma criativa.

```
- IWDG habilitado, alimentado só no laço principal
- nFAULT: interrupção por borda PARA REAGIR + leitura de NÍVEL após armar
          (uma falha durante o wake-up não gera borda nova)
- TIM1_BKIN em PB10: desliga a ponte por hardware, sem passar por software
- corte por subtensão: VBAT em PC5
- corte por temperatura: PA3
- limite de corrente coerente com o fundo de escala do amplificador
- option bytes: nSWBOOT0 = 0  (PB8 é BOOT0 e você usa como CAN RX)
```

**Conceito:** a interrupção do nFAULT reage rápido, mas nFAULT é um sinal de
**nível**. Se a falha ocorrer durante os milissegundos de wake-up, a borda já
passou e nunca virá outra. Por isso a leitura de nível após armar não é redundância.

O `TIM1_BKIN` é a proteção mais rápida que existe, porque não depende de software
nenhum. A SimpleFOC não configura isso — é código LL seu, depois do `driver.init()`.
Vale a pena: é o que separa "o firmware travou e a placa queimou" de "o firmware
travou e a ponte desligou".

**Critério:** você provoca cada falha deliberadamente e o sistema responde como
projetado. Falha que nunca foi testada não é proteção, é esperança.

---

## Módulo 10 — Telemetria e o experimento comparativo

**Objetivo:** dados para escolher entre as duas topologias e para apresentar.

```
- malha low-side injetada fecha o controle
- inline lida por _readRegularADCVoltage() a cada N ciclos, como referência
- decimar a impressão: Serial.print BLOQUEIA
- melhor ainda: buffer em RAM + despejo depois do ensaio
```

**O experimento que resolve a discussão sobre sincronismo:** varie
deliberadamente o atraso entre o evento do TIM1 e a leitura inline, e levante a
forma do ripple dentro do período de PWM. Você mede onde o triângulo cruza a média
e quantifica, em ampères, o erro da amostragem assíncrona.

**Métricas do ensaio comparativo:**
- desvio-padrão de Iq no mesmo ponto de operação
- erro médio contra medida externa
- tempo de ciclo
- **potência no barramento para o mesmo torque e velocidade na roda** — é a métrica
  que decide a prova

**Varra o índice de modulação.** As duas topologias falham em pontas opostas: o
erro da inline assíncrona é máximo perto de 50% de modulação; a janela cega da
low-side aparece perto de 100%. Ensaio num ponto só favorece arbitrariamente uma.

**Armadilha de interpretação:** não compare amostra por amostra. A low-side
injetada amostra a média; a inline assíncrona amostra um ponto do triângulo. A
diferença instantânea entre elas é o ripple, não erro. Compare médias ou os Iq/Id
filtrados.

---

## Apêndice A — Sintoma para causa

| Sintoma | Suspeitos, na ordem |
|---|---|
| Motor puxa corrente e não gira | `zero_electric_angle`; `POLE_PAIRS`; ordem de fases |
| Gira ao contrário | `sensor_direction`; duas fases trocadas |
| Corrente errada por fator inteiro | resolução do ADC; VREF+; ganho |
| Corrente com offset constante | calibração de offset rodou com corrente circulando |
| Iq oscila e ganho não resolve | `Tf` dentro da banda; tempo de ciclo menor que o suposto |
| nFAULT dispara ao armar | OCP baixo demais; tempo morto insuficiente; shoot-through |
| `initFOC()` falha | chicote de hall; `POLE_PAIRS`; alinhamento com roda travada |
| Serial mudo | solder bridge; objeto `Serial` errado; baud |
| Reset aleatório | watchdog não alimentado; BOOT0 em PB8; brownout |
| Funciona na bancada, falha no carro | ruído no chicote; queda de tensão; vibração no conector |
| Torque cai em alta rotação | saturação do SVPWM; limite de tensão; enfraquecimento necessário |

## Apêndice B — Conceitos, na ordem de estudo

1. Transformadas de Clarke e Park — por que o FOC transforma coordenadas
2. Modulação SVPWM e por que ela dá ~15% a mais que a senoidal
3. Projeto de malha de corrente: cancelamento de polo, banda contra amostragem
4. Tempo morto: por que existe e o que ele custa em distorção e perda
5. Comutação por hall e as limitações de 60° de resolução
6. Ripple de corrente e por que o instante de amostragem importa

Os quatro primeiros estão razoavelmente cobertos no *Theory corner* da
documentação da SimpleFOC, que foi ampliado na v2.4.0.

---

## Como me usar em cada módulo

Traga sempre três coisas: **o que você esperava, o que aconteceu, e o número
medido**. "Não funciona" é o pior relatório possível. "Esperava 5 A, li 1,2 A, o
alicate marca 4,8 A" me leva direto ao ganho.

E quando um módulo passar, me diga qual valor você anotou. Vários dos parâmetros
dos módulos seguintes dependem dos números dos anteriores — o tempo de ciclo do
módulo 7 vem da medida do módulo 5, os ganhos vêm do tempo de ciclo, e o limite de
corrente vem do fundo de escala do módulo 5.
