// Mapa de registradores do DRV8353RS. Referência: SLVSDY6, seção 8.6.
//
// Quadro de 16 bits: B15 = 1 para leitura, B14..B11 = endereço,
// B10..B0 = dado. Modo SPI 1 (CPOL=0, CPHA=1), nSCS alto >= 400 ns
// entre quadros, SDO open-drain com pull-up.

#pragma once
#include <stdint.h>

enum : uint8_t {
    DRV_REG_FAULT_STATUS_1 = 0x00,   // leitura
    DRV_REG_VGS_STATUS_2   = 0x01,   // leitura
    DRV_REG_DRIVER_CONTROL = 0x02,
    DRV_REG_GATE_DRIVE_HS  = 0x03,
    DRV_REG_GATE_DRIVE_LS  = 0x04,
    DRV_REG_OCP_CONTROL    = 0x05,
    DRV_REG_CSA_CONTROL    = 0x06,
    DRV_REG_DRIVER_CONFIG  = 0x07,
    DRV_NUM_REGS           = 8
};

#define DRV_BIT_LEITURA     0x8000u
#define DRV_MASCARA_DADO    0x07FFu
#define DRV_DESLOC_ENDER    11

static inline uint16_t drv_quadro_leitura(uint8_t reg) {
    return DRV_BIT_LEITURA | ((uint16_t)reg << DRV_DESLOC_ENDER);
}
static inline uint16_t drv_quadro_escrita(uint8_t reg, uint16_t dado) {
    return ((uint16_t)reg << DRV_DESLOC_ENDER) | (dado & DRV_MASCARA_DADO);
}

// Lidos do chip em bancada, não copiados de anotação. Servem de gabarito:
// o dump batendo com todos prova o barramento nos dois sentidos.
// 0x04 resetando em 0x7FF é o único caso em que "tudo em um" é correto.
static const uint16_t DRV_RESET[DRV_NUM_REGS] = {
    0x000, 0x000, 0x000, 0x3FF, 0x7FF, 0x16D, 0x283, 0x000
};

// 0x00 FAULT_STATUS_1
#define DRV_F1_FAULT        (1u << 10)   // OU de todas as falhas
#define DRV_F1_VDS_OCP      (1u <<  9)
#define DRV_F1_GDF          (1u <<  8)
#define DRV_F1_UVLO         (1u <<  7)
#define DRV_F1_OTSD         (1u <<  6)
#define DRV_F1_VDS_HA       (1u <<  5)
#define DRV_F1_VDS_LA       (1u <<  4)
#define DRV_F1_VDS_HB       (1u <<  3)
#define DRV_F1_VDS_LB       (1u <<  2)
#define DRV_F1_VDS_HC       (1u <<  1)
#define DRV_F1_VDS_LC       (1u <<  0)

// 0x01 VGS_STATUS_2
#define DRV_F2_SA_OC        (1u << 10)
#define DRV_F2_SB_OC        (1u <<  9)
#define DRV_F2_SC_OC        (1u <<  8)
#define DRV_F2_OTW          (1u <<  7)
#define DRV_F2_CPUV         (1u <<  6)
#define DRV_F2_VGS_HA       (1u <<  5)
#define DRV_F2_VGS_LA       (1u <<  4)
#define DRV_F2_VGS_HB       (1u <<  3)
#define DRV_F2_VGS_LB       (1u <<  2)
#define DRV_F2_VGS_HC       (1u <<  1)
#define DRV_F2_VGS_LC       (1u <<  0)

// 0x02 DRIVER_CONTROL
#define DRV_DC_OCP_ACT      (1u << 10)
#define DRV_DC_DIS_GDUV     (1u <<  9)
#define DRV_DC_DIS_GDF      (1u <<  8)
#define DRV_DC_OTW_REP      (1u <<  7)
#define DRV_DC_PWM_MODE_POS 5
#define DRV_DC_PWM_COM_1X   (1u <<  4)
#define DRV_DC_PWM_DIR_1X   (1u <<  3)
#define DRV_DC_COAST        (1u <<  2)
#define DRV_DC_BRAKE        (1u <<  1)
#define DRV_DC_CLR_FLT      (1u <<  0)   // auto-limpa após a escrita

enum : uint16_t { DRV_PWM_6X = 0, DRV_PWM_3X = 1, DRV_PWM_1X = 2, DRV_PWM_IND = 3 };
#define DRV_PWM_MODE(m)  ((uint16_t)(m) << DRV_DC_PWM_MODE_POS)

// 0x06 CSA_CONTROL. Reset 0x283 = VREF_DIV | ganho 20 V/V | SEN_LVL 1,0 V.
#define DRV_CSA_FET         (1u << 10)
#define DRV_CSA_VREF_DIV    (1u <<  9)   // 1 = saída repousa em VREF/2
#define DRV_CSA_LS_REF      (1u <<  8)
#define DRV_CSA_GAIN_POS    6
#define DRV_CSA_DIS_SEN     (1u <<  5)
#define DRV_CSA_CAL_A       (1u <<  4)   // curto-circuita entradas (auto-zero)
#define DRV_CSA_CAL_B       (1u <<  3)
#define DRV_CSA_CAL_C       (1u <<  2)
#define DRV_CSA_SEN_LVL_POS 0

enum : uint16_t { DRV_GANHO_5 = 0, DRV_GANHO_10 = 1, DRV_GANHO_20 = 2, DRV_GANHO_40 = 3 };
#define DRV_CSA_GAIN(g)  ((uint16_t)(g) << DRV_CSA_GAIN_POS)

// 0x03, 0x04 e 0x05 controlam IDRIVE, TDRIVE, tempo morto e proteção de
// sobrecorrente. Os campos não estão mapeados aqui porque o valor correto
// depende da carga de gate dos MOSFETs escolhidos. Ver SLVSDY6 e o README.
