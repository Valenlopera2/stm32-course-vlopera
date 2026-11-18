/*
 * stm32f4xx_it.c
 * Interrupt service routines
 */

#include "stm32f4xx_hal.h"

/* Handles defined in main.c */
extern TIM_HandleTypeDef htim11;
extern UART_HandleTypeDef huart2;
extern ADC_HandleTypeDef  hadc1;
extern DMA_HandleTypeDef  hdma_adc1;

/* SysTick handler - required by HAL for HAL_GetTick() and timeouts */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* TIM11 update event (shared vector name with TIM1 trigger/commutation) */
void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim11);
}

/*
 * USART2_IRQHandler
 * The name must match the vector table in startup_stm32f411xx.S
 */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

/*
 * ADC_IRQHandler
 * One vector shared by every ADC of the family; the F411 has only ADC1
 */
void ADC_IRQHandler(void)
{
    HAL_ADC_IRQHandler(&hadc1);
}

/*
 * DMA2_Stream0_IRQHandler
 * DMA2 Stream0 serves ADC1: half transfer, transfer complete and errors
 */
void DMA2_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc1);
}
