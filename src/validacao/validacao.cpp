#include "validacao.h"

// A automação de release injeta a tag; build local mostra "dev".
#ifndef VERSAO_FIRMWARE
#define VERSAO_FIRMWARE "dev"
#endif

HardwareSerial Dbg(CONSOLE_RX, CONSOLE_TX);

DRV8353 drv({ DRV_PINO_SCLK, DRV_PINO_NSCS, DRV_PINO_SDI,
              DRV_PINO_SDO,  DRV_PINO_ENABLE, DRV_PINO_NFAULT });

struct Modo {
    char        tecla;
    const char* nome;
    void      (*iniciar)();
    void      (*laco)();
};

static const Modo MODOS[] = {
    { '1', "bancada do driver: registradores, falhas, planar", bancadaIniciar, bancadaLaco },
    { '2', "halls: gire a roda a mao",                          hallsIniciar,   hallsLaco   },
    { '3', "offsets dos amplificadores de corrente",           offsetsIniciar, offsetsLaco },
    { '4', "PWM sem motor (DRV dormindo, 48 V desligado)",     pwmIniciar,     pwmLaco     },
};

static const Modo* modo = nullptr;

static void menu()
{
    Dbg.println(F("\n---------------- modos -----------------"));
    for (const Modo& m : MODOS) {
        Dbg.print(F("  ")); Dbg.print(m.tecla); Dbg.print(F("  ")); Dbg.println(m.nome);
    }
    Dbg.println(F("----------------------------------------"));
    Dbg.println(F("  Escolha o modo. Para trocar depois, aperte o reset da Nucleo."));
}

void setup()
{
    pinMode(PINO_LED_STATUS, OUTPUT);
    analogReadResolution(12);              // o stm32duino lê 10 bits por padrão

    Dbg.begin(CONSOLE_BAUD);
    delay(50);
    Dbg.println(F("\n\n#############################################"));
    Dbg.println(F("#  ESC Eco Octano - firmware de validacao"));
    Dbg.print  (F("#  versao "));    Dbg.println(F(VERSAO_FIRMWARE));
    Dbg.print  (F("#  compilado ")); Dbg.print(F(__DATE__));
    Dbg.print  (' ');                Dbg.println(F(__TIME__));
    Dbg.println(F("#############################################"));

    drv.iniciar();                         // driver dormindo até o modo pedir
    menu();
}

void loop()
{
    static uint32_t t_led = 0;
    // Pisca rápido esperando a escolha; devagar depois.
    if (millis() - t_led >= (modo ? 500u : 100u)) {
        t_led = millis();
        digitalWrite(PINO_LED_STATUS, !digitalRead(PINO_LED_STATUS));
    }

    if (modo) { modo->laco(); return; }

    if (!Dbg.available()) return;
    const char c = Dbg.read();
    for (const Modo& m : MODOS) {
        if (m.tecla != c) continue;
        modo = &m;
        Dbg.print(F("\n>>> modo ")); Dbg.print(m.tecla);
        Dbg.print(F(": "));          Dbg.println(m.nome);
        m.iniciar();
        return;
    }
    menu();
}
