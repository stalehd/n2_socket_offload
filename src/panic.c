#include <stdio.h>

#include <zephyr.h>
#include <kernel.h>
#include <device.h>
#include <drivers/gpio.h>
#include "panic.h"

/*
struct k_thread panic_thread;

#define PANIC_THREAD_STACK_SIZE 512
#define PANIC_THREAD_PRIORITY (CONFIG_NUM_COOP_PRIORITIES-2)
K_THREAD_STACK_DEFINE(panic_thread_stack,
                      PANIC_THREAD_STACK_SIZE);
*/

static int last_cycle = 0;

/*
static struct k_sem panic_sem;

void panic_threadproc() {
    while (true) {
        k_sem_take(&panic_sem, K_FOREVER);
        printf("Panic process\n");
    }
}
*/

/* panic button feature */
void dont_panic(struct device *gpiob, struct gpio_callback *cb, u32_t pins)
{
    int cycle = k_cycle_get_32();
    if ((cycle - last_cycle) > 10000) {
        //k_sem_give(&panic_sem);
       	printf("PANIC @ %d!\n", cycle);
        last_cycle = cycle;
    }
}

void init_panic() {
    //k_sem_init(&panic_sem, 0, 1);
/*    k_thread_create(&panic_thread, panic_thread_stack,
                K_THREAD_STACK_SIZEOF(panic_thread_stack),
                (k_thread_entry_t)panic_threadproc,
                NULL, NULL, NULL, K_PRIO_COOP(PANIC_THREAD_PRIORITY), 0, K_NO_WAIT);*/


    struct device *gpio;
    struct gpio_callback gpio_cb;

	gpio = device_get_binding("GPIO_0");
	if (!gpio) {
		printk("Unable to get GPIO device\n");
		return;
	}

	gpio_pin_configure(gpio, BUTTON_PIN, GPIO_INT | GPIO_PUD_PULL_UP | GPIO_INT_EDGE | GPIO_INT_ACTIVE_LOW | GPIO_DIR_IN);

	gpio_init_callback(&gpio_cb, dont_panic, BIT(BUTTON_PIN));

	gpio_add_callback(gpio, &gpio_cb);
	gpio_pin_enable_callback(gpio, BUTTON_PIN);

    printf("Panic subsystem. Right\n");
}