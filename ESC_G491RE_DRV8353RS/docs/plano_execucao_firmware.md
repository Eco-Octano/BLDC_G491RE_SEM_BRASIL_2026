# Plano de execução — firmware FOC, ESC custom + NUCLEO-G491RE

Estado em que estamos: **toda a cadeia analógica está definida**. Não há mais
incerteza estrutural bloqueando o código. Este documento é o que fica na bancada.

---

## Parte 0 — Constantes fechadas

Cadeia de medida, idêntica nas duas topologias:

```
shunt                 3 mΩ  (inline e low-side)
ganho                 20 V/V   INA240A1 fixo | CSA do DRV por SPI
transimpedância       60 mV/A
repouso               1,65 V
fundo de escala       ±27,5 A   (na prática um pouco menos: o INA240 não
                                 chega aos trilhos)
resolução             ~13 mA/LSB  com 12 bits e VREF+ = 3,3 V
MAX_TORQUE            19 A     — mantém, ~30% de margem até saturar
```

Motor e potência:

```
POLE_PAIRS            88        (confirmar no módulo 3)
PHASE_RESISTANCE      0,095 Ω
PHASE_INDUCTANCE      64,4 µH
POWER_SUPPLY          42 V
teto do SVPWM         42/√3 ≈ 24,2 V   <- MOTOR_LIMIT não pode passar disso
PWM_FREQ              20 kHz    (reavaliar no módulo 10 por eficiência)
```

Constantes que **não** têm valor ainda, e por quê:

| Constante | Sai de |
|---|---|
| `CURRENT_P` / `CURRENT_I` | tempo de ciclo medido no módulo 7 |
| `CURRENT_TF` | idem — tem que ficar FORA da banda |
| `ZERO_ELECTRIC_ANGLE` | alinhamento do módulo 6 |
| `SENSOR_DIRECTION` | idem |
| dead time | escolha do módulo 2, a partir do Qg dos MOSFETs |
| IDRIVE | idem |
| limiar de OCP | idem, a partir do Rds(on) quente |

---

## Parte 1 — Pendências de hardware e rework

Nada aqui bloqueia escrever código. Tudo bloqueia energizar.

### 1.1 Solder bridges — obrigatório antes de encaixar o shield

| SB | Estado | Motivo |
|---|---|---|
| SB17, SB23 | **abrir** | tira PA2/PA3 do VCP do ST-Link |
| SB18, SB22 | **fechar** | leva PA2/PA3 ao CN10-35/37 |
| SB13, SB19 | **abrir** | tira PC4/PC5 do CN10-35/37 |
| SB16 | abrir | libera PC13 do botão B1 |
| SB6 | abrir | libera PA5 do LED LD2 |
| SB15 | abrir | libera PB3 do TRACESWO |
| SB29–SB32 | conferir | PC14/PC15 estão no cristal LSE por padrão |
| SB36/SB37, SB35/SB34, SB7, SB33, SB3 | conferir | pinos compartilhados no morpho |

**Risco:** com SB13/SB19 fechados, as saídas de LSCSA e HSCS ficam em curto.
Verifique com continuidade antes de energizar.

### 1.2 JP8 para a posição [2-3]

VREF+ passa a ser alimentado por VDD (3,3 V) em vez do TL1431 (2,5 ou 3,25 V).
Sem isso, o fundo de escala do ADC pode cortar em ~14 A — abaixo do MAX_TORQUE, e
só no sentido positivo.

### 1.3 Option bytes

`nSWBOOT0 = 0`, `nBOOT0 = 1` via CubeProgrammer. PB8 é BOOT0 e você usa como CAN RX.

### 1.4 Verificações de esquemático ainda em aberto

- **Polaridade das shunts nos pinos SPx/SNx do DRV.** Trocar SP com SN inverte o
  sinal daquela fase.
- **`ANALOG_3V3` é a mesma rede do VDD do MCU?** Se for separada, o zero em 1,65 V
  não coincide exatamente com metade do VREF+, e vira um offset comum às três
  fases. A calibração da biblioteca corrige, mas é bom saber que existe.
- **Pull-ups dos halls** (5–10 kΩ): estão montados ou precisam dos internos?
- **Potência dos resistores de shunt:** a 19 A, 3 mΩ dissipa ~1,1 W. Confirme que
  são de 2 W ou mais.

### 1.5 Medições para o dia em que a placa voltar

1. VREF+ no pino da Nucleo
2. Saída de um INA240 em repouso — deve dar ~1,65 V
3. Continuidade CN10-35 → PA2 e CN10-37 → PA3, e **não** para PC4/PC5

---

## Parte 2 — Sequência de escrita do firmware

Ordem de construção. Cada módulo é testável isoladamente.

**M1 — Esqueleto.** Duas envs no `platformio.ini` (`inline` e `lowside`),
`board_pins.h` com a pinagem inteira, blink, serial em PB6/PB7. Resolver a
duplicação da `BLDCCustomMotor` entre `src/BLDC/` e `lib/BLDCCustomMotor/`.
*Aprovação: as duas builds compilam.*

**M2 — Driver SPI do DRV8353RS.** O maior bloco de código original. Framing,
escrita-com-releitura, `applyConfig()`, decodificação dos dois registradores de
falha. Configurar: modo 6x PWM, ganho do CSA em 20 V/V, **bit de referência do CSA
em VREF/2** (bidirecional), calibração de offset dos CSA, dead time, IDRIVE, OCP.
*Aprovação: escreve, relê e bate; sobrevive a um ciclo de ENABLE.*

**M3 — Halls.** Sem potência. Contar transições numa volta mecânica: devem ser
`6 × POLE_PAIRS` = 528.
*Aprovação: ângulo monotônico nos dois sentidos, contagem fecha.*

**M4 — Malha aberta.** Primeira energização, `voltage_limit = 2 V`, fonte com
limite de corrente apertado.
*Aprovação: gira suave, corrente equilibrada, nada esquenta.*

**M5 — Camada de corrente, duas implementações.** `LowsideCurrentSense(SHUNT, 20,
PA2, _NC, PB0)` e `GenericCurrentSense` com `_readRegularADCVoltage()` nos pinos
inline.
*Aprovação: as duas batem com alicate amperímetro.*

**M6 — Alinhamento.** `SKIP_STARTUP_ALIGN = 0`, roda livre, três medições com
reset entre elas.
*Aprovação: valores repetem.*

**M7 — Malha de corrente.** Medir tempo de ciclo primeiro; `Kp = L·ωc`,
`Ki = R·ωc`, `ωc ≤ 2π·(fs/10)`, `Kp/Ki = L/R = 678 µs`.
*Aprovação: degrau limpo, sem oscilação sustentada.*

**M8 — Aplicação.** Degraus de torque, botões, pulse-and-glide. Máquina de estados
sem `delay()`. Reconfigurar o DRV a cada rearme.
*Aprovação: degraus suaves, pulse-and-glide sem falha de driver.*

**M9 — Segurança.** IWDG, `TIM1_BKIN` no PB10, leitura de nível do nFAULT após
armar, corte por subtensão (PC5) e temperatura (PA3).
*Aprovação: cada falha provocada deliberadamente, resposta conforme projetado.*

**M10 — Telemetria e ensaio comparativo.** Low-side injetada fechando a malha,
inline por leitura regular como referência. Varrer o índice de modulação. Métricas:
desvio-padrão de Iq, erro contra medida externa, tempo de ciclo, e **potência no
barramento para o mesmo torque e velocidade na roda**.

---

## Parte 3 — Correções obrigatórias vindas do código antigo

Carregar para o novo firmware, não repetir:

- `BLDCDriver6PWM` instanciado com `PWM_UH_PIN…` que não existem — não compila
- `Serial.println(uint32, 3)` imprime em **base 3**, não com 3 casas
- `tempo_foc_us` só é atribuído no ramo de falha
- `driver_armed` lido/escrito na ISR sem ser `volatile`
- nFAULT tratado só por borda; é sinal de **nível**
- `delay()` de 2 ms dentro de `ArmDriver`/`DisarmDriver`
- `TORQUE_SLEW` em 1000 A/s (valor de calibração de PID)
- `MOTOR_LIMIT` em 30 V, acima do teto do SVPWM
- `CURRENT_TF` em 500 µs, dentro da banda da malha
- instrumentação de LED esquecida em volta do `loopFOC()`
- `motor.move(0)` como "planeio" — mantém as chaves comutando
- `MOTOR_KV` com comentário "ou é 10.5" ao lado de 6.064: parâmetros não fechados

---

## Parte 4 — Alavancas de eficiência

Para o comparativo com a versão CubeIDE:

- **Tempo morto:** DRV **ou** `driver.dead_zone`, nunca os dois. Somam.
- **IDRIVE:** chaveamento mais rápido reduz perda de comutação, ao custo de EMI.
- **Frequência de PWM:** perda de comutação escala com ela. Testar 16 kHz.
- **SVPWM:** ~15% a mais de barramento que senoidal. Já configurado.
- **Id = 0:** minimiza corrente total para um dado torque. Depende de medida boa.
- **Pulse-and-glide com desarme real**, não `move(0)`.
- **Malha rápida:** ripple é perda.

---

## Próximo passo imediato

Começar o **M1**. Depois **M2**, que é o bloco mais denso e o único inteiramente
escrevível sem placa.

Para o M2 é preciso ter em mãos: datasheet do DRV8353 (mapa de registradores e
valores de reset) e os parâmetros dos MOSFETs (Qg, Rds(on), Ciss) para decidir
dead time, IDRIVE e OCP.
