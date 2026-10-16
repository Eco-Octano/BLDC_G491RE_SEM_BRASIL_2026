// Bring-up e validação do DRV8353RS. Não aciona motor.
// Console: USART1 a 115200, porta da FTDI. Ver README.

#include <Arduino.h>
#include "board_pins.h"
#include "drv8353.h"

// Sem MOSFETs soldados o driver declara GDF corretamente e sem parar.
// Ponha em 0 depois de soldar, senão você mascara uma falha de verdade.
#define PLACA_SEM_ESTAGIO_DE_POTENCIA   1

HardwareSerial Dbg(CONSOLE_RX, CONSOLE_TX);
DRV8353        drv;
DrvConfig      cfg;

static void ajuda()
{
    Dbg.println(F("\n--------------- comandos ---------------"));
    Dbg.println(F("  d  dump dos 8 registradores"));
    Dbg.println(F("  s  status de falha decodificado"));
    Dbg.println(F("  c  limpa falha por SPI (CLR_FLT)"));
    Dbg.println(F("  p  pulso tRST no ENABLE"));
    Dbg.println(F("  w  acorda e configura"));
    Dbg.println(F("  z  dorme (ENABLE = 0)"));
    Dbg.println(F("  t  cronometra 100 transacoes"));
    Dbg.println(F("  a  auto-zero dos CSAs"));
    Dbg.println(F("  r  30 ciclos dormir/acordar"));
    Dbg.println(F("  ?  esta ajuda"));
    Dbg.println(F("----------------------------------------"));
}

// Mede o custo do bit-bang nesta placa em vez de confiar na estimativa.
static void cronometrar()
{
    const uint16_t N = 100;
    const uint32_t t0 = micros();
    for (uint16_t i = 0; i < N; i++) (void)drv.ler(DRV_REG_DRIVER_CONTROL);
    const uint32_t dt = micros() - t0;

    Dbg.print(F("\n  ")); Dbg.print(N);
    Dbg.print(F(" transacoes em ")); Dbg.print(dt);
    Dbg.print(F(" us -> "));         Dbg.print(dt / (float)N, 1);
    Dbg.println(F(" us por quadro"));
    Dbg.print(F("  clock efetivo ~")); Dbg.print(16000.0f * N / dt, 0);
    Dbg.println(F(" kHz"));
}

static void autoZero()
{
    Dbg.println(F("\n=== AUTO-ZERO DOS CSAs ==="));
    if (!drv.acordado()) {
        Dbg.println(F("  driver dormindo. Aperte 'w' primeiro."));
        return;
    }

    drv.inicioAutoZero();
    delay(5);
    const int a = analogRead(PINO_LSCS_A);
    const int b = analogRead(PINO_LSCS_B);
    const int c = analogRead(PINO_LSCS_C);
    drv.fimAutoZero();

    const float k = 3.3f / 4095.0f;
    Dbg.print(F("  SOA = ")); Dbg.print(a * k, 3); Dbg.println(F(" V"));
    Dbg.print(F("  SOB = ")); Dbg.print(b * k, 3); Dbg.println(F(" V"));
    Dbg.print(F("  SOC = ")); Dbg.print(c * k, 3); Dbg.println(F(" V"));
    Dbg.println(F("  esperado ~1,65 V (VREF/2). Zero: ver README."));
}

// A falha de conferência era intermitente: só uma série longa prova a
// correção do tWAKE. Termina com o driver acordado e configurado.
static void testeCiclos()
{
    const uint16_t N = 30;
    uint16_t falhas = 0;

    Dbg.println(F("\n=== CICLOS DORMIR/ACORDAR ==="));
    for (uint16_t i = 1; i <= N; i++) {
        drv.dormir();
        delay(5);
        if (!drv.acordar(cfg)) {
            falhas++;
            Dbg.print(F("  ciclo ")); Dbg.print(i); Dbg.println(F(": FALHOU"));
        }
    }
    Dbg.print(F("  ciclos: ")); Dbg.print(N);
    Dbg.print(F("  falhas: ")); Dbg.println(falhas);
}

static void acordarEConfigurar()
{
    Dbg.println(F("\n=== ACORDAR E CONFIGURAR ==="));
    const bool ok = drv.acordar(cfg);
    Dbg.print(F("  conferencia: ")); Dbg.println(ok ? F("OK") : F("FALHOU"));
    if (!ok) Dbg.println(F("  >> NAO LIGUE O PWM."));

    const DrvFalhas f = drv.lerFalhas();
    drv.imprimirFalhas(Dbg, f);
}

void setup()
{
    pinMode(PINO_LED_STATUS, OUTPUT);
    // O analogRead do stm32duino devolve 10 bits por padrão; a conversão em
    // autoZero() usa 4095. Era isso que dava 0,41 V em vez de 1,65 V.
    analogReadResolution(12);

    Dbg.begin(CONSOLE_BAUD);
    delay(50);
    Dbg.println(F("\n\n#############################################"));
    Dbg.println(F("#  ESC Eco Octano - bring-up DRV8353RS"));
    Dbg.print  (F("#  compilado ")); Dbg.print(F(__DATE__));
    Dbg.print  (' ');                Dbg.println(F(__TIME__));
    Dbg.println(F("#############################################"));

    drv.begin();

#if PLACA_SEM_ESTAGIO_DE_POTENCIA
    cfg.mascarar_gdf = true;
    Dbg.println(F("\n  [!] GDF mascarado (placa sem estagio de potencia)."));
#endif

    acordarEConfigurar();
    drv.dump(Dbg);
    ajuda();
}

void loop()
{
    static uint32_t t_led = 0, t_tick = 0, n = 0;

    if (millis() - t_led >= 500) {
        t_led = millis();
        digitalWrite(PINO_LED_STATUS, !digitalRead(PINO_LED_STATUS));
    }

    // Tique periódico: abrir o monitor a qualquer momento mostra estado,
    // em vez de só o banner que já passou.
    if (millis() - t_tick >= 2000) {
        t_tick = millis();
        Dbg.print(F("tick "));      Dbg.print(++n);
        Dbg.print(F("  ENABLE="));  Dbg.print(drv.acordado() ? '1' : '0');
        Dbg.print(F("  nFAULT="));  Dbg.print(drv.emFalha() ? '0' : '1');
        Dbg.println(F("   ('?' para o menu)"));
    }

    if (!Dbg.available()) return;
    switch (Dbg.read()) {
        case 'd': drv.dump(Dbg); break;
        case 's': { const DrvFalhas f = drv.lerFalhas();
                    Dbg.println(F("\n=== STATUS DE FALHA ==="));
                    drv.imprimirFalhas(Dbg, f); } break;
        case 'c': drv.limparFalha(); Dbg.println(F("\n  CLR_FLT enviado.")); break;
        case 'p': drv.pulsoReset();  Dbg.println(F("\n  pulso tRST.")); break;
        case 'w': acordarEConfigurar(); break;
        case 'z': drv.dormir();      Dbg.println(F("\n  dormindo.")); break;
        case 't': cronometrar(); break;
        case 'a': autoZero(); break;
        case 'r': testeCiclos(); break;
        case '?': ajuda(); break;
        default:  break;
    }
}
