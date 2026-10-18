// Modo 1: console da biblioteca lib/DRV8353 (antes testes/08).

#include "validacao.h"

static void ajuda()
{
    Dbg.println(F("\n--------------- comandos ---------------"));
    Dbg.println(F("  d  registradores: le e explica"));
    Dbg.println(F("  s  falhas: le e diz a causa"));
    Dbg.println(F("  c  reseta falhas por SPI (CLR_FLT)"));
    Dbg.println(F("  p  reseta falhas por pulso no ENABLE"));
    Dbg.println(F("  w  acorda (escreve e confere)"));
    Dbg.println(F("  z  dorme"));
    Dbg.println(F("  e  planar pelo pino ENABLE"));
    Dbg.println(F("  k  planar pelo registrador (COAST)"));
    Dbg.println(F("  v  retoma do planar"));
    Dbg.println(F("  r  30 ciclos dormir/acordar"));
    Dbg.println(F("  t  cronometra 100 leituras SPI"));
    Dbg.println(F("  ?  esta ajuda"));
    Dbg.println(F("----------------------------------------"));
}

static void resultado(const __FlashStringHelper* acao, bool ok)
{
    Dbg.print(F("\n  ")); Dbg.print(acao);
    Dbg.println(ok ? F(": OK") : F(": FALHOU"));
}

static void acordar()
{
    const bool ok = drv.acordar();
    resultado(F("acordar"), ok);
    if (!ok) Dbg.println(F("  >> NAO LIGUE O PWM. Veja 'd' e 's'."));
}

// O FALHOU intermitente era o tWAKE: só uma série longa prova a correção.
static void testeCiclos()
{
    const uint16_t N = 30;
    uint16_t falhas = 0;

    Dbg.println(F("\n=== CICLOS DORMIR/ACORDAR ==="));
    for (uint16_t i = 1; i <= N; i++) {
        drv.dormir();
        delay(5);
        if (!drv.acordar()) {
            falhas++;
            Dbg.print(F("  ciclo ")); Dbg.print(i); Dbg.println(F(": FALHOU"));
        }
    }
    Dbg.print(F("  ciclos: ")); Dbg.print(N);
    Dbg.print(F("  falhas: ")); Dbg.println(falhas);
}

static void cronometrar()
{
    const uint16_t N = 100;
    const uint32_t t0 = micros();
    for (uint16_t i = 0; i < N; i++) (void)drv.lerRegistrador(DRV_REG_DRIVER_CONTROL);
    const uint32_t dt = micros() - t0;

    Dbg.print(F("\n  ")); Dbg.print(N);
    Dbg.print(F(" leituras em ")); Dbg.print(dt);
    Dbg.print(F(" us -> "));      Dbg.print(dt / (float)N, 1);
    Dbg.println(F(" us por quadro"));
}

void bancadaIniciar()
{
#if PLACA_SEM_ESTAGIO_DE_POTENCIA
    drv.escreverRegistrador(DRV_REG_DRIVER_CONTROL, DRV_DC_DIS_GDF);
    Dbg.println(F("\n  [!] GDF desligado (placa sem estagio de potencia)."));
#endif
    acordar();
    drv.mostrarRegistradores(Dbg);
    ajuda();
}

void bancadaLaco()
{
    static uint32_t t_tick = 0, n = 0;

    // Abrir o monitor a qualquer momento mostra o estado.
    if (millis() - t_tick >= 2000) {
        t_tick = millis();
        Dbg.print(F("tick "));       Dbg.print(++n);
        Dbg.print(F("  ENABLE="));   Dbg.print(drv.acordado() ? '1' : '0');
        Dbg.print(F("  nFAULT="));   Dbg.print(drv.emFalha() ? '0' : '1');
        Dbg.print(F("  planando=")); Dbg.print(drv.planando() ? '1' : '0');
        Dbg.println(F("   ('?' para o menu)"));
    }

    if (!Dbg.available()) return;
    switch (Dbg.read()) {
        case 'd': drv.mostrarRegistradores(Dbg); break;
        case 's': drv.mostrarFalhas(Dbg); break;
        case 'c': resultado(F("reset por SPI"), drv.resetarFalhas(MetodoReset::Spi)); break;
        case 'p': resultado(F("reset por pulso"), drv.resetarFalhas(MetodoReset::PulsoEnable)); break;
        case 'w': acordar(); break;
        case 'z': drv.dormir(); Dbg.println(F("\n  dormindo.")); break;
        case 'e': resultado(F("planar por ENABLE"), drv.planar(ModoPlanar::PinoEnable)); break;
        case 'k': resultado(F("planar por COAST"), drv.planar(ModoPlanar::RegistradorCoast)); break;
        case 'v': resultado(F("retomar"), drv.retomar()); break;
        case 'r': testeCiclos(); break;
        case 't': cronometrar(); break;
        case '?': ajuda(); break;
        default:  break;
    }
}
