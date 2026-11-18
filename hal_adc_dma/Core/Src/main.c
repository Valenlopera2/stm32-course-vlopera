/*
 * main.c
 * Project 3 - TIM3 trigger at 40 kHz and DMA capture of 1024 samples
 */

#include "stm32f4xx_hal.h"
#include <stdio.h>     /* snprintf */
#include <string.h>    /* strlen   */

/* Heartbeat LED (onboard LED of the Nucleo-F411RE) */
#define LED_PORT         GPIOA
#define LED_PIN          GPIO_PIN_5

#define ADC_FULL_SCALE   4095U    /* largest 12-bit result       */
#define VREF_MV          3300U    /* VREF+ = 3.3 V on the Nucleo */

#define SAMPLE_RATE_HZ       40000U   /* TIM3 update rate = ADC trigger rate */
#define SAMPLE_PERIOD_TICKS  400U     /* 16 MHz / 40 kHz                     */
#define ADC_BUFFER_SIZE      1024U    /* 1024 samples = 25.6 ms at 40 kHz    */
#define MARK_PORT            GPIOA    /* PA11: high while a capture runs     */
#define MARK_PIN             GPIO_PIN_11
#define MIN_SWING_COUNTS     100U     /* below ~80 mV peak-to-peak: no signal */

/* Statistics of one capture, in raw counts */
typedef struct
{
    uint32_t min;
    uint32_t max;
    uint32_t mean;
} AdcStats;

TIM_HandleTypeDef  htim11;
TIM_HandleTypeDef  htim3;
UART_HandleTypeDef huart2;
ADC_HandleTypeDef  hadc1;
DMA_HandleTypeDef  hdma_adc1;                 /* global: stm32f4xx_it.c needs it */
static uint16_t    adcBuffer[ADC_BUFFER_SIZE]; /* filled by the DMA              */

/* Shared between interrupts and the main loop */
volatile uint8_t dataReceive = 0;
volatile uint8_t adcDone     = 0;

static void SystemClock_Config(void);
static void gpio_Init(void);
static void tim11_Init(void);
static void tim3_Init(void);
static void usart2_Init(void);
static void adc1_Init(void);
static void adc1_StartCapture(void);
static uint32_t adc1_ToMillivolts(uint32_t raw);
static AdcStats adc1_ComputeStats(void);
static uint32_t adc1_MeasureFrequency(AdcStats st);
static void adc1_PrintMeasurements(void);
static void adc1_SendSamples(void);
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
    tim3_Init();          /* sampling clock: 40 kHz on PC9, TRGO for the ADC */

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
            if (dataReceive == 'c')
            {
                adc1_StartCapture();         /* returns at once */
            }
            if (dataReceive == 's')
            {
                adc1_SendSamples();          /* the last capture, for the plot */
            }
            dataReceive = 0;
        }

        /* 2. The DMA filled the buffer */
        if (adcDone)
        {
            adcDone = 0;
            adc1_PrintMeasurements();
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

    /* PA11: capture marker for the oscilloscope */
    GPIO_InitStruct.Pin   = MARK_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(MARK_PORT, &GPIO_InitStruct);
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

/*
 * tim3_Init
 * TIM3 sets the sampling rate: one update event every 25 us (40 kHz).
 * Each update sends a trigger (TRGO) to the ADC; CH4 shows it on PC9.
 *
 *   fs = 16 MHz / ((PSC + 1) x (ARR + 1)) = 16 MHz / (1 x 400) = 40 kHz
 */
static void tim3_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    /* Step 1: enable clocks - TIM3 (on APB1) and GPIOC (its output pin) */
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Step 2: PC9 = TIM3_CH4, alternate function 2 */
    GPIO_InitStruct.Pin       = GPIO_PIN_9;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* Step 3: time base - count 0..399 at 16 MHz, that is 40 kHz */
    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 0;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = SAMPLE_PERIOD_TICKS - 1;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
    {
        Error_Handler();
    }

    /* Step 4: channel 4 - PWM mode 1, high while the counter is below 200 */
    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = SAMPLE_PERIOD_TICKS / 2;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
    {
        Error_Handler();
    }

    /* Step 5: every update event (counter back to 0) becomes TRGO */
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* Step 6: start counting and drive PC9; no interrupt is needed */
    if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4) != HAL_OK)
    {
        Error_Handler();
    }
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
 * ADC1, channel 8 (PB0), 12 bits, one conversion per TIM3 trigger.
 * Each result is copied to adcBuffer by DMA2 Stream0, channel 0.
 */
static void adc1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    ADC_ChannelConfTypeDef sConfig = {0};

    /* Step 1: enable clocks - ADC1, GPIOB (its input pin) and DMA2 */
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* Step 2: PB0 in analog mode, no pull resistor */
    GPIO_InitStruct.Pin  = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Step 3: DMA2 Stream0 - from the ADC data register to adcBuffer */
    hdma_adc1.Instance                 = DMA2_Stream0;
    hdma_adc1.Init.Channel             = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;  /* always DR    */
    hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;   /* next element */
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode                = DMA_NORMAL;        /* stop at end  */
    hdma_adc1.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
        Error_Handler();
    }
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    /* Step 4: configure ADC1
     *   clock 16 MHz / 2 = 8 MHz, 12 bits, result aligned to the right,
     *   one conversion on each rising edge of TIM3 TRGO
     */
    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV2;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIGCONV_T3_TRGO;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /* Step 5: channel 8, first (and only) in the sequence, 15-cycle sampling */
    sConfig.Channel      = ADC_CHANNEL_8;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* Step 6: NVIC - DMA transfer complete, and ADC errors (overrun) */
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_SetPriority(ADC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(ADC_IRQn);
}

/*
 * adc1_StartCapture
 * Arms ADC1 and the DMA for ADC_BUFFER_SIZE samples; TIM3 does the timing.
 * Returns at once: the DMA fills adcBuffer in the next 25.6 ms.
 */
static void adc1_StartCapture(void)
{
    HAL_StatusTypeDef status;

    HAL_GPIO_WritePin(MARK_PORT, MARK_PIN, GPIO_PIN_SET);
    status = HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adcBuffer, ADC_BUFFER_SIZE);
    if (status != HAL_OK)
    {
        HAL_GPIO_WritePin(MARK_PORT, MARK_PIN, GPIO_PIN_RESET);
        HAL_UART_Transmit(&huart2, (uint8_t *)"ADC busy\r\n", 10, 100);
    }
}

/* adc1_ToMillivolts - raw 12-bit result to millivolts (VREF+ = 3.3 V) */
static uint32_t adc1_ToMillivolts(uint32_t raw)
{
    return (raw * VREF_MV) / ADC_FULL_SCALE;
}

/*
 * adc1_ComputeStats
 * Minimum, maximum and mean of the last capture, in raw counts
 */
static AdcStats adc1_ComputeStats(void)
{
    AdcStats st = {ADC_FULL_SCALE, 0, 0};
    uint32_t sum = 0;

    for (uint32_t i = 0; i < ADC_BUFFER_SIZE; i++)
    {
        uint32_t sample = adcBuffer[i];

        if (sample < st.min)
        {
            st.min = sample;
        }
        if (sample > st.max)
        {
            st.max = sample;
        }
        sum += sample;
    }
    st.mean = sum / ADC_BUFFER_SIZE;
    return st;
}

/*
 * adc1_MeasureFrequency
 * Finds the rising crossings of the middle level, with hysteresis:
 * f = (crossings - 1) periods / (samples from first to last crossing / fs)
 * Returns 0 when the signal is too small or has fewer than two crossings.
 */
static uint32_t adc1_MeasureFrequency(AdcStats st)
{
    uint32_t swing = st.max - st.min;
    uint32_t mid   = st.min + swing / 2;       /* middle of the swing */
    uint32_t upper = mid + swing / 8;
    uint32_t lower = mid - swing / 8;
    uint32_t first = 0, last = 0, rises = 0;
    uint8_t  above = (adcBuffer[0] > mid);

    if (swing < MIN_SWING_COUNTS)
    {
        return 0;
    }
    for (uint32_t i = 1; i < ADC_BUFFER_SIZE; i++)
    {
        if (!above && (adcBuffer[i] > upper))      /* rising crossing */
        {
            above = 1;
            if (rises == 0)
            {
                first = i;
            }
            last = i;
            rises++;
        }
        else if (above && (adcBuffer[i] < lower))
        {
            above = 0;
        }
    }
    if (rises < 2)
    {
        return 0;
    }
    /* rounded integer division */
    return ((rises - 1) * SAMPLE_RATE_HZ + (last - first) / 2) / (last - first);
}

/*
 * adc1_PrintMeasurements
 * Prints the statistics of the last capture, in millivolts
 */
static void adc1_PrintMeasurements(void)
{
    char text[64];
    AdcStats st = adc1_ComputeStats();

    snprintf(text, sizeof(text), "min %lu  max %lu  mean %lu  Vpp %lu (mV)\r\n",
             (unsigned long)adc1_ToMillivolts(st.min),
             (unsigned long)adc1_ToMillivolts(st.max),
             (unsigned long)adc1_ToMillivolts(st.mean),
             (unsigned long)adc1_ToMillivolts(st.max - st.min));
    HAL_UART_Transmit(&huart2, (uint8_t *)text, strlen(text), 100);

    uint32_t freqHz = adc1_MeasureFrequency(st);

    if (freqHz == 0)
    {
        HAL_UART_Transmit(&huart2, (uint8_t *)"f: no period found\r\n", 20, 100);
    }
    else
    {
        snprintf(text, sizeof(text), "f = %lu Hz (%lu samples per period)\r\n",
                 (unsigned long)freqHz,
                 (unsigned long)(SAMPLE_RATE_HZ / freqHz));
        HAL_UART_Transmit(&huart2, (uint8_t *)text, strlen(text), 100);
    }
}

/*
 * adc1_SendSamples
 * Sends the last capture, one value in millivolts per line, to be plotted
 */
static void adc1_SendSamples(void)
{
    char text[12];

    for (uint32_t i = 0; i < ADC_BUFFER_SIZE; i++)
    {
        snprintf(text, sizeof(text), "%lu\r\n",
                 (unsigned long)adc1_ToMillivolts(adcBuffer[i]));
        HAL_UART_Transmit(&huart2, (uint8_t *)text, strlen(text), 100);
    }
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
 * With DMA it runs once per capture, when the last sample is in adcBuffer.
 * Interrupt context: stop the ADC, lower the marker, raise the flag.
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        HAL_ADC_Stop_DMA(hadc);
        HAL_GPIO_WritePin(MARK_PORT, MARK_PIN, GPIO_PIN_RESET);
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
