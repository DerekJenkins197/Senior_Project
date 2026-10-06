#include "main.h"
#include "stm32f0xx_hal.h"
#include "stm32f0xx_it.h"
#include "stepper.h"

/******************************************************************************/
/*           Cortex-M0 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  while (1)
  {
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/* STM32F0xx Peripheral Interrupt Handlers                                    */
/******************************************************************************/

// TIM2 update starts each step pulse on PC0, CH1 compare ends it
void TIM2_IRQHandler(void)
{
  if (TIM2->SR & TIM_SR_UIF)
  {
    TIM2->SR &= ~TIM_SR_UIF;    // clear interrupt flag
    GPIOC->BSRR = GPIO_BSRR_BS_0; // Clock pin high
    GPIOC->ODR ^= GPIO_ODR_9;   // toggle green LED each step
  }
  if (TIM2->SR & TIM_SR_CC1IF)
  {
    TIM2->SR &= ~TIM_SR_CC1IF;  // clear interrupt flag
    GPIOC->BSRR = GPIO_BSRR_BR_0; // Clock pin low
    stepper_step_done();        // count the step, reverse after 90 degrees
  }
}
