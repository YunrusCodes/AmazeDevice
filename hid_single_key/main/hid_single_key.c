// hid_single_key.c - 藍牙 HID 鍵盤實現

#include <stdint.h>
#include <string.h>
#include "btstack.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 常量和定義
#define BTSTACK_FILE__ "hid_single_key.c"
#define REPORT_ID 0x01
#define BUTTON_GPIO 18
#define HID_KEY_Q 0x14

// 鍵盤的 HID 描述符
const uint8_t hid_descriptor_keyboard[] = {

    0x05, 0x01,                    // Usage Page (通用桌面設備)
    0x09, 0x06,                    // Usage (鍵盤)
    0xa1, 0x01,                    // Collection (應用)

    // 報告 ID

    0x85, REPORT_ID,               // 報告 ID

    // 修飾符字節（輸入）

    0x75, 0x01,                    //   報告大小 (1)
    0x95, 0x08,                    //   報告次數 (8)
    0x05, 0x07,                    //   使用頁面 (按鍵代碼)
    0x19, 0xe0,                    //   使用最小值 (鍵盤左控制鍵)
    0x29, 0xe7,                    //   使用最大值 (鍵盤右 GUI 鍵)
    0x15, 0x00,                    //   邏輯最小值 (0)
    0x25, 0x01,                    //   邏輯最大值 (1)
    0x81, 0x02,                    //   輸入 (數據，變量，絕對)

    // 保留字節（輸入）

    0x75, 0x01,                    //   報告大小 (1)
    0x95, 0x08,                    //   報告次數 (8)
    0x81, 0x03,                    //   輸入 (常量，變量，絕對)

    // LED 報告 + 填充（輸出）

    0x95, 0x05,                    //   報告次數 (5)
    0x75, 0x01,                    //   報告大小 (1)
    0x05, 0x08,                    //   使用頁面 (LEDs)
    0x19, 0x01,                    //   使用最小值 (數字鎖)
    0x29, 0x05,                    //   使用最大值 (假名)
    0x91, 0x02,                    //   輸出 (數據，變量，絕對)

    0x95, 0x01,                    //   報告次數 (1)
    0x75, 0x03,                    //   報告大小 (3)
    0x91, 0x03,                    //   輸出 (常量，變量，絕對)

    // 按鍵代碼（輸入）

    0x95, 0x06,                    //   報告次數 (6)
    0x75, 0x08,                    //   報告大小 (8)
    0x15, 0x00,                    //   邏輯最小值 (0)
    0x25, 0xff,                    //   邏輯最大值 (255)
    0x05, 0x07,                    //   使用頁面 (按鍵代碼)
    0x19, 0x00,                    //   使用最小值 (保留（未指示事件）)
    0x29, 0xff,                    //   使用最大值 (保留)
    0x81, 0x00,                    //   輸入 (數據，數組)

    0xc0,                          // 結束集合
};

// 全局變量
static uint8_t hid_service_buffer[300];
static const char hid_device_name[] = "BTstack HID Keyboard";
static btstack_packet_callback_registration_t hci_event_callback_registration;
static uint16_t hid_cid;

// 應用程序狀態
static enum {
    APP_BOOTING,
    APP_NOT_CONNECTED,
    APP_CONNECTING,
    APP_CONNECTED
} app_state = APP_BOOTING;

// 函數原型
static void send_report(int modifier, int keycode);
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t packet_size);
void button_init();
bool button_is_pressed();
void send_key_q();
void release_key();
void task_button_monitor(void *arg);
int btstack_main(int argc, const char * argv[]);

// 發送 HID 報告
static void send_report(int modifier, int keycode) {
    uint8_t message[] = {0xa1, REPORT_ID, modifier, 0, keycode, 0, 0, 0, 0, 0};
    hid_device_send_interrupt_message(hid_cid, &message[0], sizeof(message));
}

// 藍牙事件的數據包處理器
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t packet_size){
    UNUSED(channel);
    UNUSED(packet_size);

    switch (packet_type){
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)){
                case BTSTACK_EVENT_STATE:
                    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
                    app_state = APP_NOT_CONNECTED;
                    break;

                case HCI_EVENT_HID_META:
                    switch (hci_event_hid_meta_get_subevent_code(packet)){
                        case HID_SUBEVENT_CONNECTION_OPENED:
                            if (hid_subevent_connection_opened_get_status(packet) != ERROR_CODE_SUCCESS) {
                                app_state = APP_NOT_CONNECTED;
                                hid_cid = 0;
                                return;
                            }
                            app_state = APP_CONNECTED;
                            hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
                            break;

                        case HID_SUBEVENT_CONNECTION_CLOSED:
                            app_state = APP_NOT_CONNECTED;
                            hid_cid = 0;
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

// 初始化按鈕 GPIO
void button_init() {
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

// 檢查按鈕是否被按下
bool button_is_pressed() {
    return gpio_get_level(BUTTON_GPIO) == 0;
}

// 發送 'Q' 鍵按下
void send_key_q() {
    uint8_t keycode = HID_KEY_Q;
    uint8_t modifier = 0;
    send_report(modifier, keycode);
}

// 釋放所有按鍵
void release_key() {
    uint8_t key_report[] = { 0xa1, 0x01, 0, 0, 0, 0, 0, 0, 0 };
    hid_device_send_interrupt_message(0, key_report, sizeof(key_report));
}

// 監控按鈕狀態的任務
void task_button_monitor(void *arg) {
    while (1) {
        if (button_is_pressed()) {
            send_key_q();
        } else {
            send_report(0, 0);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
#include "btstack_run_loop.h"
#include "hid_device.h"

// 定义全局定时器结构体
static btstack_timer_source_t button_monitor_timer;

// 按钮监控任务处理器
static void button_monitor_handler(btstack_timer_source_t *ts) {
    if (button_is_pressed()) {
        uint8_t keycode = HID_KEY_Q;
        uint8_t modifier = 0;
        send_report(modifier, keycode);
    } else {
        send_report(0, 0);
    }

    // 重新设置定时器间隔时间，10ms 后再次调用
    btstack_run_loop_set_timer(ts, 10);  // 设置 10ms 的间隔
    btstack_run_loop_add_timer(ts);      // 重新添加定时器
}

// 初始化并启动定时器
static void start_button_monitor(void) {
    // 初始化定时器
    btstack_run_loop_set_timer_handler(&button_monitor_timer, button_monitor_handler);

    // 设置初始超时时间为 10ms
    btstack_run_loop_set_timer(&button_monitor_timer, 10);

    // 添加定时器到 BTstack run loop
    btstack_run_loop_add_timer(&button_monitor_timer);
}

int btstack_main(int argc, const char * argv[]) {
    (void)argc;
    (void)argv;

    // 初始化按鈕並創建監控任務
    button_init();

    // 配置藍牙設置
    gap_discoverable_control(1);
    gap_set_local_name("HID Keyboard Button");

    // 初始化 L2CAP 和 SDP
    l2cap_init();
    sdp_init();

    // 配置 HID 服務
    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));

    // 設置 HID SDP 記錄
    hid_sdp_record_t hid_params = {
        0x2540, 33,
        0, 1,  // HID 虛擬纜線和遠端喚醒
        1, 1,  // 重新連接啟動和通常可連接
        0,  // HID boot device 關閉
        0, 0, 3200,
        hid_descriptor_keyboard,
        sizeof(hid_descriptor_keyboard),
        hid_device_name
    };

    // 創建並註冊 HID SDP 記錄
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
    sdp_register_service(hid_service_buffer);

    // 初始化 HID 設備
    hid_device_init(0, sizeof(hid_descriptor_keyboard), hid_descriptor_keyboard);

    // 註冊事件處理程序
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    hid_device_register_packet_handler(&packet_handler);

    // 開啟藍牙
    hci_power_control(HCI_POWER_ON);
    start_button_monitor();
    return 0;
}

