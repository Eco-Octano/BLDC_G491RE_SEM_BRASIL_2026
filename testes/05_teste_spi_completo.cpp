/* =====================================================================
   TESTE DE VALIDACAO DA COMUNICACAO SPI  -  DRV8353RS
   NUCLEO-G491RE (Artemis) + Ouroboros          Eco Octano UFPR

   OBJETIVO: descobrir POR QUE todos os registradores leem 0x7FF, sem
   desmontar nada e com UMA gravacao. O firmware e' interativo: voce
   manda uma letra pela serial e ele roda o teste correspondente.

   ---------------------------------------------------------------------
   O QUE CADA TESTE RESPONDE

     p  mapeamento   Os pinos PC10/PC11/PC12 sao reconhecidos como SPI3
                     por ESTE variant? (sua propria hipotese) Responde
                     sem fio nenhum, consultando as tabelas do core.

     b  bit-bang     Fala com o DRV por GPIO puro, sem o periferico SPI.
                     Se isto funcionar e o SPI de hardware nao, o problema
                     esta' no periferico/mapeamento. Se NENHUM dos dois
                     funcionar, o problema e' eletrico (fio, alimentacao,
                     DVDD, SDO).

     l  loopback     Precisa de um jumper MOSI<->MISO (PC12 ao PC11),
                     com a Ouroboros DESCONECTADA. Prova que o periferico
                     SPI realmente gera clock e trafega dados.

     v  varredura    Escreve e rele 0x400 no registrador 0x02 nos quatro
                     modos SPI. Se algum modo devolver o valor escrito,
                     era mode. (A especificacao diz MODE1; isto e'
                     confirmacao, nao chute.)

     d  dump         Le os 8 registradores e INTERPRETA o resultado.

     e  enable       Alterna o pino ENABLE (PD2).
     n  nfault       Le o nivel do pino nFAULT.
     ?  ajuda
   ---------------------------------------------------------------------
   SERIAL: PA2(TX)/PA3(RX) = USART2, 115200 8N1. Se o seu console estiver
   em PB6/PB7, troque o bloco marcado [SERIAL].

   NOMES DOS PINOS: usei o prefixo DRV_ de proposito. PIN_SPI_SCK,
   PIN_SPI_MISO e PIN_SPI_MOSI JA' EXISTEM no variant_generic.h apontando
   para PA5/PA6/PA7 (o SPI1 do header Arduino). Redefinir esses nomes
   gera warning e, pior, qualquer biblioteca que os use passa a receber
   os seus valores sem voce perceber.
   ===================================================================== */

#include <Arduino.h>
#include <SPI.h>
#include "pinmap.h"
#include "PeripheralPins.h"

// ----------------------------------------------------------- [SERIAL]
HardwareSerial Dbg(PA3, PA2);          // RX, TX  -> USART2
// Alternativa USART1:  HardwareSerial Dbg(PB7, PB6);

// -------------------------------------------------------------- pinos
#define DRV_SCK      PC10
#define DRV_MISO     PC11
#define DRV_MOSI     PC12
#define DRV_NSCS     PC9      // CS por SOFTWARE
#define PIN_ENABLE   PD2
#define PIN_NFAULT   PB10
#define PIN_LED      PA0

// ------------------------------------------------------- quadro de 16b
#define SPI_READ_CMD    0x8000
#define SPI_ADDR_MASK   0x7800
#define SPI_DATA_MASK   0x07FF

/* ATENCAO AO 4o ARGUMENTO QUE NAO EXISTE AQUI.
   SPIClass(mosi, miso, sck) -> CS por software, que e' o nosso caso.
   SPIClass(mosi, miso, sck, ssel) -> ssel e' o NSS de HARDWARE, e PC9
   nao e' NSS de nenhum SPI no G491 (SPI3_NSS = PA4 ou PA15). Com um
   pino invalido ali, o pinmap_merge interno falha, o spi_init aborta
   silenciosamente e o periferico nunca liga: MOSI e SCK ficam mudos e
   o MISO le o pull-up. Resultado: 0x7FF em tudo. */
SPIClass SPI_3(DRV_MOSI, DRV_MISO, DRV_SCK);

static const uint32_t SPI_HZ = 1000000;
static uint8_t  modo_atual   = SPI_MODE1;   // especificacao do DRV8353
static bool     usar_bitbang = false;

// ===================================================================
//  CAMADA DE TRANSPORTE
// ===================================================================

// --- SPI de hardware ---
static uint16_t transferHW(uint16_t quadro)
{
    SPISettings cfg(SPI_HZ, MSBFIRST, modo_atual);
    SPI_3.beginTransaction(cfg);

    digitalWrite(DRV_NSCS, LOW);
    delayMicroseconds(1);                  // t_SU(CS) folgado

    uint16_t rx = SPI_3.transfer16(quadro);

    digitalWrite(DRV_NSCS, HIGH);
    SPI_3.endTransaction();
    delayMicroseconds(2);                  // nSCS alto >= 400 ns
    return rx;
}

/* --- SPI por software (bit-bang) ---
   Reproduz na mao o que o datasheet manda (SLVSDY6A 8.5.1.1):
     - SCLK em nivel BAIXO quando o nSCS transiciona
     - MSB primeiro, exatamente 16 ciclos
     - o DRV PROPAGA o SDO na borda de SUBIDA
     - o DRV CAPTURA o SDI na borda de DESCIDA
   Isso e' CPOL=0 / CPHA=1, ou seja SPI_MODE1. */
static uint16_t transferBB(uint16_t quadro)
{
    pinMode(DRV_SCK,  OUTPUT);
    pinMode(DRV_MOSI, OUTPUT);
    pinMode(DRV_MISO, INPUT);

    digitalWrite(DRV_SCK,  LOW);
    digitalWrite(DRV_NSCS, LOW);
    delayMicroseconds(2);

    uint16_t rx = 0;
    for (int8_t i = 15; i >= 0; i--) {
        digitalWrite(DRV_MOSI, (quadro >> i) & 1);
        delayMicroseconds(2);

        digitalWrite(DRV_SCK, HIGH);          // DRV coloca o bit no SDO
        delayMicroseconds(2);
        rx = (rx << 1) | (digitalRead(DRV_MISO) ? 1 : 0);

        digitalWrite(DRV_SCK, LOW);           // DRV captura o nosso bit
        delayMicroseconds(2);
    }

    digitalWrite(DRV_NSCS, HIGH);
    delayMicroseconds(2);
    return rx;
}

static uint16_t transfer16(uint16_t quadro)
{
    return usar_bitbang ? transferBB(quadro) : transferHW(quadro);
}

static uint16_t montarQuadro(uint8_t endereco, uint16_t dados, bool leitura)
{
    uint16_t q = leitura ? SPI_READ_CMD : 0x0000;
    q |= ((uint16_t)endereco << 11) & SPI_ADDR_MASK;
    q |= dados & SPI_DATA_MASK;
    return q;
}

static uint16_t lerReg(uint8_t endereco)
{
    return transfer16(montarQuadro(endereco, 0x000, true)) & SPI_DATA_MASK;
}

static void escreverReg(uint8_t endereco, uint16_t dados)
{
    transfer16(montarQuadro(endereco, dados, false));
}

// ===================================================================
//  IMPRESSAO
// ===================================================================
static void imprimirHexBin(uint16_t v)
{
    Dbg.print(F("0x"));
    if (v < 0x100) Dbg.print('0');
    if (v < 0x010) Dbg.print('0');
    Dbg.print(v, HEX);
    Dbg.print(F("  0b"));
    for (int8_t b = 10; b >= 0; b--) Dbg.print((v >> b) & 1);
}

static const char* nomeModo(uint8_t m)
{
    switch (m) {
        case SPI_MODE0: return "MODE0 (CPOL=0 CPHA=0)";
        case SPI_MODE1: return "MODE1 (CPOL=0 CPHA=1)";
        case SPI_MODE2: return "MODE2 (CPOL=1 CPHA=0)";
        default:        return "MODE3 (CPOL=1 CPHA=1)";
    }
}

// ===================================================================
//  [p] MAPEAMENTO DOS PINOS
//  Testa a SUA hipotese sem mexer em fio nenhum: pergunta ao proprio
//  core quais perifericos cada pino pode atender neste variant.
//
//  Nao comparo com NP nem uso pinmap_function: esses dois simbolos
//  mudam de assinatura entre versoes do core. Em vez de perguntar
//  "falhou?", pergunto "qual periferico saiu?" - se nao for nenhum dos
//  tres SPI, nao esta' mapeado. Mesma conclusao, sem depender de
//  detalhe interno.
// ===================================================================
static const char* nomePeriferico(void* base)
{
    if (base == (void*)SPI1) return "SPI1";
    if (base == (void*)SPI2) return "SPI2";
    if (base == (void*)SPI3) return "SPI3";
    return NULL;                            // nao e' SPI nenhum
}

static bool checarUm(const char* rotulo, uint32_t pino, const PinMap* tabela)
{
    PinName pn   = digitalPinToPinName(pino);
    void*   peri = pinmap_peripheral(pn, tabela);
    const char* nome = nomePeriferico(peri);

    Dbg.print(F("  "));
    Dbg.print(rotulo);
    Dbg.print(F(" -> "));

    if (nome == NULL) {
        Dbg.println(F("NAO MAPEADO  <<< este pino nao serve para esta funcao"));
        return false;
    }
    Dbg.println(nome);
    return peri == (void*)SPI3;
}

static void checarMapeamento()
{
    Dbg.println(F("\n--- [p] MAPEAMENTO SPI ---"));
    bool ok = true;
    ok &= checarUm("SCK  PC10", DRV_SCK,  PinMap_SPI_SCLK);
    ok &= checarUm("MISO PC11", DRV_MISO, PinMap_SPI_MISO);
    ok &= checarUm("MOSI PC12", DRV_MOSI, PinMap_SPI_MOSI);

    // Qual instancia o objeto REALMENTE pegou. E' aqui que o bug de usar
    // 'SPI' no lugar de 'SPI_3' apareceria: daria SPI1 (PA5/PA6/PA7).
    void* inst = (void*)SPI_3.getHandle()->Instance;
    const char* nome_inst = nomePeriferico(inst);
    Dbg.print(F("\n  Instancia do objeto SPI_3: "));
    Dbg.println(nome_inst ? nome_inst : "nenhuma (spi_init abortou)");

    if (ok) {
        Dbg.println(F("  VEREDITO: os tres pinos sao SPI3 neste variant."));
        Dbg.println(F("            Sua hipotese de mapeamento esta' DESCARTADA."));
        Dbg.println(F("            O 0x7FF vem de outro lugar: rode [b] e [l]."));
    } else {
        Dbg.println(F("  VEREDITO: HA pino nao mapeado. Sua hipotese CONFIRMADA."));
        Dbg.println(F("            Saida: usar [b] (bit-bang) no firmware, ou"));
        Dbg.println(F("            remapear via PeripheralPins.c do variant."));
    }

    Dbg.println(F("\n  nSCS PC9 e' software (digitalWrite) - nao precisa estar"));
    Dbg.println(F("  em PinMap_SPI_SSEL, e NAO DEVE ir no construtor."));
}

// ===================================================================
//  [l] LOOPBACK  -  jumper PC12 (MOSI) no PC11 (MISO)
// ===================================================================
static void testeLoopback()
{
    Dbg.println(F("\n--- [l] LOOPBACK ---"));
    Dbg.println(F("  Pre-requisito: Ouroboros DESCONECTADA e um jumper"));
    Dbg.println(F("  ligando PC12 (MOSI) em PC11 (MISO)."));
    Dbg.println(F("  Com a Ouroboros ligada o SDO do DRV briga com o MOSI"));
    Dbg.println(F("  e o resultado nao significa nada."));

    static const uint16_t padroes[] = { 0xA5A5, 0x5A5A, 0x0001, 0x8000, 0xFFFF, 0x0000 };
    uint8_t acertos = 0;

    for (uint8_t i = 0; i < 6; i++) {
        uint16_t rx = transfer16(padroes[i]);
        Dbg.print(F("  tx=0x")); Dbg.print(padroes[i], HEX);
        Dbg.print(F("  rx=0x")); Dbg.print(rx, HEX);
        if (rx == padroes[i]) { Dbg.println(F("   ok")); acertos++; }
        else                    Dbg.println(F("   DIFERENTE"));
    }

    Dbg.print(F("\n  "));
    if (acertos == 6) {
        Dbg.println(F("VEREDITO: periferico SPI gera clock e trafega dados."));
        Dbg.println(F("          O problema esta' FORA do MCU: fio, alimentacao"));
        Dbg.println(F("          do DRV (medir DVDD!), SDO ou nSCS."));
    } else if (acertos == 0) {
        Dbg.println(F("VEREDITO: nada volta. Ou o jumper nao esta' no lugar, ou"));
        Dbg.println(F("          o periferico SPI nao esta' funcionando mesmo."));
        Dbg.println(F("          Rode [p] e confira o clock no osciloscopio."));
    } else {
        Dbg.println(F("VEREDITO: parcial - suspeite de integridade de sinal."));
    }
}

// ===================================================================
//  [v] VARREDURA DE MODOS
// ===================================================================
static void varrerModos()
{
    Dbg.println(F("\n--- [v] VARREDURA DE MODOS SPI ---"));
    Dbg.println(F("  Escreve 0x400 no reg 0x02 e rele, em cada modo."));

    const uint8_t modos[4] = { SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3 };
    uint8_t salvo = modo_atual;
    int8_t  vencedor = -1;

    for (uint8_t i = 0; i < 4; i++) {
        modo_atual = modos[i];
        escreverReg(0x02, 0x400);
        delayMicroseconds(10);
        uint16_t lido = lerReg(0x02);

        Dbg.print(F("  "));
        Dbg.print(nomeModo(modos[i]));
        Dbg.print(F(" -> leu "));
        imprimirHexBin(lido);
        if (lido == 0x400) { Dbg.println(F("   <<< CASOU")); if (vencedor < 0) vencedor = i; }
        else if (lido == 0x7FF) Dbg.println(F("   (MISO preso em 1)"));
        else if (lido == 0x000) Dbg.println(F("   (MISO preso em 0)"));
        else                    Dbg.println(F("   (responde, mas nao e' o escrito)"));
    }

    modo_atual = (vencedor >= 0) ? modos[vencedor] : salvo;

    Dbg.print(F("\n  "));
    if (vencedor >= 0) {
        Dbg.print(F("VEREDITO: use "));
        Dbg.println(nomeModo(modos[vencedor]));
    } else {
        Dbg.println(F("VEREDITO: nenhum modo responde. Nao e' questao de modo."));
        Dbg.println(F("          O DRV nao esta' falando. Ordem de ataque:"));
        Dbg.println(F("          1) MEDIR DVDD (regulador interno, ~5 V). Sem"));
        Dbg.println(F("             DVDD o SPI do driver esta' morto e nenhum"));
        Dbg.println(F("             ajuste de software resolve."));
        Dbg.println(F("          2) [b] bit-bang"));
        Dbg.println(F("          3) [l] loopback"));
    }
    escreverReg(0x02, 0x000);
}

// ===================================================================
//  [d] DUMP COM INTERPRETACAO
// ===================================================================
static void dump()
{
    static const char* nomes[8] = {
        "0x00 FAULT_STATUS_1 ", "0x01 VGS_STATUS_2   ",
        "0x02 DRIVER_CONTROL ", "0x03 GATE_DRIVE_HS  ",
        "0x04 GATE_DRIVE_LS  ", "0x05 OCP_CONTROL    ",
        "0x06 CSA_CONTROL    ", "0x07 DRIVER_CONFIG  "
    };
    // Valores de reset (SLVSDY6A). Servem de gabarito: se o DRV estiver
    // vivo e virgem, 0x03..0x07 tem que bater com esta coluna.
    // Repare que o reset do 0x04 E' 0x7FF - e' o unico registrador em
    // que 0x7FF nao quer dizer nada de errado. E' por isso que um dump
    // dos oito e' diagnostico e uma leitura isolada nao e'.
    static const uint16_t reset[8] = {
        0x000, 0x000, 0x000, 0x3FF, 0x7FF, 0x15B, 0x283, 0x000
    };

    Dbg.print(F("\n--- [d] DUMP  ("));
    Dbg.print(usar_bitbang ? F("bit-bang") : F("SPI hardware"));
    Dbg.print(F(", "));
    Dbg.print(nomeModo(modo_atual));
    Dbg.println(F(") ---"));

    uint8_t  n7FF = 0, n000 = 0, nIguaisReset = 0;
    uint16_t v[8];

    for (uint8_t a = 0; a < 8; a++) {
        v[a] = lerReg(a);
        Dbg.print(F("  "));
        Dbg.print(nomes[a]);
        Dbg.print(F("= "));
        imprimirHexBin(v[a]);
        if (a >= 3 && v[a] == reset[a]) { Dbg.print(F("   = reset")); nIguaisReset++; }
        Dbg.println();
        if (v[a] == 0x7FF) n7FF++;
        if (v[a] == 0x000) n000++;
    }

    Dbg.print(F("\n  nFAULT (pino) = "));
    Dbg.println(digitalRead(PIN_NFAULT) ? F("alto (sem falha)") : F("BAIXO (FALHA ATIVA)"));
    Dbg.print(F("  ENABLE (pino) = "));
    Dbg.println(digitalRead(PIN_ENABLE) ? F("alto") : F("baixo"));

    Dbg.println();
    if (n7FF == 8) {
        Dbg.println(F("  VEREDITO: TODOS 0x7FF. O MISO esta' preso em 1 - o DRV"));
        Dbg.println(F("  nunca puxa a linha. Isto NAO e' um registrador com"));
        Dbg.println(F("  valor estranho: e' ausencia de resposta. Note que"));
        Dbg.println(F("  0x00/0x01/0x02 deveriam ler 0x000 num DRV vivo, e o"));
        Dbg.println(F("  0x03 deveria ler 0x3FF, nao 0x7FF."));
        Dbg.println(F("  Em ordem de probabilidade:"));
        Dbg.println(F("   1. O codigo transmitia por outro periferico (o bug do"));
        Dbg.println(F("      'SPI' no lugar de 'SPI_3'). Corrigido aqui."));
        Dbg.println(F("   2. DVDD do DRV nao presente -> MEDIR."));
        Dbg.println(F("   3. SCK/MOSI nao chegam ao DRV -> [l] e osciloscopio."));
        Dbg.println(F("   4. nSCS nao desce -> ver PC9 no osciloscopio."));
    } else if (n000 == 8) {
        Dbg.println(F("  VEREDITO: TODOS 0x000. MISO preso em 0. Sem o pull-up"));
        Dbg.println(F("  o SDO (dreno aberto) nunca sobe - mas voce confirmou"));
        Dbg.println(F("  10k. Suspeite de curto do MISO ao GND."));
    } else if (nIguaisReset >= 3) {
        Dbg.println(F("  VEREDITO: COMUNICACAO OK. Os registradores de"));
        Dbg.println(F("  configuracao leem exatamente os valores de reset do"));
        Dbg.println(F("  datasheet - isso so' acontece se o DRV estiver"));
        Dbg.println(F("  respondendo de verdade. Pode seguir para a"));
        Dbg.println(F("  configuracao e a calibracao."));
    } else {
        Dbg.println(F("  VEREDITO: ha' resposta e ela varia por registrador -"));
        Dbg.println(F("  sinal de comunicacao viva. Compare com a coluna de"));
        Dbg.println(F("  reset. Se o 0x00 tiver bits em 1, LEIA AS FALHAS."));
    }
}

// ===================================================================
//  ESCRITA-E-VERIFICACAO
// ===================================================================
static bool escreverEVerificar(uint8_t endereco, uint16_t dados)
{
    escreverReg(endereco, dados);
    delayMicroseconds(10);
    uint16_t lido = lerReg(endereco);

    Dbg.print(F("  reg 0x0")); Dbg.print(endereco, HEX);
    Dbg.print(F("  escrito 0x")); Dbg.print(dados, HEX);
    Dbg.print(F("  lido 0x"));    Dbg.print(lido, HEX);

    bool ok = (lido == (dados & SPI_DATA_MASK));
    Dbg.println(ok ? F("   ok") : F("   NAO CONFERE"));
    return ok;
}

// ===================================================================
//  AJUDA / HEARTBEAT
// ===================================================================
static void ajuda()
{
    Dbg.println(F("\n================ TESTE SPI DRV8353RS ================"));
    Dbg.println(F("  p  mapeamento dos pinos (sem fio, responde na hora)"));
    Dbg.println(F("  d  dump dos 8 registradores, com interpretacao"));
    Dbg.println(F("  v  varredura dos 4 modos SPI"));
    Dbg.println(F("  b  alterna hardware SPI <-> bit-bang por GPIO"));
    Dbg.println(F("  l  loopback (precisa de jumper PC12-PC11)"));
    Dbg.println(F("  w  escreve-e-verifica 0x400 no reg 0x02"));
    Dbg.println(F("  e  alterna o pino ENABLE"));
    Dbg.println(F("  n  le o pino nFAULT"));
    Dbg.println(F("  0..3  forca SPI_MODEx"));
    Dbg.println(F("  ?  esta ajuda"));
    Dbg.println(F("====================================================="));
    Dbg.print  (F("  estado: "));
    Dbg.print(usar_bitbang ? F("bit-bang") : F("SPI hardware"));
    Dbg.print(F(", "));
    Dbg.print(nomeModo(modo_atual));
    Dbg.print(F(", ENABLE="));
    Dbg.println(digitalRead(PIN_ENABLE) ? F("1") : F("0"));
}

static uint32_t t_led = 0;
static void heartbeat()
{
    // Pisca sem delay(): se o LED parar, o firmware travou, e isso e'
    // informacao. Com delay() voce nao distingue travado de rodando.
    if (millis() - t_led >= 500) {
        t_led = millis();
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }
}

// ===================================================================
//  SETUP
// ===================================================================
void setup()
{
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    Dbg.begin(115200);
    delay(300);

    Dbg.println(F("\n\n#####################################################"));
    Dbg.println(F("#  DRV8353RS - validacao de SPI"));
    Dbg.print  (F("#  compilado "));
    Dbg.print(F(__DATE__)); Dbg.print(F(" ")); Dbg.println(F(__TIME__));
    Dbg.println(F("#####################################################"));
    // A data de compilacao acima e' deliberada: e' como voce confirma
    // que a placa esta' rodando o binario que acabou de gravar, e nao
    // um antigo que ficou na flash.

    // nSCS ALTO antes de qualquer outra coisa. Um CS flutuando faz o DRV
    // interpretar ruido do barramento como transacao.
    pinMode(DRV_NSCS, OUTPUT);
    digitalWrite(DRV_NSCS, HIGH);

    pinMode(PIN_NFAULT, INPUT);     // ja' tem pull-up externo na placa

    // ENABLE alto: sem ele o DRV dorme e o SPI NAO RESPONDE - os
    // registradores nem existem. E' pre-condicao de todo o resto.
    pinMode(PIN_ENABLE, OUTPUT);
    digitalWrite(PIN_ENABLE, HIGH);
    delay(5);                        // t_WAKE com folga

    SPI_3.begin();

    checarMapeamento();   // roda sozinho: e' a sua hipotese principal
    dump();               // e o estado de partida
    ajuda();
}

// ===================================================================
//  LOOP
// ===================================================================
void loop()
{
    heartbeat();

    if (!Dbg.available()) return;
    char c = Dbg.read();

    switch (c) {
        case 'p': checarMapeamento(); break;
        case 'd': dump();             break;
        case 'v': varrerModos();      break;
        case 'l': testeLoopback();    break;

        case 'b':
            usar_bitbang = !usar_bitbang;
            Dbg.print(F("\n>> transporte agora: "));
            Dbg.println(usar_bitbang ? F("BIT-BANG por GPIO") : F("SPI de hardware"));
            dump();
            break;

        case 'w':
            Dbg.println(F("\n--- [w] escreve-e-verifica ---"));
            escreverEVerificar(0x02, 0x400);
            escreverEVerificar(0x02, 0x000);
            break;

        case 'e':
            digitalWrite(PIN_ENABLE, !digitalRead(PIN_ENABLE));
            Dbg.print(F("\n>> ENABLE = "));
            Dbg.println(digitalRead(PIN_ENABLE) ? F("1") : F("0"));
            if (digitalRead(PIN_ENABLE)) delay(5);
            break;

        case 'n':
            Dbg.print(F("\n>> nFAULT = "));
            Dbg.println(digitalRead(PIN_NFAULT) ? F("alto (sem falha)")
                                                : F("BAIXO (falha ativa)"));
            break;

        case '0': modo_atual = SPI_MODE0; goto imprime_modo;
        case '1': modo_atual = SPI_MODE1; goto imprime_modo;
        case '2': modo_atual = SPI_MODE2; goto imprime_modo;
        case '3': modo_atual = SPI_MODE3;
        imprime_modo:
            Dbg.print(F("\n>> modo = "));
            Dbg.println(nomeModo(modo_atual));
            break;

        case '?': ajuda(); break;
        default: break;        // ignora \r \n e tecla errada
    }
}
