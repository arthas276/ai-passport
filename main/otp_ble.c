// otp_ble.c - NimBLE Nordic UART Service transport for the link console.
// Lifecycle mirrors main/demo_ble.c (proven init/teardown pattern); the task
// that owns the link page calls start/stop so no button callback ever blocks.
#include "otp_ble.h"

#include "otp_link.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

/* ESP-IDF NimBLE 没有公开头声明此函数（官方例程同样 extern 后调用）；
 * 它把 NVS 持久化的 bond 存储回调接进 ble_hs_cfg。 */
extern void ble_store_config_init(void);

static const char *TAG = "otp_ble";
static const char *DEVICE_NAME = "OTP-LINK";

/* 6E400001-B5A3-F393-E0A9-E50E24DCCA9E Nordic UART Service. */
static const ble_uuid128_t NUS_SVC_UUID =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t NUS_RX_UUID =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);
static const ble_uuid128_t NUS_TX_UUID =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

static SemaphoreHandle_t s_host_stopped;
static TaskHandle_t s_host_task;
static QueueHandle_t s_line_q;
static uint8_t s_addr_type;
static bool s_initialized;
static volatile bool s_start_requested;
static volatile bool s_stopping;
static volatile int s_conn_handle = -1;
static volatile bool s_pair_pending;
static uint16_t s_tx_handle;
static char s_rx_line[OTP_LINK_LINE_MAX];
static size_t s_rx_fill;

static int gap_event(struct ble_gap_event *event, void *arg);

static int advertise(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;
    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect status=%d", event->connect.status);
        if (event->connect.status != 0) {
            (void)advertise();
            return 0;
        }
        s_conn_handle = event->connect.conn_handle;
        /* 安全协商改按需:不在连接时主动 initiate(Windows/Chrome 栈会直接断链),
         * 由首次写入触发 rx_access 的 INSUFFICIENT_ENC 应答,中心端再发起配对。 */
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect handle=%d reason=%d", event->disconnect.conn.conn_handle,
                 event->disconnect.reason);
        s_conn_handle = -1;
        s_pair_pending = false;
        if (s_start_requested && !s_stopping) (void)advertise();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe cur=%d val=%d", event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);
        /* Windows 本地拒绝未配对写入且不会自动补配对（写入根本到不了设备），
         * 订阅成功即代表页面即将发指令，此刻主动升级安全等级 */
        if (event->subscribe.cur_notify) s_pair_pending = true;
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu=%d", event->mtu.value);
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc d;
        int fd = ble_gap_conn_find(event->enc_change.conn_handle, &d);
        ESP_LOGI(TAG, "enc change status=%d encrypted=%d", event->enc_change.status,
                 fd == 0 ? d.sec_state.encrypted : -1);
        return 0;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_start_requested && !s_stopping) (void)advertise();
        return 0;
    default:
        return 0;
    }
}

static int rx_access(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0 || !desc.sec_state.encrypted) {
        ESP_LOGW(TAG, "write before encryption, requesting pairing");
        s_pair_pending = true;  /* 由 pop_line 轮询点在 NimBLE 上下文外发起 */
        return BLE_ATT_ERR_INSUFFICIENT_ENC;
    }
    uint8_t chunk[64];
    uint16_t out_len = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, chunk, sizeof(chunk) - 1u, &out_len);
    if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
    for (uint16_t i = 0; i < out_len; i++) {
        char ch = (char)chunk[i];
        if (ch == '\n' || ch == '\r') {
            if (s_rx_fill == 0) continue;
            s_rx_line[s_rx_fill] = '\0';
            s_rx_fill = 0;
            if (xQueueSend(s_line_q, s_rx_line, 0) != pdTRUE) {
                ESP_LOGW(TAG, "line queue full, dropped");
            }
            continue;
        }
        if (s_rx_fill + 1u >= sizeof(s_rx_line)) {
            s_rx_fill = 0;  /* overflow: discard the line like USB does */
            continue;
        }
        s_rx_line[s_rx_fill++] = ch;
    }
    return 0;
}

static int tx_access(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return 0;
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &NUS_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &NUS_RX_UUID.u,
                .access_cb = rx_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &NUS_TX_UUID.u,
                .access_cb = tx_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset: %d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0 && s_start_requested && !s_stopping) rc = advertise();
    if (rc != 0) ESP_LOGE(TAG, "sync/advertise failed: %d", rc);
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    xSemaphoreGive(s_host_stopped);
    for (;;) vTaskSuspend(NULL);
}

esp_err_t otp_ble_start(void)
{
    if (s_initialized) return ESP_OK;
    s_stopping = false;
    s_start_requested = true;
    s_conn_handle = -1;
    s_rx_fill = 0;
    if (!s_line_q) {
        s_line_q = xQueueCreate(8, OTP_LINK_LINE_MAX);
        if (!s_line_q) return ESP_ERR_NO_MEM;
    }
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;
    s_initialized = true;
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) {
        err = ESP_ERR_NO_MEM;
        goto failed_start;
    }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_svcs);
    if (rc == 0) rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt setup failed: %d", rc);
        err = ESP_FAIL;
        goto failed_start;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    /* SM/SC 由 Kconfig 开启（SECURITY_ENABLE=y、SM_SC=y）；本移植的 ble_hs_cfg
     * 没有 sm/sm_sc/sm_bonding 字段，只需 IO 能力与 bond 存储。 */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_store_config_init();
    if (xTaskCreatePinnedToCore(host_task, "otp_ble", NIMBLE_HS_STACK_SIZE,
                                NULL, configMAX_PRIORITIES - 4, &s_host_task,
                                NIMBLE_CORE) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto failed_start;
    }
    return ESP_OK;

failed_start:
    s_start_requested = false;
    (void)otp_ble_stop();
    return err;
}

esp_err_t otp_ble_stop(void)
{
    s_start_requested = false;
    if (!s_initialized) return ESP_OK;
    s_stopping = true;
    if (s_conn_handle >= 0) {
        (void)ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        s_conn_handle = -1;
    }
    (void)ble_gap_adv_stop();
    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_stop failed: %d", rc);
        s_stopping = false;
        return ESP_FAIL;
    }
    if (!s_host_stopped ||
        xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "host stop timeout");
        s_stopping = false;
        return ESP_ERR_TIMEOUT;
    }
    if (s_host_task) {
        vTaskDelete(s_host_task);
        s_host_task = NULL;
    }
    nimble_port_deinit();
    if (s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    s_initialized = false;
    s_stopping = false;
    return ESP_OK;
}

bool otp_ble_ready(void)
{
    return s_initialized && s_conn_handle >= 0;
}

bool otp_ble_pop_line(char *buf, size_t cap)
{
    /* 写入被加密门拦下时，在这里（应用任务上下文，非 NimBLE 回调）发起配对；
     * Windows 收到配对请求后完成 Just Works，用户重发指令即可通过。 */
    if (s_pair_pending && s_conn_handle >= 0) {
        s_pair_pending = false;
        int rc = ble_gap_security_initiate(s_conn_handle);
        ESP_LOGI(TAG, "security initiate rc=%d", rc);
    }
    if (!s_line_q) return false;
    if (cap < OTP_LINK_LINE_MAX) return false;
    return xQueueReceive(s_line_q, buf, 0) == pdTRUE;
}

void otp_ble_reply(const char *text)
{
    if (!otp_ble_ready()) return;
    size_t len = strlen(text);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(text, len);
    if (!om) return;
    (void)ble_gatts_notify_custom(s_conn_handle, s_tx_handle, om);
}
