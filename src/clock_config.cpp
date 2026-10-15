/*
  clock_config.cpp - configuracao de clock do STM32G491RE
  [G491] Correcao obrigatoria. Nao remova.

  A variante GENERICA do stm32duino nao entrega os 170 MHz declarados no
  board. O SysTick acaba configurado a partir de um SystemCoreClock que nao
  corresponde ao clock real, e o delay() ou trava ou fica muito mais lento
  que o pedido.

  Sintoma: um blink com delay() deixa o pino parado, enquanto o MESMO blink
  com um laco de contagem no lugar do delay funciona.

  O SystemClock_Config() do core e' WEAK, entao esta definicao tem
  precedencia. Nada precisa ser chamado.

  IMPORTA MUITO PARA SPI: a frequencia do SCLK e' derivada do APB. Com o
  clock errado, o SPI roda numa frequencia diferente da que voce configurou
  em SPISettings - e voce vai medir isso no osciloscopio sem entender.

  CADEIA:
    HSI16 -> PLLM /4 -> 4 MHz -> PLLN x85 -> 340 MHz -> PLLR /2 -> 170 MHz
    AHB /1, APB1 /1, APB2 /1

  Usa HSI (interno, ~1%), nao HSE - o HSE da Nucleo depende de solder bridges.
*/

#include <Arduino.h>

extern "C" void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {};
    RCC_ClkInitTypeDef clk = {};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM            = RCC_PLLM_DIV4;
    osc.PLL.PLLN            = 85;
    osc.PLL.PLLP            = RCC_PLLP_DIV2;
    osc.PLL.PLLQ            = RCC_PLLQ_DIV2;
    osc.PLL.PLLR            = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { while (1) { } }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK  | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) { while (1) { } }
}
