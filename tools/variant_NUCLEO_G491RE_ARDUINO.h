/*
  Cópia de segurança. O original é instalado por tools/install_variant.py em:

    ~/.platformio/packages/framework-arduinoststm32/variants/STM32G4xx/
      G491RC(I-T)_G491RE(I-T-Y)x(Z)_G4A1RE(I-T-Y)/

  O PlatformIO deriva o nome do header de variante a partir do ID do board
  (nucleo_g491re_arduino -> NUCLEO_G491RE_ARDUINO). A variante genérica só
  tem variant_generic.h, então o build falha sem este redirecionamento.

  Ele vive dentro do pacote do framework e some numa reinstalação ou
  atualização. Se o build quebrar com "variant_NUCLEO_G491RE_ARDUINO.h:
  No such file or directory", é isso.
*/
#include "variant_generic.h"
