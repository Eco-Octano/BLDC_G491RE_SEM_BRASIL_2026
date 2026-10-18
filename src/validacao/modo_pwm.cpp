// Modo 4: PWM 6x no TIM1 pela SimpleFOC, para o osciloscópio (antes
// testes/10). O DRV fica DORMINDO o tempo todo: ignora INHx/INLx e mantém
// os gates desligados. Deixe também os 48 V desligados. As razões
// cíclicas daqui nunca podem ir para um driver acordado.

#include <SimpleFOC.h>
#include "validacao.h"

static BLDCDriver6PWM pwm(PINO_INHA, PINO_INLA,
                          PINO_INHB, PINO_INLB,
                          PINO_INHC, PINO_INLC);

static const float TENSAO_BARRAMENTO = 48.0f;
static const long  FREQ_PWM          = 20000;
// Fração do período em que alto e baixo ficam os dois desligados.
static const float ZONA_MORTA        = 0.02f;

static bool iniciado = false;

static void ajuda()
{
    Dbg.println(F("\n--------------- comandos ---------------"));
    Dbg.println(F("  i  inicia o PWM: A 25%, B 50%, C 75%"));
    Dbg.println(F("  0  razao ciclica zero nas tres fases"));
    Dbg.println(F("  r  volta para A 25%, B 50%, C 75%"));
    Dbg.println(F("  x  desliga o PWM"));
    Dbg.println(F("  ?  esta ajuda"));
    Dbg.println(F("  Meca INHA/INLA (PA8/PA11), INHB/INLB (PA9/PA12),"));
    Dbg.println(F("  INHC/INLC (PA10/PB1). Alto e baixo NUNCA em 1 juntos."));
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
    if (iniciado) { Dbg.println(F("\n  PWM ja iniciado.")); return; }
    if (drv.acordado()) {
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
    iniciado = true;
    Dbg.println(F("\n  PWM iniciado (20 kHz)."));
    aplicarRazoes(0.25f, 0.50f, 0.75f);
}

void pwmIniciar()
{
    drv.dormir();                          // garantia: o menu já o deixa dormindo
    SimpleFOCDebug::enable(&Dbg);          // a SimpleFOC diz qual timer usou
    Dbg.println(F("\n  DRV dormindo. Meca os seis pinos agora (devem estar em 0)."));
    ajuda();
}

void pwmLaco()
{
    if (!Dbg.available()) return;
    switch (Dbg.read()) {
        case 'i': iniciarPwm(); break;
        case '0': if (iniciado) aplicarRazoes(0, 0, 0); break;
        case 'r': if (iniciado) aplicarRazoes(0.25f, 0.50f, 0.75f); break;
        case 'x':
            if (iniciado) {
                pwm.disable();
                iniciado = false;
                Dbg.println(F("\n  PWM desligado."));
            }
            break;
        case '?': ajuda(); break;
        default:  break;
    }
}
