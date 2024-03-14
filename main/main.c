/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint BTN_1_OLED = 28;

const uint LED_1_OLED = 20;

int TRIG_PIN = 17;
int ECHO_PIN = 16;

volatile int time_init = 0;
volatile int time_end = 0;

QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;

// pin_callback: Função callback do pino do echo.
void gpio_callback(uint gpio, uint32_t events) {
    if (events == 0x4) {
        time_end = to_us_since_boot(get_absolute_time());
    } else if (events == 0x8) {
        time_init = to_us_since_boot(get_absolute_time());
    }
}

// trigger_task: Task responsável por gerar o trigger.
void trigger_task(void *p) {
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);

    while (1) {
        xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY);
        gpio_put(TRIG_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_put(TRIG_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// echo_task: Task que faz a leitura do tempo que o pino echo ficou levantado.
void echo_task(void *p) {
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    int distance = 0;
    while (1) {
        if (time_end > time_init) {
            distance = (time_end - time_init) / 58;
            xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// oled_task: Task que exibe a informação da distancia no display. Faz uso de dois recursos, xSemaphoreTrigger e xQueueDistance
void oled_task(void *p) {
    ssd1306_init();
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    int distance = 0;
    int previous_distance = 0;
    char str[20];
    while (1) {
        xSemaphoreGive(xSemaphoreTrigger);
        xQueueReceive(xQueueDistance, &distance, portMAX_DELAY);
        
        // Suavização linear para o que é exibido no display
        int steps = abs(distance - previous_distance);
        for (int i = 0; i <= steps; i++){
            int current_distance = previous_distance + ((distance - previous_distance) * i / steps);
            gfx_clear_buffer(&disp);
            sprintf(str, "Distancia: %d cm", current_distance);
            gfx_draw_string(&disp, 0, 0, 1, str);
            gfx_draw_line(&disp, 15, 27, 10 + current_distance, 27);
            gfx_show(&disp);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        previous_distance = distance;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int main() {
    stdio_init_all();

    xQueueDistance = xQueueCreate(1, sizeof(int));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "Trigger", 4095, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 4095, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
