#include <stdint.h>
#include <stm32f0xx_hal.h>
#include <stm32f0xx_hal_gpio.h>

void My_HAL_GPIO_Init(GPIO_TypeDef  *GPIOx, GPIO_InitTypeDef *GPIO_Init)
{
    (void)GPIO_Init; 

    if (GPIOx == GPIOC)
    {
        // Set output mode
        GPIOC->MODER |=
            (1U << (6 * 2)) |
            (1U << (7 * 2)) |
            (1U << (8 * 2)) |
            (1U << (9 * 2));

        GPIOC->OTYPER &= ~(
            (1U << 6) |
            (1U << 7) |
            (1U << 8) |
            (1U << 9)
        );

        GPIOC->OSPEEDR &= ~(
            (3U << (6 * 2)) |
            (3U << (7 * 2)) |
            (3U << (8 * 2)) |
            (3U << (9 * 2))
        );

        GPIOC->PUPDR &= ~(
            (3U << (6 * 2)) |
            (3U << (7 * 2)) |
            (3U << (8 * 2)) |
            (3U << (9 * 2))
        );
    }

    if (GPIOx == GPIOA)
    {
    // MODER: PA0 input 
    GPIOA->MODER &= ~(3U << (0 * 2));

    // OSPEEDR: low speed 
    GPIOA->OSPEEDR &= ~(3U << (0 * 2));

    // PUPDR: pull-down 
    GPIOA->PUPDR &= ~(3U << (0 * 2));
    GPIOA->PUPDR |=  (2U << (0 * 2));
    }
}

GPIO_PinState My_HAL_GPIO_ReadPin(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin)
{
    if (GPIOx->IDR & GPIO_Pin)
        return GPIO_PIN_SET;
    else
        return GPIO_PIN_RESET;
}

void My_HAL_GPIO_WritePin(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
    if (PinState == GPIO_PIN_SET)
    {
        GPIOx->BSRR = GPIO_Pin;              // Set bit
    }
    else
    {
        GPIOx->BSRR = (GPIO_Pin << 16);      // Reset bit
    }
}

void My_HAL_GPIO_TogglePin(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin)
{
    GPIOx->ODR ^= GPIO_Pin;
}

