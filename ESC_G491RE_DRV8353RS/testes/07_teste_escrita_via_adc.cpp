/* =====================================================================
   AS ESCRITAS CHEGAM AO DRIVER?   -   teste por caminho independente
   NUCLEO-G491RE (Artemis) + Ouroboros          Eco Octano UFPR

   O IMPASSE QUE ISTO RESOLVE
   O DRV so' fica acordado com a Artemis encaixada, e com ela encaixada
   os pinos do DRV ficam embaixo dela - inacessiveis a ponta de prova.
   Entao nao da' para medir nada no driver com ele funcionando.

   A SAIDA
   Nao medir no driver: medir no MCU. O bit VREF_DIV do registrador 0x06
   muda a tensao de repouso das saidas SOx do driver, e essas saidas ja'
   vao para pinos de ADC do MCU. O MCU escreve pelo SPI e le o efeito
   pelo proprio ADC.

     VREF_DIV = 1 (reset) -> SOx = VREF/2      = 1,65 V
     VREF_DIV = 0         -> SOx = VREF - 0,3  = 3,00 V

   O caminho de volta e' ANALOGICO, nao passa pelo SDO. Por isso este
   teste funciona mesmo com o SDO quebrado - e e' exatamente essa a
   ambiguidade que ele desfaz:

     tensao MUDA    -> nSCS/SCLK/SDI OK. So' o SDO esta' quebrado.
                       O driver esta' vivo e configuravel.
     tensao NAO muda-> nada chega ao driver. nSCS, SCLK ou SDI aberto.

   ---------------------------------------------------------------------
   IMPORTANTE SOBRE OS PINOS

   PA2 NAO e' lido aqui, de proposito. Na sua placa PA2 e' a low-side A
   (SOA), mas tambem e' o TX da serial que esta' funcionando. O MCU
   dirige PA2 como saida push-pull do UART, e o SOx do driver fornece
   no maximo 5 mA - o UART ganha a disputa. Qualquer leitura de PA2
   seria a sua propria serial, nao o driver.

   Lemos PA6 (SOB) e PB0 (SOC). Duas fases bastam: o teste procura uma
   MUDANCA, nao um valor absoluto.

   Se na sua placa o mapeamento SOx->ADC for outro, corrija os dois
   #define abaixo. Os valores vieram do bring-up anterior.

   SERIAL: PA2 (TX) / PA3 (RX), 115200 - a mesma que ja' funcionou.

   COMANDOS
     t  roda o teste (escreve, mede, decide)
     m  so' mede as tensoes agora
     e  alterna o ENABLE
     ?  ajuda
   ===================================================================== */

#include <Arduino.h>
#include <SPI.h>

HardwareSerial Dbg(PA3, PA2);

// ------------------------------------------------------------- pinos
#define DRV_SCK      PC10
#define DRV_MISO     PC11
#define DRV_MOSI     PC12
#define DRV_NSCS     PC9
#define PIN_ENABLE   PD2
#define PIN_NFAULT   PB10
#define PIN_LED      PA0

#define PIN_SOB      PA6     // saida do CSA da fase B
#define PIN_SOC      PB0     // saida do CSA da fase C

// ------------------------------------------------- registrador 0x06
/* CSA Control, bit a bit (SLVSDY6A, tabela 18):
     10    CSA_FET    0 = entrada positiva e' SPx            <- shunt
      9    VREF_DIV   1 = referencia VREF/2 (bidirecional)   <- O BIT DO TESTE
      8    LS_REF     0
     7-6   CSA_GAIN   10b = 20 V/V
      5    DIS_SEN    0 = falha de sobrecorrente habilitada
     4-2   CSA_CAL_x  0 = operacao normal
     1-0   SEN_LVL    11b = 1,0 V

   Monta:  bit9=0x200 | gain10b=0x080 | sen_lvl11b=0x003            */
static const uint16_t CSA_VREF_DIV_1 = 0x283;   // 1,65 V  (= valor de reset)
static const uint16_t CSA_VREF_DIV_0 = 0x083;   // 3,00 V

#define SPI_READ_CMD   0x8000
#define SPI_ADDR_MASK  0x7800
#define SPI_DATA_MASK  0x07FF

SPIClass SPI_3(DRV_MOSI, DRV_MISO, DRV_SCK);

static uint16_t transfer16(uint16_t quadro)
{
    SPISettings cfg(1000000, MSBFIRST, SPI_MODE1);
    SPI_3.beginTransaction(cfg);
    digitalWrite(DRV_NSCS, LOW);
    delayMicroseconds(1);
    uint16_t rx = SPI_3.transfer16(quadro);
    digitalWrite(DRV_NSCS, HIGH);
    SPI_3.endTransaction();
    delayMicroseconds(2);
    return rx;
}

static void escreverReg(uint8_t endereco, uint16_t dados)
{
    transfer16((((uint16_t)endereco << 11) & SPI_ADDR_MASK) | (dados & SPI_DATA_MASK));
}

static uint16_t lerReg(uint8_t endereco)
{
    return transfer16(SPI_READ_CMD | (((uint16_t)endereco << 11) & SPI_ADDR_MASK)) & SPI_DATA_MASK;
}

// ------------------------------------------------------------ medida
/* Media de 64 amostras. Sem media, o ruido de alguns mV mascara a
   diferenca que a gente procura - e aqui a diferenca esperada e' de
   1,35 V, entao a media e' barata e torna a conclusao inequivoca. */
static float lerVolts(uint32_t pino)
{
    uint32_t soma = 0;
    for (uint8_t i = 0; i < 64; i++) {
        soma += analogRead(pino);
        delayMicroseconds(50);
    }
    return (soma / 64.0f) * 3.3f / 4095.0f;
}

static void medir(const char *rotulo, float &vb, float &vc)
{
    vb = lerVolts(PIN_SOB);
    vc = lerVolts(PIN_SOC);
    Dbg.print(F("  "));
    Dbg.print(rotulo);
    Dbg.print(F("   SOB(PA6) = ")); Dbg.print(vb, 3); Dbg.print(F(" V"));
    Dbg.print(F("   SOC(PB0) = ")); Dbg.print(vc, 3); Dbg.println(F(" V"));
}

// ------------------------------------------------------------- teste
static void rodarTeste()
{
    Dbg.println(F("\n=== AS ESCRITAS CHEGAM AO DRIVER? ==="));
    Dbg.print  (F("  ENABLE = "));
    Dbg.print(digitalRead(PIN_ENABLE) ? F("1") : F("0  <<< PRECISA ESTAR EM 1"));
    Dbg.print  (F("   nFAULT = "));
    Dbg.println(digitalRead(PIN_NFAULT) ? F("alto") : F("BAIXO (falha)"));
    Dbg.println();

    float b1, c1, b0, c0;

    // Estado 1: VREF_DIV = 1  -> esperado 1,65 V
    escreverReg(0x06, CSA_VREF_DIV_1);
    delay(5);                        // tempo para a saida acomodar
    medir("VREF_DIV=1 (esperado 1,65 V)", b1, c1);

    // Estado 2: VREF_DIV = 0  -> esperado 3,00 V
    escreverReg(0x06, CSA_VREF_DIV_0);
    delay(5);
    medir("VREF_DIV=0 (esperado 3,00 V)", b0, c0);

    // Volta ao estado util (bidirecional) e deixa assim.
    escreverReg(0x06, CSA_VREF_DIV_1);
    delay(5);
    medir("restaurado                  ", b1, c1);

    float db = b0 - b1;
    float dc = c0 - c1;

    Dbg.println();
    Dbg.print(F("  delta SOB = ")); Dbg.print(db, 3); Dbg.println(F(" V"));
    Dbg.print(F("  delta SOC = ")); Dbg.print(dc, 3); Dbg.println(F(" V"));
    Dbg.print(F("  (0x06 lido de volta = 0x")); Dbg.print(lerReg(0x06), HEX);
    Dbg.println(F(" - irrelevante aqui, o SDO esta' sob suspeita)"));

    // Limiar de 0,3 V: a mudanca esperada e' 1,35 V e o ruido medido e'
    // de poucos mV, entao 0,3 V fica longe dos dois e nao gera duvida.
    bool mudouB = (db > 0.3f) || (db < -0.3f);
    bool mudouC = (dc > 0.3f) || (dc < -0.3f);

    Dbg.println();
    if (mudouB || mudouC) {
        Dbg.println(F("  VEREDITO: AS ESCRITAS CHEGAM."));
        Dbg.println(F("  O driver respondeu a um comando SPI com uma mudanca"));
        Dbg.println(F("  fisica na saida analogica. Logo nSCS, SCLK e SDI estao"));
        Dbg.println(F("  todos funcionando, e o driver esta' vivo e configuravel."));
        Dbg.println(F("  O defeito esta' ISOLADO no SDO (pino 29) ou na trilha"));
        Dbg.println(F("  dele ate' o PC11."));
        Dbg.println(F("  Consequencia pratica: da' para configurar o driver"));
        Dbg.println(F("  inteiro escrevendo sem ler, e seguir o projeto, enquanto"));
        Dbg.println(F("  o SDO e' investigado. Perde-se a verificacao de escrita."));
    } else {
        Dbg.println(F("  VEREDITO: NADA CHEGA AO DRIVER."));
        Dbg.println(F("  Um comando SPI valido nao produziu efeito nenhum. O"));
        Dbg.println(F("  problema NAO e' o SDO - esta' em nSCS, SCLK ou SDI."));
        Dbg.println(F("  Proximo passo: continuidade, placa a placa e sem"));
        Dbg.println(F("  energia. Comece pelo nSCS: se ele nao desce no pino do"));
        Dbg.println(F("  driver, o DRV ignora tudo e TODO registrador le 0x7FF -"));
        Dbg.println(F("  que e' exatamente o que voce ve."));
        Dbg.println();
        Dbg.println(F("  ATENCAO: este veredito so' vale se SOB/SOC estiverem"));
        Dbg.println(F("  nos pinos certos. Confira no esquematico se SOB vai ao"));
        Dbg.println(F("  PA6 e SOC ao PB0. Se as duas leituras acima derem ~0 V"));
        Dbg.println(F("  ou ~3,3 V fixos nos tres estados, provavelmente os"));
        Dbg.println(F("  pinos estao errados e o teste nao mediu nada."));
    }
}

static void ajuda()
{
    Dbg.println(F("\n--------------------------------------------"));
    Dbg.println(F("  t  roda o teste (escreve, mede, decide)"));
    Dbg.println(F("  m  so' mede as tensoes agora"));
    Dbg.println(F("  e  alterna o ENABLE"));
    Dbg.println(F("  ?  ajuda"));
    Dbg.println(F("--------------------------------------------"));
}

void setup()
{
    pinMode(PIN_LED, OUTPUT);

    Dbg.begin(115200);
    delay(300);
    Dbg.println(F("\n\n#############################################"));
    Dbg.println(F("#  DRV8353RS - as escritas chegam?"));
    Dbg.print  (F("#  compilado "));
    Dbg.print(F(__DATE__)); Dbg.print(' '); Dbg.println(F(__TIME__));
    Dbg.println(F("#############################################"));

    pinMode(DRV_NSCS, OUTPUT);
    digitalWrite(DRV_NSCS, HIGH);
    pinMode(PIN_NFAULT, INPUT);

    pinMode(PIN_ENABLE, OUTPUT);
    digitalWrite(PIN_ENABLE, HIGH);
    delay(5);

    analogReadResolution(12);
    pinMode(PIN_SOB, INPUT_ANALOG);
    pinMode(PIN_SOC, INPUT_ANALOG);

    SPI_3.begin();

    rodarTeste();
    ajuda();
}

void loop()
{
    static uint32_t t = 0;
    if (millis() - t >= 500) {
        t = millis();
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }

    if (!Dbg.available()) return;
    switch (Dbg.read()) {
        case 't': rodarTeste(); break;
        case 'm': {
            float b, c;
            Dbg.println();
            medir("agora", b, c);
            break;
        }
        case 'e':
            digitalWrite(PIN_ENABLE, !digitalRead(PIN_ENABLE));
            Dbg.print(F("\n>> ENABLE = "));
            Dbg.println(digitalRead(PIN_ENABLE) ? F("1") : F("0"));
            if (digitalRead(PIN_ENABLE)) delay(5);
            break;
        case '?': ajuda(); break;
        default: break;
    }
}
