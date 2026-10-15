#pragma once
/*
  current_backend.h - camada de corrente, tres implementacoes atras de uma
  interface so'.

  O FOC nao deve saber COMO a corrente foi lida. Se souber, comparar as duas
  topologias vira comparar dois firmwares, e a comparacao perde o sentido.

  Selecione pela env do platformio.ini:

    CS_MODE_INLINE        producao. GenericCurrentSense + _readRegularADCVoltage()
                          nos tres pinos inline. Sem restricao de ADC.
    CS_MODE_INLINE_ALIGN  FERRAMENTA. InlineCurrentSense convencional nos mesmos
                          pinos, com o alinhamento LIGADO. Use uma vez para
                          descobrir o sinal de cada fase (ver abaixo).
    CS_MODE_LOWSIDE       producao. LowsideCurrentSense em duas fases (A e C,
                          ambas no ADC1), conversao injetada sincrona ao TIM1.
                          Fase B reconstruida por Ia + Ib + Ic = 0.

  ------------------------------------------------------------------
  POR QUE EXISTE O MODO _ALIGN
  ------------------------------------------------------------------
  Duas descobertas na leitura do codigo da SimpleFOC 2.4.0:

  1. O driverAlign() da GenericCurrentSense e' um STUB - retorna 1 e nao faz
     nada. Ela NAO descobre o sinal nem a ordem das fases.

  2. O getPhaseCurrents() dela SO' subtrai o offset:
         current.a = (current.a - offset_ia);
     Os campos gain_a/gain_b/gain_c NAO sao usados. Escrever
     "cs.gain_a *= -1" nao tem efeito nenhum - e' o pior tipo de bug,
     o que compila, roda e produz sinal errado em silencio.

  Conclusao: no modo INLINE o callback e' o unico lugar onde o sinal pode ser
  corrigido. E como descobrir o sinal certo sem chutar? Rodando UMA vez a
  InlineCurrentSense convencional, que tem o alinhamento de verdade, nos
  mesmos pinos. Ela imprime o que encontrou; voce traz para CS_SIGN_A/B/C.

  ------------------------------------------------------------------
  OFFSET: NAO SE PREOCUPE COM O 1,65 V
  ------------------------------------------------------------------
  A calibrateOffsets() chama o callback 1000 vezes e tira a media, JA em
  amperes. Ou seja: devolva  tensao / (shunt * ganho)  e deixe a biblioteca
  medir e remover o residuo. Isso absorve de uma vez o repouso do INA240, a
  incerteza do VREF+ e o offset do proprio amplificador.

  DUAS CONSEQUENCIAS:
    - o init() BLOQUEIA por ~1 s (1000 iteracoes com _delay(1)). Nao e' travo.
    - NADA pode estar acionado nesse momento. Corrente circulando durante a
      calibracao vira offset permanente, e o sintoma depois e' "o motor puxa
      corrente parado".
*/

#include <SimpleFOC.h>
#include "board_pins.h"

// ===================================================================
// VREF+ DO ADC
// ===================================================================
// A SimpleFOC assume 3,3 V (_ADC_VOLTAGE_G4 3.3f, _ADC_RESOLUTION_G4 4096.0f).
// A NUCLEO-G491RE, de fabrica, alimenta o VREF+ pelo TL1431: 2,5 V ou 3,25 V
// conforme o SB14. Com 2,5 V a leitura satura em ~14 A - abaixo do
// MAX_CURRENT_A, e so' no sentido positivo.
//
// Se o JP8 for movido para [2-3] (VREF+ = VDD = 3,3 V), deixe 3.3f e o fator
// vira 1,0. Se mantiver o TL1431, MEDA com multimetro e ponha o valor aqui.
//
// So' vale para o modo INLINE: no LOWSIDE a conversao e' interna a biblioteca
// e este fator nao tem como ser aplicado - ali o JP8 precisa mesmo ir para VDD.
static const float VREF_PLUS_V   = 3.3f;
static const float VREF_SCALE    = VREF_PLUS_V / 3.3f;

// ===================================================================
// SINAL DE CADA FASE  (so' usado no modo INLINE)
// ===================================================================
// Preencher com o resultado do modo CS_MODE_INLINE_ALIGN. Enquanto nao
// medir, deixe todos em +1: o motor nao vai girar direito, mas nada quebra
// enquanto a malha de corrente estiver aberta.
static const float CS_SIGN_A = +1.0f;
static const float CS_SIGN_B = +1.0f;
static const float CS_SIGN_C = +1.0f;

// Transimpedancia: 3 mOhm x 20 V/V = 60 mV/A, identica nas duas topologias.
static const float CS_VOLTS_PER_AMP = SHUNT_OHM * CS_GAIN_VpV;

// ===================================================================
#if defined(CS_MODE_INLINE)

  // Caminho interno da biblioteca. Se a SimpleFOC reorganizar as pastas numa
  // atualizacao, e' esta linha que quebra primeiro.
  #include "current_sense/hardware_specific/stm32/stm32_mcu.h"

  // Devolve amperes. O offset e' removido pela biblioteca depois.
  static PhaseCurrent_s readInlinePhases()
  {
      PhaseCurrent_s c;
      c.a = CS_SIGN_A * _readRegularADCVoltage(PIN_ILCS_A) * VREF_SCALE / CS_VOLTS_PER_AMP;
      c.b = CS_SIGN_B * _readRegularADCVoltage(PIN_ILCS_B) * VREF_SCALE / CS_VOLTS_PER_AMP;
      c.c = CS_SIGN_C * _readRegularADCVoltage(PIN_ILCS_C) * VREF_SCALE / CS_VOLTS_PER_AMP;
      return c;
  }

  GenericCurrentSense current_sense = GenericCurrentSense(readInlinePhases);

  static const char *CS_BACKEND_NAME = "INLINE (Generic + regular ADC)";
  static const bool  CS_NEEDS_DRIVER = false;   // le sem PWM: nao depende do driver
  static const bool  CS_IS_ALIGN_TOOL = false;

// ===================================================================
#elif defined(CS_MODE_INLINE_ALIGN)

  // FERRAMENTA, nao producao. A InlineCurrentSense usa analogRead() - a
  // propria documentacao adverte contra isso durante o FOC (reconfigura o ADC
  // a cada chamada, adiciona latencia e jitter). Serve so' para uma coisa:
  // rodar o driverAlign() de verdade e revelar o sinal de cada fase.
  //
  // Note que o construtor recebe o ganho REAL do INA240 (20 V/V) e o shunt.
  // Aqui a conversao e' interna a biblioteca, entao o VREF_SCALE nao se aplica:
  // se o VREF+ nao for 3,3 V, os valores absolutos sairao errados por um fator
  // fixo. Nao importa - o que interessa neste modo e' o SINAL, nao o modulo.
  InlineCurrentSense current_sense =
      InlineCurrentSense(SHUNT_OHM, CS_GAIN_VpV, PIN_ILCS_A, PIN_ILCS_B, PIN_ILCS_C);

  static const char *CS_BACKEND_NAME = "INLINE-ALIGN (ferramenta de calibracao)";
  static const bool  CS_NEEDS_DRIVER = true;    // o alinhamento energiza as fases
  static const bool  CS_IS_ALIGN_TOOL = true;

// ===================================================================
#elif defined(CS_MODE_LOWSIDE)

  // PA2 = ADC1_IN3 e PB0 = ADC1_IN15: ambos no ADC1, entao a SimpleFOC
  // consegue montar a sequencia injetada disparada pelo TIM1.
  // PA6 (fase B) so' existe no ADC2 -> _NC. A fase B e' reconstruida.
  //
  // A ORDEM IMPORTA: sao as fases A e C, entao o _NC vai no MEIO.
  //
  // O ganho de 20 V/V aqui NAO e' fixo por hardware: vem do registrador
  // CSA_GAIN do DRV8353RS. Se o applyConfig() nao rodar (ou rodar com outro
  // valor), esta constante mente e toda a malha de corrente sai escalada.
  // Por isso o enable() do driver reconfigura e VERIFICA a cada rearme.
  LowsideCurrentSense current_sense =
      LowsideCurrentSense(SHUNT_OHM, CS_GAIN_VpV, PIN_LSCS_A, _NC, PIN_LSCS_C);

  static const char *CS_BACKEND_NAME = "LOWSIDE (2 fases, injetada, ADC1)";
  static const bool  CS_NEEDS_DRIVER = true;    // precisa do TIM1 para disparar
  static const bool  CS_IS_ALIGN_TOOL = false;

#else
  #error "Defina CS_MODE_INLINE, CS_MODE_INLINE_ALIGN ou CS_MODE_LOWSIDE"
#endif
