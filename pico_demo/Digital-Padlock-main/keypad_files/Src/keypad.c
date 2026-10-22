#include "main.h"
#include "assert.h"
#include "hal_gpio.h"
#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_gpio.h"

/* 3×4 matrix keypad wiring on STM32F072
 *
 * Rows:   PA0, PA1, PA2, PA3  -> INPUT with internal pull-up
 * Columns: PA4, PA5, PA6      -> OUTPUT push-pull
 *
 *
 *         C0     C1     C2
 *          |      |      |
 * R0 -----[SW]---[SW]---[SW]
 *          |      |      |
 * R1 -----[SW]---[SW]---[SW]
 *          |      |      |
 * R2 -----[SW]---[SW]---[SW]
 *          |      |      |
 * R3 -----[SW]---[SW]---[SW]
 */

#define KEYPAD_ROWS 4
#define KEYPAD_COLS 3
#define KEYPAD_NONE 0xFFu

static const uint16_t keypad_row_pins[KEYPAD_ROWS] = {
    GPIO_PIN_0,
    GPIO_PIN_1,
    GPIO_PIN_2,
    GPIO_PIN_3
};

static const uint16_t keypad_col_pins[KEYPAD_COLS] = {
    GPIO_PIN_4,
    GPIO_PIN_5,
    GPIO_PIN_6
};

static const char keypad_map[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

static void Keypad_EnableGPIOA(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    __DSB();
    __ISB();
}

static void Keypad_DelayLoop(uint32_t count)
{
    while (count--) {
        __NOP();
    }
}

static void Keypad_Init(void)
{
    Keypad_EnableGPIOA();

    /* Configure rows as inputs with internal pull-up */
    for (uint32_t row = 0; row < KEYPAD_ROWS; ++row)
    {
        uint32_t pos = row;
        GPIOA->MODER &= ~(3U << (pos * 2));           /* input mode */
        GPIOA->PUPDR &= ~(3U << (pos * 2));
        GPIOA->PUPDR |=  (1U << (pos * 2));           /* pull-up */
        GPIOA->OSPEEDR &= ~(3U << (pos * 2));         /* low speed */
    }

    /* Configure columns as outputs, push-pull, no pull resistor */
    for (uint32_t col = 0; col < KEYPAD_COLS; ++col)
    {
        uint32_t pos = 4 + col;
        GPIOA->MODER &= ~(3U << (pos * 2));
        GPIOA->MODER |=  (1U << (pos * 2));           /* output mode */
        GPIOA->OTYPER &= ~(1U << pos);                /* push-pull */
        GPIOA->OSPEEDR &= ~(3U << (pos * 2));         /* low speed */
        GPIOA->PUPDR &= ~(3U << (pos * 2));           /* no pull */
        My_HAL_GPIO_WritePin(GPIOA, keypad_col_pins[col], GPIO_PIN_SET);
    }
}

static uint8_t Keypad_Scan(void)
{
    uint8_t result = KEYPAD_NONE;

    for (uint32_t col = 0; col < KEYPAD_COLS; ++col)
    {
        /* Drive one column low while leaving others high */
        for (uint32_t c = 0; c < KEYPAD_COLS; ++c)
        {
            My_HAL_GPIO_WritePin(GPIOA,
                                 keypad_col_pins[c],
                                 (c == col) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }

        Keypad_DelayLoop(1000);

        for (uint32_t row = 0; row < KEYPAD_ROWS; ++row)
        {
            if (My_HAL_GPIO_ReadPin(GPIOA, keypad_row_pins[row]) == GPIO_PIN_RESET)
            {
                result = (uint8_t)(row * KEYPAD_COLS + col);
                break;
            }
        }

        if (result != KEYPAD_NONE)
        {
            break;
        }
    }

    /* Restore idle state (AKA all columns high) */
    for (uint32_t c = 0; c < KEYPAD_COLS; ++c)
    {
        My_HAL_GPIO_WritePin(GPIOA, keypad_col_pins[c], GPIO_PIN_SET);
    }

    return result;
}



/*
  Uses pin PC4 for UART3_TX and PC5 for UART3_RX. Alternate function 1 for TX and AF1 for RX.
*/
void Setup_UART3(){
  // Enable USART3

  RCC->APB1RSTR |= RCC_APB1RSTR_USART3RST;
  RCC->APB1RSTR &= ~RCC_APB1RSTR_USART3RST;

  RCC->APB1ENR |= RCC_APB1ENR_USART3EN;

  USART3->CR1 &= ~USART_CR1_UE;

  GPIO_InitTypeDef UART3_TX = {GPIO_PIN_4, GPIO_MODE_AF_PP, GPIO_SPEED_FREQ_HIGH,GPIO_PULLUP};
  GPIO_InitTypeDef UART3_RX = {GPIO_PIN_5, GPIO_MODE_AF_PP, GPIO_SPEED_FREQ_HIGH,GPIO_PULLUP};


  // Clear Pin 4 and 5 AFR
  GPIOC->AFR[0] &= ~(GPIO_AFRL_AFSEL4 | GPIO_AFRL_AFSEL5);
  // Set alternate mode to AF1
  GPIOC->AFR[0] |= (1U << GPIO_AFRL_AFSEL4_Pos) | (1U << GPIO_AFRL_AFSEL5_Pos);

  // Initialize using the HAL
  My_HAL_GPIO_Init(GPIOC, &UART3_TX);
  My_HAL_GPIO_Init(GPIOC, &UART3_RX);

  // Set the Baud rate to target 115200 before we enable it.
  USART3->BRR = (HAL_RCC_GetHCLKFreq()/115200);

  // Enable the TE and RE bits
  USART3->CR1 |= (USART_CR1_TE | USART_CR1_RE);
  // Enable USART communication
  USART3->CR1 |= USART_CR1_UE;
}

int main(void)
{
    Keypad_Init();

    while (1)
    {
        uint8_t index = Keypad_Scan();

        if (index != KEYPAD_NONE)
        {
            uint32_t row = index / KEYPAD_COLS;
            uint32_t col = index % KEYPAD_COLS;
            char key = keypad_map[row][col];
            (void)key; /* Replace with real handling code */
        }

        Keypad_DelayLoop(200000u);
    }
}

/home/andre/projects/ECE5780 
C:\Digital-Padlock\testing_files\Src\keypad.c
