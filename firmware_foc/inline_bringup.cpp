/*
  BRING-UP DA LEITURA DE CORRENTE INLINE
  NUCLEO-G491RE + shield ESC (DRV8353RS) - Eco Octano UFPR
  SimpleFOC 2.3.4

  Objetivo unico: provar que as tres correntes inline sao lidas com sinal,
  ganho e ordem de fases corretos. A malha de corrente NAO e' fechada aqui -
  o controle de torque continua em MODO TENSAO, e as correntes sao apenas
  medidas e impressas. So' depois de validar isso e' que vale ligar o
  foc_current.

  DUAS ETAPAS, selecionadas por BRINGUP_STAGE:

    1 = driver DESARMADO. Nada e' energizado. Serve para ver offset e ruido
        de fundo: as tres correntes tem que ficar em ~0 A e o desvio que
        voce ve e' o piso de ruido do seu sistema de medida.

    2 = driver armado, tensao BAIXA em malha aberta de torque. O motor gira
        devagar. As tres correntes tem que sair como senoides defasadas de
        120 graus, e a soma Ia+Ib+Ic tem que ficar em ~0 o tempo todo.
        A soma e' o teste que pega quase tudo: ganho errado numa fase,
        sinal invertido, pino trocado.

  ANTES DE LIGAR:
    - Abra os solder bridges do VCP do ST-Link (PA2/PA3). PA2 e' a low-side A
      e PA3 e' a temperatura do motor; com as pontes fechadas o ST-Link
      briga com os amplificadores. O debug aqui sai por USART1 (PB6/PB7).
    - Preencha SHUNT_INLINE e GAIN_INLINE com os valores do SEU esquematico.
      Os que estao aqui sao placeholders.
    - Motor com a roda LIVRE.

  platformio.ini:
    [env:nucleo_g491re]
    platform  = ststm32
    board     = nucleo_g491re
    framework = arduino
    lib_archive = no
    lib_deps  = askuric/Simple FOC@^2.3.4
*/

#include <SimpleFOC.h>

#define BRINGUP_STAGE 1

// ------------------------------------------------------------------ pinos
// 6PWM em TIM1: CH1/CH1N, CH2/CH2N, CH3/CH3N (AF6 no STM32G4)
const int PWM_AH_PIN = PA8;
const int PWM_AL_PIN = PA11;
const int PWM_BH_PIN = PA9;
const int PWM_BL_PIN = PA12;
const int PWM_CH_PIN = PA10;
const int PWM_CL_PIN = PB1;

const int HALL_A_PIN = PC6;   // TIM3_CH1
const int HALL_B_PIN = PC7;   // TIM3_CH2
const int HALL_C_PIN = PC8;   // TIM3_CH3

// Correntes INLINE. Espalhadas entre ADC1/ADC2/ADC3 de proposito - a classe
// InlineCurrentSense usa analogRead por pino, entao isso nao e' problema.
const int ILCS_A_PIN = PC0;   // ADC1_IN6
const int ILCS_B_PIN = PA7;   // ADC2_IN4
const int ILCS_C_PIN = PB13;  // ADC3_IN5

const int DRV_ENB_PIN  = PD2;
const int NFAULT_PIN   = PB10;
const int DRV_NSCS_PIN = PC9;   // mantido em HIGH: SPI do DRV ainda nao usado
const int LED_PIN      = PC14;  // GPIO_6 livre (PC13 e' o botao B1 da Nucleo)

// ---------------------------------------------------- parametros da medida
// PREENCHER com os valores reais do esquematico da placa.
const float SHUNT_INLINE = 0.001f;   // ohm
const float GAIN_INLINE  = 20.0f;    // V/V

// ------------------------------------------------------------- potencia
const float POWER_SUPPLY  = 42.0f;
const int   PWM_FREQ      = 20000;

// Tensao de bring-up: comece BAIXO. Suba de a pouco so' depois que as
// leituras fizerem sentido.
const float BRINGUP_VOLTAGE = 1.5f;   // V - alvo do modo tensao
const float VOLTAGE_LIMIT   = 3.0f;   // V - teto duro nesta etapa

const int POLE_PAIRS = 88;

// ------------------------------------------------------------- objetos
HardwareSerial SerialDbg(PB7, PB6);   // USART1: RX=PB7, TX=PB6

BLDCMotor      motor  = BLDCMotor(POLE_PAIRS);
BLDCDriver6PWM driver = BLDCDriver6PWM(PWM_AH_PIN, PWM_AL_PIN,
                                       PWM_BH_PIN, PWM_BL_PIN,
                                       PWM_CH_PIN, PWM_CL_PIN);

HallSensor sensor = HallSensor(HALL_A_PIN, HALL_B_PIN, HALL_C_PIN, POLE_PAIRS);
void doA() { sensor.handleA(); }
void doB() { sensor.handleB(); }
void doC() { sensor.handleC(); }

InlineCurrentSense current_sense =
    InlineCurrentSense(SHUNT_INLINE, GAIN_INLINE, ILCS_A_PIN, ILCS_B_PIN, ILCS_C_PIN);

volatile bool fault_flag = false;
void OnDriverFault() { fault_flag = true; }

uint32_t last_print = 0;

// ================================================================== SETUP
void setup()
{
  SerialDbg.begin(115200);
  delay(200);
  SimpleFOCDebug::enable(&SerialDbg);
  SerialDbg.println(F("=== bring-up leitura inline ==="));

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // SPI do DRV nao usado nesta etapa: chip select desativado explicitamente
  // para o driver nao interpretar ruido como transacao.
  pinMode(DRV_NSCS_PIN, OUTPUT);
  digitalWrite(DRV_NSCS_PIN, HIGH);

  pinMode(NFAULT_PIN, INPUT_PULLUP);

  // O ADC do stm32duino nao necessariamente inicia em 12 bits. A SimpleFOC
  // converte a contagem crua assumindo a resolucao definida em
  // _ADC_RESOLUTION (stm32_mcu.cpp). Se as duas nao baterem, a corrente sai
  // errada por um fator inteiro - CONFIRA esse #define na sua copia.
  analogReadResolution(12);

  // ENABLE do DRV8353RS. Sobe antes de tudo: sem ele o driver dorme e as
  // fases nao energizam. O pino tem pull-down de 100k na placa.
  pinMode(DRV_ENB_PIN, OUTPUT);
#if BRINGUP_STAGE == 1
  digitalWrite(DRV_ENB_PIN, LOW);    // etapa 1: nada energizado
#else
  digitalWrite(DRV_ENB_PIN, HIGH);
  delay(10);
#endif

  sensor.init();
  sensor.enableInterrupts(doA, doB, doC);
  motor.linkSensor(&sensor);

  driver.pwm_frequency       = PWM_FREQ;
  driver.voltage_power_supply = POWER_SUPPLY;
  driver.voltage_limit       = VOLTAGE_LIMIT;
  driver.dead_zone           = 0.025f;
  if (!driver.init()) {
    SerialDbg.println(F("ERRO: driver.init()"));
    while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(200); }
  }
  motor.linkDriver(&driver);

  current_sense.linkDriver(&driver);
  if (!current_sense.init()) {
    SerialDbg.println(F("ERRO: current_sense.init()"));
    while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(500); }
  }

  // Malha de corrente NAO fechada: as correntes sao so' medidas.
  motor.foc_modulation    = FOCModulationType::SpaceVectorPWM;
  motor.torque_controller = TorqueControlType::voltage;
  motor.controller        = MotionControlType::torque;
  motor.voltage_limit     = VOLTAGE_LIMIT;

#if BRINGUP_STAGE == 1
  // Sem energizar nada, alinhar nao faz sentido nem e' possivel.
  current_sense.skip_align = true;
  motor.voltage_sensor_align = 0;
#else
  // Deixe o alinhamento RODAR: e' ele que descobre sinal e ordem dos ganhos
  // das tres medidas, e reclama se um pino estiver trocado.
  current_sense.skip_align = false;
  motor.voltage_sensor_align = 1.0f;
#endif

  motor.useMonitoring(SerialDbg);

  if (!motor.init()) {
    SerialDbg.println(F("ERRO: motor.init()"));
    while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(800); }
  }
  motor.linkCurrentSense(&current_sense);

#if BRINGUP_STAGE == 2
  attachInterrupt(digitalPinToInterrupt(NFAULT_PIN), OnDriverFault, FALLING);
  if (!motor.initFOC()) {
    SerialDbg.println(F("ERRO: motor.initFOC()"));
    while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(1200); }
  }
  motor.target = BRINGUP_VOLTAGE;
  SerialDbg.println(F("etapa 2: girando em modo tensao"));
#else
  motor.disable();
  SerialDbg.println(F("etapa 1: driver desarmado, medindo offset/ruido"));
#endif

  SerialDbg.println(F("Ia,Ib,Ic,soma"));
  digitalWrite(LED_PIN, HIGH);
}

// =================================================================== LOOP
void loop()
{
  if (fault_flag) {
    fault_flag = false;
    digitalWrite(DRV_ENB_PIN, LOW);
    motor.disable();
    SerialDbg.println(F("FALHA: nFAULT do driver - desarmado"));
  }

#if BRINGUP_STAGE == 2
  motor.loopFOC();
  motor.move();
#endif

  // Impressao lenta de proposito: aqui interessa ler o numero, nao fechar
  // banda. Serial.print bloqueia, entao nao deixe isso no firmware final.
  if (millis() - last_print >= 50) {
    last_print = millis();

    PhaseCurrent_s c = current_sense.getPhaseCurrents();
    float soma = c.a + c.b + c.c;

    SerialDbg.print(c.a, 3);  SerialDbg.print(',');
    SerialDbg.print(c.b, 3);  SerialDbg.print(',');
    SerialDbg.print(c.c, 3);  SerialDbg.print(',');
    SerialDbg.println(soma, 3);
  }
}
