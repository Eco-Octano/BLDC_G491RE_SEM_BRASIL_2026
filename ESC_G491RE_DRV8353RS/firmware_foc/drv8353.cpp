#include "drv8353.h"

// Tempo entre subir o ENABLE e o SPI responder. CONFERIR o t_WAKE no
// datasheet. Se estiver curto, as primeiras transacoes voltam 0x0000 ou
// 0xFFFF e voce vai procurar o problema no SPI, que esta correto.
static const uint32_t DRV_WAKEUP_MS = 2;

DRV8353::DRV8353(SPIClass &spi, uint8_t pin_cs, uint8_t pin_enable, uint8_t pin_nfault)
    : spi_(spi), pin_cs_(pin_cs), pin_enable_(pin_enable), pin_nfault_(pin_nfault) {}

// --------------------------------------------------------------- begin
void DRV8353::begin(const DrvConfig &cfg)
{
    cfg_ = cfg;

    // CS em nivel alto ANTES de qualquer outra coisa. Um CS flutuando faz
    // o driver interpretar ruido do barramento como transacao valida.
    pinMode(pin_cs_, OUTPUT);
    digitalWrite(pin_cs_, HIGH);

    pinMode(pin_enable_, OUTPUT);
    digitalWrite(pin_enable_, LOW);      // acordado so' quando pedirem
    enabled_ = false;

    pinMode(pin_nfault_, INPUT_PULLUP);

    spi_.begin();
}

// ------------------------------------------------------- quadro de 16 bits
uint16_t DRV8353::transfer16_(uint16_t frame)
{
    spi_.beginTransaction(settings_);
    digitalWrite(pin_cs_, LOW);
    uint16_t rx = spi_.transfer16(frame);
    digitalWrite(pin_cs_, HIGH);
    spi_.endTransaction();
    return rx & DRV_DATA_MASK;
}

uint16_t DRV8353::readReg(uint8_t addr)
{
    uint16_t frame = DRV_FRAME_READ | (uint16_t(addr & 0x0F) << DRV_ADDR_SHIFT);
    return transfer16_(frame);
}

// Escreve e RELE. Nunca confie numa escrita em gate driver: um bit de modo
// que nao pegou transforma 6x em 3x, e o sintoma e' "o motor nao gira",
// sem nenhuma mensagem de erro.
bool DRV8353::writeReg(uint8_t addr, uint16_t data)
{
    uint16_t frame = (uint16_t(addr & 0x0F) << DRV_ADDR_SHIFT) | (data & DRV_DATA_MASK);
    transfer16_(frame);
    return readReg(addr) == (data & DRV_DATA_MASK);
}

// -------------------------------------------------------------- enable
bool DRV8353::enable()
{
    digitalWrite(pin_enable_, HIGH);
    delay(DRV_WAKEUP_MS);
    enabled_ = true;

    if (!applyConfig_()) return false;

    // Uma falha que apareca durante o wake-up nao gera borda nova depois -
    // por isso o nivel do pino e' consultado explicitamente aqui.
    if (faultPinActive()) return false;

    return true;
}

void DRV8353::disable()
{
    digitalWrite(pin_enable_, LOW);
    enabled_ = false;
    // A configuracao no chip morre aqui. O proximo enable() reescreve tudo.
}

// --------------------------------------------------------- configuracao
bool DRV8353::applyConfig_()
{
    // 1) Destravar. Se alguem deixou o LOCK ligado numa sessao anterior,
    //    todas as escritas seguintes seriam silenciosamente ignoradas.
    uint16_t hs = readReg(DRV_REG_GATE_DRIVE_HS);
    hs = (hs & ~DRV_LOCK_MASK) | DRV_LOCK_UNLOCK;
    transfer16_((uint16_t(DRV_REG_GATE_DRIVE_HS) << DRV_ADDR_SHIFT) | (hs & DRV_DATA_MASK));

    // 2) Limpar falhas herdadas do ciclo anterior.
    clearFaults();

    bool ok = true;

    // 3) Controle do driver: modo 6x PWM.
    //    OCP_ACT em 0 -> so' o meio-braco que faltou desliga, e nao a ponte
    //    inteira. Para um veiculo, prefira o desligamento completo: um braco
    //    ativo com outro desligado gera torque de frenagem assimetrico.
    ok &= writeReg(DRV_REG_DRIVER_CONTROL,
                   cfg_.pwm_mode | DRV_DC_OCP_ACT);

    // 4) IDRIVE.
    //    Como escolher: Qg do MOSFET dividido pelo tempo de comutacao que
    //    voce quer. Mais corrente = comutacao mais rapida = menos perda de
    //    comutacao, mas mais dV/dt, mais EMI e mais risco de ringing acionar
    //    o FET oposto pelo Miller. Comece CONSERVADOR (corrente baixa) e
    //    suba medindo a borda no osciloscopio.
    ok &= writeReg(DRV_REG_GATE_DRIVE_HS,
                   DRV_LOCK_UNLOCK
                 | (cfg_.idrive_p_hs << DRV_IDRIVEP_SHIFT)
                 | (cfg_.idrive_n_hs << DRV_IDRIVEN_SHIFT));

    ok &= writeReg(DRV_REG_GATE_DRIVE_LS,
                   DRV_LS_CBC
                 | cfg_.tdrive
                 | (cfg_.idrive_p_ls << DRV_IDRIVEP_SHIFT)
                 | (cfg_.idrive_n_ls << DRV_IDRIVEN_SHIFT));

    // 5) OCP e dead time.
    //    ATENCAO: este dead time SOMA com o driver.dead_zone da SimpleFOC.
    //    Escolha UM dos dois e zere o outro. Somar os dois distorce a
    //    corrente em baixa modulacao e joga energia fora.
    ok &= writeReg(DRV_REG_OCP_CONTROL,
                   cfg_.dead_time
                 | cfg_.ocp_mode
                 | cfg_.ocp_deg
                 | (cfg_.vds_lvl & DRV_VDS_LVL_MASK));

    // 6) CSA.
    //    CSA_FET = 0 -> mede pelo shunt (SPx-SNx), que e' o nosso caso.
    //    VREF_DIV = 1 -> saida centrada em VREF/2 = 1,65 V.
    //    Sem o VREF_DIV a saida centra em 0 V e voce perde o sentido
    //    negativo da corrente. O FOC fica IMPOSSIVEL: a corrente de fase
    //    e' alternada, e metade da senoide simplesmente nao aparece.
    uint16_t csa = cfg_.csa_gain | (cfg_.sen_lvl & DRV_CSA_SEN_LVL_MASK);
    if (cfg_.bidirectional) csa |= DRV_CSA_VREF_DIV;
    ok &= writeReg(DRV_REG_CSA_CONTROL, csa);

    return ok && verifyConfig_();
}

bool DRV8353::verifyConfig_()
{
    uint16_t dc  = readReg(DRV_REG_DRIVER_CONTROL);
    uint16_t csa = readReg(DRV_REG_CSA_CONTROL);

    // Barramento SPI morto: MISO preso alto ou baixo.
    if ((dc == 0x0000 && csa == 0x0000) || (dc == DRV_DATA_MASK && csa == DRV_DATA_MASK))
        return false;

    if ((dc & DRV_DC_PWM_MODE_MASK) != cfg_.pwm_mode)   return false;
    if ((csa & DRV_CSA_GAIN_MASK)   != cfg_.csa_gain)   return false;
    if (cfg_.bidirectional && !(csa & DRV_CSA_VREF_DIV)) return false;

    return true;
}

// -------------------------------------------------------------- falhas
DrvFaults DRV8353::readFaults()
{
    DrvFaults f{};
    f.status1 = readReg(DRV_REG_FAULT_STATUS_1);
    f.status2 = readReg(DRV_REG_FAULT_STATUS_2);

    f.any           = f.status1 & DRV_FS1_FAULT;
    f.vds_ocp       = f.status1 & DRV_FS1_VDS_OCP;
    f.gate_fault    = f.status1 & DRV_FS1_GDF;
    f.uvlo          = f.status1 & DRV_FS1_UVLO;
    f.overtemp      = f.status1 & DRV_FS1_OTSD;
    f.overtemp_warn = f.status2 & DRV_FS2_OTW;
    f.cp_uv         = f.status2 & DRV_FS2_CPUV;
    f.sense_oc      = f.status2 & (DRV_FS2_SA_OC | DRV_FS2_SB_OC | DRV_FS2_SC_OC);
    return f;
}

void DRV8353::clearFaults()
{
    uint16_t dc = readReg(DRV_REG_DRIVER_CONTROL);
    transfer16_((uint16_t(DRV_REG_DRIVER_CONTROL) << DRV_ADDR_SHIFT)
                | ((dc | DRV_DC_CLR_FLT) & DRV_DATA_MASK));
    // CLR_FLT e' auto-clear: o proprio driver zera o bit.
}

// -------------------------------------------------- calibracao dos CSA
void DRV8353::calibrateCSA(uint16_t settle_us)
{
    uint16_t csa = readReg(DRV_REG_CSA_CONTROL);

    writeReg(DRV_REG_CSA_CONTROL, csa | DRV_CSA_CAL_A | DRV_CSA_CAL_B | DRV_CSA_CAL_C);
    delayMicroseconds(settle_us);
    writeReg(DRV_REG_CSA_CONTROL, csa);
    delayMicroseconds(settle_us);
}

// ------------------------------------------------------------ dump
void DRV8353::dumpRegisters(Stream &out)
{
    static const char *nomes[] = {
        "0x00 FAULT_ST1 ", "0x01 FAULT_ST2 ", "0x02 DRIVER_CTL",
        "0x03 GATE_HS   ", "0x04 GATE_LS   ", "0x05 OCP_CTL   ",
        "0x06 CSA_CTL   "
    };

    out.println(F("--- registradores DRV8353RS ---"));
    for (uint8_t a = 0; a <= 0x06; a++) {
        uint16_t v = readReg(a);
        out.print(nomes[a]);
        out.print(F(" = 0x"));
        if (v < 0x100) out.print('0');
        if (v < 0x010) out.print('0');
        out.print(v, HEX);
        out.print(F("  0b"));
        for (int8_t b = 10; b >= 0; b--) out.print((v >> b) & 1);
        out.println();
    }

    DrvFaults f = readFaults();
    out.print(F("nFAULT(pino) = ")); out.println(faultPinActive() ? F("ATIVO") : F("ok"));
    if (f.any) {
        out.print(F("FALHAS:"));
        if (f.vds_ocp)       out.print(F(" VDS_OCP"));
        if (f.gate_fault)    out.print(F(" GATE"));
        if (f.uvlo)          out.print(F(" UVLO"));
        if (f.overtemp)      out.print(F(" OTSD"));
        if (f.cp_uv)         out.print(F(" CPUV"));
        if (f.sense_oc)      out.print(F(" SENSE_OC"));
        out.println();
    }
    if (f.overtemp_warn) out.println(F("aviso: OTW (temperatura alta)"));
    out.println();
}
