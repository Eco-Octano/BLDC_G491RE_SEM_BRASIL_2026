/* =====================================================================
   PISO ABSOLUTO  -  NUCLEO-G491RE
   Eco Octano UFPR

   Programa mais simples possivel que ainda produz informacao. Nao usa
   serial, nao usa SPI, nao usa delay(), nao usa millis(), nao depende
   de interrupcao nenhuma. Pisca o PA0 com um laco de espera ocupada.

   POR QUE ASSIM: delay() e millis() dependem do SysTick, o SysTick
   depende do clock, e o clock e' exatamente o que esta' sob suspeita.
   Um blink feito com delay() nao distingue "clock errado" de "nucleo
   parado" - ele falha nos dois casos. Um laco de espera ocupada roda
   com QUALQUER clock: se o clock estiver em 16 MHz em vez de 170 MHz,
   o LED so' pisca mais devagar. Ele nao para.

   ---------------------------------------------------------------------
   COMO LER O RESULTADO (multimetro no PA0, ou LED com resistor)

   FASE 1 - piscadas de espera ocupada (primeiros ~20 s)
     oscilando ............. o nucleo esta' executando o seu codigo.
                             O problema esta' no clock/SysTick, nao no
                             boot. Siga para a fase 2.
     fixo em 0 ou 3,3 V .... o nucleo NAO chega aqui. O problema e'
                             antes do main(): SystemClock_Config
                             travando num Error_Handler, option bytes,
                             ou binario errado na flash. Nada de
                             aplicacao vai funcionar enquanto isso
                             nao for resolvido.

   FASE 2 - veredito sobre o SysTick (depois dos ~20 s)
     piscada LENTA (~1 s aceso, ~1 s apagado) ... millis() avanca.
             SysTick vivo. delay() deveria funcionar; se nao funciona,
             o problema e' outro e a gente investiga separado.
     piscada MUITO RAPIDA (quase continua) ...... millis() esta' PARADO.
             SysTick morto. E' a causa de delay() travar. Causa provavel:
             o clock configurado nao bate com o SystemCoreClock que o
             HAL usa para programar o SysTick, ou HAL_InitTick nao rodou.

   Deixe rodando um minuto inteiro antes de concluir: as duas fases
   precisam ser observadas.
   ===================================================================== */

#include <Arduino.h>

#define PIN_LED  PA0

/* Espera ocupada. 'volatile' e' obrigatorio: sem ele o -O2 apaga o laco
   inteiro, porque o compilador ve que o resultado nao e' usado. Esse e'
   um erro classico de codigo de bancada - o laco "desaparece" e o LED
   pisca em megahertz, invisivel ao olho e ao multimetro. */
static void esperaOcupada(uint32_t voltas)
{
    for (volatile uint32_t i = 0; i < voltas; i++) {
        __asm__ volatile("nop");
    }
}

// Calibrado por cima para 170 MHz; em 16 MHz fica ~10x mais lento, o
// que ainda e' perfeitamente visivel. Nao precisa ser exato.
static const uint32_t MEIO_SEGUNDO = 4000000UL;

void setup()
{
    pinMode(PIN_LED, OUTPUT);

    // ---------------- FASE 1: o nucleo esta' vivo? ----------------
    // 20 piscadas de espera ocupada. Se voce ver isto, o problema NAO
    // esta' antes do main().
    for (uint8_t n = 0; n < 20; n++) {
        digitalWrite(PIN_LED, HIGH);
        esperaOcupada(MEIO_SEGUNDO);
        digitalWrite(PIN_LED, LOW);
        esperaOcupada(MEIO_SEGUNDO);
    }

    // ---------------- FASE 2: o SysTick esta' vivo? ----------------
    // Le millis(), espera um tempo medido por espera ocupada (que nao
    // depende do SysTick), e le millis() de novo. Se nao andou, o
    // SysTick nao esta' gerando interrupcao.
    uint32_t antes = millis();
    esperaOcupada(MEIO_SEGUNDO * 4);     // alguns segundos de relogio real
    uint32_t depois = millis();

    if (depois == antes) {
        // SysTick MORTO. Pisca muito rapido, para sempre.
        // Este e' o caso que explica delay() travando.
        while (1) {
            digitalWrite(PIN_LED, HIGH);
            esperaOcupada(MEIO_SEGUNDO / 20);
            digitalWrite(PIN_LED, LOW);
            esperaOcupada(MEIO_SEGUNDO / 20);
        }
    }

    // SysTick vivo: cai no loop() e pisca devagar usando millis().
}

void loop()
{
    // Blink por millis(), sem delay(). Se chegou aqui e esta' piscando
    // devagar, o SysTick funciona e delay() tambem deveria.
    static uint32_t t = 0;
    if (millis() - t >= 1000) {
        t = millis();
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }
}
