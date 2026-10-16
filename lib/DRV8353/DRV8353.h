// Biblioteca do DRV8353RS (gate driver trifásico da TI, SLVSDY6).
//
//   DRV8353   drv(pinos);
//   DrvConfig cfg;                    // nasce com os valores de reset do chip
//   cfg.ganho_csa = GanhoCsa::x40;    // muda só o que precisar
//   drv.begin();
//   drv.acordar(cfg);                 // escreve e confere os 6 registradores
//
// Transporte por bit-bang (ERRATA 001 da Ouroboros). Nenhuma função daqui
// pode ser chamada de dentro da malha de controle: uma transação custa
// ~130 us e o período de uma malha a 20 kHz é 50 us. O caminho rápido de
// falha é o nFAULT, que é um GPIO e pode ser EXTI.

#pragma once
#include <Arduino.h>
#include "DRV8353_regs.h"

struct DrvPinos {
    uint32_t sclk;
    uint32_t nscs;
    uint32_t sdi;      // entrada de dados do driver
    uint32_t sdo;      // saída open-drain do driver; pull-up na placa
    uint32_t enable;
    uint32_t nfault;
};

// Opções de cada campo, com os códigos do SLVSDY6 (seção 8.6.2). O nome
// diz o valor físico: IdriveP::mA300 é 300 mA de pico.

enum class ModoPwm : uint16_t { x6 = 0, x3 = 1, x1 = 2, Independente = 3 };

// Corrente de pico que carrega o gate (liga o MOSFET).
enum class IdriveP : uint16_t {
    mA50  = 0x1, mA100 = 0x2, mA150 = 0x3, mA300 = 0x4, mA350 = 0x5,
    mA400 = 0x6, mA450 = 0x7, mA550 = 0x8, mA600 = 0x9, mA650 = 0xA,
    mA700 = 0xB, mA850 = 0xC, mA900 = 0xD, mA950 = 0xE, mA1000 = 0xF
};

// Corrente de pico que descarrega o gate (desliga o MOSFET).
enum class IdriveN : uint16_t {
    mA100  = 0x1, mA200  = 0x2, mA300  = 0x3, mA600  = 0x4, mA700  = 0x5,
    mA800  = 0x6, mA900  = 0x7, mA1100 = 0x8, mA1200 = 0x9, mA1300 = 0xA,
    mA1400 = 0xB, mA1700 = 0xC, mA1800 = 0xD, mA1900 = 0xE, mA2000 = 0xF
};

// Quanto tempo a corrente de pico fica aplicada no gate.
enum class TDrive : uint16_t { ns500 = 0, ns1000 = 1, ns2000 = 2, ns4000 = 3 };

enum class TempoMorto : uint16_t { ns50 = 0, ns100 = 1, ns200 = 2, ns400 = 3 };

enum class ModoOcp : uint16_t {
    Trava     = 0,   // desliga e espera CLR_FLT ou pulso tRST
    Retry     = 1,   // desliga e religa sozinho após o tempo de retry
    SoReporta = 2,   // só sinaliza no nFAULT, não desliga nada
    Desligado = 3    // nem sinaliza
};

enum class DeglitchOcp : uint16_t { us1 = 0, us2 = 1, us4 = 2, us8 = 3 };

enum class TempoRetry : uint16_t { ms8 = 0, us50 = 1 };

// Limiar de VDS do MOSFET conduzindo que dispara a sobrecorrente.
enum class NivelVds : uint16_t {
    V0_06 = 0x0, V0_07 = 0x1, V0_08 = 0x2, V0_09 = 0x3,
    V0_10 = 0x4, V0_20 = 0x5, V0_30 = 0x6, V0_40 = 0x7,
    V0_50 = 0x8, V0_60 = 0x9, V0_70 = 0xA, V0_80 = 0xB,
    V0_90 = 0xC, V1_00 = 0xD, V1_50 = 0xE, V2_00 = 0xF
};

enum class GanhoCsa : uint16_t { x5 = 0, x10 = 1, x20 = 2, x40 = 3 };

// Limiar da sobrecorrente medida no shunt (tensão no shunt, antes do ganho).
enum class NivelSen : uint16_t { V0_25 = 0, V0_50 = 1, V0_75 = 2, V1_00 = 3 };

// Valores padrão = reset do chip. Gate drive e OCP não estão
// dimensionados: dimensione contra o SLVSDY6 e a carga de gate dos
// MOSFETs antes de ligar potência.
struct DrvConfig {
    // 0x02 DRIVER_CONTROL
    ModoPwm modo_pwm                = ModoPwm::x6;
    bool    pwm1x_assincrono        = false;  // só no modo x1
    bool    ocp_desliga_tres_bracos = false;  // false: só o braço em falha
    bool    otw_no_nfault           = false;
    // Máscaras de falha. Só em bancada: na placa sem estágio de potência
    // o GDF é declarado corretamente e sem parar. Em veículo, false.
    bool    mascarar_gdf            = false;
    bool    mascarar_gduv           = false;

    // 0x03 GATE_DRIVE_HS
    IdriveP idrivep_hs = IdriveP::mA1000;
    IdriveN idriven_hs = IdriveN::mA2000;

    // 0x04 GATE_DRIVE_LS
    IdriveP idrivep_ls = IdriveP::mA1000;
    IdriveN idriven_ls = IdriveN::mA2000;
    TDrive  tdrive     = TDrive::ns4000;
    bool    ocp_ciclo_a_ciclo = true;         // CBC; só vale com ModoOcp::Retry

    // 0x05 OCP_CONTROL
    TempoMorto  tempo_morto  = TempoMorto::ns100;
    ModoOcp     modo_ocp     = ModoOcp::Retry;
    NivelVds    nivel_vds    = NivelVds::V1_00;
    DeglitchOcp deglitch_ocp = DeglitchOcp::us4;
    TempoRetry  tempo_retry  = TempoRetry::ms8;

    // 0x06 CSA_CONTROL
    GanhoCsa ganho_csa         = GanhoCsa::x20;
    bool     csa_bidirecional  = true;    // saída repousa em VREF/2
    bool     csa_entrada_shx   = false;   // mede pelo RDS(on) do low-side, sem shunt
    bool     vds_ls_ref_snx    = false;   // VDS do low-side medido até SNx, não SPx
    NivelSen nivel_sen         = NivelSen::V1_00;
    bool     sen_ocp_desligado = false;

    // 0x07 DRIVER_CONFIG
    bool     autocalibracao    = false;   // calibração dos CSAs pelo próprio chip
};

struct DrvFalhas {
    uint16_t status1 = 0;
    uint16_t status2 = 0;
    bool     alguma  = false;
};

class DRV8353 {
public:
    explicit DRV8353(const DrvPinos& pinos) : p_(pinos) {}

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

    // Depois de um aplicar() que falhou: bit n ligado = registrador 0x0n
    // não ficou com o valor escrito.
    uint8_t registradoresDivergentes() const { return divergentes_; }

    // Desligue o PWM antes. O sono leva o chip a 20 uA e apaga os
    // registradores, por isso todo despertar reconfigura.
    void dormir();
    bool acordado() const { return acordado_; }

    // Pulso tRST: limpa falha travada sem dormir e sem perder a
    // configuração. Acima de 1 ms o chip dorme.
    void pulsoReset();

    bool      emFalha() const { return digitalRead(p_.nfault) == LOW; }
    DrvFalhas lerFalhas();
    void      limparFalha();
    void      imprimirFalhas(Print& saida, const DrvFalhas& f);

    void dump(Print& saida);
    // Lê a configuração do chip e imprime em unidades físicas.
    void imprimirConfig(Print& saida);
    bool barramentoOk();

    // Curto-circuita as entradas dos CSAs para medir offset com corrente
    // nula. Chamar com o motor parado.
    void inicioAutoZero();
    void fimAutoZero();

private:
    uint16_t transferir(uint16_t quadro);
    static uint16_t montar(const DrvConfig& c, uint8_t reg);

    static constexpr uint32_t ATRASO_US = 2;   // meia fase do SCLK
    const DrvPinos p_;
    bool    acordado_    = false;
    uint8_t divergentes_ = 0;
};
