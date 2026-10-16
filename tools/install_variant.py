# Instala o header de variante dentro do pacote do framework antes de cada
# build. Necessário porque o PlatformIO deriva o nome do header do ID do
# board (nucleo_g491re_arduino -> variant_NUCLEO_G491RE_ARDUINO.h) e a
# variante genérica do G491 só tem variant_generic.h.
#
# O header precisa morar dentro do pacote: os fontes do framework são
# compilados num ambiente que não enxerga a pasta include/ do projeto.
# Copiar a mão funciona, mas some a cada reinstalação e a cada computador
# novo da equipe.

Import("env")
import os
import shutil

HEADER  = "variant_NUCLEO_G491RE_ARDUINO.h"
VARIANT = os.path.join("variants", "STM32G4xx",
                       "G491RC(I-T)_G491RE(I-T-Y)x(Z)_G4A1RE(I-T-Y)")

pkg = env.PioPlatform().get_package_dir("framework-arduinoststm32")
src = os.path.join(env.subst("$PROJECT_DIR"), "tools", HEADER)

if not pkg:
    print("[G491] framework-arduinoststm32 ainda nao instalado - rode o build de novo")
elif not os.path.isfile(src):
    print("[G491] ERRO: nao achei %s" % src)
else:
    dst_dir = os.path.join(pkg, VARIANT)
    dst     = os.path.join(dst_dir, HEADER)
    if not os.path.isdir(dst_dir):
        print("[G491] ERRO: pasta da variante nao existe: %s" % dst_dir)
        print("[G491] a versao do framework mudou o nome da pasta - confira")
    elif not os.path.isfile(dst):
        shutil.copyfile(src, dst)
        print("[G491] header de variante instalado em %s" % dst)
