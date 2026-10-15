#pragma once
/*
  drv8353.h - driver SPI do DRV8353RS

  Ponto central de projeto, e o que mais gera bug se for esquecido:

    O SPI so' responde com o ENABLE ALTO, e a configuracao dos registradores
    E' PERDIDA quando o ENABLE cai.

  Consequencia: applyConfig() nao pode viver so' no setup(). Toda vez que o
  driver e' rearmado - e em pulse-and-glide isso acontece o tempo todo - a
  configuracao precisa ser reescrita. Por isso enable() ja faz isso sozinho.

  Se voce esquecer, o driver volta em modo de reset: ganho de CSA errado
  (leitura de corrente com fator de escala errado), dead time de reset,
  OCP no padrao. O motor pode ate girar, e por isso o bug e' traicoeiro.
*/

#include <Arduino.h>
#include <SPI.h>
#include "drv8353_regs.h"

struct DrvFaults {
    uint16_t status1;
    uint16_t status2;
    bool any;          // o bit FAULT de 0x00
    bool vds_ocp;      // sobrecorrente
    bool gate_fault;   // falha de acionamento de gate
    bool uvlo;         // subtensao
    bool overtemp;     // desligamento termico
    bool overtemp_warn;
    bool cp_uv;        // subtensao da bomba de carga
    bool sense_oc;     // sobrecorrente detectada pelo shunt
};

// Valores de configuracao. Todos precisam ser decididos a partir dos
// parametros dos SEUS MOSFETs - ver os comentarios em drv8353.cpp.
struct DrvConfig {
    uint16_t pwm_mode;      // DRV_DC_PWM_MODE_6X
    uint16_t idrive_p_hs;   // codigo 0-15 da tabela de IDRIVE
    uint16_t idrive_n_hs;
    uint16_t idrive_p_ls;
    uint16_t idrive_n_ls;
    uint16_t tdrive;        // DRV_LS_TDRIVE_*
    uint16_t dead_time;     // DRV_OCP_DEADTIME_*
    uint16_t ocp_mode;      // DRV_OCP_MODE_*
    uint16_t ocp_deg;       // DRV_OCP_DEG_*
    uint16_t vds_lvl;       // codigo 0-15
    uint16_t csa_gain;      // DRV_CSA_GAIN_20
    uint16_t sen_lvl;       // DRV_CSA_SEN_LVL_*
    bool     bidirectional; // true -> VREF/2. OBRIGATORIO para FOC
};

class DRV8353 {
public:
    DRV8353(SPIClass &spi, uint8_t pin_cs, uint8_t pin_enable, uint8_t pin_nfault);

    // Configura os GPIOs e o barramento. NAO acorda o driver.
    void begin(const DrvConfig &cfg);

    // Sobe o ENABLE, espera o wake-up, aplica a configuracao e VERIFICA.
    // Retorna false se a verificacao falhar - nesse caso NAO acione o motor.
    bool enable();

    // Baixa o ENABLE. A configuracao no chip e' perdida (ver o topo do arquivo).
    void disable();

    bool isEnabled() const { return enabled_; }

    // Le os dois registradores de status e decodifica em campos nomeados.
    DrvFaults readFaults();

    // Le o nFAULT como NIVEL. A interrupcao por borda perde uma falha que
    // ocorra durante o wake-up - por isso o nivel tambem e' consultado.
    bool faultPinActive() const { return digitalRead(pin_nfault_) == LOW; }

    // Pulso no bit CLR_FLT. Nao resolve a causa: se a falha persistir,
    // o nFAULT volta a cair no ciclo seguinte.
    void clearFaults();

    // Curto-circuita a entrada dos CSA e os deixa auto-calibrar o offset.
    // O motor NAO pode estar acionado. So' faz sentido no modo low-side.
    void calibrateCSA(uint16_t settle_us = 200);

    uint16_t readReg(uint8_t addr);
    bool     writeReg(uint8_t addr, uint16_t data);   // escreve e RELE

    // Despeja os 7 registradores. Ferramenta de bancada - use sempre que
    // algo estiver estranho, antes de sair mudando codigo.
    void dumpRegisters(Stream &out);

private:
    uint16_t transfer16_(uint16_t frame);
    bool     applyConfig_();
    bool     verifyConfig_();

    SPIClass &spi_;
    uint8_t   pin_cs_, pin_enable_, pin_nfault_;
    DrvConfig cfg_;
    bool      enabled_ = false;

    // 1 MHz e' folgado para um barramento curto. O DRV aceita bem mais,
    // mas nao ha razao para correr: sao ~10 transacoes por rearme.
    SPISettings settings_{1000000, MSBFIRST, SPI_MODE1};
};
