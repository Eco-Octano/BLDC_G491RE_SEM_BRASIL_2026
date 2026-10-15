#pragma once
/*
  board_pins.h - pinagem e constantes eletricas do shield ESC
  NUCLEO-G491RE + ESC custom Eco Octano UFPR

  REGRA: nenhum numero de pino aparece em outro arquivo. Se voce se pegar
  escrevendo PA8 fora daqui, pare e traga para ca. Numero de pino espalhado
  pelo codigo e' a causa mais comum de bug depois de uma revisao de placa.

  ------------------------------------------------------------------
  PRE-REQUISITO DE HARDWARE - a placa NAO funciona sem isto
  ------------------------------------------------------------------
  Solder bridges da NUCLEO-G491RE (UM2505):

    SB17, SB23  ABRIR    tira PA2/PA3 do VCP do ST-Link
    SB18, SB22  FECHAR   leva PA2/PA3 ao CN10-35 / CN10-37
    SB13, SB19  ABRIR    tira PC4/PC5 do CN10-35 / CN10-37
    SB16        ABRIR    libera PC13 do botao B1
    SB6         ABRIR    libera PA5 do LED LD2
    SB15        ABRIR    libera PB3 do TRACESWO
    SB29..SB32  CONFERIR PC14/PC15 estao no cristal LSE por padrao

  Com SB13/SB19 fechados, LSCSA (PA2) e HSCS (PC4) ficam EM CURTO no
  CN10-35: duas saidas de amplificador ligadas uma na outra.

  JP8 -> posicao [2-3]: VREF+ alimentado por VDD (3,3 V). No padrao [1-2]
  o VREF+ vem do TL1431 e vale 2,5 V ou 3,25 V - com 2,5 V a leitura de
  corrente satura em ~14 A, abaixo do MAX_TORQUE, e so' no sentido positivo.

  Option bytes: nSWBOOT0 = 0. PB8 e' BOOT0 e aqui e' usado como CAN RX.

  O VCP do ST-Link e' perdido. O SWD (PA13/PA14) continua funcionando.
  Debug serial passa a ser USART1 em PB6/PB7, com conversor USB-serial.
*/

#include <Arduino.h>

// ===================================================== ponte / TIM1 6PWM
// TIM1 no G4, AF6: CH1/CH1N, CH2/CH2N, CH3/CH3N.
// Fases: A = AZUL   B = VERDE   C = AMARELO   (manter esta convencao)
#define PIN_PWM_AH   PA8    // TIM1_CH1
#define PIN_PWM_AL   PA11   // TIM1_CH1N
#define PIN_PWM_BH   PA9    // TIM1_CH2
#define PIN_PWM_BL   PA12   // TIM1_CH2N
#define PIN_PWM_CH   PA10   // TIM1_CH3
#define PIN_PWM_CL   PB1    // TIM1_CH3N

// ============================================================ gate driver
#define PIN_DRV_ENABLE  PD2    // pull-down de 100k na placa
#define PIN_DRV_NFAULT  PB10   // TIM1_BKIN + EXTI10. Sinal de NIVEL, ativo baixo
#define PIN_DRV_NSCS    PC9    // pull-up de 5k-10k na placa
#define PIN_DRV_SCK     PC10   // SPI3
#define PIN_DRV_MISO    PC11   // SPI3
#define PIN_DRV_MOSI    PC12   // SPI3

// ================================================================= halls
// TIM3_CH1/2/3 -> permite usar a interface Hall por XOR do timer no futuro.
// Hoje usamos EXTI (linhas 6, 7, 8). Precisam de pull-up de 5k-10k:
// CONFERIR se estao montados na placa ou se e' preciso o interno.
#define PIN_HALL_A   PC6
#define PIN_HALL_B   PC7
#define PIN_HALL_C   PC8

// ============================================================== corrente
// Cadeia identica nas duas topologias:
//   shunt 3 mOhm  x  ganho 20 V/V  =  60 mV/A
//   repouso 1,65 V   ->   fundo de escala +-27,5 A
//   12 bits com VREF+ = 3,3 V   ->   ~13 mA por LSB

// INLINE - INA240A1DR externo, REF1 no 3V3 e REF2 no GND (bidirecional).
#define PIN_ILCS_A   PC0    // ADC1_IN6  (tambem ADC2_IN6)
#define PIN_ILCS_B   PA7    // ADC2_IN4
#define PIN_ILCS_C   PB13   // ADC3_IN5

// LOW-SIDE - CSA interno do DRV8353RS, VREF do driver em 3,3 V.
// PA2 e PB0 estao AMBOS no ADC1 -> a SimpleFOC consegue montar a sequencia
// injetada. PA6 so' existe no ADC2, entao a fase B fica de fora e e'
// reconstruida por Ia + Ib + Ic = 0.
#define PIN_LSCS_A   PA2    // ADC1_IN3
#define PIN_LSCS_B   PA6    // ADC2_IN3  - NAO USADO (ADC incompativel)
#define PIN_LSCS_C   PB0    // ADC1_IN15 (tambem ADC3_IN12)

// ==================================================== outros analogicos
#define PIN_VBAT        PC5    // ADC2_IN11 - tensao de barramento
#define PIN_MOTOR_TEMP  PA3    // ADC1_IN4  - filtro passa-baixas forte (10k + 1uF)
#define PIN_HSCS        PC4    // ADC2_IN5  - corrente de high-side
#define PIN_BEMF_A      PC2    // ADC1_IN8
#define PIN_BEMF_B      PC3    // ADC2_IN9
#define PIN_BEMF_C      PA1    // ADC1_IN2

// ========================================================= interface/IO
#define PIN_BTN_UP      PB14   // BOT1 - pull-down + capacitor na placa
#define PIN_BTN_DOWN    PB12   // BOT2 - idem
#define PIN_LED_STATUS  PA0    // GPIO_1 (console). PC13 e' o botao B1 da Nucleo
#define PIN_MOTOR_VEL   PC15   // entrada de velocidade (pull-up 5k-10k)

#define PIN_UART_TX     PB6    // USART1
#define PIN_UART_RX     PB7    // USART1

// ============================================================= potencia
static const float POWER_SUPPLY_V   = 42.0f;
static const int   PWM_FREQUENCY_HZ = 20000;

// Teto do SVPWM: Vbus / sqrt(3). Passar disso satura a modulacao e o
// integrador do PID acumula numa regiao inalcancavel (windup).
static const float SVPWM_CEILING_V  = POWER_SUPPLY_V / 1.7320508f;   // ~24,25 V

// ============================================================== corrente
static const float SHUNT_OHM   = 0.003f;   // identico nas duas topologias
static const float CS_GAIN_VpV = 20.0f;    // INA240A1 fixo | CSA do DRV por SPI

// Fundo de escala teorico. O INA240 nao chega aos trilhos, entao a faixa
// util real e' um pouco menor - nao conte com o ultimo ampere.
static const float CS_FULL_SCALE_A = 1.65f / (SHUNT_OHM * CS_GAIN_VpV);  // ~27,5 A

// Limite de operacao. Deixa ~30% de folga ate saturar, para o degrau de
// corrente nao ceifar a medida no transitorio.
static const float MAX_CURRENT_A = 19.0f;

// ================================================================ motor
static const int   MOTOR_POLE_PAIRS    = 88;         // CONFERIR no modulo 3
static const float MOTOR_PHASE_R_OHM   = 0.095f;
static const float MOTOR_PHASE_L_H     = 64.4e-6f;

// Constante de tempo eletrica: L/R. O PI de corrente deve ter Kp/Ki igual
// a isto para cancelar o polo do motor.
static const float MOTOR_TAU_E_S       = MOTOR_PHASE_L_H / MOTOR_PHASE_R_OHM;  // ~678 us
