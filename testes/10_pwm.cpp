/* =====================================================================
   PWM 6x SEM MOTOR  -  TIM1 pela SimpleFOC, conferido no osciloscópio
   NUCLEO-G491RE + Artemis + Ouroboros          Eco Octano UFPR

   Como usar: copie este arquivo para src/main.cpp, grave e abra o
   monitor na porta da FTDI (115200).

   SEGURANÇA: o DRV8353RS fica DORMINDO (ENABLE em 0) o tempo todo.
   Dormindo, ele ignora INHx/INLx e mantém os gates desligados: o PWM
   chega aos pinos do driver, mas nenhum MOSFET chaveia. Deixe também a
   fonte de 48 V (VM) desligada. NUNCA use as razões cíclicas deste
   teste com o driver acordado: são altas e sem malha nenhuma.

   Onde medir: nos pinos INHA/INLA (PA8/PA11), INHB/INLB (PA9/PA12) e
   INHC/INLC (PA10/PB1), com o terra da ponteira no GND da Nucleo.

   Sequência sugerida:
     1. Antes de apertar 'i': todos os seis pinos em 0 (ou em alta
        impedância puxados para 0 pelo DRV).
     2. 'i' inicia o PWM com fase A em 25 %, B em 50 % e C em 75 %:
        cada fase fica reconhecível pela largura do pulso.
     3. Confira 20 kHz, alto e baixo complementares, tempo morto
        visível entre eles e nunca os dois em 1 ao mesmo tempo.
     4. '0' zera as três fases; 'x' desliga o PWM (pinos em 0).
   ===================================================================== */

#include <Arduino.h>
#include <SimpleFOC.h>
#include <DRV8353.h>
#include "board_pins.h"

HardwareSerial Dbg(CONSOLE_RX, CONSOLE_TX);

DRV8353 drv({ DRV_PINO_SCLK, DRV_PINO_NSCS, DRV_PINO_SDI,
              DRV_PINO_SDO,  DRV_PINO_ENABLE, DRV_PINO_NFAULT });

BLDCDriver6PWM pwm(PINO_INHA, PINO_INLA,
                   PINO_INHB, PINO_INLB,
                   PINO_INHC, PINO_INLC);

const float TENSAO_BARRAMENTO = 48.0f;
const long  FREQ_PWM          = 20000;
// Fração do período em que alto e baixo ficam os dois desligados.
const float ZONA_MORTA        = 0.02f;

bool pwm_iniciado = false;

static void ajuda()
{
    Dbg.println(F("\n--------------- comandos ---------------"));
    Dbg.println(F("  i  inicia o PWM: A 25%, B 50%, C 75%"));
    Dbg.println(F("  0  razao ciclica zero nas tres fases"));
    Dbg.println(F("  r  volta para A 25%, B 50%, C 75%"));
    Dbg.println(F("  x  desliga o PWM"));
    Dbg.println(F("  ?  esta ajuda"));
    Dbg.println(F("----------------------------------------"));
}

// A SimpleFOC recebe tensão; razão cíclica = tensão / barramento.
static void aplicarRazoes(float a, float b, float c)
{
    pwm.setPwm(a * TENSAO_BARRAMENTO, b * TENSAO_BARRAMENTO, c * TENSAO_BARRAMENTO);
    Dbg.print(F("\n  razao ciclica A=")); Dbg.print(a * 100, 0);
    Dbg.print(F("%  B="));                Dbg.print(b * 100, 0);
    Dbg.print(F("%  C="));                Dbg.print(c * 100, 0);
    Dbg.println(F("%"));
}

static void iniciarPwm()
{
    if (pwm_iniciado) { Dbg.println(F("\n  PWM ja iniciado.")); return; }
    if (drv.acordado()) {                  // não deveria acontecer neste teste
        Dbg.println(F("\n  DRV acordado: abortado por seguranca."));
        return;
    }

    pwm.pwm_frequency        = FREQ_PWM;
    pwm.voltage_power_supply = TENSAO_BARRAMENTO;
    pwm.voltage_limit        = TENSAO_BARRAMENTO;
    pwm.dead_zone            = ZONA_MORTA;

    if (!pwm.init()) {
        Dbg.println(F("\n  pwm.init() FALHOU: veja as mensagens da SimpleFOC acima."));
        return;
    }
    pwm.enable();
    pwm_iniciado = true;
    Dbg.println(F("\n  PWM iniciado (20 kHz)."));
    aplicarRazoes(0.25f, 0.50f, 0.75f);
}

void setup()
{
    Dbg.begin(CONSOLE_BAUD);
    delay(50);
    Dbg.println(F("\n\n#############################################"));
    Dbg.println(F("#  ESC Eco Octano - teste de PWM sem motor"));
    Dbg.print  (F("#  compilado ")); Dbg.print(F(__DATE__));
    Dbg.print  (' ');                Dbg.println(F(__TIME__));
    Dbg.println(F("#############################################"));

    drv.iniciar();                         // ENABLE em 0 durante todo o teste
    SimpleFOCDebug::enable(&Dbg);          // a SimpleFOC diz qual timer usou

    Dbg.println(F("\n  DRV dormindo. Meca os seis pinos agora (devem estar em 0)."));
    ajuda();
}

void loop()
{
    if (!Dbg.available()) return;
    switch (Dbg.read()) {
        case 'i': iniciarPwm(); break;
        case '0': if (pwm_iniciado) aplicarRazoes(0, 0, 0); break;
        case 'r': if (pwm_iniciado) aplicarRazoes(0.25f, 0.50f, 0.75f); break;
        case 'x':
            if (pwm_iniciado) {
                pwm.disable();
                pwm_iniciado = false;
                Dbg.println(F("\n  PWM desligado."));
            }
            break;
        case '?': ajuda(); break;
        default:  break;
    }
}
