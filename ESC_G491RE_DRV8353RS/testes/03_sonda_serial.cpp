/* =====================================================================
   SONDA DE SERIAL  -  NUCLEO-G491RE
   Eco Octano UFPR

   Nao faz SPI, nao toca no DRV. Responde UMA pergunta: por onde sai a
   serial nesta placa, com estes jumpers?

   ESTRATEGIA: transmite o MESMO texto em duas UARTs ao mesmo tempo.
   Voce nao precisa adivinhar qual esta' ligada ao conversor - abre o
   monitor e ve qual das duas aparece. O texto diz qual e'.

     USART2 -> PA2 (TX) / PA3 (RX)      <- CN10-35 / CN10-37
     USART1 -> PB6 (TX) / PB7 (RX)

   O LED (PA0) conta em que ponto do setup() o firmware chegou. Isso
   separa "firmware travou" de "firmware roda mas a UART esta' errada",
   que e' a duvida que importa agora:

     1 piscada longa por ciclo .. chegou ao fim do setup(), esta' no loop
     piscada rapida continua ..... travou DEPOIS de configurar o LED e
                                   ANTES do fim do setup (nao deve
                                   acontecer: nao ha nada que trave aqui)
     apagado ou aceso fixo ....... nao chegou nem a configurar o LED:
                                   problema de clock, de boot ou de
                                   binario errado na flash

   COMO USAR
     1. grava
     2. abre o monitor em 115200 na COM do conversor USB-serial
     3. aperta o NRST (botao preto) com o monitor JA' aberto
     4. se nada aparecer, troca o conversor para o outro par de pinos
   ===================================================================== */

#include <Arduino.h>

#define PIN_LED  PA0

// As duas UARTs, instanciadas pelos pinos. Ambas comecam no setup.
HardwareSerial S2(PA3, PA2);   // RX, TX -> USART2
HardwareSerial S1(PB7, PB6);   // RX, TX -> USART1

static uint32_t t_led  = 0;
static uint32_t t_msg  = 0;
static uint32_t ciclo  = 0;

static void anunciar(HardwareSerial &s, const char *quem)
{
    s.println();
    s.println(F("---------------------------------------------"));
    s.print  (F("SONDA DE SERIAL  -  saindo por "));
    s.println(quem);
    s.print  (F("compilado "));
    s.print(F(__DATE__)); s.print(' '); s.println(F(__TIME__));
    s.print  (F("SystemCoreClock = "));
    s.print(SystemCoreClock / 1000000UL);
    s.println(F(" MHz"));
    // O clock importa: se o SystemClock_Config nao rodou como esperado,
    // o divisor de baud sai errado e voce ve lixo em vez de texto - ou
    // nada. Imprimir a frequencia confirma de uma vez que o clock_config
    // esta' no projeto e pegou.
    s.println(F("---------------------------------------------"));
}

void setup()
{
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);   // aceso durante o setup

    S2.begin(115200);
    S1.begin(115200);
    delay(300);

    anunciar(S2, "USART2  (PA2 TX / PA3 RX)  = CN10-35 / CN10-37");
    anunciar(S1, "USART1  (PB6 TX / PB7 RX)");

    digitalWrite(PIN_LED, LOW);    // fim do setup: comeca a piscar 1 Hz
}

void loop()
{
    if (millis() - t_led >= 500) {
        t_led = millis();
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }

    // Repete a cada segundo, para voce poder abrir o monitor depois do
    // boot e ainda assim ver algo sem precisar resetar.
    if (millis() - t_msg >= 1000) {
        t_msg = millis();
        ciclo++;

        S2.print(F("USART2 vivo, ciclo "));  S2.println(ciclo);
        S1.print(F("USART1 vivo, ciclo "));  S1.println(ciclo);
    }
}
