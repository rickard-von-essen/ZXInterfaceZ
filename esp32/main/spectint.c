#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gpio.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "defs.h"
#include "spectint.h"
#include "hal/gpio_hal.h"

static xQueueHandle gpio_evt_queue = NULL;

static volatile uint32_t interrupt_count = 0;

static void IRAM_ATTR spectint__isr_handler(void* arg)
{
    BaseType_t need_yield = 0;
    // This is done like this so it stays in IRAM
    gpio_hal_context_t _gpio_hal = {
        .dev = GPIO_HAL_GET_HW(GPIO_PORT_0)
    };

    gpio_hal_set_level(&_gpio_hal, PIN_NUM_INTACK, 0);
    // Small delay
    for (register int z=4; z!=0;--z) {
        __asm__ __volatile__ ("nop");
    }
    gpio_hal_set_level(&_gpio_hal, PIN_NUM_INTACK, 1);

    uint32_t gpio_num = ((uint32_t)(size_t) arg );
    while ( xQueueSendFromISR(gpio_evt_queue, &gpio_num, &need_yield) != pdTRUE);

    if (need_yield)
        portYIELD_FROM_ISR ();
}

void spectint__init()
{
    gpio_evt_queue = xQueueCreate(8, sizeof(uint32_t));

    gpio_set_intr_type(PIN_NUM_CMD_INTERRUPT, GPIO_INTR_LOW_LEVEL);
    interrupt_count = 0;

    //install gpio isr service
    gpio_install_isr_service(0);
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_NUM_CMD_INTERRUPT, spectint__isr_handler, (void*) PIN_NUM_CMD_INTERRUPT));
}

int spectint__getinterrupt()
{
    uint32_t io_num;
    if (!xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        return 0;

    return io_num;
}
