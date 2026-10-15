/*
  ============================================================================
   DRV8353RS - bring-up: configuracao verificada + calibracao dos CSA
   NUCLEO-G491RE + placa Ouroboros
  ============================================================================

   SEQUENCIA (a ordem importa, ver comentarios em cada etapa):
     1. ENABLE alto e espera o t_WAKE
     2. destrava e escreve TODA a configuracao, com o ganho final
     3. COAST = 1  (corrente zero garantida, sem chaveamento)
     4. autocalibracao: CAL_MODE = 1, PULSA os bits CSA_CAL
     5. volta para CAL_MODE = 0
     6. calibracao manual: PULSA os bits CSA_CAL e le o ADC no meio
     7. COAST = 0 so' quando for acionar

   POR QUE AUTO ANTES DE MANUAL: a autocalibracao MUDA o offset (faz um trim
   interno no amplificador). A manual MEDE o offset e guarda o numero aqui no
   firmware. Medir antes de trimar guarda um valor que deixa de valer no
   instante seguinte.

   POR QUE A CONFIGURACAO INTEIRA VEM ANTES DAS DUAS: o offset e' medido em
   volts na saida do amplificador, e esse valor escala com o CSA_GAIN. Medir
   com um ganho e operar com outro invalida a medida.
  ============================================================================
*/

#include <Arduino.h>
#include <stdint.h>
#include <SPI.h>

// ------------------------------------------------- registradores (enderecos)
#define DRV_REG_FAULT_STAT_1   0x00
#define DRV_REG_FAULT_STAT_2   0x01
#define DRV_REG_DRIVER_CTRL    0x02
#define DRV_REG_GATE_DRIVER_HS 0x03
#define DRV_REG_GATE_DRIVER_LS 0x04
#define DRV_REG_OCP_CTRL       0x05
#define DRV_REG_CSA_CTRL       0x06
#define DRV_REG_DRIVER_CONFIG  0x07   // so' existe no DRV8353/8353R

// --------------------------------------------------------- formato do quadro
#define SPI_WRITE_CMD    0x0000
#define SPI_READ_CMD     0x8000
#define SPI_ADDRESS_MASK 0x7800   // bits 14-11
#define SPI_DATA_MASK    0x07FF   // bits 10-0

// ------------------------------------------------------------------- pinos
#define PIN_SPI_SCK   PC10
#define PIN_SPI_MISO  PC11
#define PIN_SPI_MOSI  PC12
#define PIN_SPI_CS    PC9
#define PIN_DRV_ENB   PD2

// Console. NAO use PA2/PA3 nesta placa: depois do rework dos solder bridges
// eles sao LSCSA e MOTOR_TEMP do shield. USART1 em PB6/PB7 foi o caminho
// validado em bancada.
#define PIN_UART_TX   PB6
#define PIN_UART_RX   PB7

// Saidas dos CSA de low-side do proprio DRV. Sao ESTES pinos que a
// calibracao manual le - repare que LSCSA e' o PA2, o mesmo pino que estava
// declarado como UART no codigo anterior. Nao da' para ser os dois.
#define PIN_LSCS_A    PA2
#define PIN_LSCS_B    PA6
#define PIN_LSCS_C    PB0

#define PIN_LED       PA0

// ------------------------------------------------- valores de configuracao
// Dados de 11 bits, SEM o endereco. Calculados para o MOSFET CSD18535KCS.
#define VAL_DRIVER_CTRL   0x400   // OCP_ACT=1, PWM_MODE=6x, COAST=0
#define BIT_COAST         (1u << 2)
#define VAL_GATE_HS       0x344   // LOCK=destrava, IDRIVEP=300mA, IDRIVEN=600mA
#define VAL_GATE_LS       0x444   // CBC=1, TDRIVE=500ns, 300mA / 600mA
#define VAL_OCP_LATCH     0x225   // dead time 200ns, LATCH,  deglitch 4us, VDS 0,2V
#define VAL_OCP_RETRY     0x265   // igual, mas OCP em RETRY (era o seu valor)
#define VAL_OCP_CTRL      VAL_OCP_LATCH
#define VAL_CSA_CTRL      0x280   // shunt, VREF/2, ganho 20 V/V, SEN_LVL 0,25V
#define BITS_CSA_CAL      (0x7u << 2)   // CAL_A | CAL_B | CAL_C
#define VAL_CFG_MANUAL    0x000   // CAL_MODE = 0
#define VAL_CFG_AUTO      0x001   // CAL_MODE = 1

// Tempos. O datasheet declara t_WAKE <= 1 ms e 50 us para a rotina de
// autocalibracao de power-up. Para o rerun em runtime nao ha' tempo
// declarado - 200 us e' folga sobre os 50 us conhecidos.
#define T_WAKE_MS         2
#define T_AUTOCAL_US      200
#define T_SETTLE_US       200

// ------------------------------------------------------------------ objetos
SPIClass       SPI_3(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK);
HardwareSerial SerialDbg(USART1);

// Offsets medidos na calibracao manual, em contagens de ADC.
static uint16_t offset_a = 0, offset_b = 0, offset_c = 0;
static bool     offsets_validos = false;

// ============================================================== SPI basico

uint16_t DRV8353_BuildSPICommand(uint16_t endereco, uint16_t dados, bool isRead)
{
    uint16_t comando = isRead ? SPI_READ_CMD : SPI_WRITE_CMD;
    comando |= ((endereco << 11) & SPI_ADDRESS_MASK);
    comando |= (dados & SPI_DATA_MASK);
    return comando;
}

uint16_t DRV8353_SPI_Transfer(uint16_t comando)
{
    SPISettings mySetting(1000000, MSBFIRST, SPI_MODE1);
    SPI_3.beginTransaction(mySetting);

    digitalWrite(PIN_SPI_CS, LOW);
    delayMicroseconds(1);
    uint16_t receivedData = SPI_3.transfer16(comando);
    digitalWrite(PIN_SPI_CS, HIGH);

    SPI_3.endTransaction();
    // o datasheet exige nSCS alto por pelo menos 400 ns entre quadros
    delayMicroseconds(1);
    return receivedData;
}

uint16_t DRV8353_LerRegistrador(uint8_t endereco)
{
    uint16_t comando = DRV8353_BuildSPICommand(endereco, 0x000, true);
    return DRV8353_SPI_Transfer(comando) & SPI_DATA_MASK;
}

// Escreve e RELE. Um bit que nao pegou fica invisivel sem isto.
bool DRV8353_Escrever(uint8_t endereco, uint16_t dados)
{
    DRV8353_SPI_Transfer(DRV8353_BuildSPICommand(endereco, dados, false));
    uint16_t lido = DRV8353_LerRegistrador(endereco);
    bool ok = (lido == (dados & SPI_DATA_MASK));

    if (!ok) {
        SerialDbg.print(F("  FALHOU reg 0x")); SerialDbg.print(endereco, HEX);
        SerialDbg.print(F(": escrito 0x"));    SerialDbg.print(dados & SPI_DATA_MASK, HEX);
        SerialDbg.print(F(", lido 0x"));       SerialDbg.println(lido, HEX);
    }
    return ok;
}

void DRV8353_DumpRegistradores(void)
{
    static const char *nomes[] = {
        "0x00 FAULT_ST1", "0x01 FAULT_ST2", "0x02 DRIVER_CTL",
        "0x03 GATE_HS  ", "0x04 GATE_LS  ", "0x05 OCP_CTL  ",
        "0x06 CSA_CTL  ", "0x07 DRV_CFG  "
    };
    SerialDbg.println(F("--- registradores ---"));
    for (uint8_t a = 0; a <= 0x07; a++) {
        SerialDbg.print(nomes[a]);
        SerialDbg.print(F(" = 0x"));
        SerialDbg.println(DRV8353_LerRegistrador(a), HEX);
    }
    SerialDbg.println();
}

// ================================================ etapa 1 e 2: acorda e configura

bool DRV8353_AcordaEConfigura(void)
{
    digitalWrite(PIN_DRV_ENB, HIGH);
    delay(T_WAKE_MS);

    bool ok = true;

    // GATE_HS PRIMEIRO, porque o campo LOCK mora nele. Se um ciclo anterior
    // deixou os registradores travados, todas as outras escritas seriam
    // ignoradas em silencio. Este valor ja' traz LOCK = destrava.
    ok &= DRV8353_Escrever(DRV_REG_GATE_DRIVER_HS, VAL_GATE_HS);

    ok &= DRV8353_Escrever(DRV_REG_DRIVER_CTRL,    VAL_DRIVER_CTRL);
    ok &= DRV8353_Escrever(DRV_REG_GATE_DRIVER_LS, VAL_GATE_LS);
    ok &= DRV8353_Escrever(DRV_REG_OCP_CTRL,       VAL_OCP_CTRL);
    ok &= DRV8353_Escrever(DRV_REG_CSA_CTRL,       VAL_CSA_CTRL);
    ok &= DRV8353_Escrever(DRV_REG_DRIVER_CONFIG,  VAL_CFG_MANUAL);

    return ok;
}

// ============================================================ COAST liga/desliga

bool DRV8353_Coast(bool ligado)
{
    return DRV8353_Escrever(DRV_REG_DRIVER_CTRL,
                            VAL_DRIVER_CTRL | (ligado ? BIT_COAST : 0));
}

// ======================================================== etapa 4: autocalibracao
// O PULSO: liga os bits, ESPERA (quem trabalha aqui e' o DRV), desliga.

bool DRV8353_AutoCalibrar(void)
{
    // modo automatico
    if (!DRV8353_Escrever(DRV_REG_DRIVER_CONFIG, VAL_CFG_AUTO)) return false;

    // --- PULSO: liga ---
    DRV8353_SPI_Transfer(DRV8353_BuildSPICommand(
        DRV_REG_CSA_CTRL, VAL_CSA_CTRL | BITS_CSA_CAL, false));

    delayMicroseconds(T_AUTOCAL_US);   // o DRV roda o trim interno

    // Diagnostico: o datasheet nao diz se os bits se limpam sozinhos no modo
    // automatico. Esta leitura responde isso na sua placa.
    uint16_t durante = DRV8353_LerRegistrador(DRV_REG_CSA_CTRL);
    SerialDbg.print(F("  bits CAL apos a rotina: 0b"));
    SerialDbg.println((durante >> 2) & 0x7, BIN);
    SerialDbg.println(F("  111 = quem limpa e' o firmware | 000 = o chip limpou"));

    // --- PULSO: desliga --- (incondicional: se o chip ja' limpou, nao muda nada)
    if (!DRV8353_Escrever(DRV_REG_CSA_CTRL, VAL_CSA_CTRL)) return false;

    // volta ao modo manual
    return DRV8353_Escrever(DRV_REG_DRIVER_CONFIG, VAL_CFG_MANUAL);
}

// ==================================================== etapa 6: calibracao manual
// O PULSO: liga os bits, LE O ADC (quem trabalha aqui e' o firmware), desliga.

bool DRV8353_MedirOffsets(void)
{
    const uint8_t N = 64;   // media, para tirar o ruido da leitura

    // --- PULSO: liga --- entradas dos amplificadores em curto
    DRV8353_SPI_Transfer(DRV8353_BuildSPICommand(
        DRV_REG_CSA_CTRL, VAL_CSA_CTRL | BITS_CSA_CAL, false));

    delayMicroseconds(T_SETTLE_US);

    // --- no meio do pulso: a medida ---
    // Com as entradas em curto, a corrente vista pelo amplificador e' ZERO.
    // Tudo o que o ADC ler aqui e' offset: do amplificador, do ADC e da placa.
    uint32_t soma_a = 0, soma_b = 0, soma_c = 0;
    for (uint8_t i = 0; i < N; i++) {
        soma_a += analogRead(PIN_LSCS_A);
        soma_b += analogRead(PIN_LSCS_B);
        soma_c += analogRead(PIN_LSCS_C);
    }
    offset_a = soma_a / N;
    offset_b = soma_b / N;
    offset_c = soma_c / N;

    // --- PULSO: desliga --- entradas voltam aos shunts
    bool ok = DRV8353_Escrever(DRV_REG_CSA_CTRL, VAL_CSA_CTRL);

    offsets_validos = ok;
    return ok;
}

// Conversao usando o offset medido. 3 mOhm x 20 V/V = 60 mV/A.
float DRV8353_LerCorrente(uint32_t pino, uint16_t offset)
{
    const float VREF_V      = 3.3f;
    const float CONTAGENS   = 4096.0f;
    const float VOLTS_POR_A = 0.003f * 20.0f;

    int32_t bruto = (int32_t)analogRead(pino) - (int32_t)offset;
    return (bruto * VREF_V / CONTAGENS) / VOLTS_POR_A;
}

// ================================================================== SETUP

void setup()
{
    SerialDbg.setRx(PIN_UART_RX);
    SerialDbg.setTx(PIN_UART_TX);
    SerialDbg.begin(115200);

    pinMode(PIN_LED, OUTPUT);

    pinMode(PIN_SPI_CS, OUTPUT);
    digitalWrite(PIN_SPI_CS, HIGH);   // CS inativo ANTES de qualquer coisa

    pinMode(PIN_DRV_ENB, OUTPUT);
    digitalWrite(PIN_DRV_ENB, LOW);

    analogReadResolution(12);
    SPI_3.begin();

    delay(300);
    SerialDbg.println();
    SerialDbg.println(F("=== DRV8353RS - bring-up ==="));

    // ---- etapas 1 e 2 ----
    SerialDbg.println(F("[1/5] acordando e configurando..."));
    if (!DRV8353_AcordaEConfigura()) {
        SerialDbg.println(F("  ERRO: configuracao nao confirmada."));
        SerialDbg.println(F("  Se TUDO leu 0x7FF, o SPI nao esta' falando:"));
        SerialDbg.println(F("   - conferir o pull-up no SDO (saida dreno aberto)"));
        SerialDbg.println(F("   - conferir o modo SPI na secao 8.5.1.1"));
        DRV8353_DumpRegistradores();
        digitalWrite(PIN_DRV_ENB, LOW);
        return;                        // nao segue com configuracao duvidosa
    }
    SerialDbg.println(F("  ok"));

    // ---- etapa 3 ----
    SerialDbg.println(F("[2/5] COAST = 1 (ponte em Hi-Z durante a calibracao)"));
    DRV8353_Coast(true);

    // ---- etapa 4 ----
    SerialDbg.println(F("[3/5] autocalibracao (o DRV faz o trim)..."));
    SerialDbg.println(DRV8353_AutoCalibrar() ? F("  ok") : F("  FALHOU"));

    // ---- etapa 6 ----
    SerialDbg.println(F("[4/5] calibracao manual (o firmware mede)..."));
    if (DRV8353_MedirOffsets()) {
        SerialDbg.print(F("  offsets (contagens): A="));
        SerialDbg.print(offset_a);
        SerialDbg.print(F("  B=")); SerialDbg.print(offset_b);
        SerialDbg.print(F("  C=")); SerialDbg.println(offset_c);
        SerialDbg.println(F("  esperado: perto de 2048 (1,65 V com VREF de 3,3 V)"));
    } else {
        SerialDbg.println(F("  FALHOU"));
    }

    SerialDbg.println(F("[5/5] estado final:"));
    DRV8353_DumpRegistradores();

    SerialDbg.println(F("COAST continua LIGADO. Chame DRV8353_Coast(false)"));
    SerialDbg.println(F("so' quando for acionar o motor."));
}

void loop()
{
    // Corrente das tres fases a cada segundo. Com COAST ligado e o motor
    // parado, as tres devem ficar perto de zero - e o desvio que sobrar e' o
    // seu piso de ruido.
    static uint32_t t = 0;
    if (offsets_validos && millis() - t > 1000) {
        t = millis();
        SerialDbg.print(DRV8353_LerCorrente(PIN_LSCS_A, offset_a), 3);
        SerialDbg.print(F("  "));
        SerialDbg.print(DRV8353_LerCorrente(PIN_LSCS_B, offset_b), 3);
        SerialDbg.print(F("  "));
        SerialDbg.print(DRV8353_LerCorrente(PIN_LSCS_C, offset_c), 3);
        SerialDbg.println(F("  [A]"));
    }

    digitalWrite(PIN_LED, (millis() / 500) & 1);
}
