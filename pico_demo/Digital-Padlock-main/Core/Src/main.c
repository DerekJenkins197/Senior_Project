#include "stm32f0xx.h"

static void delay(volatile uint32_t count)
{
    while (count--) {
        __NOP();
    }
}


int main(void)
{

    // Enable GPIOA
RCC->AHBENR |= RCC_AHBENR_GPIOAEN;

// Configure PA5
GPIOA->MODER &= ~(3 << (5 * 2));
GPIOA->MODER |=  (1 << (5 * 2));

GPIOA->OTYPER &= ~(1 << 5);
GPIOA->PUPDR  &= ~(3 << (5 * 2));

while (1)
{
    GPIOA->ODR ^= (1 << 5);
    delay(1000000);
}
}