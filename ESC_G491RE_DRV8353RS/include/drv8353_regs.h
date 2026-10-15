#pragma once
/*
  drv8353_regs.h - mapa de registradores do DRV8353RS

  VERIFICADO campo a campo contra o datasheet DRV835x Rev A (SLVSDY6A),
  secao 8.6 Register Maps, tabelas 11, 14, 15, 17 e 18.

  Um bit errado aqui pode significar shoot-through: se o DEAD_TIME sair
  menor que o tempo de DESLIGAMENTO dos MOSFETs, os dois FETs do mesmo
  braco conduzem juntos e voce curto-circuita 42 V. E atencao ao contra-
  intuitivo: corrente de gate BAIXA nao e' o lado seguro - ela ALONGA o
  desligamento e pode faze-lo exceder o tempo morto.

  ------------------------------------------------------------------
  FORMATO DO QUADRO SPI (16 bits, MSB primeiro)
  ------------------------------------------------------------------
    bit 15     : 1 = leitura, 0 = escrita
    bits 14-11 : endereco (4 bits)
    bits 10-0  : dados (11 bits)

  Na leitura, os 11 bits de dados enviados sao ignorados pelo driver, e a
  resposta vem no MISO durante o mesmo quadro.

  MODO SPI: CPOL = 0, CPHA = 1  (SPI_MODE1)
    O DRV captura o SDI na borda de SUBIDA do SCLK e propaga o SDO na
    borda de DESCIDA. CONFERIR no diagrama de temporizacao do datasheet -
    se estiver errado voce le 0x0000 ou 0xFFFF em tudo.
*/

#include <stdint.h>

// ===================================================== enderecos (bits 14-11)
#define DRV_REG_FAULT_STATUS_1  0x00   // somente leitura
#define DRV_REG_FAULT_STATUS_2  0x01   // somente leitura
#define DRV_REG_DRIVER_CONTROL  0x02
#define DRV_REG_GATE_DRIVE_HS   0x03
#define DRV_REG_GATE_DRIVE_LS   0x04
#define DRV_REG_OCP_CONTROL     0x05
#define DRV_REG_CSA_CONTROL     0x06

#define DRV_FRAME_READ          0x8000
#define DRV_ADDR_SHIFT          11
#define DRV_DATA_MASK           0x07FF   // 11 bits

// ================================================ 0x00 - Fault Status 1 (RO)
#define DRV_FS1_FAULT      (1u << 10)  // OR de todas as falhas (espelha o nFAULT)
#define DRV_FS1_VDS_OCP    (1u <<  9)  // sobrecorrente por VDS
#define DRV_FS1_GDF        (1u <<  8)  // falha de gate drive
#define DRV_FS1_UVLO       (1u <<  7)  // subtensao de alimentacao
#define DRV_FS1_OTSD       (1u <<  6)  // desligamento por temperatura
#define DRV_FS1_VDS_HA     (1u <<  5)  // VDS: high-side fase A
#define DRV_FS1_VDS_LA     (1u <<  4)  // VDS: low-side  fase A
#define DRV_FS1_VDS_HB     (1u <<  3)
#define DRV_FS1_VDS_LB     (1u <<  2)
#define DRV_FS1_VDS_HC     (1u <<  1)
#define DRV_FS1_VDS_LC     (1u <<  0)

// ================================================ 0x01 - Fault Status 2 (RO)
#define DRV_FS2_SA_OC      (1u << 10)  // sobrecorrente no amplificador A
#define DRV_FS2_SB_OC      (1u <<  9)
#define DRV_FS2_SC_OC      (1u <<  8)
#define DRV_FS2_OTW        (1u <<  7)  // aviso de temperatura (nao desliga)
#define DRV_FS2_CPUV       (1u <<  6)  // subtensao da bomba de carga
#define DRV_FS2_VGS_HA     (1u <<  5)  // VGS: high-side fase A
#define DRV_FS2_VGS_LA     (1u <<  4)
#define DRV_FS2_VGS_HB     (1u <<  3)
#define DRV_FS2_VGS_LB     (1u <<  2)
#define DRV_FS2_VGS_HC     (1u <<  1)
#define DRV_FS2_VGS_LC     (1u <<  0)

// ============================================== 0x02 - Driver Control (RW)
#define DRV_DC_OCP_ACT     (1u << 10)  // 0 = so' o meio-braco em falha desliga
#define DRV_DC_DIS_GDUV    (1u <<  9)  // 1 = desabilita falha de UV do gate drive
#define DRV_DC_DIS_GDF     (1u <<  8)  // 1 = desabilita falha de gate drive
#define DRV_DC_OTW_REP     (1u <<  7)  // 1 = OTW tambem reporta no nFAULT
#define DRV_DC_PWM_MODE_SHIFT   5      // bits 6-5
#define DRV_DC_PWM_MODE_MASK    (0x3u << DRV_DC_PWM_MODE_SHIFT)
#define DRV_DC_PWM_MODE_6X      (0x0u << DRV_DC_PWM_MODE_SHIFT)  // <-- o nosso
#define DRV_DC_PWM_MODE_3X      (0x1u << DRV_DC_PWM_MODE_SHIFT)
#define DRV_DC_PWM_MODE_1X      (0x2u << DRV_DC_PWM_MODE_SHIFT)
#define DRV_DC_PWM_MODE_INDEP   (0x3u << DRV_DC_PWM_MODE_SHIFT)
#define DRV_DC_1PWM_COM   (1u <<  4)  // so' vale em modo 1x
#define DRV_DC_1PWM_DIR   (1u <<  3)  // so' vale em modo 1x
#define DRV_DC_COAST      (1u <<  2)  // 1 = todos os FETs em alta impedancia
#define DRV_DC_BRAKE      (1u <<  1)  // 1 = liga os tres low-side (frenagem)
#define DRV_DC_CLR_FLT    (1u <<  0)  // 1 = limpa as falhas (auto-clear)

// ============================ 0x03 - Gate Drive HS (RW)  /  0x04 - LS (RW)
// LOCK fica em 0x03, bits 10-8:
//   0b110 = trava os registradores   0b011 = destrava
// Escrever LOCK por engano te deixa sem conseguir configurar nada ate
// destravar. Comece SEMPRE mandando o unlock.
#define DRV_LOCK_SHIFT     8
#define DRV_LOCK_MASK      (0x7u << DRV_LOCK_SHIFT)
#define DRV_LOCK_LOCK      (0x6u << DRV_LOCK_SHIFT)
#define DRV_LOCK_UNLOCK    (0x3u << DRV_LOCK_SHIFT)

// IDRIVE: corrente de acionamento do gate. Bits 7-4 = fonte (P),
// bits 3-0 = dreno (N). Os codigos NAO sao lineares - a tabela do
// datasheet lista cada um. CONFERIR os valores abaixo na tabela.
#define DRV_IDRIVEP_SHIFT  4
#define DRV_IDRIVEN_SHIFT  0

// 0x04 tem, alem de IDRIVE:
#define DRV_LS_CBC        (1u << 10)  // 1 = falha de OCP limpa a cada ciclo de PWM
#define DRV_LS_TDRIVE_SHIFT     8     // bits 9-8: duracao do pico de corrente de gate
#define DRV_LS_TDRIVE_MASK      (0x3u << DRV_LS_TDRIVE_SHIFT)
#define DRV_LS_TDRIVE_500NS     (0x0u << DRV_LS_TDRIVE_SHIFT)
#define DRV_LS_TDRIVE_1000NS    (0x1u << DRV_LS_TDRIVE_SHIFT)
#define DRV_LS_TDRIVE_2000NS    (0x2u << DRV_LS_TDRIVE_SHIFT)
#define DRV_LS_TDRIVE_4000NS    (0x3u << DRV_LS_TDRIVE_SHIFT)

// ================================================ 0x05 - OCP Control (RW)
#define DRV_OCP_TRETRY    (1u << 10)  // tempo de retentativa: 0 = 8 ms, 1 = 50 us
#define DRV_OCP_DEADTIME_SHIFT  8     // bits 9-8
#define DRV_OCP_DEADTIME_MASK   (0x3u << DRV_OCP_DEADTIME_SHIFT)
#define DRV_OCP_DEADTIME_50NS   (0x0u << DRV_OCP_DEADTIME_SHIFT)
#define DRV_OCP_DEADTIME_100NS  (0x1u << DRV_OCP_DEADTIME_SHIFT)
#define DRV_OCP_DEADTIME_200NS  (0x2u << DRV_OCP_DEADTIME_SHIFT)
#define DRV_OCP_DEADTIME_400NS  (0x3u << DRV_OCP_DEADTIME_SHIFT)

#define DRV_OCP_MODE_SHIFT      6     // bits 7-6
#define DRV_OCP_MODE_MASK       (0x3u << DRV_OCP_MODE_SHIFT)
#define DRV_OCP_MODE_LATCH      (0x0u << DRV_OCP_MODE_SHIFT)  // trava ate CLR_FLT
#define DRV_OCP_MODE_RETRY      (0x1u << DRV_OCP_MODE_SHIFT)  // retenta sozinho
#define DRV_OCP_MODE_REPORT     (0x2u << DRV_OCP_MODE_SHIFT)  // so' reporta, nao age
#define DRV_OCP_MODE_DISABLED   (0x3u << DRV_OCP_MODE_SHIFT)  // NUNCA use isto

#define DRV_OCP_DEG_SHIFT       4     // bits 5-4: tempo de deglitch
#define DRV_OCP_DEG_MASK        (0x3u << DRV_OCP_DEG_SHIFT)
#define DRV_OCP_DEG_1US         (0x0u << DRV_OCP_DEG_SHIFT)
#define DRV_OCP_DEG_2US         (0x1u << DRV_OCP_DEG_SHIFT)
#define DRV_OCP_DEG_4US         (0x2u << DRV_OCP_DEG_SHIFT)
#define DRV_OCP_DEG_8US         (0x3u << DRV_OCP_DEG_SHIFT)

// VDS_LVL: bits 3-0. Limiar de tensao dreno-fonte que dispara a OCP.
// A corrente de disparo e' VDS_LVL / Rds(on) NA TEMPERATURA DE OPERACAO.
// O Rds(on) de datasheet e' a 25 C e e' otimista - a quente pode dobrar,
// o que faz a protecao disparar na METADE da corrente que voce calculou.
#define DRV_VDS_LVL_SHIFT       0
#define DRV_VDS_LVL_MASK        0x0Fu

// ================================================ 0x06 - CSA Control (RW)
#define DRV_CSA_FET       (1u << 10)  // 0 = mede pelo shunt (SPx-SNx)  <-- o nosso
                                      // 1 = mede pelo Vds do FET
#define DRV_CSA_VREF_DIV  (1u <<  9)  // 1 = referencia em VREF/2 -> BIDIRECIONAL
                                      // ===> ESTE BIT E' OBRIGATORIO PARA FOC <===
#define DRV_CSA_LS_REF    (1u <<  8)  // 0 = OCP mede SHx-SPx, 1 = SHx-SNx

#define DRV_CSA_GAIN_SHIFT      6     // bits 7-6
#define DRV_CSA_GAIN_MASK       (0x3u << DRV_CSA_GAIN_SHIFT)
#define DRV_CSA_GAIN_5          (0x0u << DRV_CSA_GAIN_SHIFT)
#define DRV_CSA_GAIN_10         (0x1u << DRV_CSA_GAIN_SHIFT)
#define DRV_CSA_GAIN_20         (0x2u << DRV_CSA_GAIN_SHIFT)  // <-- o nosso
#define DRV_CSA_GAIN_40         (0x3u << DRV_CSA_GAIN_SHIFT)

#define DRV_CSA_DIS_SEN   (1u <<  5)  // 1 = desabilita a OCP por shunt
#define DRV_CSA_CAL_A     (1u <<  4)  // 1 = curto-circuita a entrada do CSA A
#define DRV_CSA_CAL_B     (1u <<  3)  //     para auto-calibrar o offset
#define DRV_CSA_CAL_C     (1u <<  2)

#define DRV_CSA_SEN_LVL_SHIFT   0     // bits 1-0: limiar de OCP pelo shunt
#define DRV_CSA_SEN_LVL_MASK    0x3u
#define DRV_CSA_SEN_LVL_025V    0x0u
#define DRV_CSA_SEN_LVL_050V    0x1u
#define DRV_CSA_SEN_LVL_075V    0x2u
#define DRV_CSA_SEN_LVL_100V    0x3u
// Com shunt de 3 mOhm: 0,25 V -> 83 A | 0,5 V -> 167 A | 1,0 V -> 333 A.
// Com 0,25 V esta e' a protecao PRIMARIA e e' boa: bem acima dos 19 A de
// operacao, bem abaixo dos 200 A do encapsulamento, e estavel com a
// temperatura (o shunt nao deriva como o Rds(on)). O limite de OPERACAO
// continua sendo o current_limit em software.

// ------------------------------------------- VALORES DE RESET (datasheet)
//   0x02 = 0x000  -> PWM_MODE 6x  (ja' e' o que queremos)
//   0x03 = 0x3FF  -> LOCK destravado, IDRIVEP 1000 mA, IDRIVEN 2000 mA
//   0x04 = 0x7FF
//   0x05 = 0x15D  -> dead time 100 ns, OCP retry, deglitch 4 us, VDS 1 V
//   0x06 = 0x283  -> VREF_DIV 1, CSA_GAIN 20 V/V, SEN_LVL 1 V
// Util no bring-up: se o dump mostrar EXATAMENTE estes valores, o
// applyConfig() nao rodou (ou o ENABLE caiu e levou a configuracao junto).
