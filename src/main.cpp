#include <Arduino.h>
#include <DRV8353.h>
#include "board_pins.h"

HardwareSerial Dbg(CONSOLE_RX, CONSOLE_TX);

// Inicialização do objeto do driver
DRV8353 drv({
    DRV_PINO_SCLK,
    DRV_PINO_NSCS,
    DRV_PINO_SDI,
    DRV_PINO_SDO,
    DRV_PINO_ENABLE,
    DRV_PINO_NFAULT
});

void setup()
{
    analogReadResolution(12);
    Dbg.begin(CONSOLE_BAUD);

    drv.iniciar();                                            // driver dormindo
    drv.escreverRegistrador(DRV_REG_DRIVER_CONTROL, 0x1480);   // 
    drv.escreverRegistrador(DRV_REG_GATE_DRIVE_HS,  0x1B44);   // 
    drv.escreverRegistrador(DRV_REG_GATE_DRIVE_LS,  0x2444);   // 
    drv.escreverRegistrador(DRV_REG_OCP_CONTROL,  0x2A65); 
    drv.escreverRegistrador(DRV_REG_CSA_CONTROL,  0x3280); 
    drv.escreverRegistrador(DRV_REG_DRIVER_CONFIG,  0x3800);
     
   
   
   
    if (!drv.acordar()) {                                     // escreve e confere tudo
        drv.mostrarFalhas(Dbg);                               // não ligue o PWM
        return;
    }

    else {
        
    }
    drv.mostrarRegistradores(Dbg);                            // lê e explica
}

void loop() {}