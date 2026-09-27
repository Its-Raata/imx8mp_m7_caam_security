/* M7 crypto service. Channel rpmsg-virtual-tty-channel-1 -> /dev/ttyRPMSG30. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rpmsg_lite.h"
#include "rpmsg_queue.h"
#include "rpmsg_ns.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app.h"
#include "rsc_table.h"

#include "crypto_service.h"
#include "m7_crypto_protocol.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define APP_TASK_STACK_SIZE (1024U)
#ifndef LOCAL_EPT_ADDR
#define LOCAL_EPT_ADDR (30U)
#endif

#undef RPMSG_LITE_NS_ANNOUNCE_STRING
#define RPMSG_LITE_NS_ANNOUNCE_STRING "rpmsg-virtual-tty-channel-1"

#define RX_BUF_SIZE (496U)
#define TX_BUF_SIZE (496U)

/*******************************************************************************
 * Variables
 ******************************************************************************/
static TaskHandle_t app_task_handle = NULL;
static struct rpmsg_lite_instance *volatile my_rpmsg = NULL;
static struct rpmsg_lite_endpoint *volatile my_ept   = NULL;
static volatile rpmsg_queue_handle my_queue          = NULL;

static uint8_t s_rx[RX_BUF_SIZE];
static uint8_t s_tx[TX_BUF_SIZE];

/*******************************************************************************
 * Code
 ******************************************************************************/
static void app_nameservice_isr_cb(uint32_t new_ept, const char *new_ept_name, uint32_t flags, void *user_data)
{
    (void)new_ept;
    (void)new_ept_name;
    (void)flags;
    (void)user_data;
}

static void app_task(void *param)
{
    uint32_t remote_addr = 0U;
    rpmsg_ns_handle ns_handle = NULL;
    uint32_t len = 0;

    (void)param;
    (void)ns_handle;
    crypto_service_init();

    (void)PRINTF("\r\nM7 Crypto RPMsg service (AES-GCM + HMAC, soft blob)\r\n");
    (void)PRINTF("RPMSG Share Base Addr is 0x%x\r\n", RPMSG_LITE_SHMEM_BASE);

    my_rpmsg = rpmsg_lite_remote_init((void *)RPMSG_LITE_SHMEM_BASE, RPMSG_LITE_LINK_ID, RL_NO_FLAGS);
    if (my_rpmsg == NULL)
    {
        (void)PRINTF("Failed to initialize rpmsg\r\n");
        goto hang;
    }

    (void)rpmsg_lite_wait_for_link_up(my_rpmsg, RL_BLOCK);
    (void)PRINTF("Link is up!\r\n");

    my_queue = rpmsg_queue_create(my_rpmsg);
    my_ept   = rpmsg_lite_create_ept(my_rpmsg, LOCAL_EPT_ADDR, rpmsg_queue_rx_cb, my_queue);
    ns_handle = rpmsg_ns_bind(my_rpmsg, app_nameservice_isr_cb, NULL);

    SDK_DelayAtLeastUs(1000000U, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
    (void)rpmsg_ns_announce(my_rpmsg, my_ept, RPMSG_LITE_NS_ANNOUNCE_STRING, (uint32_t)RL_NS_CREATE);
    (void)PRINTF("Nameservice announce: %s\r\n", RPMSG_LITE_NS_ANNOUNCE_STRING);
    (void)PRINTF("Ready for crypto requests.\r\n");

    for (;;)
    {
        uint32_t rsp_len;

        len = 0;
        (void)rpmsg_queue_recv(my_rpmsg, my_queue, &remote_addr, (char *)s_rx, RX_BUF_SIZE, &len, RL_BLOCK);
        if (len == 0U)
        {
            continue;
        }

        rsp_len = crypto_service_handle(s_rx, len, s_tx, TX_BUF_SIZE);
        if (rsp_len == 0U)
        {
            (void)PRINTF("handler failed\r\n");
            continue;
        }

        (void)rpmsg_lite_send(my_rpmsg, my_ept, remote_addr, (char *)s_tx, rsp_len, RL_BLOCK);
    }

hang:
    for (;;)
    {
    }
}

void app_create_task(void)
{
    if ((app_task_handle == NULL) &&
        (xTaskCreate(app_task, "CRYPTO", APP_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &app_task_handle) != pdPASS))
    {
        (void)PRINTF("Failed to create application task\r\n");
        for (;;)
        {
        }
    }
}

int main(void)
{
    BOARD_InitMemory();
    BOARD_RdcInit();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    copyResourceTable();

    app_create_task();
    vTaskStartScheduler();

    (void)PRINTF("Failed to start FreeRTOS\r\n");
    for (;;)
    {
    }
}
