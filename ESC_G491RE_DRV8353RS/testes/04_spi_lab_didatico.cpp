/*
  SPI LAB - NUCLEO-G491RE
  Bancada de estudo e pratica de SPI. Nenhum periferico obrigatorio.

  CONSOLE em PB6 (TX) / PB7 (RX), 115200, via conversor USB-serial.
  Se o VCP do ST-Link estiver ativo na sua placa (SB17/SB23 fechados), o
  console tambem funciona pelo USB - mas ai' PA2/PA3 nao podem ir ao morpho.

  PINOS (SPI3, os mesmos da placa Ouroboros):
    SCK  = PC10      MISO = PC11      MOSI = PC12      CS = PC9

  EXERCICIO 1 - LOOPBACK (faca este ANTES de qualquer chip)
    Ligue um jumper entre PC12 (MOSI) e PC11 (MISO).
    Comando: L
    Se o valor enviado voltar identico, o mestre esta' funcionando: clock,
    pinos, periferico, ordem de bits. Isso elimina metade das causas antes
    de voce culpar o escravo.
    Sem o jumper, o retorno e' 0x0000 ou 0xFFFF - que e' exatamente o que um
    escravo mudo devolve. Entender essa equivalencia vale a sessao inteira.

  EXERCICIO 2 - VER NO OSCILOSCOPIO
    Comando: R  (repete um quadro continuamente, para estabilizar a tela)
    Dispare na borda de DESCIDA do CS. Depois capture, um par por vez:
      CS  + SCK   -> o envelope do quadro
      SCK + MOSI  -> o que o mestre manda
      SCK + MISO  -> o que o escravo responde

  EXERCICIO 3 - OS QUATRO MODOS
    Comando: M<0-3>
    Observe no osciloscopio o que muda entre eles: o nivel de repouso do SCK
    e a borda em que o MOSI troca de valor. Compare com o diagrama de
    temporizacao do datasheet do chip que voce quer falar.
    O objetivo NAO e' achar o modo por tentativa - e' conseguir PREVER qual
    e', e usar a bancada so' para confirmar.

  COMANDOS
    ?          ajuda
    L          teste de loopback (precisa do jumper MOSI-MISO)
    M<0-3>     seleciona o modo SPI
    F<kHz>     frequencia do SCLK em kHz (ex.: F1000 = 1 MHz)
    B<8|16>    largura da transferencia
    S<hex>     envia um quadro e imprime o que voltou (ex.: S86A0)
    R          repete o ultimo quadro continuamente (qualquer tecla para)
    C          mostra a configuracao atual e os clocks do MCU
*/

#include <Arduino.h>
#include <SPI.h>

// --------------------------------------------------------------- pinos
#define PIN_SPI_SCK   PC10
#define PIN_SPI_MISO  PC11
#define PIN_SPI_MOSI  PC12
#define PIN_SPI_CS    PC9

#define PIN_UART_TX   PB6
#define PIN_UART_RX   PB7

#define PIN_LED       PA0    // status; PC13 e' o botao B1 da Nucleo

// -------------------------------------------------------------- objetos
// [G491] HardwareSerial(rx, tx) faz a biblioteca DEDUZIR o periferico a
// partir dos pinos, consultando o PeripheralPins.c. Na variante generica
// isso falha no RX: o TX transmite, mas available() e' sempre false e o
// console nunca recebe comando. Passar USART1 direto resolve.
HardwareSerial SerialDbg(USART1);

SPIClass SPI_3(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK);

// ----------------------------------------------------------- estado
static uint8_t  spi_mode_idx = 0;
static uint32_t spi_hz       = 1000000;
static uint8_t  spi_bits     = 16;
static uint16_t last_frame   = 0x0000;

static SPISettings currentSettings()
{
    switch (spi_mode_idx) {
    case 1:  return SPISettings(spi_hz, MSBFIRST, SPI_MODE1);
    case 2:  return SPISettings(spi_hz, MSBFIRST, SPI_MODE2);
    case 3:  return SPISettings(spi_hz, MSBFIRST, SPI_MODE3);
    default: return SPISettings(spi_hz, MSBFIRST, SPI_MODE0);
    }
}

// Uma transacao completa: CS baixo -> transfere -> CS alto.
// O CS delimita o quadro. Alguns chips exigem que ele suba entre quadros
// para validar a escrita - confira isso no datasheet do seu escravo.
static uint16_t transferFrame(uint16_t tx)
{
    uint16_t rx;
    SPI_3.beginTransaction(currentSettings());
    digitalWrite(PIN_SPI_CS, LOW);
    if (spi_bits == 8) rx = SPI_3.transfer((uint8_t)(tx & 0xFF));
    else               rx = SPI_3.transfer16(tx);
    digitalWrite(PIN_SPI_CS, HIGH);
    SPI_3.endTransaction();
    return rx;
}

static void printBin(uint16_t v, uint8_t bits)
{
    for (int8_t b = bits - 1; b >= 0; b--) SerialDbg.print((v >> b) & 1);
}

static void showConfig()
{
    SerialDbg.println(F("--- configuracao SPI ---"));
    SerialDbg.print(F("modo      : SPI_MODE")); SerialDbg.println(spi_mode_idx);
    SerialDbg.print(F("SCLK      : "));         SerialDbg.print(spi_hz / 1000);
    SerialDbg.println(F(" kHz (pedido)"));
    SerialDbg.print(F("largura   : "));         SerialDbg.print(spi_bits);
    SerialDbg.println(F(" bits"));
    SerialDbg.print(F("ordem     : MSB primeiro"));
    SerialDbg.println();
    SerialDbg.println(F("--- clocks do MCU ---"));
    SerialDbg.print(F("SYSCLK    : ")); SerialDbg.println(HAL_RCC_GetSysClockFreq());
    SerialDbg.print(F("HCLK      : ")); SerialDbg.println(HAL_RCC_GetHCLKFreq());
    SerialDbg.print(F("PCLK1     : ")); SerialDbg.println(HAL_RCC_GetPCLK1Freq());
    SerialDbg.print(F("PCLK2     : ")); SerialDbg.println(HAL_RCC_GetPCLK2Freq());
    SerialDbg.println(F("(SYSCLK deve dar 170000000 - se nao, o clock_config"
                        " nao esta' sendo compilado)"));
    SerialDbg.println();
    SerialDbg.println(F("A frequencia REAL do SCLK e' o PCLK dividido por uma"));
    SerialDbg.println(F("potencia de 2. Meca no osciloscopio: raramente e'"));
    SerialDbg.println(F("exatamente a que voce pediu."));
    SerialDbg.println();
}

static void loopbackTest()
{
    SerialDbg.println(F("--- loopback: jumper entre PC12 (MOSI) e PC11 (MISO) ---"));
    const uint16_t padroes[] = { 0x0000, 0xFFFF, 0xAAAA, 0x5555, 0x1234, 0x8001 };
    uint8_t ok = 0;

    for (uint8_t i = 0; i < 6; i++) {
        uint16_t tx = padroes[i];
        if (spi_bits == 8) tx &= 0xFF;
        uint16_t rx = transferFrame(tx);
        bool match = (rx == tx);
        if (match) ok++;
        SerialDbg.print(F("  tx 0x")); SerialDbg.print(tx, HEX);
        SerialDbg.print(F("  rx 0x")); SerialDbg.print(rx, HEX);
        SerialDbg.println(match ? F("   ok") : F("   DIVERGE"));
        delay(5);
    }

    SerialDbg.print(F("resultado: ")); SerialDbg.print(ok); SerialDbg.println(F("/6"));
    if (ok == 6) {
        SerialDbg.println(F("Mestre funcionando. Clock, pinos e ordem de bits ok."));
        SerialDbg.println(F("Repare: o loopback passa nos QUATRO modos. Ele valida"));
        SerialDbg.println(F("o mestre, nao o acordo com um escravo especifico."));
    } else {
        SerialDbg.println(F("Sem o jumper isso e' esperado. COM o jumper, e' bug."));
        SerialDbg.println(F("Tudo 0x0 ou 0xFFFF = MISO nao esta' chegando."));
    }
    SerialDbg.println();
}

static void ajuda()
{
    SerialDbg.println(F("? ajuda | L loopback | C config"));
    SerialDbg.println(F("M<0-3> modo | F<kHz> clock | B<8|16> largura"));
    SerialDbg.println(F("S<hex> envia quadro | R repete (tecla para parar)"));
}

static uint32_t parseHex()
{
    uint32_t v = 0; bool any = false;
    uint32_t t0 = millis();
    while (millis() - t0 < 50) {
        if (!SerialDbg.available()) continue;
        int c = SerialDbg.peek();
        uint8_t d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        SerialDbg.read();
        v = (v << 4) | d; any = true; t0 = millis();
    }
    return any ? v : 0;
}

static void repeatFrame()
{
    SerialDbg.println(F("repetindo - dispare o osciloscopio na descida do CS"));
    SerialDbg.println(F("qualquer tecla para parar"));
    while (!SerialDbg.available()) {
        transferFrame(last_frame);
        delayMicroseconds(200);
    }
    while (SerialDbg.available()) SerialDbg.read();
    SerialDbg.println(F("parado"));
}

// ================================================================ SETUP
void setup()
{
    // [G491] setRx/setTx TEM que vir antes do begin().
    SerialDbg.setRx(PIN_UART_RX);
    SerialDbg.setTx(PIN_UART_TX);
    SerialDbg.begin(115200);
    delay(300);

    pinMode(PIN_LED, OUTPUT);

    // CS em nivel alto ANTES de tudo: flutuando, o escravo interpreta ruido
    // do barramento como transacao valida.
    pinMode(PIN_SPI_CS, OUTPUT);
    digitalWrite(PIN_SPI_CS, HIGH);

    SPI_3.begin();

    SerialDbg.println();
    SerialDbg.println(F("=== SPI LAB - NUCLEO-G491RE ==="));
    SerialDbg.println(F("SCK=PC10  MISO=PC11  MOSI=PC12  CS=PC9"));
    SerialDbg.println(F("Comece pelo loopback: jumper PC12-PC11, comando L"));
    SerialDbg.println();
    showConfig();
    ajuda();
}

// ================================================================= LOOP
void loop()
{
    if (SerialDbg.available()) {
        char c = SerialDbg.read();
        switch (c) {
        case '?': ajuda(); break;
        case 'C': case 'c': showConfig(); break;
        case 'L': case 'l': loopbackTest(); break;
        case 'R': case 'r': repeatFrame(); break;

        case 'M': case 'm': {
            int m = SerialDbg.parseInt();
            if (m >= 0 && m <= 3) {
                spi_mode_idx = m;
                SerialDbg.print(F("modo -> SPI_MODE")); SerialDbg.println(m);
            }
            break;
        }

        case 'F': case 'f': {
            long k = SerialDbg.parseInt();
            if (k > 0) {
                spi_hz = k * 1000UL;
                SerialDbg.print(F("SCLK -> ")); SerialDbg.print(k);
                SerialDbg.println(F(" kHz (pedido; meca no osciloscopio)"));
            }
            break;
        }

        case 'B': case 'b': {
            int b = SerialDbg.parseInt();
            if (b == 8 || b == 16) {
                spi_bits = b;
                SerialDbg.print(F("largura -> ")); SerialDbg.println(b);
            }
            break;
        }

        case 'S': case 's': {
            uint16_t tx = (uint16_t)parseHex();
            if (spi_bits == 8) tx &= 0xFF;
            last_frame = tx;
            uint16_t rx = transferFrame(tx);
            SerialDbg.print(F("tx 0x")); SerialDbg.print(tx, HEX);
            SerialDbg.print(F("  0b"));  printBin(tx, spi_bits);
            SerialDbg.print(F("   ->   rx 0x")); SerialDbg.print(rx, HEX);
            SerialDbg.print(F("  0b"));  printBin(rx, spi_bits);
            SerialDbg.println();
            break;
        }

        default: break;
        }
    }

    digitalWrite(PIN_LED, (millis() / 500) & 1);
}
