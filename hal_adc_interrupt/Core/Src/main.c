/*
 * main.c
 * Project 2 - commands and ADC by interrupt
 */

#include "stm32f4xx_hal.h"
#include <stdio.h>     /* snprintf */
#include <string.h>    /* strlen   */

/* Heartbeat LED (onboard LED of the Nucleo-F411RE) */
#define LED_PORT         GPIOA
#define LED_PIN          GPIO_PIN_5

#define ADC_FULL_SCALE   4095U    /* largest 12-bit result       */
#define VREF_MV          3300U    /* VREF+ = 3.3 V on the Nucleo */

TIM_HandleTypeDef  htim11;
UART_HandleTypeDef huart2;
ADC_HandleTypeDef  hadc1;

/* Last character received by USART2 - written in the interrupt, read in the loop */
volatile uint8_t dataReceive = 0;

/* ADC result - written in the interrupt, read in the loop */
volatile uint32_t adcRawData = 0;
volatile uint8_t  adcDone    = 0;

static void SystemClock_Config(void);
static void gpio_Init(void);
static void tim11_Init(void);
static void usart2_Init(void);
static void adc1_Init(void);
static void adc1_PrintValue(uint32_t raw);
static void Error_Handler(void);

int main(void)
{
    char msg[] = "Hello from STM32\r\n";

    HAL_Init();
    SystemClock_Config();
    gpio_Init();
    tim11_Init();
    usart2_Init();
    adc1_Init();

    HAL_UART_Transmit(&huart2, (uint8_t *)msg, sizeof(msg) - 1, 100);

    while (1)
    {
        /* 1. A character arrived: run the command, then forget it */
        if (dataReceive != 0)
        {
            if (dataReceive == 'm')
            {
                HAL_UART_Transmit(&huart2, (uint8_t *)msg, sizeof(msg) - 1, 100);
            }
            if ((dataReceive == 'c') && (HAL_ADC_Start_IT(&hadc1) != HAL_OK))
            {
                HAL_UART_Transmit(&huart2, (uint8_t *)"ADC error\r\n", 11, 100);
            }
            dataReceive = 0;
        }

        /* 2. A conversion ended: print the result the interrupt stored */
        if (adcDone)
        {
            adcDone = 0;
            adc1_PrintValue(adcRawData);
        }
    }
}

/* HSI at 16 MHz, no PLL, every bus at 16 MHz */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_HCLK   |
                                       RCC_CLOCKTYPE_PCLK1  |
                                       RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
}

/* LED pin as push-pull output */
static void gpio_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin   = LED_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
}

/*
 * tim11_Init
 * Heartbeat: TIM11 (APB2, 16 MHz) update every 250 ms
 *   PSC = 15999 -> 1 kHz tick;  ARR = 249 -> (249 + 1) x 1 ms = 250 ms
 */
static void tim11_Init(void)
{
    __HAL_RCC_TIM11_CLK_ENABLE();

    htim11.Instance               = TIM11;
    htim11.Init.Prescaler         = 15999;
    htim11.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim11.Init.Period            = 249;
    htim11.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim11.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim11);

    HAL_TIM_Base_Start_IT(&htim11);
    HAL_NVIC_EnableIRQ(TIM1_TRG_COM_TIM11_IRQn);
}

/* USART2 on PA2 (TX) / PA3 (RX), AF7, 115200 8N1 */
static void usart2_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);

    /* Step 4: NVIC - accept USART2 interrupt requests (priority 1) */
    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    /* Step 5: ask for the first character; the interrupt will store it */
    HAL_UART_Receive_IT(&huart2, (uint8_t *)&dataReceive, 1);
}

/*
 * adc1_Init
 * ADC1, channel 8 (PB0), 12 bits, one conversion per software start
 *
 * Clock chain:
 *   HSI (16 MHz) -> APB2 (16 MHz) -> ADC prescaler /2 -> ADCCLK = 8 MHz
 */
static void adc1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    ADC_ChannelConfTypeDef sConfig = {0};

    /* Step 1: enable clocks - ADC1 (on APB2) and GPIOB (its input pin) */
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Step 2: PB0 in analog mode, no pull resistor */
    GPIO_InitStruct.Pin  = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Step 3: configure ADC1
     *   clock 16 MHz / 2 = 8 MHz, 12 bits, result aligned to the right,
     *   one channel (no scan), one conversion per software start,
     *   EOC flag set after each conversion
     */
    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV2;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /* Step 4: channel 8, first (and only) in the sequence, 15-cycle sampling */
    sConfig.Channel      = ADC_CHANNEL_8;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* Step 5: NVIC - accept ADC interrupt requests (priority 1) */
    HAL_NVIC_SetPriority(ADC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(ADC_IRQn);
}

/*
 * adc1_PrintValue
 * Converts a raw 12-bit result to millivolts and prints both
 */
static void adc1_PrintValue(uint32_t raw)
{
    char text[40];
    uint32_t millivolts = (raw * VREF_MV) / ADC_FULL_SCALE;

    snprintf(text, sizeof(text), "adc = %lu (%lu mV)\r\n",
             (unsigned long)raw, (unsigned long)millivolts);
    HAL_UART_Transmit(&huart2, (uint8_t *)text, strlen(text), 100);
}

/* Called by HAL_TIM_IRQHandler() on every timer update event */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM11)
    {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    }
}

/*
 * HAL_UART_RxCpltCallback
 * Called by HAL_UART_IRQHandler() when the requested character arrived.
 * It is already in dataReceive: ask for the next one, and leave.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        HAL_UART_Receive_IT(&huart2, (uint8_t *)&dataReceive, 1);
    }
}

/*
 * HAL_ADC_ConvCpltCallback
 * Called by HAL_ADC_IRQHandler() at the end of conversion (EOC).
 * Interrupt context: store the result, raise the flag, leave.
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        adcRawData = HAL_ADC_GetValue(hadc);   /* read the data register */
        adcDone = 1;
    }
}

/*
 * Error_Handler
 * Stops every interrupt and stays here forever.
 * The LED freezes: the heartbeat rule's "CPU stuck" signature.
 */
static void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
        /* the debugger's call stack shows which call failed */
    }
}
