// Clock do STM32G491RE. Sobrepõe o SystemClock_Config() fraco do core.
// Corrige duas coisas que a variante genérica do stm32duino não entrega:
//
//   1. Os 170 MHz declarados pelo board. Sem isso o SystemCoreClock
//      diverge do clock real e o delay() trava ou fica lento.
//   2. O clock de kernel do ADC. No G4 o ADC tem multiplexador próprio,
//      cujo estado de reset é "nenhuma fonte". Nesse estado toda conversão
//      devolve zero, sem erro e sem travar.
//
// Cadeia: HSI16 -> /4 -> x85 -> /2 = 170 MHz, AHB/APB1/APB2 sem divisão.
// Usa HSI e não HSE porque o HSE da Nucleo depende de solder bridges.

#include <Arduino.h>

extern "C" void SystemClock_Config(void)
{
    RCC_OscInitTypeDef       osc = {};
    RCC_ClkInitTypeDef       clk = {};
    RCC_PeriphCLKInitTypeDef per = {};

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

    per.PeriphClockSelection = RCC_PERIPHCLK_ADC12 | RCC_PERIPHCLK_ADC345;
    per.Adc12ClockSelection  = RCC_ADC12CLKSOURCE_SYSCLK;
    per.Adc345ClockSelection = RCC_ADC345CLKSOURCE_SYSCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK) { while (1) { } }
}
