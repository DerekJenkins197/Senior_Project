#include "stm32f0xx.h"
#include "stepper.h"

static volatile uint32_t steps_taken;   // steps in the current sweep
static volatile uint8_t  sweep_cw = 1;  // current direction

/* PC0 has no timer alternate function, so TIM2 interrupts drive it:
 *   update event (every STEP_PERIOD_MS)   -> PC0 high
 *   CH1 compare  (STEP_PULSE_WIDTH_MS in) -> PC0 low
 * See TIM2_IRQHandler in stm32f0xx_it.c.
 */
void stepper_init(void)
{
    RCC->AHBENR  |= RCC_AHBENR_GPIOCEN;                     // Enable GPIOC clock
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;                     // Enable TIM2 clock

    // PC0 (Clock) and PC3 (CW/CCW) -> push-pull outputs, no pull-up/down
    GPIOC->MODER &= ~(GPIO_MODER_MODER0 | GPIO_MODER_MODER3);
    GPIOC->MODER |=  (GPIO_MODER_MODER0_0 | GPIO_MODER_MODER3_0);
    GPIOC->OTYPER &= ~(GPIO_OTYPER_OT_0 | GPIO_OTYPER_OT_3);
    GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPDR0 | GPIO_PUPDR_PUPDR3);
    GPIOC->BSRR = GPIO_BSRR_BR_0;                           // Clock starts low
    stepper_set_direction(sweep_cw);

    // 8 MHz / 8000 = 1 kHz timer tick (1 ms per count)
    TIM2->PSC = 7999;
    TIM2->ARR = STEP_PERIOD_MS - 1;                         // 1 count per ms
    TIM2->CCR1 = STEP_PULSE_WIDTH_MS;                       // End of the high pulse
    TIM2->EGR |= TIM_EGR_UG;                                // Load PSC/ARR

    // Interrupt on update (pulse start) and CH1 compare (pulse end)
    TIM2->SR   &= ~(TIM_SR_UIF | TIM_SR_CC1IF);
    TIM2->DIER |= TIM_DIER_UIE | TIM_DIER_CC1IE;
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM2->CR1 |= TIM_CR1_CEN;                               // Start timer
}

// SAA1042 CW/CCW: low = clockwise, high = counter-clockwise
void stepper_set_direction(uint8_t clockwise)
{
    if (clockwise) {
        GPIOC->BSRR = GPIO_BSRR_BR_3;
    } else {
        GPIOC->BSRR = GPIO_BSRR_BS_3;
    }
}

/* Called from TIM2_IRQHandler at the end of each step pulse.
 * Changing direction here, while the clock is low, gives CW/CCW the
 * rest of the period to settle before the next rising edge.
 */
void stepper_step_done(void)
{
    steps_taken++;
    if (steps_taken >= STEPS_PER_SWEEP) {
        steps_taken = 0;
        sweep_cw = !sweep_cw;
        stepper_set_direction(sweep_cw);
    }
}
