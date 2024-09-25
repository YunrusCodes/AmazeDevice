#include "btstack.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_GPIO 18
#define HID_KEY_Q 0x14

static uint16_t hid_cid;
static uint8_t hid_service_buffer[300];

// HID 描述符（必須與 HID 規範匹配）
const uint8_t hid_descriptor_keyboard[] = {
    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x06,                    // Usage (Keyboard)
    0xa1, 0x01,                    // Collection (Application)
    0x85, 0x01,                    //   Report ID (1)
    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x05, 0x07,                    //   Usage Page (Key codes)
    0x19, 0xe0,                    //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xe7,                    //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x01,                    //   Logical Maximum (1)
    0x81, 0x02,                    //   Input (Data, Variable, Absolute)
    0x95, 0x01,                    //   Report Count (1)
    0x75, 0x08,                    //   Report Size (8)
    0x81, 0x03,                    //   Input (Constant, Variable, Absolute)
    0x95, 0x06,                    //   Report Count (6)
    0x75, 0x08,                    //   Report Size (8)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x65,                    //   Logical Maximum (101)
    0x05, 0x07,                    //   Usage Page (Key codes)
    0x19, 0x00,                    //   Usage Minimum (0)
    0x29, 0x65,                    //   Usage Maximum (101)
    0x81, 0x00,                    //   Input (Data, Array)
    0xc0                           // End Collection
};

// 初始化按鈕
void button_init() {
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

// 檢查按鈕是否按下
bool button_is_pressed() {
    return gpio_get_level(BUTTON_GPIO) == 0;
}

// 發送 'q' 鍵的 HID 報告
void send_key_q() {
    uint8_t report[] = {0xa1, 0x01, 0, 0, HID_KEY_Q, 0, 0, 0, 0, 0};
    hid_device_send_interrupt_message(hid_cid, report, sizeof(report));
}

// 釋放按鍵
void release_key() {
    uint8_t report[] = {0xa1, 0x01, 0, 0, 0, 0, 0, 0, 0, 0};
    hid_device_send_interrupt_message(hid_cid, report, sizeof(report));
}

// 按鈕監控任務
void task_button_monitor(void *arg) {
    while (1) {
        if (button_is_pressed()) {
            send_key_q();  // 發送 'q' 鍵
        } else {
            release_key();  // 釋放按鍵
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);  // 10 毫秒延遲
    }
}

// 處理藍牙 HID 事件
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t packet_size) {
    UNUSED(channel);
    UNUSED(packet_size);

    switch (packet_type) {
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case BTSTACK_EVENT_STATE:
                    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                        printf("Bluetooth is up and running\n");
                    }
                    break;
                case HCI_EVENT_HID_META:
                    switch (hci_event_hid_meta_get_subevent_code(packet)) {
                        case HID_SUBEVENT_CONNECTION_OPENED:
                            hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
                            printf("HID connection opened\n");
                            break;
                        case HID_SUBEVENT_CONNECTION_CLOSED:
                            hid_cid = 0;
                            printf("HID connection closed\n");
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

// 主程序入口
int btstack_main(int argc, const char *argv[]) {
    button_init();
    xTaskCreate(task_button_monitor, "button_monitor_task", 2048, NULL, 10, NULL);

    // 初始化 HID 服務
    l2cap_init();
    sdp_init();

    // 註冊 HID 服務
    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));
    hid_sdp_record_t hid_params = {
        .hid_device_subclass = 0x2540,   // HID subclass (鍵盤)
        .hid_country_code = 33,          // US 鍵盤
        .hid_virtual_cable = 0,
        .hid_remote_wake = 1,
        .hid_reconnect_initiate = 1,
        .hid_normally_connectable = 1,
        .hid_boot_device = 0,
        .hid_ssr_host_max_latency = 1600,   // 修正的成員名稱
        .hid_ssr_host_min_timeout = 3200,   // 修正的成員名稱
        .hid_descriptor = hid_descriptor_keyboard
    };
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
    sdp_register_service(hid_service_buffer);

    // 初始化 HID 裝置
    hid_device_init(0, sizeof(hid_descriptor_keyboard), hid_descriptor_keyboard);

    // 註冊 HID 事件處理器
    hid_device_register_packet_handler(packet_handler);

    // 開啟藍牙
    hci_power_control(HCI_POWER_ON);
    return 0;
}
