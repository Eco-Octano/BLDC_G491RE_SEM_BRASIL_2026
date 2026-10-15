/*
  TESTE MINIMO - so' blink em PA0. Nada alem disso.

  Objetivo: separar "o nosso codigo trava" de "o ambiente nao roda".

  IMPORTANTE - ONDE COLOCAR:
    Este arquivo tem que ir em  src/main.cpp
    A pasta test/ do PlatformIO NAO e' compilada por 'pio run' - ela e'
    reservada para testes unitarios. Se o arquivo ficar la', o build
    continua usando o main.cpp completo e o teste nao prova nada.

    Tire o main.cpp completo de src/ (mova para a raiz como
    main_full.cpp.bak). Nao deixe os dois em src/, senao da' erro de
    setup()/loop() duplicados.

  COMO SABER QUE O BUILD PEGOU:
    Flash tem que cair para ~12-15 KB. O firmware completo da' ~52 KB.

  MEDIDA:
    PA0 = morpho CN7-28, ou "A0" no header Arduino (mais facil de achar).
    Deve alternar 0 / 3,3 V a cada 500 ms.

  POR QUE SO' PA0:
    A versao anterior deste teste tambem acionava PA5 (o LD2 da Nucleo),
    para dar um retorno visual. Isso causou HardFault: estamos usando a
    variante GENERICA do stm32duino, que nao traz o mapeamento de placa da
    Nucleo, e o pino pode nao resolver para uma entrada valida da tabela de
    pinos. digitalWrite num pino invalido desreferencia ponteiro nulo.

    Licao geral para o resto do projeto: com variante generica, so' use
    pinos que existam no PeripheralPins.c dela. Nada de LED_BUILTIN, D13,
    A0 e afins.
*/

#include <Arduino.h>

void setup()
{
    pinMode(PA0, OUTPUT);
}

void loop()
{
    digitalWrite(PA0, HIGH);
    delay(500);
    digitalWrite(PA0, LOW);
    delay(500);
}
