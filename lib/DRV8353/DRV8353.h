// Biblioteca do DRV8353RS (gate driver trifásico da TI, SLVSDY6A).
//
//   DRV8353 drv({ DRV_PINO_SCLK, DRV_PINO_NSCS, DRV_PINO_SDI,
//                 DRV_PINO_SDO,  DRV_PINO_ENABLE, DRV_PINO_NFAULT });
//
//   drv.iniciar();                                          // driver dormindo
//   drv.escreverRegistrador(DRV_REG_GATE_DRIVE_HS, 0x344);  // guarda o valor
//   drv.acordar();                                          // escreve e confere
//   drv.mostrarRegistradores(Serial);                       // lê e explica
//
// Os valores passados ficam guardados na biblioteca: o sono apaga o chip,
// e todo acordar() os reescreve e confere. Sem nenhum valor passado, vale
// o reset do chip.
//
// Transporte por bit-bang (ERRATA 001 da Ouroboros). Uma transação custa
// ~130 us: nenhuma função daqui pode rodar dentro da malha de 20 kHz. O
// caminho rápido de falha é o nFAULT, como GPIO ou EXTI.

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

// Como o modo planar desliga a ponte. Os dois deixam os MOSFETs abertos.
enum class ModoPlanar : uint8_t {
    PinoEnable,        // ENABLE em 0: o chip dorme (~20 uA) e apaga os
                       // registradores; retomar() reescreve tudo (~3 ms)
    RegistradorCoast   // bit COAST: chip acordado (~10 mA), configuração
                       // mantida; retomar() só limpa o bit (~0,3 ms)
};

// Os dois métodos limpam falhas travadas sem perder a configuração.
enum class MetodoReset : uint8_t {
    Spi,               // bit CLR_FLT no 0x02
    PulsoEnable        // pulso tRST de 20 us no ENABLE; não depende do SPI
};

struct DrvFalhas {
    bool     valida  = false;   // false: driver dormindo, nada foi lido
    uint16_t status1 = 0;       // 0x00 cru
    uint16_t status2 = 0;       // 0x01 cru
    bool     alguma  = false;   // bit FAULT do 0x00

    bool sobrecorrente_vds    = false;   // VDS_OCP
    bool falha_gate           = false;   // GDF
    bool subtensao_vm         = false;   // UVLO
    bool desligamento_termico = false;   // OTSD
    bool aviso_temperatura    = false;   // OTW
    bool subtensao_gate       = false;   // GDUV
    bool sobrecorrente_shunt  = false;   // SA_OC, SB_OC ou SC_OC
};

class DRV8353 {
public:
    explicit DRV8353(const DrvPinos& pinos);

    // Pinos em estado seguro (nSCS alto, ENABLE baixo): driver dormindo.
    // Chame antes de qualquer outra função.
    void iniciar();

    // ---- acordar e dormir --------------------------------------------
    // Sobe o ENABLE, espera o tWAKE e escreve e confere os seis
    // registradores guardados. Leva ~3,1 ms. Retorna true só se todos
    // conferiram e o nFAULT está alto; senão, não ligue o PWM.
    bool acordar();
    // Desligue o PWM antes. O sono apaga os registradores do chip.
    void dormir();
    bool acordado() const { return acordado_; }

    // ---- passar valores ----------------------------------------------
    // Guarda o valor de um registrador de configuração (0x02 a 0x07) e,
    // com o driver acordado, escreve e confere. Dormindo, só guarda: vale
    // no próximo acordar(). Ajustes que o chip faria sozinho já vêm no
    // valor guardado: LOCK fica destravado, CSA_FET liga LS_REF, CLR_FLT
    // e CSA_CAL não são guardados e os bits reservados do 0x07 vão a 0.
    bool     escreverRegistrador(uint8_t reg, uint16_t valor);
    uint16_t valorConfigurado(uint8_t reg) const;
    // Bit n ligado = o registrador 0x0n não conferiu na última escrita.
    uint8_t  registradoresDivergentes() const { return divergentes_; }

    // ---- ler e explicar ----------------------------------------------
    uint16_t lerRegistrador(uint8_t reg);
    // Lê os oito registradores e explica cada campo e o modo resultante.
    // Dormindo, explica os valores guardados.
    void mostrarRegistradores(Print& saida);
    // Explica um valor sem tocar no chip: útil para conferir um hex antes
    // de escrevê-lo.
    static void explicarRegistrador(Print& saida, uint8_t reg, uint16_t valor);

    // ---- falhas ------------------------------------------------------
    bool      emFalha() const { return digitalRead(p_.nfault) == LOW; }
    DrvFalhas lerFalhas();
    // Lê as falhas e diz o que cada uma significa e o que costuma causá-la.
    void      mostrarFalhas(Print& saida);
    // Retorna true se o nFAULT subiu. Se a causa persistir, ele cai de novo.
    bool      resetarFalhas(MetodoReset metodo = MetodoReset::Spi);

    // ---- modo planar -------------------------------------------------
    // Abre todos os MOSFETs. Com PinoEnable, desligue o PWM antes.
    bool planar(ModoPlanar modo = ModoPlanar::PinoEnable);
    // Desfaz o planar pelo mesmo caminho. Retorna true se o driver ficou
    // pronto para o PWM; ligue o PWM só depois.
    bool retomar();
    bool planando() const { return planando_; }

    // ---- calibração dos CSAs -----------------------------------------
    // Curto-circuita as entradas dos amplificadores para medir o offset
    // com corrente nula. Motor parado; leia o ADC entre as duas chamadas.
    bool iniciarAutoZero();
    bool terminarAutoZero();

private:
    uint16_t transferir(uint16_t quadro);
    void     escrever(uint8_t reg, uint16_t valor);
    bool     escreverEConferir(uint8_t reg, uint16_t valor);
    bool     aplicarConfiguracao();
    uint16_t valorEfetivo(uint8_t reg) const;
    static uint16_t normalizar(uint8_t reg, uint16_t valor);

    static constexpr uint32_t ATRASO_US = 2;   // meia fase do SCLK
    const DrvPinos p_;
    uint16_t   config_[DRV_NUM_REGS];
    bool       acordado_    = false;
    bool       planando_    = false;
    ModoPlanar modo_planar_ = ModoPlanar::PinoEnable;
    uint8_t    divergentes_ = 0;
};
