/* =====================================================================
   ESTIMULO DE CICLO CONHECIDO  -  teste de continuidade com MULTIMETRO
   NUCLEO-G491RE + Artemis + Ouroboros          Eco Octano UFPR

   O PROBLEMA QUE ISTO RESOLVE
   Ja' sabemos que o MCU gera as transacoes (SPI de hardware e bit-bang
   deram o mesmo resultado) e que o MISO fica preso em 1. Falta saber se
   SCLK, SDI e nSCS CHEGAM ao pino do driver. Isso normalmente pede
   osciloscopio: uma transacao de 16 bits dura microssegundos e um
   multimetro nao ve nada.

   A IDEIA
   Em vez de uma transacao isolada, gerar um padrao PERIODICO com ciclo
   de trabalho exato. A media DC de uma onda quadrada e' Vcc x duty, e
   media DC e' justamente o que o multimetro mede. A frequencia nao
   importa para a media - so' o duty - entao o resultado e' robusto.

   O padrao (periodo ~640 us, ~1,5 kHz):
     FASE A (320 us): nSCS BAIXO, 16 ciclos de SCLK, SDI com 0xAAAA
     FASE B (320 us): nSCS ALTO,  SCLK baixo, SDI baixo

   MEDIAS ESPERADAS (multimetro em DC, VDD = 3,3 V):

     nSCS  ..... 1,65 V      (metade do tempo em baixo)
     SCLK  ..... 0,83 V      (alto 1/4 do tempo: metade de A, A = metade)
     SDI   ..... 0,83 V      (0xAAAA = 8 uns em 16 bits, durante A)
     SDO   ..... 3,3 V       se o driver NUNCA responde
                 < 3,3 V     se ele puxa a linha em algum bit  <-- queremos isto

   ---------------------------------------------------------------------
   PROCEDIMENTO  (o ponto todo e' medir nos DOIS extremos de cada linha)

   Para cada sinal, meca primeiro no pino da Nucleo e depois no pino
   correspondente do DRV8353RS (48 pinos):

     sinal   Nucleo    DRV8353RS      esperado nos dois
     -----   ------    ---------      -----------------
     nSCS    PC9       pino 32        1,65 V
     SCLK    PC10      pino 31        0,83 V
     SDI     PC12      pino 30        0,83 V
     SDO     PC11      pino 29        3,3 V (ou menos, se responder)

   COMO INTERPRETAR

     Nucleo certo + DRV certo .......... o sinal chega. Linha ok.
     Nucleo certo + DRV em 0 ou 3,3 V .. TRILHA ABERTA ou solda fria
                                         entre a Artemis e o pino do DRV.
                                         Achamos o problema.
     Nucleo errado ..................... o pino nao esta' sendo acionado;
                                         conflito de funcao no MCU.

   MEDIR TAMBEM, no mesmo momento:
     DVDD = pino 40 contra DGND = pino 41  ->  deve dar 5,0 V
            (sao adjacentes; o capacitor de 1 uF entre eles e' o
             melhor ponto de prova)
     VGLS = pino 2   ->  ~11 V com VM = 42 V
     VCP  = pino 7   ->  VDRAIN + ~11 V

   Se DVDD estiver em 0 V, pare aqui: o bloco digital do driver esta'
   desligado, o SPI dele nao existe, e nenhuma medida de SCLK/SDI muda
   essa conclusao.
   ---------------------------------------------------------------------
   O PA0 pisca ~2 Hz. Se parar de piscar, o firmware travou e as medidas
   nao valem mais.
   ===================================================================== */

#include <Arduino.h>

#define DRV_SCK      PC10
#define DRV_MISO     PC11
#define DRV_MOSI     PC12
#define DRV_NSCS     PC9
#define PIN_ENABLE   PD2
#define PIN_NFAULT   PB10
#define PIN_LED      PA0

// Meio periodo de SCLK. 10 us -> SCLK ~50 kHz, periodo total ~640 us.
// Lento de proposito: nenhuma conclusao aqui pode ser atribuida a
// integridade de sinal, reflexao ou capacitancia de trilha.
static const uint32_t MEIO_CLOCK_US = 10;

static const uint16_t PADRAO = 0xAAAA;   // 8 uns em 16 bits -> duty 50%

void setup()
{
    pinMode(PIN_LED, OUTPUT);

    pinMode(DRV_NSCS, OUTPUT);
    digitalWrite(DRV_NSCS, HIGH);

    pinMode(DRV_SCK,  OUTPUT);
    digitalWrite(DRV_SCK, LOW);

    pinMode(DRV_MOSI, OUTPUT);
    digitalWrite(DRV_MOSI, LOW);

    pinMode(DRV_MISO, INPUT);        // pull-up externo de 10k na Artemis
    pinMode(PIN_NFAULT, INPUT);

    // ENABLE alto e MANTIDO alto: sem ele o driver dorme e nao ha' nada
    // para medir do lado dele.
    pinMode(PIN_ENABLE, OUTPUT);
    digitalWrite(PIN_ENABLE, HIGH);
}

void loop()
{
    // ----------------------------- FASE A: transacao
    digitalWrite(DRV_NSCS, LOW);

    for (int8_t i = 15; i >= 0; i--) {
        digitalWrite(DRV_MOSI, (PADRAO >> i) & 1);
        digitalWrite(DRV_SCK, HIGH);
        delayMicroseconds(MEIO_CLOCK_US);
        digitalWrite(DRV_SCK, LOW);
        delayMicroseconds(MEIO_CLOCK_US);
    }

    digitalWrite(DRV_NSCS, HIGH);
    digitalWrite(DRV_MOSI, LOW);

    // ----------------------------- FASE B: repouso de igual duracao
    // Mesma duracao da fase A (16 x 2 x MEIO_CLOCK_US), para que o duty
    // do nSCS seja exatamente 50% e a media DC seja 1,65 V.
    for (uint8_t k = 0; k < 16; k++) {
        delayMicroseconds(MEIO_CLOCK_US * 2);
    }

    // Pisca o LED a cada ~256 periodos (~6 Hz/4 = sinal de vida visivel).
    static uint16_t n = 0;
    if (++n >= 256) {
        n = 0;
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }
}
