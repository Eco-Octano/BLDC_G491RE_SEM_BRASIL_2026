#include "DRV8353.h"

DRV8353::DRV8353(const DrvPinos& pinos) : p_(pinos)
{
    for (uint8_t r = 0; r < DRV_NUM_REGS; r++) config_[r] = DRV_RESET[r];
}

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

void DRV8353::iniciar()
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
    planando_ = false;
    travado_  = false;
}

uint16_t DRV8353::lerRegistrador(uint8_t reg)
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
    return (lerRegistrador(reg) & mascara) == (valor & mascara);
}

// Guarda o valor que o chip vai de fato reter: o que ele ignora ou altera
// sozinho viraria uma falha de conferência que não é falha.
uint16_t DRV8353::normalizar(uint8_t reg, uint16_t v)
{
    v &= DRV_MASCARA_DADO;
    switch (reg) {
    case DRV_REG_DRIVER_CONTROL:
        v &= (uint16_t)~DRV_DC_CLR_FLT;
        break;
    case DRV_REG_GATE_DRIVE_HS:
        // Travar faria o chip ignorar as escritas seguintes do despertar.
        v = (uint16_t)((v & ~DRV_GD_LOCK_MASCARA)
                       | (DRV_LOCK_DESTRAVADO << DRV_GD_LOCK_POS));
        break;
    case DRV_REG_CSA_CONTROL:
        // Guardado, o curto do auto-zero voltaria a cada despertar.
        v &= (uint16_t)~(DRV_CSA_CAL_A | DRV_CSA_CAL_B | DRV_CSA_CAL_C);
        if (v & DRV_CSA_FET) v |= DRV_CSA_LS_REF;
        break;
    case DRV_REG_DRIVER_CONFIG:
        v &= DRV_DCFG_CAL_MODE;
        break;
    }
    return v;
}

uint16_t DRV8353::valorEfetivo(uint8_t reg) const
{
    uint16_t v = config_[reg];
    if (reg == DRV_REG_DRIVER_CONTROL && planando_
        && modo_planar_ == ModoPlanar::RegistradorCoast)
        v |= DRV_DC_COAST;
    if (reg == DRV_REG_GATE_DRIVE_HS && travado_)
        v = (uint16_t)((v & ~DRV_GD_LOCK_MASCARA) | (DRV_LOCK_TRAVADO << DRV_GD_LOCK_POS));
    return v;
}

uint16_t DRV8353::valorConfigurado(uint8_t reg) const
{
    return (reg < DRV_NUM_REGS) ? config_[reg] : 0;
}

bool DRV8353::escreverRegistrador(uint8_t reg, uint16_t valor)
{
    if (reg < DRV_REG_DRIVER_CONTROL || reg >= DRV_NUM_REGS) return false;
    if (valor > DRV_MASCARA_DADO) return false;
    config_[reg] = normalizar(reg, valor);
    if (!acordado_) return true;
    if (travado_)   return false;   // o chip ignoraria a escrita

    const bool ok = escreverEConferir(reg, valorEfetivo(reg));
    if (ok) divergentes_ &= (uint8_t)~(1u << reg);
    else    divergentes_ |= (uint8_t)(1u << reg);
    return ok;
}

bool DRV8353::aplicarConfiguracao()
{
    // 0x02 por último: leva o CLR_FLT, que limpa as falhas acumuladas
    // durante a própria configuração.
    static const uint8_t ORDEM[] = {
        DRV_REG_GATE_DRIVE_HS, DRV_REG_GATE_DRIVE_LS, DRV_REG_OCP_CONTROL,
        DRV_REG_CSA_CONTROL,   DRV_REG_DRIVER_CONFIG
    };

    divergentes_ = 0;
    for (uint8_t reg : ORDEM)
        if (!escreverEConferir(reg, valorEfetivo(reg)))
            divergentes_ |= (uint8_t)(1u << reg);

    if (!escreverEConferir(DRV_REG_DRIVER_CONTROL,
                           valorEfetivo(DRV_REG_DRIVER_CONTROL) | DRV_DC_CLR_FLT))
        divergentes_ |= (uint8_t)(1u << DRV_REG_DRIVER_CONTROL);

    return divergentes_ == 0;
}

bool DRV8353::acordar()
{
    digitalWrite(p_.enable, HIGH);
    // tWAKE <= 1 ms. delay(1) pode esperar quase nada se cair logo antes
    // do tique do SysTick, e a primeira escrita se perde.
    delayMicroseconds(1500);
    acordado_ = true;
    planando_ = false;
    travado_  = false;              // o sono destrava junto com o resto

    const bool conferiu = aplicarConfiguracao();
    // Uma falha surgida durante o despertar não gera borda nova para a
    // EXTI do nFAULT; por isso o nível é consultado aqui.
    return conferiu && !emFalha();
}

void DRV8353::dormir()
{
    digitalWrite(p_.enable, LOW);
    acordado_ = false;
    planando_ = false;
    travado_  = false;
}

bool DRV8353::travarRegistradores()
{
    if (!acordado_) return false;
    travado_ = true;
    escrever(DRV_REG_GATE_DRIVE_HS, valorEfetivo(DRV_REG_GATE_DRIVE_HS));
    const uint8_t lock = (lerRegistrador(DRV_REG_GATE_DRIVE_HS) >> DRV_GD_LOCK_POS) & 0x7;
    travado_ = (lock == DRV_LOCK_TRAVADO);
    return travado_;
}

bool DRV8353::destravarRegistradores()
{
    if (!acordado_) return false;
    travado_ = false;
    return escreverEConferir(DRV_REG_GATE_DRIVE_HS, valorEfetivo(DRV_REG_GATE_DRIVE_HS));
}

bool DRV8353::planar(ModoPlanar modo)
{
    if (modo == ModoPlanar::PinoEnable) {
        dormir();
    } else {
        if (!acordado_) return false;
        if (!escreverEConferir(DRV_REG_DRIVER_CONTROL,
                               config_[DRV_REG_DRIVER_CONTROL] | DRV_DC_COAST))
            return false;
    }
    planando_    = true;
    modo_planar_ = modo;
    return true;
}

bool DRV8353::retomar()
{
    if (!planando_) return acordado_ && !emFalha();
    if (modo_planar_ == ModoPlanar::PinoEnable) return acordar();

    if (!escreverEConferir(DRV_REG_DRIVER_CONTROL, config_[DRV_REG_DRIVER_CONTROL]))
        return false;
    planando_ = false;
    return !emFalha();
}

DrvFalhas DRV8353::lerFalhas()
{
    DrvFalhas f;
    if (!acordado_) return f;          // dormindo, o chip não responde

    f.valida  = true;
    f.status1 = lerRegistrador(DRV_REG_FAULT_STATUS_1);
    f.status2 = lerRegistrador(DRV_REG_VGS_STATUS_2);

    f.alguma               = f.status1 & DRV_F1_FAULT;
    f.sobrecorrente_vds    = f.status1 & DRV_F1_VDS_OCP;
    f.falha_gate           = f.status1 & DRV_F1_GDF;
    f.subtensao_vm         = f.status1 & DRV_F1_UVLO;
    f.desligamento_termico = f.status1 & DRV_F1_OTSD;
    f.aviso_temperatura    = f.status2 & DRV_F2_OTW;
    f.subtensao_gate       = f.status2 & DRV_F2_GDUV;
    f.sobrecorrente_shunt  = f.status2 & (DRV_F2_SA_OC | DRV_F2_SB_OC | DRV_F2_SC_OC);
    return f;
}

bool DRV8353::resetarFalhas(MetodoReset metodo)
{
    if (!acordado_) return false;

    bool ok = true;
    if (metodo == MetodoReset::Spi) {
        ok = escreverEConferir(DRV_REG_DRIVER_CONTROL,
                               valorEfetivo(DRV_REG_DRIVER_CONTROL) | DRV_DC_CLR_FLT);
    } else {
        digitalWrite(p_.enable, LOW);
        delayMicroseconds(20);         // tRST: 5 a 40 us; acima de 1 ms o chip dorme
        digitalWrite(p_.enable, HIGH);
    }
    delayMicroseconds(100);            // folga para o nFAULT, dreno aberto, subir
    return ok && !emFalha();
}

bool DRV8353::iniciarAutoZero()
{
    if (!acordado_) return false;
    const bool ok = escreverEConferir(DRV_REG_CSA_CONTROL,
        config_[DRV_REG_CSA_CONTROL] | DRV_CSA_CAL_A | DRV_CSA_CAL_B | DRV_CSA_CAL_C);
    delayMicroseconds(100);            // acomodação do amplificador
    return ok;
}

bool DRV8353::terminarAutoZero()
{
    if (!acordado_) return false;
    const bool ok = escreverEConferir(DRV_REG_CSA_CONTROL, config_[DRV_REG_CSA_CONTROL]);
    delayMicroseconds(100);
    return ok;
}

// ---------------------------------------------------------------------------
// Explicação em texto. As tabelas vêm do SLVSDY6A, seção 8.6.2, indexadas
// pelo código do campo. Strings em ASCII: terminal serial não mostra acento.

static const char* const NOMES_REG[DRV_NUM_REGS] = {
    "FAULT_STATUS_1", "VGS_STATUS_2", "DRIVER_CONTROL", "GATE_DRIVE_HS",
    "GATE_DRIVE_LS",  "OCP_CONTROL",  "CSA_CONTROL",    "DRIVER_CONFIG"
};

static const uint16_t IDRIVEP_MA[16] = {
    50, 50, 100, 150, 300, 350, 400, 450, 550, 600, 650, 700, 850, 900, 950, 1000
};
static const uint16_t IDRIVEN_MA[16] = {
    100, 100, 200, 300, 600, 700, 800, 900, 1100, 1200, 1300, 1400, 1700, 1800, 1900, 2000
};
static const uint16_t VDS_MV[16] = {
    60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1500, 2000
};
static const uint16_t TDRIVE_NS[4]   = { 500, 1000, 2000, 4000 };
static const uint16_t MORTO_NS[4]    = { 50, 100, 200, 400 };
static const uint8_t  DEGLITCH_US[4] = { 1, 2, 4, 8 };
static const uint8_t  GANHO[4]       = { 5, 10, 20, 40 };
static const uint16_t SEN_MV[4]      = { 250, 500, 750, 1000 };
static const char* const MODO_PWM[4] = { "6x", "3x", "1x", "independente" };
static const char* const MODO_OCP[4] = {
    "trava (latched)", "retry automatico", "so reporta", "desligado"
};

// Índice = número do bit.
static const char* const BITS_F1[11] = {
    "VDS_LC", "VDS_HC", "VDS_LB", "VDS_HB", "VDS_LA", "VDS_HA",
    "OTSD", "UVLO", "GDF", "VDS_OCP", "FAULT"
};
static const char* const BITS_F2[11] = {
    "VGS_LC", "VGS_HC", "VGS_LB", "VGS_HB", "VGS_LA", "VGS_HA",
    "GDUV", "OTW", "SC_OC", "SB_OC", "SA_OC"
};
static const char* const MOSFETS[6] = { "LC", "HC", "LB", "HB", "LA", "HA" };

struct Explicacao {
    uint8_t     reg;
    uint16_t    bit;
    const char* nome;
    const char* oque;
    const char* causa;
};

static const Explicacao EXPLICACOES[] = {
    { 0, DRV_F1_VDS_OCP, "VDS_OCP",
      "sobrecorrente: a queda VDS de um MOSFET ligado passou do limiar",
      "curto na fase ou no motor, corrente alta, ou VDS_LVL e OCP_DEG apertados (0x05)" },
    { 0, DRV_F1_GDF, "GDF",
      "falha de gate: a VGS de um MOSFET nao chegou ao nivel esperado",
      "MOSFET ausente ou gate aberto, gate em curto, ou IDRIVE e TDRIVE fracos (0x03, 0x04)" },
    { 0, DRV_F1_UVLO, "UVLO",
      "subtensao em VM: a alimentacao do driver caiu abaixo do limite",
      "fonte desligada ou fraca, bateria baixa, mau contato em VM" },
    { 0, DRV_F1_OTSD, "OTSD",
      "desligamento termico: o chip passou da temperatura maxima",
      "dissipacao alta no driver; espere esfriar antes de resetar" },
    { 1, DRV_F2_SA_OC, "SA_OC",
      "sobrecorrente no shunt da fase A: tensao acima de SEN_LVL",
      "corrente alta na fase A ou SEN_LVL baixo demais (0x06)" },
    { 1, DRV_F2_SB_OC, "SB_OC",
      "sobrecorrente no shunt da fase B: tensao acima de SEN_LVL",
      "corrente alta na fase B ou SEN_LVL baixo demais (0x06)" },
    { 1, DRV_F2_SC_OC, "SC_OC",
      "sobrecorrente no shunt da fase C: tensao acima de SEN_LVL",
      "corrente alta na fase C ou SEN_LVL baixo demais (0x06)" },
    { 1, DRV_F2_OTW, "OTW",
      "aviso de temperatura alta; o driver continua operando",
      "dissipacao alta; se a temperatura subir mais, vira OTSD" },
    { 1, DRV_F2_GDUV, "GDUV",
      "subtensao da bomba de carga (VCP) ou do regulador do low-side (VGLS)",
      "VM baixo ou consumo de gate alto demais" },
};

static void hex3(Print& s, uint16_t v)
{
    for (int8_t d = 8; d >= 0; d -= 4) s.print((v >> d) & 0xF, HEX);
}

// "       CAMPO       = ", alinhado, para o valor vir em seguida.
static void campo(Print& s, const char* nome)
{
    s.print(F("       "));
    s.print(nome);
    for (size_t n = strlen(nome); n < 11; n++) s.print(' ');
    s.print(F("= "));
}

static void listarBits(Print& s, uint16_t v, const char* const nomes[11])
{
    bool primeiro = true;
    for (int8_t b = 10; b >= 0; b--) {
        if (!(v & (1u << b))) continue;
        if (!primeiro) s.print(' ');
        s.print(nomes[b]);
        primeiro = false;
    }
    if (primeiro) s.print(F("nenhuma"));
    s.println();
}

static void listarMosfets(Print& s, uint16_t v)
{
    for (int8_t b = 5; b >= 0; b--)
        if (v & (1u << b)) { s.print(' '); s.print(MOSFETS[b]); }
    s.println();
}

static void cabecalho(Print& s, uint8_t reg, uint16_t v)
{
    s.print(F("  0x0")); s.print(reg); s.print(' ');
    s.print(NOMES_REG[reg]);
    for (size_t n = strlen(NOMES_REG[reg]); n < 15; n++) s.print(' ');
    s.print(F("= 0x"));
    hex3(s, v);
}

void DRV8353::explicarRegistrador(Print& s, uint8_t reg, uint16_t v)
{
    switch (reg) {
    case DRV_REG_FAULT_STATUS_1:
        campo(s, "ativas"); listarBits(s, v, BITS_F1);
        break;

    case DRV_REG_VGS_STATUS_2:
        campo(s, "ativas"); listarBits(s, v, BITS_F2);
        break;

    case DRV_REG_DRIVER_CONTROL: {
        const uint8_t modo = (v >> DRV_DC_PWM_MODE_POS) & 0x3;
        campo(s, "PWM_MODE"); s.println(MODO_PWM[modo]);
        if (modo == 2) {
            campo(s, "1PWM_COM");
            s.println((v & DRV_DC_PWM_COM_1X) ? F("retificacao assincrona")
                                              : F("retificacao sincrona"));
            campo(s, "1PWM_DIR"); s.println((v & DRV_DC_PWM_DIR_1X) ? 1 : 0);
        }
        campo(s, "OCP_ACT");
        s.println((v & DRV_DC_OCP_ACT) ? F("sobrecorrente desliga os 3 meio-bracos")
                                       : F("sobrecorrente desliga so' o meio-braco em falha"));
        campo(s, "DIS_GDUV");
        s.println((v & DRV_DC_DIS_GDUV) ? F("falha GDUV DESLIGADA") : F("falha GDUV ativa"));
        campo(s, "DIS_GDF");
        s.println((v & DRV_DC_DIS_GDF) ? F("falha GDF DESLIGADA") : F("falha GDF ativa"));
        campo(s, "OTW_REP");
        s.println((v & DRV_DC_OTW_REP) ? F("aviso termico tambem no nFAULT")
                                       : F("aviso termico fora do nFAULT"));
        campo(s, "COAST");
        s.println((v & DRV_DC_COAST) ? F("1, ponte em alta impedancia") : F("0"));
        campo(s, "BRAKE");
        s.println((v & DRV_DC_BRAKE) ? F("1, os 3 low-side ligados (freio)") : F("0"));
        break;
    }

    case DRV_REG_GATE_DRIVE_HS: {
        const uint8_t lock = (v >> DRV_GD_LOCK_POS) & 0x7;
        campo(s, "LOCK");
        if      (lock == DRV_LOCK_DESTRAVADO) s.println(F("destravado (011b)"));
        else if (lock == DRV_LOCK_TRAVADO)    s.println(F("TRAVADO (110b)"));
        else { s.print(F("sem efeito (")); s.print(lock, BIN); s.println(F("b)")); }
        campo(s, "IDRIVEP_HS");
        s.print(IDRIVEP_MA[(v >> DRV_GD_IDRIVEP_POS) & 0xF]); s.println(F(" mA, liga o MOSFET"));
        campo(s, "IDRIVEN_HS");
        s.print(IDRIVEN_MA[(v >> DRV_GD_IDRIVEN_POS) & 0xF]); s.println(F(" mA, desliga o MOSFET"));
        break;
    }

    case DRV_REG_GATE_DRIVE_LS:
        campo(s, "CBC");
        s.println((v & DRV_GDLS_CBC) ? F("1, no retry a falha limpa no proximo PWM")
                                     : F("0, no retry a falha limpa so' apos tRETRY"));
        campo(s, "TDRIVE");
        s.print(TDRIVE_NS[(v >> DRV_GDLS_TDRIVE_POS) & 0x3]); s.println(F(" ns de corrente de pico"));
        campo(s, "IDRIVEP_LS");
        s.print(IDRIVEP_MA[(v >> DRV_GD_IDRIVEP_POS) & 0xF]); s.println(F(" mA, liga o MOSFET"));
        campo(s, "IDRIVEN_LS");
        s.print(IDRIVEN_MA[(v >> DRV_GD_IDRIVEN_POS) & 0xF]); s.println(F(" mA, desliga o MOSFET"));
        break;

    case DRV_REG_OCP_CONTROL:
        campo(s, "TRETRY");
        s.println((v & DRV_OCP_TRETRY) ? F("50 us") : F("8 ms"));
        campo(s, "DEAD_TIME");
        s.print(MORTO_NS[(v >> DRV_OCP_DEAD_POS) & 0x3]); s.println(F(" ns"));
        campo(s, "OCP_MODE");
        s.println(MODO_OCP[(v >> DRV_OCP_MODE_POS) & 0x3]);
        campo(s, "OCP_DEG");
        s.print(DEGLITCH_US[(v >> DRV_OCP_DEG_POS) & 0x3]); s.println(F(" us"));
        campo(s, "VDS_LVL");
        s.print(VDS_MV[(v >> DRV_OCP_VDS_POS) & 0xF]); s.println(F(" mV"));
        break;

    case DRV_REG_CSA_CONTROL:
        campo(s, "CSA_FET");
        s.println((v & DRV_CSA_FET) ? F("entrada positiva em SHx")
                                    : F("entrada positiva em SPx (shunt)"));
        campo(s, "VREF_DIV");
        s.println((v & DRV_CSA_VREF_DIV) ? F("bidirecional, repousa em VREF/2")
                                         : F("unidirecional, referencia VREF"));
        campo(s, "LS_REF");
        s.println((v & DRV_CSA_LS_REF) ? F("VDS do low-side medido ate SNx")
                                       : F("VDS do low-side medido ate SPx"));
        campo(s, "CSA_GAIN");
        s.print(GANHO[(v >> DRV_CSA_GAIN_POS) & 0x3]); s.println(F(" V/V"));
        campo(s, "DIS_SEN");
        s.println((v & DRV_CSA_DIS_SEN) ? F("OCP do shunt DESLIGADA") : F("OCP do shunt ativa"));
        campo(s, "CSA_CAL");
        if (v & (DRV_CSA_CAL_A | DRV_CSA_CAL_B | DRV_CSA_CAL_C)) {
            s.print(F("entradas em curto:"));
            if (v & DRV_CSA_CAL_A) s.print(F(" A"));
            if (v & DRV_CSA_CAL_B) s.print(F(" B"));
            if (v & DRV_CSA_CAL_C) s.print(F(" C"));
            s.println();
        } else {
            s.println(F("normal"));
        }
        campo(s, "SEN_LVL");
        s.print(SEN_MV[(v >> DRV_CSA_SEN_LVL_POS) & 0x3]); s.println(F(" mV"));
        break;

    case DRV_REG_DRIVER_CONFIG:
        campo(s, "CAL_MODE");
        s.println((v & DRV_DCFG_CAL_MODE) ? F("calibracao automatica") : F("calibracao manual"));
        break;
    }
}

void DRV8353::mostrarRegistradores(Print& s)
{
    s.println(F("\n=== REGISTRADORES DRV8353RS ==="));

    if (!acordado_) {
        s.println(F("  Driver dormindo: o chip nao responde ao SPI."));
        if (planando_) s.println(F("  Planando pelo ENABLE: retomar() acorda e reescreve tudo."));
        s.println(F("  Valores guardados, que vao para o chip no proximo acordar():"));
        for (uint8_t r = DRV_REG_DRIVER_CONTROL; r < DRV_NUM_REGS; r++) {
            cabecalho(s, r, config_[r]);
            if (config_[r] == DRV_RESET[r]) s.print(F("  = reset"));
            s.println();
            explicarRegistrador(s, r, config_[r]);
        }
        return;
    }

    uint16_t lido[DRV_NUM_REGS];
    for (uint8_t r = 0; r < DRV_NUM_REGS; r++) {
        lido[r] = lerRegistrador(r);
        cabecalho(s, r, lido[r]);
        if (r >= DRV_REG_DRIVER_CONTROL) {
            const uint16_t esperado = valorEfetivo(r);
            if (lido[r] == esperado) {
                s.print(F("  ok"));
            } else {
                s.print(F("  DIFERENTE do configurado (0x"));
                hex3(s, esperado);
                s.print(')');
            }
            if (lido[r] == DRV_RESET[r]) s.print(F("  = reset"));
        }
        s.println();
        explicarRegistrador(s, r, lido[r]);
    }

    const uint16_t dc  = lido[DRV_REG_DRIVER_CONTROL];
    const uint16_t ocp = lido[DRV_REG_OCP_CONTROL];
    const uint16_t csa = lido[DRV_REG_CSA_CONTROL];
    s.print(F("\n  Modo: PWM "));
    s.print(MODO_PWM[(dc >> DRV_DC_PWM_MODE_POS) & 0x3]);
    s.print(F(" | OCP "));
    s.print(MODO_OCP[(ocp >> DRV_OCP_MODE_POS) & 0x3]);
    s.print((dc & DRV_DC_OCP_ACT) ? F(", desliga 3 bracos") : F(", desliga 1 braco"));
    s.print(F(" | tempo morto "));
    s.print(MORTO_NS[(ocp >> DRV_OCP_DEAD_POS) & 0x3]);
    s.print(F(" ns | CSA "));
    s.print(GANHO[(csa >> DRV_CSA_GAIN_POS) & 0x3]);
    s.println((csa & DRV_CSA_VREF_DIV) ? F(" V/V bidirecional") : F(" V/V unidirecional"));

    bool reset = true;
    for (uint8_t r = DRV_REG_GATE_DRIVE_HS; r <= DRV_REG_CSA_CONTROL; r++)
        if (lido[r] != DRV_RESET[r]) reset = false;
    if (reset)
        s.println(F("  Atencao: gate drive, OCP e CSA no reset do chip (nao dimensionados)."));
    if (dc & DRV_DC_DIS_GDF)
        s.println(F("  Atencao: falha GDF desligada (so' em bancada sem estagio de potencia)."));

    s.print(F("  ENABLE = 1   nFAULT = "));
    s.print(emFalha() ? F("0 (falha)") : F("1 (ok)"));
    s.print(travado_ ? F("   TRAVADO") : F("   destravado"));
    s.print(F("   planando: "));
    if (!planando_)
        s.println(F("nao"));
    else
        s.println(modo_planar_ == ModoPlanar::RegistradorCoast ? F("sim, por COAST")
                                                               : F("sim, por ENABLE"));
    if (divergentes_) {
        s.print(F("  Ultima escrita nao conferiu em:"));
        for (uint8_t r = 0; r < DRV_NUM_REGS; r++)
            if (divergentes_ & (1u << r)) { s.print(F(" 0x0")); s.print(r); }
        s.println();
    }
}

void DRV8353::mostrarFalhas(Print& s)
{
    s.println(F("\n=== FALHAS DRV8353RS ==="));
    s.print(F("  nFAULT = "));
    s.println(emFalha() ? F("0 (falha ativa)") : F("1 (sem falha ativa)"));

    const DrvFalhas f = lerFalhas();
    if (!f.valida) {
        s.println(F("  Driver dormindo: os registradores de falha nao respondem."));
        return;
    }

    s.print(F("  0x00 = 0x")); hex3(s, f.status1);
    s.print(F("   0x01 = 0x")); hex3(s, f.status2);
    s.println();

    bool alguma = false;
    for (const Explicacao& e : EXPLICACOES) {
        const uint16_t st = (e.reg == 0) ? f.status1 : f.status2;
        if (!(st & e.bit)) continue;
        alguma = true;
        s.print(F("  ")); s.print(e.nome); s.print(F(": ")); s.println(e.oque);
        s.print(F("      causa provavel: ")); s.println(e.causa);
    }
    if (f.status1 & 0x3F) {
        alguma = true;
        s.print(F("  MOSFETs com VDS acima do limiar:"));
        listarMosfets(s, f.status1);
    }
    if (f.status2 & 0x3F) {
        alguma = true;
        s.print(F("  MOSFETs com falha de gate (VGS):"));
        listarMosfets(s, f.status2);
    }

    const uint16_t baixo = DRV_F2_VGS_LA | DRV_F2_VGS_LB | DRV_F2_VGS_LC;
    const uint16_t alto  = DRV_F2_VGS_HA | DRV_F2_VGS_HB | DRV_F2_VGS_HC;
    if ((f.status2 & baixo) && !(f.status2 & alto))
        s.println(F("  -> so' o low-side acusa: ver MOSFETs e shunts (o monitor do low-side usa SPx)."));

    if (!alguma) {
        s.println(F("  Sem falha registrada."));
        if (emFalha())
            s.println(F("  nFAULT em 0 sem bit de falha: confira o pull-up e o jumper J7."));
        return;
    }

    const uint16_t dc  = lerRegistrador(DRV_REG_DRIVER_CONTROL);
    const uint16_t ocp = lerRegistrador(DRV_REG_OCP_CONTROL);
    if (dc & DRV_DC_DIS_GDF)
        s.println(F("  obs: deteccao de falha de gate desligada (DIS_GDF = 1)."));
    if (((ocp >> DRV_OCP_MODE_POS) & 0x3) == 0)
        s.println(F("  OCP em modo trava: a ponte so' volta com resetarFalhas()."));
    s.println(F("  Corrija a causa e chame resetarFalhas(). Se a falha voltar, a causa continua."));
}
