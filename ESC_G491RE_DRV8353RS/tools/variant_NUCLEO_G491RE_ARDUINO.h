/*
  [G491] COPIA DE SEGURANCA. O ORIGINAL VAI DENTRO DO PACOTE:

    ~/.platformio/packages/framework-arduinoststm32/variants/STM32G4xx/
      G491RC(I-T)_G491RE(I-T-Y)x(Z)_G4A1RE(I-T-Y)/
        variant_NUCLEO_G491RE_ARDUINO.h

  O PlatformIO deriva o nome do header de variante a partir do ID do board
  (nucleo_g491re_arduino -> NUCLEO_G491RE_ARDUINO). A variante generica so'
  tem variant_generic.h, entao o build falha. Este arquivo e' so' um
  redirecionamento.

  ATENCAO: ele vive dentro do pacote do framework e SOME numa reinstalacao
  ou atualizacao. Se o build quebrar com "variant_NUCLEO_G491RE_ARDUINO.h:
  No such file or directory", e' isto - copie de volta.
*/
#include "variant_generic.h"
