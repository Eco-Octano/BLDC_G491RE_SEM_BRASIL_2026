// Modo 2: halls com a roda girada à mão, driver dormindo (antes testes/09).
// Esperado: só os seis estados válidos e 30 trocas por volta da roda.

#include "validacao.h"

static const int16_t TROCAS_POR_VOLTA = 5 * 6;   // pares de polos x estados

// Estado = A<<2 | B<<1 | C. Com halls a 120 graus os seis estados formam
// um ciclo em que cada passo muda um bit; trocar a ordem dos fios só
// inverte o sentido do ciclo, então a tabela vale para qualquer ligação.
static const int8_t SETOR[8] = { -1, 4, 2, 3, 0, 5, 1, -1 };

static volatile uint8_t  estado    = 0;
static volatile int32_t  posicao   = 0;
static volatile uint32_t trocas    = 0;
static volatile uint32_t invalidos = 0;   // 000 ou 111: no histórico, mau contato no cabo
static volatile uint32_t pulos     = 0;   // troca que não andou um setor
static volatile bool     mudou     = false;

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
    if (de < 0) return;

    const int8_t passo = (para - de + 6) % 6;
    if      (passo == 1) posicao++;
    else if (passo == 5) posicao--;
    else                 pulos++;
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
    Dbg.println(F("  Uma volta da roda = 30 trocas."));
    Dbg.println(F("----------------------------------------"));
}

void hallsIniciar()
{
    pinMode(PINO_HALL_A, INPUT);           // pull-up de 5 a 10 k na Artemis
    pinMode(PINO_HALL_B, INPUT);
    pinMode(PINO_HALL_C, INPUT);
    estado = lerHalls();
    attachInterrupt(digitalPinToInterrupt(PINO_HALL_A), aoMudarHall, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PINO_HALL_B), aoMudarHall, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PINO_HALL_C), aoMudarHall, CHANGE);

    Dbg.print(F("\n  estado inicial ABC = ")); imprimirBits(estado);
    if (SETOR[estado] < 0) Dbg.println(F("  << INVALIDO: confira cabo, pull-ups e +5 V dos halls"));
    else                   Dbg.println();
    ajuda();
}

void hallsLaco()
{
    static uint32_t t_print = 0;

    if (mudou && millis() - t_print >= 100) {
        t_print = millis();
        noInterrupts();
        const uint8_t  e   = estado;
        const int32_t  pos = posicao;
        const uint32_t tr  = trocas, inv = invalidos, pul = pulos;
        mudou = false;
        interrupts();

        Dbg.print(F("ABC=")); imprimirBits(e);
        Dbg.print(F("  setor="));     Dbg.print(SETOR[e]);
        Dbg.print(F("  posicao="));   Dbg.print(pos);
        Dbg.print(F("  voltas="));    Dbg.print(pos / (float)TROCAS_POR_VOLTA, 2);
        Dbg.print(F("  trocas="));    Dbg.print(tr);
        Dbg.print(F("  invalidos=")); Dbg.print(inv);
        Dbg.print(F("  pulos="));     Dbg.println(pul);
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
