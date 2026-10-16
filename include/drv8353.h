// Driver do DRV8353RS. Transporte por bit-bang (ver ERRATA 001 no README).
//
// Nenhuma função daqui pode ser chamada de dentro da malha de controle:
// uma transação custa ~130 us e o período de uma malha a 20 kHz é 50 us.
// O caminho rápido de falha é o nFAULT, que é um GPIO e pode ser EXTI.

#pragma once
#include <Arduino.h>
#include "board_pins.h"
#include "drv8353_regs.h"

struct DrvConfig {
    uint16_t pwm_mode      = DRV_PWM_6X;
    uint16_t csa_ganho     = DRV_GANHO_20;
    bool     csa_bidir     = true;    // saída repousa em VREF/2
    bool     otw_no_nfault = false;

    // Máscaras de falha. Só em bancada: na placa sem estágio de potência
    // o GDF é declarado corretamente e sem parar. Em veículo, false.
    bool     mascarar_gdf  = false;
    bool     mascarar_gduv = false;

    // Valores de reset. Dimensione contra o SLVSDY6 antes de ligar potência.
    uint16_t gate_drive_hs = 0x3FF;
    uint16_t gate_drive_ls = 0x7FF;
    uint16_t ocp_control   = 0x16D;
};

struct DrvFalhas {
    uint16_t status1 = 0;
    uint16_t status2 = 0;
    bool     alguma  = false;
};

class DRV8353 {
public:
    bool begin();

    uint16_t ler(uint8_t reg);
    void     escrever(uint8_t reg, uint16_t valor);
    bool     escreverEConferir(uint8_t reg, uint16_t valor);
    bool     aplicar(const DrvConfig& c);

    // Acorda, espera tWAKE e escreve a configuração conferindo cada
    // registrador. Leva ~3,1 ms. Retorna false se a conferência falhar —
    // e nesse caso o PWM não pode ser ligado, porque o 0x02 ainda está em
    // reset e o modo de PWM pode divergir do que o timer vai gerar.
    bool acordar(const DrvConfig& c);

    // Desligue o PWM antes. O sono leva o chip a 20 uA e apaga os
    // registradores, por isso todo despertar reconfigura.
    void dormir();
    bool acordado() const { return acordado_; }

    // Pulso tRST: limpa falha travada sem dormir e sem perder a
    // configuração. Acima de 1 ms o chip dorme.
    void pulsoReset();

    bool      emFalha() const { return digitalRead(DRV_PINO_NFAULT) == LOW; }
    DrvFalhas lerFalhas();
    void      limparFalha();
    void      imprimirFalhas(Print& saida, const DrvFalhas& f);

    void dump(Print& saida);
    bool barramentoOk();

    // Curto-circuita as entradas dos CSAs para medir offset com corrente
    // nula. Chamar com o motor parado.
    void inicioAutoZero();
    void fimAutoZero();

private:
    uint16_t transferir(uint16_t quadro);
    static constexpr uint32_t ATRASO_US = 2;   // meia fase do SCLK
    bool acordado_ = false;
};
