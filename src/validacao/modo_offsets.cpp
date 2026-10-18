// Modo 3: despertar completo com calibração dos amplificadores internos e
// externos (antes testes/11): configura, acorda, mede os offsets com a
// ponte em COAST, sai do COAST e trava os registradores.

#include "validacao.h"

// Registradores de uso: 11 bits de dado, sem o endereço. Valores do
// configurador para o CSD18535KCS, ainda não revalidados.
static const uint16_t REG_DRIVER_CONTROL = 0x480;   // OCP desliga os 3 bracos, OTW no nFAULT, PWM 6x
static const uint16_t REG_GATE_DRIVE_HS  = 0x344;   // IDRIVEP 300 mA, IDRIVEN 600 mA
static const uint16_t REG_GATE_DRIVE_LS  = 0x444;   // CBC, TDRIVE 500 ns, 300 / 600 mA
static const uint16_t REG_OCP_CONTROL    = 0x265;   // morto 200 ns, retry, 4 us, VDS 0,2 V
static const uint16_t REG_CSA_CONTROL    = 0x280;   // VREF/2, 20 V/V, SEN 0,25 V
static const uint16_t REG_DRIVER_CONFIG  = 0x000;   // calibracao manual

// Os dois grupos repousam em VREF/2 = 1,65 V com corrente nula. A janela
// é um ponto de partida: estreite depois de medir na bancada.
static const float    VREF_ADC       = 3.3f;
static const float    OFFSET_NOMINAL = 1.65f;
static const float    JANELA_OFFSET  = 0.20f;
static const uint16_t AMOSTRAS       = 64;

static float ls_a, ls_b, ls_c;   // CSAs internos (low-side)
static float il_a, il_b, il_c;   // amplificadores externos (inline)

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

// COAST garante corrente nula nas fases para os inline; nos internos o
// próprio chip curto-circuita as entradas.
static bool calibrarAmplificadores()
{
    if (!drv.planar(ModoPlanar::RegistradorCoast)) return false;
    delay(5);
    il_a = mediaVolts(PINO_ILCS_A);
    il_b = mediaVolts(PINO_ILCS_B);
    il_c = mediaVolts(PINO_ILCS_C);

    if (!drv.iniciarAutoZero()) return false;
    delay(5);
    ls_a = mediaVolts(PINO_LSCS_A);
    ls_b = mediaVolts(PINO_LSCS_B);
    ls_c = mediaVolts(PINO_LSCS_C);
    if (!drv.terminarAutoZero()) return false;

    Dbg.println(F("\n=== OFFSETS (corrente nula) ==="));
    imprimirOffset(F("inline A "), il_a);
    imprimirOffset(F("inline B "), il_b);
    imprimirOffset(F("inline C "), il_c);
    imprimirOffset(F("interno A"), ls_a);
    imprimirOffset(F("interno B"), ls_b);
    imprimirOffset(F("interno C"), ls_c);

    return dentroDaJanela(il_a) && dentroDaJanela(il_b) && dentroDaJanela(il_c)
        && dentroDaJanela(ls_a) && dentroDaJanela(ls_b) && dentroDaJanela(ls_c);
}

// A configuração vem antes da calibração: o offset dos CSAs internos
// escala com o ganho.
static bool despertarDriver()
{
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
    if (!drv.retomar()) {
        Dbg.println(F("\n  retomar FALHOU."));
        drv.mostrarFalhas(Dbg);
        return false;
    }
    if (!drv.travarRegistradores()) {
        Dbg.println(F("\n  travar FALHOU."));
        return false;
    }
    return true;
}

static void ajuda()
{
    Dbg.println(F("\n--------------- comandos ---------------"));
    Dbg.println(F("  o  dorme e repete o despertar com calibracao"));
    Dbg.println(F("  d  registradores: le e explica"));
    Dbg.println(F("  s  falhas: le e diz a causa"));
    Dbg.println(F("  ?  esta ajuda"));
    Dbg.println(F("----------------------------------------"));
}

static void rodar()
{
    const bool pronto = despertarDriver();
    Dbg.println(pronto ? F("\n  DRIVER PRONTO (configurado, calibrado e travado).")
                       : F("\n  >> NAO LIGUE O PWM."));
}

void offsetsIniciar()
{
    rodar();
    ajuda();
}

void offsetsLaco()
{
    if (!Dbg.available()) return;
    switch (Dbg.read()) {
        case 'o': drv.dormir(); delay(5); rodar(); break;
        case 'd': drv.mostrarRegistradores(Dbg); break;
        case 's': drv.mostrarFalhas(Dbg); break;
        case '?': ajuda(); break;
        default:  break;
    }
}
