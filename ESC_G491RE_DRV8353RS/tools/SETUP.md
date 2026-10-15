# SETUP — projeto Arduino na NUCLEO-G491RE

Passo a passo para criar um projeto novo. As cinco correções abaixo foram
descobertas por eliminação; nenhuma delas dá mensagem de erro útil.

**Causa raiz comum:** o stm32duino conhece o *chip* STM32G491, mas não conhece
a *placa* NUCLEO-G491RE. Não existe variante de placa — só a genérica do chip,
que é compartilhada com o G491RC e o G4A1RE.

## Passo a passo

1. **Criar o projeto** no PlatformIO. Board: `Nucleo G491RE (ST)`.
   Framework: `STM32Cube` — sim, o "errado", porque é o único que aparece.
   O passo 3 substitui isso.

2. **Criar a pasta `boards/`** na raiz e colocar lá o
   `nucleo_g491re_arduino.json`.

3. **Substituir o `platformio.ini`** inteiro pelo do projeto.

4. **Nada a fazer com o header de variante.** O `tools/install_variant.py`
   roda antes de cada build e copia `variant_NUCLEO_G491RE_ARDUINO.h` para
   dentro do pacote do framework, se ainda não estiver lá.

   Num **computador novo**, o pacote `framework-arduinoststm32` ainda não
   existe — o PlatformIO só baixa no primeiro build. O script lida com isso:
   se o primeiro build reclamar do header, simplesmente rode de novo.
   No segundo, o pacote já existe e o script instala o arquivo.

5. **Colocar `clock_config.cpp` em `src/`**, ao lado do `main.cpp`.
   São dois arquivos separados — não junte um dentro do outro.

6. `pio run -t upload`

## As cinco correções

| # | Sintoma | Correção |
|---|---|---|
| 1 | O wizard só oferece STM32Cube | Board customizado em `boards/` |
| 2 | `variant_NUCLEO_G491RE_ARDUINO.h: No such file` | `board_build.variant_h` + o header no pacote |
| 3 | HardFault antes do `main()`; nem um blink roda | `maximum_ram_size = 114688` (112 KB, não 128) |
| 4 | `Debug adapter doesn't support 'hla_swd'` | `upload_protocol = custom` com openocd e o `.elf` |
| 5 | `delay()` trava; blink com laço de contagem funciona | `clock_config.cpp` próprio |

E uma sexta, de firmware: `HardwareSerial(rx, tx)` deduz o periférico pelos
pinos e o **RX não funciona** — o TX transmite, mas `available()` é sempre
falso. Use `HardwareSerial(USART1)` com `setRx()`/`setTx()` antes do `begin()`.

## Detalhes que custam tempo

**RAM de 112 KB, não 128.** O G491 tem SRAM1+SRAM2 contíguas até `0x2001C000`,
mais 16 KB de CCM em endereço separado. O `boards_entry.txt` do core declara
131072 porque esse é o valor do G4A1RE, que compartilha a mesma pasta de
variante. Com 128 KB o `_estack` nasce em memória inexistente e o primeiro
`push` vira HardFault.

O JSON sozinho **não basta** — é preciso o `board_upload.maximum_ram_size` no
`platformio.ini` também.

**Grave o `.elf`, não o `.bin`.** O `.bin` não carrega endereço e o OpenOCD
tenta gravar em `0x00000000`: a gravação até acontece, mas a verificação falha
com `checksum mismatch`.

**O `variant_generic.h` não mapeia pinos de placa.** Nada de `LED_BUILTIN`,
`D13` ou `A0`. Use só nomes `PXn`, e confirme no `PeripheralPins.c` da variante
que o pino existe para a função que você quer. `digitalWrite` num pino não
mapeado é HardFault na hora.

**O header de variante mora dentro do pacote do framework** e some em
computador novo, reinstalação ou atualização do framework. Por isso o
`install_variant.py` o reinstala a cada build. No log você vê
`[G491] header de variante instalado em ...` quando ele age.

Se a ST mudar o nome da pasta da variante numa versão futura do framework, o
script avisa com `pasta da variante nao existe` — aí é só atualizar o caminho
em `VARIANT` no script.

## Como saber que tudo pegou

No log do build:

```
HARDWARE: STM32G491RET6 170MHz, 112KB RAM, 512KB Flash
RAM: ... from 114688 bytes
Compiling .pio\build\...\src\clock_config.cpp.o
Compiling .pio\build\...\src\main.cpp.o
```

E no console, o comando `C` deve mostrar `SYSCLK : 170000000`.

Se o SYSCLK vier diferente, o `clock_config.cpp` não está sendo compilado.

## Verificação por SWD, quando nada aparece

```powershell
$ocd = "$env:USERPROFILE\.platformio\packages\tool-openocd"
& "$ocd\bin\openocd.exe" -s "$ocd\openocd\scripts" -f interface/stlink.cfg `
  -c "transport select swd" -f target/stm32g4x.cfg `
  -c "init; reset run; sleep 500; halt; reg pc; reg msp; shutdown"
```

- `current mode: Thread` → executando
- `current mode: Handler HardFault` → travou; olhe RAM e pinos
- `msp` acima de `0x2001C000` → o tamanho de RAM não pegou
