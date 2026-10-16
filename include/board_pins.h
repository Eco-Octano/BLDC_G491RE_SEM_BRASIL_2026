// Pinagem da Ouroboros + Artemis sobre NUCLEO-G491RE.
// Verificada com ponteira no pino do MCU e no pino do CI.
// Ver ERRATA 001 no README antes de mexer no bloco de SPI.

#pragma once
#include <Arduino.h>

// SPI do DRV8353RS. SDI e SDO estão invertidos nesta revisão da placa:
// PC11 (MISO) chega na entrada do driver e PC12 (MOSI) na saída.
// Por isso o transporte é bit-bang. Com a placa corrigida, troque os dois.
#define DRV_PINO_SCLK     PC10   // pino 31
#define DRV_PINO_NSCS     PC9    // pino 32
#define DRV_PINO_SDI      PC11   // pino 30  (errata)
#define DRV_PINO_SDO      PC12   // pino 29  (errata)

#define DRV_PINO_ENABLE   PD2    // pino 33
#define DRV_PINO_NFAULT   PB10   // pino 28, pull-up na Artemis via jumper J7

// Correntes de fase: CSAs internos do DRV (low-side) e amplificadores
// externos (inline). O projeto existe para comparar os dois.
#define PINO_LSCS_A       PA2    // pino 25, SOA
#define PINO_LSCS_B       PA6    // pino 24, SOB
#define PINO_LSCS_C       PB0    // pino 23, SOC

#define PINO_ILCS_A       PC0
#define PINO_ILCS_B       PA7
#define PINO_ILCS_C       PB13

#define PINO_VBAT         PC5
#define PINO_HSCS         PC4
#define PINO_LED_STATUS   PA0

// USART1. A USART2 não serve: o TX dela é PA2, que aqui é o LSCS_A.
#define CONSOLE_TX        PB6
#define CONSOLE_RX        PB7
#define CONSOLE_BAUD      115200
