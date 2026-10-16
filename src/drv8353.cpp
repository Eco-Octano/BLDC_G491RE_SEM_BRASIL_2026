#include "drv8353.h"

// Modo 1: SDO propaga na subida, SDI é capturado na descida.
// O SDO do driver é open-drain; o pull-up de 4,7 k fica na placa.
uint16_t DRV8353::transferir(uint16_t quadro)
{
    pinMode(DRV_PINO_SCLK, OUTPUT);
    pinMode(DRV_PINO_SDI,  OUTPUT);
    pinMode(DRV_PINO_SDO,  INPUT);

    digitalWrite(DRV_PINO_SCLK, LOW);
    digitalWrite(DRV_PINO_NSCS, LOW);
    delayMicroseconds(ATRASO_US);

    uint16_t rx = 0;
    for (int8_t i = 15; i >= 0; i--) {
        digitalWrite(DRV_PINO_SDI, (quadro >> i) & 1);
        delayMicroseconds(ATRASO_US);
        digitalWrite(DRV_PINO_SCLK, HIGH);
        delayMicroseconds(ATRASO_US);
        rx = (uint16_t)(rx << 1) | (digitalRead(DRV_PINO_SDO) ? 1 : 0);
        digitalWrite(DRV_PINO_SCLK, LOW);
        delayMicroseconds(ATRASO_US);
    }

    digitalWrite(DRV_PINO_NSCS, HIGH);
    delayMicroseconds(ATRASO_US);
    return rx;
}

bool DRV8353::begin()
{
    pinMode(DRV_PINO_NSCS, OUTPUT);
    digitalWrite(DRV_PINO_NSCS, HIGH);
    pinMode(DRV_PINO_SCLK, OUTPUT);
    digitalWrite(DRV_PINO_SCLK, LOW);
    pinMode(DRV_PINO_SDI, OUTPUT);
    pinMode(DRV_PINO_SDO, INPUT);
    pinMode(DRV_PINO_NFAULT, INPUT);

    pinMode(DRV_PINO_ENABLE, OUTPUT);
    digitalWrite(DRV_PINO_ENABLE, LOW);
    acordado_ = false;
    return true;
}

uint16_t DRV8353::ler(uint8_t reg)
{
    return transferir(drv_quadro_leitura(reg)) & DRV_MASCARA_DADO;
}

void DRV8353::escrever(uint8_t reg, uint16_t valor)
{
    transferir(drv_quadro_escrita(reg, valor));
}

bool DRV8353::escreverEConferir(uint8_t reg, uint16_t valor)
{
    escrever(reg, valor);
    // CLR_FLT se auto-limpa, então sai da comparação.
    const uint16_t mascara = (reg == DRV_REG_DRIVER_CONTROL)
                           ? (uint16_t)(DRV_MASCARA_DADO & ~DRV_DC_CLR_FLT)
                           : DRV_MASCARA_DADO;
    return (ler(reg) & mascara) == (valor & mascara);
}

bool DRV8353::aplicar(const DrvConfig& c)
{
    uint16_t dc = DRV_PWM_MODE(c.pwm_mode);
    if (c.otw_no_nfault) dc |= DRV_DC_OTW_REP;
    if (c.mascarar_gdf)  dc |= DRV_DC_DIS_GDF;
    if (c.mascarar_gduv) dc |= DRV_DC_DIS_GDUV;

    uint16_t csa = DRV_CSA_GAIN(c.csa_ganho)
                 | (DRV_RESET[DRV_REG_CSA_CONTROL] & 0x003);   // SEN_LVL
    if (c.csa_bidir) csa |= DRV_CSA_VREF_DIV;

    bool ok = true;
    ok &= escreverEConferir(DRV_REG_GATE_DRIVE_HS,  c.gate_drive_hs);
    ok &= escreverEConferir(DRV_REG_GATE_DRIVE_LS,  c.gate_drive_ls);
    ok &= escreverEConferir(DRV_REG_OCP_CONTROL,    c.ocp_control);
    ok &= escreverEConferir(DRV_REG_CSA_CONTROL,    csa);
    ok &= escreverEConferir(DRV_REG_DRIVER_CONFIG,  0x000);
    ok &= escreverEConferir(DRV_REG_DRIVER_CONTROL, dc | DRV_DC_CLR_FLT);
    return ok;
}

bool DRV8353::acordar(const DrvConfig& c)
{
    digitalWrite(DRV_PINO_ENABLE, HIGH);
    delay(1);                      // tWAKE <= 1 ms
    acordado_ = true;
    return aplicar(c);
}

void DRV8353::dormir()
{
    digitalWrite(DRV_PINO_ENABLE, LOW);
    acordado_ = false;
}

void DRV8353::pulsoReset()
{
    digitalWrite(DRV_PINO_ENABLE, LOW);
    delayMicroseconds(20);         // tRST: 5 a 40 us
    digitalWrite(DRV_PINO_ENABLE, HIGH);
}

DrvFalhas DRV8353::lerFalhas()
{
    DrvFalhas f;
    f.status1 = ler(DRV_REG_FAULT_STATUS_1);
    f.status2 = ler(DRV_REG_VGS_STATUS_2);
    f.alguma  = (f.status1 & DRV_F1_FAULT) != 0;
    return f;
}

void DRV8353::limparFalha()
{
    escrever(DRV_REG_DRIVER_CONTROL,
             ler(DRV_REG_DRIVER_CONTROL) | DRV_DC_CLR_FLT);
}

static void linha(Print& s, const char* nome, bool ativo)
{
    if (ativo) { s.print(F("    ")); s.println(nome); }
}

void DRV8353::imprimirFalhas(Print& s, const DrvFalhas& f)
{
    s.print(F("  0x00 FAULT_STATUS_1 = 0x")); s.println(f.status1, HEX);
    s.print(F("  0x01 VGS_STATUS_2   = 0x")); s.println(f.status2, HEX);

    if (!f.alguma && f.status2 == 0) {
        s.println(F("  sem falha ativa."));
        return;
    }

    s.println(F("  falhas ativas:"));
    linha(s, "VDS_OCP  sobrecorrente por VDS",     f.status1 & DRV_F1_VDS_OCP);
    linha(s, "GDF      gate drive fault",          f.status1 & DRV_F1_GDF);
    linha(s, "UVLO     subtensao de alimentacao",  f.status1 & DRV_F1_UVLO);
    linha(s, "OTSD     desligamento termico",      f.status1 & DRV_F1_OTSD);
    linha(s, "VDS braco A", f.status1 & (DRV_F1_VDS_HA | DRV_F1_VDS_LA));
    linha(s, "VDS braco B", f.status1 & (DRV_F1_VDS_HB | DRV_F1_VDS_LB));
    linha(s, "VDS braco C", f.status1 & (DRV_F1_VDS_HC | DRV_F1_VDS_LC));

    linha(s, "SA_OC    sobrecorrente de sense A",  f.status2 & DRV_F2_SA_OC);
    linha(s, "SB_OC    sobrecorrente de sense B",  f.status2 & DRV_F2_SB_OC);
    linha(s, "SC_OC    sobrecorrente de sense C",  f.status2 & DRV_F2_SC_OC);
    linha(s, "OTW      aviso de temperatura",      f.status2 & DRV_F2_OTW);
    linha(s, "CPUV     subtensao da bomba de carga", f.status2 & DRV_F2_CPUV);
    linha(s, "VGS_HA   gate high-side A",          f.status2 & DRV_F2_VGS_HA);
    linha(s, "VGS_LA   gate low-side  A",          f.status2 & DRV_F2_VGS_LA);
    linha(s, "VGS_HB   gate high-side B",          f.status2 & DRV_F2_VGS_HB);
    linha(s, "VGS_LB   gate low-side  B",          f.status2 & DRV_F2_VGS_LB);
    linha(s, "VGS_HC   gate high-side C",          f.status2 & DRV_F2_VGS_HC);
    linha(s, "VGS_LC   gate low-side  C",          f.status2 & DRV_F2_VGS_LC);

    const uint16_t low  = DRV_F2_VGS_LA | DRV_F2_VGS_LB | DRV_F2_VGS_LC;
    const uint16_t high = DRV_F2_VGS_HA | DRV_F2_VGS_HB | DRV_F2_VGS_HC;
    if ((f.status2 & low) && !(f.status2 & high))
        s.println(F("  -> so' o low-side acusa: ver MOSFETs e shunts."));
}

static const char* NOMES[DRV_NUM_REGS] = {
    "FAULT_STATUS_1", "VGS_STATUS_2", "DRIVER_CONTROL", "GATE_DRIVE_HS",
    "GATE_DRIVE_LS",  "OCP_CONTROL",  "CSA_CONTROL",    "DRIVER_CONFIG"
};

void DRV8353::dump(Print& s)
{
    s.println(F("\n=== DUMP DRV8353RS ==="));
    for (uint8_t r = 0; r < DRV_NUM_REGS; r++) {
        const uint16_t v = ler(r);
        s.print(F("  0x0")); s.print(r);
        s.print(F(" "));     s.print(NOMES[r]);
        s.print(F(" = 0x")); s.print(v, HEX);
        if (v == DRV_RESET[r]) s.print(F("   = reset"));
        s.println();
    }
    s.print(F("  nFAULT = ")); s.print(emFalha() ? F("0 (falha)") : F("1 (ok)"));
    s.print(F("   ENABLE = ")); s.println(acordado_ ? F("1") : F("0"));
}

// Só os registradores de configuração: os dois de status refletem o
// estado real do hardware e não servem de gabarito.
bool DRV8353::barramentoOk()
{
    for (uint8_t r = DRV_REG_DRIVER_CONTROL; r < DRV_NUM_REGS; r++)
        if (ler(r) != DRV_RESET[r]) return false;
    return true;
}

void DRV8353::inicioAutoZero()
{
    escrever(DRV_REG_CSA_CONTROL, ler(DRV_REG_CSA_CONTROL)
             | DRV_CSA_CAL_A | DRV_CSA_CAL_B | DRV_CSA_CAL_C);
    delayMicroseconds(100);        // acomodação do amplificador
}

void DRV8353::fimAutoZero()
{
    escrever(DRV_REG_CSA_CONTROL, ler(DRV_REG_CSA_CONTROL)
             & ~(DRV_CSA_CAL_A | DRV_CSA_CAL_B | DRV_CSA_CAL_C));
    delayMicroseconds(100);
}
