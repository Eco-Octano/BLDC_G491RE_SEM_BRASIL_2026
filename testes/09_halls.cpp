/* =====================================================================
   HALLS (M3)  -  leitura dos sensores com o motor girado à mão
   NUCLEO-G491RE + Artemis + Ouroboros          Eco Octano UFPR

   Como usar: copie este arquivo para src/main.cpp, grave, abra o monitor
   na porta da FTDI (115200) e gire a roda devagar com a mão. O driver
   fica dormindo o tempo todo: nada é energizado.

   O que confere:
   - só aparecem os 6 estados válidos de halls a 120 graus (nunca 000
     nem 111);
   - cada troca muda um bit só e anda um setor para o mesmo lado;
   - uma volta mecânica dá 30 trocas (5 pares de polos x 6 estados).

   Halls em coletor aberto com pull-up de 5 a 10 k na Artemis: INPUT.
   ===================================================================== */

#include <Arduino.h>
#include <DRV8353.h>
#include "board_pins.h"

HardwareSerial Dbg(CONSOLE_RX, CONSOLE_TX);

DRV8353 drv({ DRV_PINO_SCLK, DRV_PINO_NSCS, DRV_PINO_SDI,
              DRV_PINO_SDO,  DRV_PINO_ENABLE, DRV_PINO_NFAULT });

const int16_t TROCAS_POR_VOLTA = 5 * 6;   // pares de polos x estados

// Estado = A<<2 | B<<1 | C. Com halls a 120 graus os seis estados formam
// um ciclo em que cada passo muda um bit; trocar a ordem dos fios só
// inverte o sentido do ciclo, então a tabela vale para qualquer ligação.
const int8_t SETOR[8] = { -1, 4, 2, 3, 0, 5, 1, -1 };

volatile uint8_t  estado        = 0;
volatile int32_t  posicao       = 0;   // trocas somadas com sinal
volatile uint32_t trocas        = 0;
volatile uint32_t invalidos     = 0;   // 000 ou 111
volatile uint32_t pulos         = 0;   // troca que não andou um setor
volatile bool     mudou         = false;

static uint8_t lerHalls()
{
    return (digitalRead(PINO_HALL_A) << 2)
         | (digitalRead(PINO_HALL_B) << 1)
         |  digitalRead(PINO_HALL_C);
}

static void aoMudarHall()
{
    const uint8_t novo = lerHalls();
    if (novo == estado) return;            // repique na mesma borda

    const int8_t de   = SETOR[estado];
    const int8_t para = SETOR[novo];
    estado = novo;
    trocas++;
    mudou = true;

    if (para < 0) { invalidos++; return; }
    if (de < 0) return;                    // vindo de estado inválido

    const int8_t passo = (para - de + 6) % 6;
    if      (passo == 1) posicao++;
    else if (passo == 5) posicao--;
    else                 pulos++;          // perdeu borda ou ruído
}

static void imprimirBits(uint8_t e)
{
    Dbg.print((e >> 2) & 1); Dbg.print((e >> 1) & 1); Dbg.print(e & 1);
}

static void ajuda()
{
    Dbg.println(F("\n--------------- comandos ---------------"));
    Dbg.println(F("  z  zera a contagem (marque a roda antes)"));
    Dbg.println(F("  h  le os tres halls agora"));
    Dbg.println(F("  ?  esta ajuda"));
    Dbg.println(F("  Gire a roda: uma volta = 30 trocas."));
    Dbg.println(F("----------------------------------------"));
}

void setup()
{
    pinMode(PINO_LED_STATUS, OUTPUT);
    Dbg.begin(CONSOLE_BAUD);
    delay(50);
    Dbg.println(F("\n\n#############################################"));
    Dbg.println(F("#  ESC Eco Octano - teste dos halls (M3)"));
    Dbg.print  (F("#  compilado ")); Dbg.print(F(__DATE__));
    Dbg.print  (' ');                Dbg.println(F(__TIME__));
    Dbg.println(F("#############################################"));

    drv.iniciar();                         // ENABLE em 0: ponte desligada

    pinMode(PINO_HALL_A, INPUT);
    pinMode(PINO_HALL_B, INPUT);
    pinMode(PINO_HALL_C, INPUT);
    estado = lerHalls();
    attachInterrupt(digitalPinToInterrupt(PINO_HALL_A), aoMudarHall, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PINO_HALL_B), aoMudarHall, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PINO_HALL_C), aoMudarHall, CHANGE);

    Dbg.print(F("\n  estado inicial ABC = ")); imprimirBits(estado);
    if (SETOR[estado] < 0) Dbg.println(F("  << INVALIDO: confira pull-ups e +5 V dos halls"));
    else                   Dbg.println();
    ajuda();
}

void loop()
{
    static uint32_t t_led = 0, t_print = 0;

    if (millis() - t_led >= 500) {
        t_led = millis();
        digitalWrite(PINO_LED_STATUS, !digitalRead(PINO_LED_STATUS));
    }

    // Copia com interrupções desligadas para os campos ficarem coerentes.
    if (mudou && millis() - t_print >= 100) {
        t_print = millis();
        noInterrupts();
        const uint8_t  e   = estado;
        const int32_t  pos = posicao;
        const uint32_t tr  = trocas, inv = invalidos, pul = pulos;
        mudou = false;
        interrupts();

        Dbg.print(F("ABC=")); imprimirBits(e);
        Dbg.print(F("  setor=")); Dbg.print(SETOR[e]);
        Dbg.print(F("  posicao=")); Dbg.print(pos);
        Dbg.print(F("  voltas=")); Dbg.print(pos / (float)TROCAS_POR_VOLTA, 2);
        Dbg.print(F("  trocas=")); Dbg.print(tr);
        Dbg.print(F("  invalidos=")); Dbg.print(inv);
        Dbg.print(F("  pulos=")); Dbg.println(pul);
    }

    if (!Dbg.available()) return;
    switch (Dbg.read()) {
        case 'z':
            noInterrupts();
            posicao = 0; trocas = 0; invalidos = 0; pulos = 0;
            interrupts();
            Dbg.println(F("\n  contagem zerada."));
            break;
        case 'h': {
            const uint8_t e = lerHalls();
            Dbg.print(F("\n  ABC = ")); imprimirBits(e);
            Dbg.print(F("  setor ")); Dbg.println(SETOR[e]);
        } break;
        case '?': ajuda(); break;
        default:  break;
    }
}
