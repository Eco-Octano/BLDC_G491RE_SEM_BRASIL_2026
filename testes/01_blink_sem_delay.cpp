/*
  BLINK SEM delay() - teste de SysTick / clock.

  O delay() do Arduino depende do SysTick, que depende do clock estar
  configurado. Se a configuracao de clock nao fechar, o SysTick nunca
  incrementa e o delay() fica preso PARA SEMPRE - com o pino travado no
  ultimo nivel escrito.

  Sintoma correspondente: PA0 fixo em 3,3 V (pinMode ok, primeiro
  digitalWrite(HIGH) ok, delay travou).

  Este teste troca o delay() por um laco de contagem, que nao depende de
  temporizador nenhum.

    PISCA aqui, mas nao piscava com delay()  ->  problema de clock/SysTick
    NAO pisca nem aqui                        ->  problema anterior, no boot

  A frequencia sera' irregular e provavelmente rapida (dezenas de Hz).
  Com multimetro voce ve algo em torno de 1,6 V (a media). Se tiver
  osciloscopio, melhor.

  Colocar em src/main.cpp (NAO em test/, que o PlatformIO nao compila).
*/

#include <Arduino.h>

static volatile uint32_t dummy;

static void busyWait()
{
    // ~1 milhao de iteracoes. Sem depender de timer.
    for (volatile uint32_t i = 0; i < 1000000UL; i++) { dummy = i; }
}

void setup()
{
    pinMode(PA0, OUTPUT);
}

void loop()
{
    digitalWrite(PA0, HIGH);
    busyWait();
    digitalWrite(PA0, LOW);
    busyWait();
}
