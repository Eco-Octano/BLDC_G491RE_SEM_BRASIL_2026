#include "DRV8353.h"

// Modo 1: SDO propaga na subida, SDI é capturado na descida.
// O SDO do driver é open-drain; o pull-up de 4,7 k fica na placa.
uint16_t DRV8353::transferir(uint16_t quadro)
{
    pinMode(p_.sclk, OUTPUT);
    pinMode(p_.sdi,  OUTPUT);
    pinMode(p_.sdo,  INPUT);

    digitalWrite(p_.sclk, LOW);
    digitalWrite(p_.nscs, LOW);
    delayMicroseconds(ATRASO_US);

    uint16_t rx = 0;
    for (int8_t i = 15; i >= 0; i--) {
        digitalWrite(p_.sdi, (quadro >> i) & 1);
        delayMicroseconds(ATRASO_US);
        digitalWrite(p_.sclk, HIGH);
        delayMicroseconds(ATRASO_US);
        rx = (uint16_t)(rx << 1) | (digitalRead(p_.sdo) ? 1 : 0);
        digitalWrite(p_.sclk, LOW);
        delayMicroseconds(ATRASO_US);
    }

    digitalWrite(p_.nscs, HIGH);
    delayMicroseconds(ATRASO_US);
    return rx;
}

bool DRV8353::begin()
{
    pinMode(p_.nscs, OUTPUT);
    digitalWrite(p_.nscs, HIGH);
    pinMode(p_.sclk, OUTPUT);
    digitalWrite(p_.sclk, LOW);
    pinMode(p_.sdi, OUTPUT);
    pinMode(p_.sdo, INPUT);
    pinMode(p_.nfault, INPUT);

    pinMode(p_.enable, OUTPUT);
    digitalWrite(p_.enable, LOW);
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

template <typename E>
static inline uint16_t campo(E valor, uint8_t pos)
{
    return (uint16_t)((uint16_t)valor << pos);
}

uint16_t DRV8353::montar(const DrvConfig& c, uint8_t reg)
{
    uint16_t v = 0;
    switch (reg) {
    case DRV_REG_DRIVER_CONTROL:
        v = campo(c.modo_pwm, DRV_DC_PWM_MODE_POS);
        if (c.pwm1x_assincrono)        v |= DRV_DC_PWM_COM_1X;
        if (c.ocp_desliga_tres_bracos) v |= DRV_DC_OCP_ACT;
        if (c.otw_no_nfault)           v |= DRV_DC_OTW_REP;
        if (c.mascarar_gdf)            v |= DRV_DC_DIS_GDF;
        if (c.mascarar_gduv)           v |= DRV_DC_DIS_GDUV;
        break;

    case DRV_REG_GATE_DRIVE_HS:
        // LOCK fica em 011b: qualquer outro valor é ignorado pelo chip e a
        // releitura divergiria do que foi escrito.
        v = campo(DRV_LOCK_DESTRAVADO, DRV_GD_LOCK_POS)
          | campo(c.idrivep_hs, DRV_GD_IDRIVEP_POS)
          | campo(c.idriven_hs, DRV_GD_IDRIVEN_POS);
        break;

    case DRV_REG_GATE_DRIVE_LS:
        v = campo(c.tdrive, DRV_GDLS_TDRIVE_POS)
          | campo(c.idrivep_ls, DRV_GD_IDRIVEP_POS)
          | campo(c.idriven_ls, DRV_GD_IDRIVEN_POS);
        if (c.ocp_ciclo_a_ciclo) v |= DRV_GDLS_CBC;
        break;

    case DRV_REG_OCP_CONTROL:
        v = campo(c.tempo_retry,  DRV_OCP_TRETRY_POS)
          | campo(c.tempo_morto,  DRV_OCP_DEAD_POS)
          | campo(c.modo_ocp,     DRV_OCP_MODE_POS)
          | campo(c.deglitch_ocp, DRV_OCP_DEG_POS)
          | campo(c.nivel_vds,    DRV_OCP_VDS_POS);
        break;

    case DRV_REG_CSA_CONTROL:
        v = campo(c.ganho_csa, DRV_CSA_GAIN_POS)
          | campo(c.nivel_sen, DRV_CSA_SEN_LVL_POS);
        if (c.csa_bidirecional)  v |= DRV_CSA_VREF_DIV;
        if (c.sen_ocp_desligado) v |= DRV_CSA_DIS_SEN;
        if (c.vds_ls_ref_snx)    v |= DRV_CSA_LS_REF;
        // Com CSA_FET o chip liga LS_REF sozinho; espelhar evita uma
        // falha de conferência que não é falha.
        if (c.csa_entrada_shx)   v |= DRV_CSA_FET | DRV_CSA_LS_REF;
        break;

    case DRV_REG_DRIVER_CONFIG:
        if (c.autocalibracao) v |= DRV_DCFG_CAL_MODE;
        break;
    }
    return v;
}

bool DRV8353::aplicar(const DrvConfig& c)
{
    // 0x02 por último: leva o CLR_FLT, que limpa as falhas acumuladas
    // durante a própria configuração.
    static const uint8_t ORDEM[] = {
        DRV_REG_GATE_DRIVE_HS, DRV_REG_GATE_DRIVE_LS, DRV_REG_OCP_CONTROL,
        DRV_REG_CSA_CONTROL,   DRV_REG_DRIVER_CONFIG
    };

    divergentes_ = 0;
    for (uint8_t reg : ORDEM)
        if (!escreverEConferir(reg, montar(c, reg)))
            divergentes_ |= (uint8_t)(1u << reg);

    if (!escreverEConferir(DRV_REG_DRIVER_CONTROL,
                           montar(c, DRV_REG_DRIVER_CONTROL) | DRV_DC_CLR_FLT))
        divergentes_ |= (uint8_t)(1u << DRV_REG_DRIVER_CONTROL);

    return divergentes_ == 0;
}

bool DRV8353::acordar(const DrvConfig& c)
{
    digitalWrite(p_.enable, HIGH);
    // tWAKE <= 1 ms. delay(1) pode esperar quase nada se cair logo antes
    // do tique do SysTick, e a primeira escrita se perde.
    delayMicroseconds(1500);
    acordado_ = true;
    return aplicar(c);
}

void DRV8353::dormir()
{
    digitalWrite(p_.enable, LOW);
    acordado_ = false;
}

void DRV8353::pulsoReset()
{
    digitalWrite(p_.enable, LOW);
    delayMicroseconds(20);         // tRST: 5 a 40 us
    digitalWrite(p_.enable, HIGH);
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
    linha(s, "GDUV     subtensao de VCP ou VGLS",  f.status2 & DRV_F2_GDUV);
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

// Tabelas do SLVSDY6, seção 8.6.2, indexadas pelo código do campo.
static const uint16_t IDRIVEP_MA[16] = {
    50, 50, 100, 150, 300, 350, 400, 450, 550, 600, 650, 700, 850, 900, 950, 1000
};
static const uint16_t IDRIVEN_MA[16] = {
    100, 100, 200, 300, 600, 700, 800, 900, 1100, 1200, 1300, 1400, 1700, 1800, 1900, 2000
};
static const uint16_t VDS_MV[16] = {
    60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1500, 2000
};
static const uint16_t TDRIVE_NS[4]  = { 500, 1000, 2000, 4000 };
static const uint16_t MORTO_NS[4]   = { 50, 100, 200, 400 };
static const uint8_t  DEGLITCH_US[4] = { 1, 2, 4, 8 };
static const uint8_t  GANHO[4]      = { 5, 10, 20, 40 };
static const uint16_t SEN_MV[4]     = { 250, 500, 750, 1000 };
static const char*    MODO_PWM[4]   = { "6x", "3x", "1x", "independente" };
static const char*    MODO_OCP[4]   = { "trava", "retry", "so reporta", "desligado" };

static void rotulo(Print& s, const char* nome)
{
    s.print(F("  "));
    s.print(nome);
    for (size_t n = strlen(nome); n < 18; n++) s.print(' ');
}

void DRV8353::imprimirConfig(Print& s)
{
    s.println(F("\n=== CONFIGURACAO LIDA DO CHIP ==="));
    if (!acordado_) {
        s.println(F("  driver dormindo: nao responde ao SPI. Aperte 'w'."));
        return;
    }

    const uint16_t dc  = ler(DRV_REG_DRIVER_CONTROL);
    const uint16_t hs  = ler(DRV_REG_GATE_DRIVE_HS);
    const uint16_t ls  = ler(DRV_REG_GATE_DRIVE_LS);
    const uint16_t ocp = ler(DRV_REG_OCP_CONTROL);
    const uint16_t csa = ler(DRV_REG_CSA_CONTROL);
    const uint16_t cfg = ler(DRV_REG_DRIVER_CONFIG);

    rotulo(s, "modo PWM");
    s.print(MODO_PWM[(dc >> DRV_DC_PWM_MODE_POS) & 0x3]);
    if (dc & DRV_DC_PWM_COM_1X) s.print(F(" (1x assincrono)"));
    s.println();
    rotulo(s, "OCP desliga");
    s.println((dc & DRV_DC_OCP_ACT) ? F("os tres bracos") : F("so o braco em falha"));
    rotulo(s, "OTW no nFAULT");
    s.println((dc & DRV_DC_OTW_REP) ? F("sim") : F("nao"));
    rotulo(s, "falha GDF");
    s.println((dc & DRV_DC_DIS_GDF) ? F("MASCARADA") : F("ativa"));
    rotulo(s, "falha GDUV");
    s.println((dc & DRV_DC_DIS_GDUV) ? F("MASCARADA") : F("ativa"));

    rotulo(s, "HS IDRIVE P/N");
    s.print(IDRIVEP_MA[(hs >> DRV_GD_IDRIVEP_POS) & 0xF]); s.print(F(" / "));
    s.print(IDRIVEN_MA[(hs >> DRV_GD_IDRIVEN_POS) & 0xF]); s.println(F(" mA"));
    rotulo(s, "LS IDRIVE P/N");
    s.print(IDRIVEP_MA[(ls >> DRV_GD_IDRIVEP_POS) & 0xF]); s.print(F(" / "));
    s.print(IDRIVEN_MA[(ls >> DRV_GD_IDRIVEN_POS) & 0xF]); s.println(F(" mA"));
    rotulo(s, "TDRIVE");
    s.print(TDRIVE_NS[(ls >> DRV_GDLS_TDRIVE_POS) & 0x3]); s.println(F(" ns"));
    rotulo(s, "LOCK");
    s.println(((hs >> DRV_GD_LOCK_POS) & 0x7) == DRV_LOCK_TRAVADO
              ? F("travado") : F("destravado"));

    rotulo(s, "tempo morto");
    s.print(MORTO_NS[(ocp >> DRV_OCP_DEAD_POS) & 0x3]); s.println(F(" ns"));
    rotulo(s, "OCP modo");
    s.print(MODO_OCP[(ocp >> DRV_OCP_MODE_POS) & 0x3]);
    s.print(F(", retry "));
    s.print(((ocp >> DRV_OCP_TRETRY_POS) & 0x1) ? F("50 us") : F("8 ms"));
    s.print(F(", ciclo a ciclo "));
    s.println((ls & DRV_GDLS_CBC) ? F("sim") : F("nao"));
    rotulo(s, "OCP limiar VDS");
    s.print(VDS_MV[(ocp >> DRV_OCP_VDS_POS) & 0xF]); s.print(F(" mV, deglitch "));
    s.print(DEGLITCH_US[(ocp >> DRV_OCP_DEG_POS) & 0x3]); s.println(F(" us"));

    rotulo(s, "CSA ganho");
    s.print(GANHO[(csa >> DRV_CSA_GAIN_POS) & 0x3]);
    s.println((csa & DRV_CSA_VREF_DIV) ? F(" V/V, bidirecional (VREF/2)")
                                       : F(" V/V, unidirecional"));
    rotulo(s, "CSA entrada");
    s.println((csa & DRV_CSA_FET) ? F("SHx (RDS(on) do low-side)") : F("SPx (shunt)"));
    rotulo(s, "VDS low-side ate");
    s.println((csa & DRV_CSA_LS_REF) ? F("SNx") : F("SPx"));
    rotulo(s, "OCP do shunt");
    if (csa & DRV_CSA_DIS_SEN) {
        s.println(F("desligado"));
    } else {
        s.print(SEN_MV[(csa >> DRV_CSA_SEN_LVL_POS) & 0x3]); s.println(F(" mV"));
    }
    rotulo(s, "calibracao CSA");
    s.println((cfg & DRV_DCFG_CAL_MODE) ? F("automatica") : F("manual"));
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
