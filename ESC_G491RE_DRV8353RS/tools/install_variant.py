# install_variant.py - [G491] instala o header de variante automaticamente
#
# Roda antes de cada build (extra_scripts = pre:...). Copia
# tools/variant_NUCLEO_G491RE_ARDUINO.h para dentro do pacote do framework,
# SE ele ainda nao estiver la'.
#
# Por que existe: o PlatformIO deriva o nome do header de variante a partir
# do ID do board (nucleo_g491re_arduino -> variant_NUCLEO_G491RE_ARDUINO.h).
# A variante generica do G491 so' tem variant_generic.h, e o build falha.
# O header precisa morar DENTRO do pacote, porque os fontes do framework sao
# compilados num ambiente que nao enxerga a pasta include/ do projeto.
#
# Colocar a copia a mao funciona, mas some em cada computador novo, em cada
# reinstalacao e em cada atualizacao do framework. Este script resolve isso.

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
