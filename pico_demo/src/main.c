#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"
#include "stepper.h"
#include "rfid.h"

int main(void)
{
    stdio_init_all();
    stepper_init();         // GP1 pulses the SAA1042 clock, GP2 sets direction
    rfid_init();            // GP9 = UART1 RX from the RFID module's TX

    xTaskCreate(stepper_task, "stepper", STEPPER_TASK_STACK, NULL,
                STEPPER_TASK_PRIORITY, NULL);
    xTaskCreate(rfid_task, "rfid", RFID_TASK_STACK, NULL,
                RFID_TASK_PRIORITY, NULL);

    vTaskStartScheduler();

    // Only reached if there wasn't enough heap to start the scheduler
    while (1) {
    }
    return -1;
}
