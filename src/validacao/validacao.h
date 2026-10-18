// Firmware de validação da placa: um menu escolhe o modo no boot.
// Compilado só no ambiente "validacao" (pio run -e validacao); o
// ambiente principal ignora esta pasta e usa o src/main.cpp.

#pragma once
#include <Arduino.h>
#include <DRV8353.h>
#include "board_pins.h"

// Sem MOSFETs soldados o driver declara GDF corretamente e sem parar.
// Ponha em 0 depois de soldar, senão você mascara uma falha de verdade.
#define PLACA_SEM_ESTAGIO_DE_POTENCIA   1

extern HardwareSerial Dbg;
extern DRV8353        drv;

void bancadaIniciar();
void bancadaLaco();
void hallsIniciar();
void hallsLaco();
void offsetsIniciar();
void offsetsLaco();
void pwmIniciar();
void pwmLaco();
