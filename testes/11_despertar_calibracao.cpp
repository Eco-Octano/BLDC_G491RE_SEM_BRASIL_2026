/* =====================================================================
   DESPERTAR COM CALIBRAÇÃO  -  rotina de aplicação do usuário
   NUCLEO-G491RE + Artemis + Ouroboros          Eco Octano UFPR

   Era o src/main.cpp de 2026-10-17 (calibração validada: offsets em
   torno de 1,65 V). Guardado aqui porque o main.cpp passou a receber os
   testes de bancada. Para usar, copie de volta para src/main.cpp.

   despertarDriver(): configura, acorda, calibra os amplificadores
   internos e externos com a ponte em COAST, sai do COAST e trava os
   registradores. Chame em todo despertar, inclusive na volta do planar
   pelo ENABLE.
   ===================================================================== */

#include <Arduino.h>
#include <DRV8353.h>
#include "board_pins.h"

// Sem MOSFETs soldados o driver declara GDF corretamente e sem parar.
// Ponha em 0 depois de soldar, senão você mascara uma falha de verdade.
#define PLACA_SEM_ESTAGIO_DE_POTENCIA   1

HardwareSerial Dbg(CONSOLE_RX, CONSOLE_TX);

// Inicialização do objeto do driver
DRV8353 drv({
    DRV_PINO_SCLK,
    DRV_PINO_NSCS,
    DRV_PINO_SDI,
    DRV_PINO_SDO,
    DRV_PINO_ENABLE,
    DRV_PINO_NFAULT
});

// Registradores em modo de uso: só os 11 bits de dado, sem o endereço.
// Valores do firmware_foc/drv8353_bringup.cpp, ainda não revalidados.
const uint16_t REG_DRIVER_CONTROL = 0x480;   // OCP desliga os 3 bracos, OTW no nFAULT, PWM 6x
const uint16_t REG_GATE_DRIVE_HS  = 0x344;   // IDRIVEP 300 mA, IDRIVEN 600 mA
const uint16_t REG_GATE_DRIVE_LS  = 0x444;   // CBC, TDRIVE 500 ns, 300 / 600 mA
const uint16_t REG_OCP_CONTROL    = 0x265;   // morto 200 ns, retry, 4 us, VDS 0,2 V
const uint16_t REG_CSA_CONTROL    = 0x280;   // VREF/2, 20 V/V, SEN 0,25 V
const uint16_t REG_DRIVER_CONFIG  = 0x000;   // calibracao manual

// Os dois grupos de amplificadores repousam em VREF/2 = 1,65 V com corrente
// nula. A janela é um ponto de partida: ajuste depois de medir na bancada.
const float    VREF_ADC        = 3.3f;
const float    OFFSET_NOMINAL  = 1.65f;
const float    JANELA_OFFSET   = 0.20f;
const uint16_t AMOSTRAS        = 64;

// Offsets em volts, a unidade que a SimpleFOC usa em offset_ia/ib/ic.
struct Offsets {
    float ls_a = 0, ls_b = 0, ls_c = 0;   // CSAs internos (low-side)
    float il_a = 0, il_b = 0, il_c = 0;   // amplificadores externos (inline)
    bool  validos = false;
};
Offsets offsets;

static float mediaVolts(uint32_t pino)
{
    uint32_t soma = 0;
    for (uint16_t i = 0; i < AMOSTRAS; i++) soma += analogRead(pino);
    return (soma / (float)AMOSTRAS) * VREF_ADC / 4095.0f;
}

static bool dentroDaJanela(float v)
{
    return fabsf(v - OFFSET_NOMINAL) <= JANELA_OFFSET;
}

static void imprimirOffset(const __FlashStringHelper* nome, float v)
{
    Dbg.print(F("  ")); Dbg.print(nome); Dbg.print(F(" = "));
    Dbg.print(v, 3);
    Dbg.println(dentroDaJanela(v) ? F(" V") : F(" V  << FORA DA JANELA"));
}

static void configurarRegistradores()
{
    uint16_t dc = REG_DRIVER_CONTROL;
#if PLACA_SEM_ESTAGIO_DE_POTENCIA
    dc |= DRV_DC_DIS_GDF;
#endif
    drv.escreverRegistrador(DRV_REG_DRIVER_CONTROL, dc);
    drv.escreverRegistrador(DRV_REG_GATE_DRIVE_HS,  REG_GATE_DRIVE_HS);
    drv.escreverRegistrador(DRV_REG_GATE_DRIVE_LS,  REG_GATE_DRIVE_LS);
    drv.escreverRegistrador(DRV_REG_OCP_CONTROL,    REG_OCP_CONTROL);
    drv.escreverRegistrador(DRV_REG_CSA_CONTROL,    REG_CSA_CONTROL);
    drv.escreverRegistrador(DRV_REG_DRIVER_CONFIG,  REG_DRIVER_CONFIG);
}

// Ponte em COAST garante corrente nula nas fases para os inline; os
// internos ainda têm as entradas curto-circuitadas pelo próprio chip.
static bool calibrarAmplificadores()
{
    if (!drv.planar(ModoPlanar::RegistradorCoast)) return false;
    delay(5);

    offsets.il_a = mediaVolts(PINO_ILCS_A);
    offsets.il_b = mediaVolts(PINO_ILCS_B);
    offsets.il_c = mediaVolts(PINO_ILCS_C);

    if (!drv.iniciarAutoZero()) return false;
    delay(5);
    offsets.ls_a = mediaVolts(PINO_LSCS_A);
    offsets.ls_b = mediaVolts(PINO_LSCS_B);
    offsets.ls_c = mediaVolts(PINO_LSCS_C);
    if (!drv.terminarAutoZero()) return false;

    Dbg.println(F("\n=== OFFSETS (corrente nula) ==="));
    imprimirOffset(F("inline A "), offsets.il_a);
    imprimirOffset(F("inline B "), offsets.il_b);
    imprimirOffset(F("inline C "), offsets.il_c);
    imprimirOffset(F("interno A"), offsets.ls_a);
    imprimirOffset(F("interno B"), offsets.ls_b);
    imprimirOffset(F("interno C"), offsets.ls_c);

    return dentroDaJanela(offsets.il_a) && dentroDaJanela(offsets.il_b)
        && dentroDaJanela(offsets.il_c) && dentroDaJanela(offsets.ls_a)
        && dentroDaJanela(offsets.ls_b) && dentroDaJanela(offsets.ls_c);
}

// Toda volta do sono passa por aqui: o sono apaga os registradores e os
// offsets podem ter derivado. A configuração vem antes da calibração
// porque o offset dos CSAs internos escala com o ganho.
// Retorna true quando o driver está pronto para a SimpleFOC ligar o PWM.
static bool despertarDriver()
{
    offsets.validos = false;
    configurarRegistradores();

    if (!drv.acordar()) {
        Dbg.println(F("\n  acordar FALHOU: configuracao nao conferiu ou falha ativa."));
        drv.mostrarFalhas(Dbg);
        return false;
    }
    if (!calibrarAmplificadores()) {
        Dbg.println(F("\n  calibracao FALHOU: motor girando ou amplificador fora do esperado?"));
        drv.dormir();
        return false;
    }
    if (!drv.retomar()) {                 // sai do COAST: modo de uso
        Dbg.println(F("\n  retomar FALHOU."));
        drv.mostrarFalhas(Dbg);
        return false;
    }
    if (!drv.travarRegistradores()) {
        Dbg.println(F("\n  travar FALHOU."));
        return false;
    }
    offsets.validos = true;
    return true;
}

void setup()
{
    analogReadResolution(12);
    Dbg.begin(CONSOLE_BAUD);

    drv.iniciar();                                            // driver dormindo

    const bool pronto = despertarDriver();
    drv.mostrarRegistradores(Dbg);                            // lê e explica
    Dbg.println(pronto ? F("\n  DRIVER PRONTO.") : F("\n  >> NAO LIGUE O PWM."));
}

void loop() {}
