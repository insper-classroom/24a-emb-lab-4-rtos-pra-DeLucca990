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
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

int TRIG_PIN = 17;
int ECHO_PIN = 16;

volatile bool display_info = true;


QueueHandle_t xQueueDistance;
QueueHandle_t xQueueTime;
SemaphoreHandle_t xSemaphoreTrigger;

typedef struct {
    int time_init;
    int time_end;
    int64_t last_valid_read_time;
} Time;

// pin_callback: Função callback do pino do echo.
void gpio_callback(uint gpio, uint32_t events) {
    static int time_init;
    Time time;
    if (events == 0x8) {
        time_init = to_us_since_boot(get_absolute_time());
    } else if (events == 0x4) {
        time.time_init = time_init;
        time.time_end = to_us_since_boot(get_absolute_time());
        time.last_valid_read_time = time.time_end;
        xQueueSendFromISR(xQueueTime, &time, NULL);
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
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

// echo_task: Task que faz a leitura do tempo que o pino echo ficou levantado.
void echo_task(void *p) {
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    Time time;
    time.last_valid_read_time = 0;
    int distance;
    while (1) {
        int64_t current_time = to_us_since_boot(get_absolute_time());
        if (xQueueReceive(xQueueTime, &time, 0)) {
            if (time.time_end > time.time_init) {
                distance = (time.time_init - time.time_end) / 58;
                distance = abs(distance);
                if (distance > 200) {
                    distance = - 1;
                }
                xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
            }
        } else {
            time.last_valid_read_time = current_time;
            printf("Erro: sensor desconectado 2\n");
            distance = -2;
            xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
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
        if (display_info) {
            xSemaphoreGive(xSemaphoreTrigger);
            xQueueReceive(xQueueDistance, &distance, portMAX_DELAY);
        
            // Suavização linear para o que é exibido no display
            int steps = abs(distance - previous_distance);
            for (int i = 0; i <= steps; i++){
                int current_distance = previous_distance + ((distance - previous_distance) * i / steps);
                gfx_clear_buffer(&disp);
                if (distance == -2) {
                    sprintf(str, "Disancia: Inifinito");
                    gfx_draw_string(&disp, 0, 10, 1, str);
                    gfx_draw_line(&disp, 15, 27, 200, 27);
                }
                if (distance == -1) {
                    sprintf(str, "Distancia: Infinito");
                    gfx_draw_string(&disp, 0, 0, 1, str);
                    gfx_draw_line(&disp, 15, 27, 200, 27);
                } 
                else {
                    sprintf(str, "Distancia: %d cm", current_distance);
                    gfx_draw_string(&disp, 0, 0, 1, str);
                    gfx_draw_string(&disp, 0, 10, 1, "");
                    gfx_draw_line(&disp, 15, 27, 20 + current_distance, 27);
                }
                gfx_show(&disp);
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            previous_distance = distance;
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            gfx_clear_buffer(&disp);
            gfx_show(&disp);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void led_btn_1_init(void) {
    gpio_init(BTN_1_OLED);
    gpio_set_dir(BTN_1_OLED, GPIO_IN);
    gpio_pull_up(BTN_1_OLED);
    gpio_init(LED_1_OLED);
    gpio_set_dir(LED_1_OLED, GPIO_OUT);

    gpio_init(LED_2_OLED);
    gpio_set_dir(LED_2_OLED, GPIO_OUT);

    gpio_init(LED_3_OLED);
    gpio_set_dir(LED_3_OLED, GPIO_OUT);

    gpio_put(LED_1_OLED, 0);
    gpio_put(LED_2_OLED, 1);
    gpio_put(LED_3_OLED, 1);
}

void display_control_task(void *p) {
    led_btn_1_init();

    while (1) {
        if (gpio_get(BTN_1_OLED) == 0) {
            display_info = !display_info;

            if (display_info) {
                gpio_put(LED_1_OLED, 0);
                gpio_put(LED_2_OLED, 1);
                gpio_put(LED_3_OLED, 1);
            } else {
                gpio_put(LED_1_OLED, 1);
                gpio_put(LED_2_OLED, 1);
                gpio_put(LED_3_OLED, 1);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

int main() {
    stdio_init_all();

    Time time;
    time.last_valid_read_time;

    xQueueDistance = xQueueCreate(1, sizeof(int));
    xQueueTime = xQueueCreate(1, sizeof(Time));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "Trigger", 4095, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 4095, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 4095, NULL, 1, NULL);
    xTaskCreate(display_control_task, "Display Control", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
