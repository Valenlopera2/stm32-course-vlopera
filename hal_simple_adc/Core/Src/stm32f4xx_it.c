/*
 * stm32f4xx_it.c
 * Interrupt service routines
 */

#include "stm32f4xx_hal.h"

/* Handles defined in main.c */
extern TIM_HandleTypeDef htim11;

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
