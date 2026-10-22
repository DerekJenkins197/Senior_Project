/*
 * pins to connect rfid reader: PA10 (RX) needs a 2k resistor in series to step voltage down
 * rfid power connect to 5v and GND
 * 
 * pins to connect for terminal PC5 to the red chips TX and PC4 to the red chips RX.
 * 
 * pins for keypad
 * ROW1 = PIN3 = PC10
 * ROW2 = PIN8 = PC11
 * ROW3 = PIN7 = PC12
 * COL1 = PIN4 = PB10
 * COL2 = PIN2 = PB13
 * COL3 = PIN6 = PB14
 * 
 * trigger pin connect PB2

*/



#include "main.h"
#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_gpio.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* MACROS & CONSTANTS                                                        */
/* ========================================================================= */
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 3
#define KEYPAD_NONE 0xFFu
#define KEYPAD_OUT_PORT GPIOB
#define KEYPAD_OUT_PIN  GPIO_PIN_2
#define DEBOUNCE_THRESHOLD 5   
#define RFID_BUFFER_SIZE 16
#define MAX_PASS_LEN 32

/* ========================================================================= */
/* GLOBAL VARIABLES                                                          */
/* ========================================================================= */
volatile char rfid_buffer[RFID_BUFFER_SIZE];
volatile uint8_t rfid_index = 0;
volatile uint8_t rfid_ready = 0;

/* ========================================================================= */
/* PASSCODE & STATE VARIABLES                                            */
/* ========================================================================= */
typedef enum {
    STATE_NORMAL,
    STATE_REPROGRAMMING
} SystemState;

SystemState current_state = STATE_NORMAL;

char current_passcode[MAX_PASS_LEN] = "2488"; /* Default code */
char input_buffer[MAX_PASS_LEN] = {0};
uint8_t input_index = 0;

uint32_t star_hold_timer = 0;
uint8_t star_held = 0;

/* ========================================================================= */
/* SOLENOID HELPER FUNCTION                                                  */
/* ========================================================================= */
/* Blocking function to pulse the solenoid for feedback */
void Pulse_Solenoid(uint8_t pulses) {
    for (uint8_t i = 0; i < pulses; i++) {
        HAL_GPIO_WritePin(KEYPAD_OUT_PORT, KEYPAD_OUT_PIN, GPIO_PIN_SET);
        HAL_Delay(150); // ON for 150ms
        HAL_GPIO_WritePin(KEYPAD_OUT_PORT, KEYPAD_OUT_PIN, GPIO_PIN_RESET);
        HAL_Delay(150); // OFF for 150ms
    }
}

/* ========================================================================= */
/* DEBUG UART3 FUNCTIONS (115200 Baud on PC4/PC5)                            */
/* ========================================================================= */
void Transmit(char c) {
    while (!(USART3->ISR & USART_ISR_TXE)) {}
    USART3->TDR = c;
}

void Transmit_String(const char* str) {
    while (*str != '\0') {
        Transmit(*str);
        str++;
    }
}


void Setup_UART3(void) {
    /* 1. CLOCKS FIRST */
    RCC->AHBENR  |= RCC_AHBENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;

    /* 2. RESET */
    RCC->APB1RSTR |= RCC_APB1RSTR_USART3RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_USART3RST;
    USART3->CR1 &= ~USART_CR1_UE;

    /* 3. PINS (PC4/PC5) */
    GPIOC->MODER &= ~(GPIO_MODER_MODER4 | GPIO_MODER_MODER5);
    GPIOC->MODER |=  (2U << GPIO_MODER_MODER4_Pos) | (2U << GPIO_MODER_MODER5_Pos);

    GPIOC->AFR[0] &= ~(GPIO_AFRL_AFSEL4 | GPIO_AFRL_AFSEL5);
    GPIOC->AFR[0] |= (1U << GPIO_AFRL_AFSEL4_Pos) | (1U << GPIO_AFRL_AFSEL5_Pos);

    GPIOC->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEEDR4_Pos) | (3U << GPIO_OSPEEDR_OSPEEDR5_Pos);
    GPIOC->PUPDR   &= ~(GPIO_PUPDR_PUPDR4 | GPIO_PUPDR_PUPDR5);
    GPIOC->PUPDR   |=  (1U << GPIO_PUPDR_PUPDR4_Pos) | (1U << GPIO_PUPDR_PUPDR5_Pos);

    /* LED PC6 for debug toggle */
    GPIOC->MODER   &= ~(3U << (6 * 2));
    GPIOC->MODER   |=  (1U << (6 * 2));     
    GPIOC->OTYPER  &= ~(1U << 6);           
    GPIOC->OSPEEDR &= ~(3U << (6 * 2));     
    GPIOC->PUPDR   &= ~(3U << (6 * 2));     

    /* 4. PERIPHERAL */
    USART3->BRR = (HAL_RCC_GetHCLKFreq() / 115200);
    USART3->CR1 |= (USART_CR1_TE | USART_CR1_RE);
    USART3->CR1 |= USART_CR1_UE;

    GPIOC->ODR ^= GPIO_ODR_6; 
}

static const char keypad_map[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

/* Struct to handle pins scattered across different GPIO ports */
typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} KeypadPin_t;

/* Mapped to clean pins */
static const KeypadPin_t row_pins[KEYPAD_ROWS] = {
    {GPIOC, GPIO_PIN_10}, // Row 1: PC10
    {GPIOC, GPIO_PIN_11}, // Row 2: PC11
    {GPIOC, GPIO_PIN_12}, // Row 3: PC12
    {GPIOA, GPIO_PIN_5}  // Row 4: PA5
};

static const KeypadPin_t col_pins[KEYPAD_COLS] = {
    {GPIOB, GPIO_PIN_10}, // Col 1: PB10
    {GPIOB, GPIO_PIN_13}, // Col 2: PB13
    {GPIOB, GPIO_PIN_14}  // Col 3: PB14
};

/* ========================================================================= */
/* KEYPAD FUNCTIONS                                                          */
/* ========================================================================= */
static void Keypad_DelayLoop(uint32_t count) {
    while (count--) {
        __NOP();
    }
}

// Keypad wire order: col2, row1, col1, row4, col3, row3, row2

static void Keypad_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Configure Row 4 as Input Pull-Up (PA5) */
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Configure Rows 1, 2, 3 (PC10, PC11, PC12) as Input Pull-Up */
    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* Configure Columns as Output Push-Pull (PB10, PB13, PB14) */
    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_13 | GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Default: Drive Columns HIGH */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10 | GPIO_PIN_13 | GPIO_PIN_14, GPIO_PIN_SET);

    /* Solenoid Trigger (PB2) */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static uint8_t Keypad_Scan(void) {
    for (uint32_t col = 0; col < KEYPAD_COLS; ++col) {
        
        /* Drive the current column LOW (0V) */
        HAL_GPIO_WritePin(col_pins[col].port, col_pins[col].pin, GPIO_PIN_RESET);
        
        // Keypad_DelayLoop(100);
        HAL_Delay(2); // Give capacitive lines time to settle

        /* Check all rows to see if any got pulled LOW */
        for (uint32_t row = 0; row < KEYPAD_ROWS; ++row) {
            if (HAL_GPIO_ReadPin(row_pins[row].port, row_pins[row].pin) == GPIO_PIN_RESET) {
                
                /* Match found! Restore column HIGH before returning */
                HAL_GPIO_WritePin(col_pins[col].port, col_pins[col].pin, GPIO_PIN_SET);
                
                /* Return the actual ASCII character from the map */
                return keypad_map[row][col]; 
            }
        }

        /* Restore the current column HIGH (3.3V) before testing the next one */
        HAL_GPIO_WritePin(col_pins[col].port, col_pins[col].pin, GPIO_PIN_SET);
        HAL_Delay(1); // Short delay to prevent ghosting
    }

    return KEYPAD_NONE; 
}

/* ========================================================================= */
/* RFID UART1 FUNCTIONS (9600 Baud on PA10 RX)                               */
/* ========================================================================= */
static void RFID_UART_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->AHBENR  |= RCC_AHBENR_GPIOAEN;

    /* PA10 = RX */
    GPIOA->MODER &= ~(3U << (10 * 2));
    GPIOA->MODER |=  (2U << (10 * 2));  
    GPIOA->AFR[1] |= (1U << ((10 - 8) * 4)); 

    USART1->BRR = HAL_RCC_GetHCLKFreq() / 9600; 
    
    /* Enable Receiver, UART, AND the RX Not Empty Interrupt (RXNEIE) */
    USART1->CR1 |= USART_CR1_RE | USART_CR1_UE | USART_CR1_RXNEIE;
    
    /* Enable the interrupt in the NVIC */
    NVIC_EnableIRQ(USART1_IRQn);
}

void USART1_IRQHandler(void) {
    if (USART1->ISR & USART_ISR_RXNE) {
        char c = USART1->RDR;

        if (c == 0x02) {
            rfid_index = 0;
        } else if (c == 0x03) {
            rfid_buffer[rfid_index] = '\0';
            rfid_ready = 1;
        } else {
            if (rfid_index < RFID_BUFFER_SIZE - 1) {
                rfid_buffer[rfid_index++] = c;
            }
        }
    }
}

int RFID_IsValid(void) {
    /* Safe checking: See if the 10-char string contains the 8-char target */
    if (strstr((const char*)rfid_buffer, "BEAD1010") != NULL) {
        return 1;
    }
    return 0;
}

/* ========================================================================= */
/* MAIN APPLICATION                                                          */
/* ========================================================================= */
int main(void) {
    HAL_Init(); 
    
    Keypad_Init();
    RFID_UART_Init();
    Setup_UART3();

    Transmit_String("\r\n--- STM32 Authentication System Initialized ---\r\n");

    /* Status LED PC9 Initialization */
    RCC->AHBENR |= RCC_AHBENR_GPIOCEN;
    GPIOC->MODER &= ~(3U << (9 * 2));
    GPIOC->MODER |=  (1U << (9 * 2));
    GPIOC->OTYPER &= ~(1U << 9);
    GPIOC->OSPEEDR &= ~(3U << (9 * 2));
    GPIOC->PUPDR &= ~(3U << (9 * 2));

    uint8_t stable_key = KEYPAD_NONE;
    uint8_t last_press = KEYPAD_NONE;
    uint8_t first_prev_key = KEYPAD_NONE; 
    uint8_t second_prev_key = KEYPAD_NONE; 
    uint8_t third_prev_key = KEYPAD_NONE; 
    uint8_t debounce_count = 0;
    uint8_t authenticated = 0;

    uint32_t unlock_timer = 0;
    uint8_t unlocked = 0;

    while (1) {
        /* ---------------------------------------------------
         * 1. CHECK RFID
         * --------------------------------------------------- */
        if (rfid_ready) {
            rfid_ready = 0;
            
            /* Debug Print Tag to Terminal */
            char debug_msg[64];
            snprintf(debug_msg, sizeof(debug_msg), "Raw Tag Read: [%s]\r\n", (char*)rfid_buffer);
            Transmit_String(debug_msg);

            if (RFID_IsValid()) {
                authenticated = 1;
                Transmit_String("-> RFID AUTH SUCCESS\r\n");
            } else {
                Transmit_String("-> RFID AUTH FAILED\r\n");
            }
        }

        /* ---------------------------------------------------
         * 2. CHECK KEYPAD
         * --------------------------------------------------- */
        uint8_t press = Keypad_Scan();

        if (press == last_press) {
            if (debounce_count < DEBOUNCE_THRESHOLD) debounce_count++;
        } else {
            debounce_count = 0;
            star_held = 0; /* Reset hold timer if key changes */
        }

        last_press = press;

        if (debounce_count >= DEBOUNCE_THRESHOLD) {
            
            /* --- CHECK FOR 3-SECOND STAR HOLD --- */
            if (press == '*' && current_state == STATE_NORMAL) {
                if (!star_held) {
                    star_held = 1;
                    star_hold_timer = HAL_GetTick(); /* Start the clock */
                } 
                /* If held for 3000ms AND the buffer matches the current passcode */
                else if ((HAL_GetTick() - star_hold_timer) >= 3000) {
                    star_held = 0; /* Prevent re-triggering */
                    
                    if (strcmp(input_buffer, current_passcode) == 0) {
                        /* Switch to Reprogramming Mode */
                        current_state = STATE_REPROGRAMMING;
                        input_index = 0;
                        memset(input_buffer, 0, MAX_PASS_LEN);
                        Transmit_String("-> ENTERING REPROGRAM MODE\r\n");
                        Pulse_Solenoid(2); /* 2 Pulses: Success */
                    } else {
                        /* Nice try, intruder! */
                        Transmit_String("-> REPROGRAM DENIED: INCORRECT PIN\r\n");
                        input_index = 0;
                        memset(input_buffer, 0, MAX_PASS_LEN);
                        Pulse_Solenoid(3); /* 3 Pulses: Error */
                    }
                }
            }

            /* --- DETECT A NEW DISCRETE KEY PRESS --- */
            if (stable_key != press) {
                stable_key = press;

                if (stable_key != KEYPAD_NONE) {
                    char key_msg[32];
                    snprintf(key_msg, sizeof(key_msg), "Key Pressed: %c\r\n", stable_key);
                    Transmit_String(key_msg);

                    /* Handle Numbers 0-9 */
                    if (stable_key >= '0' && stable_key <= '9') {
                        if (input_index < MAX_PASS_LEN - 1) {
                            input_buffer[input_index++] = stable_key;
                            input_buffer[input_index] = '\0'; /* Keep string null-terminated */
                        }
                    } 
                    /* Handle the '#' (ENTER) Key */
                    else if (stable_key == '#') {
                        if (current_state == STATE_NORMAL) {
                            /* Normal Unlock Attempt */
                            if (strcmp(input_buffer, current_passcode) == 0) {
                                authenticated = 1;
                                Transmit_String("-> PIN AUTH SUCCESS\r\n");
                            } else {
                                Transmit_String("-> PIN AUTH FAILED\r\n");
                            }
                            
                            /* Clear buffer after attempting to unlock */
                            input_index = 0;
                            memset(input_buffer, 0, MAX_PASS_LEN);
                        } 
                        else if (current_state == STATE_REPROGRAMMING) {
                            /* Saving New Passcode */
                            if (input_index >= 4) {
                                strcpy(current_passcode, input_buffer);
                                Transmit_String("-> PASSCODE UPDATED!\r\n");
                                authenticated = 1; /* Trigger normal 2-sec unlock */
                            } else {
                                Transmit_String("-> UPDATE FAILED: PIN TOO SHORT\r\n");
                                Pulse_Solenoid(3); /* 3 Pulses: Error */
                            }
                            
                            /* Return to Normal Mode */
                            current_state = STATE_NORMAL;
                            input_index = 0;
                            memset(input_buffer, 0, MAX_PASS_LEN);
                        }
                    }
                    /* Handle the '*' Key (When Tapped, not held) */
                    else if (stable_key == '*') {
                        if (current_state == STATE_NORMAL && !star_held) {
                            /* Optional: Let '*' act as a 'Clear' button if they mess up typing */
                            input_index = 0;
                            memset(input_buffer, 0, MAX_PASS_LEN);
                            Transmit_String("-> BUFFER CLEARED\r\n");
                        }
                    }
                }
            }
        }

        /* ---------------------------------------------------
         * 3. ACTUATE HARDWARE TRIGGER
         * --------------------------------------------------- */
        if (authenticated) {
            unlocked = 1;
            unlock_timer = HAL_GetTick();
            

            HAL_GPIO_WritePin(KEYPAD_OUT_PORT, KEYPAD_OUT_PIN, GPIO_PIN_SET);
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_9); /* Green LED */

            authenticated = 0;
        } 

        if (unlocked && (HAL_GetTick() - unlock_timer >= 2000)) {
            unlocked = 0;
            HAL_GPIO_WritePin(KEYPAD_OUT_PORT, KEYPAD_OUT_PIN, GPIO_PIN_RESET);
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_9); /* Green LED */
        }

        // else {
        //     HAL_GPIO_WritePin(KEYPAD_OUT_PORT, KEYPAD_OUT_PIN, GPIO_PIN_RESET);
        // }

        /* Reset the flag so PB2 returns low on the next loop cycle (~3ms) */
        // authenticated = 0;

        Keypad_DelayLoop(5000);
    }
}
