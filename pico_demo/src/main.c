#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "stepper.h"
#include "rfid.h"

/* Debug console for a USB-UART adapter / PuTTY (115200 8N1, no flow control)
 *   GP4 (pin 6, UART1 TX) -> adapter RX
 *   GND (pin 8)           -> adapter GND
 * It is on UART1 so it never shares a UART (or a baud rate) with the RFID
 * reader on UART0. Everything is also printed on the Pico's own USB serial.
 */
#define CONSOLE_UART        uart1
#define CONSOLE_TX_PIN      4
#define CONSOLE_BAUD        115200

#define CONSOLE_TASK_PRIORITY   (tskIDLE_PRIORITY + 1)
#define CONSOLE_TASK_STACK      configMINIMAL_STACK_SIZE
#define CONSOLE_ALIVE_MS        5000

// Proves the console link works even with no RFID reader attached
static void console_task(void *params)
{
    (void)params;
    while (1) {
        printf("Derek is King\n");
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_ALIVE_MS));
    }
}

int main(void)
{
    // Not stdio_init_all(): that would put the default UART (UART0) on GP0/GP1 at 115200
    stdio_usb_init();
    stdio_uart_init_full(CONSOLE_UART, CONSOLE_BAUD, CONSOLE_TX_PIN, -1);   // TX only
    printf("\npico_demo starting\n");

    stepper_init();         // GP22 pulses the SAA1042 clock, GP21 sets direction
    rfid_init();            // GP1 = UART0 RX from the RFID module's TX

    xTaskCreate(console_task, "console", CONSOLE_TASK_STACK, NULL,
                CONSOLE_TASK_PRIORITY, NULL);
    xTaskCreate(stepper_task, "stepper", STEPPER_TASK_STACK, NULL,
                STEPPER_TASK_PRIORITY, NULL);
    xTaskCreate(rfid_task, "rfid", RFID_TASK_STACK, NULL,
                RFID_TASK_PRIORITY, NULL);
#if RFID_SELFTEST
    xTaskCreate(rfid_test_task, "rfid_test", RFID_TASK_STACK, NULL,
                RFID_TASK_PRIORITY, NULL);
#endif

    vTaskStartScheduler();

    // Only reached if there wasn't enough heap to start the scheduler
    while (1) {
    }
    return -1;
}
