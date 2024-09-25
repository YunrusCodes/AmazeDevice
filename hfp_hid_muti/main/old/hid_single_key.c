#define BTSTACK_FILE__ "hid_single_key.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "btstack.h"

// timing of keypresses
#define TYPING_KEYDOWN_MS  20
#define TYPING_DELAY_MS    20

// When not set to 0xffff, sniff and sniff subrating are enabled
static uint16_t host_max_latency = 1600;
static uint16_t host_min_timeout = 3200;

#define REPORT_ID 0x01

// close to USB HID Specification 1.1, Appendix B.1
const uint8_t hid_descriptor_keyboard[] = {

    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x06,                    // Usage (Keyboard)
    0xa1, 0x01,                    // Collection (Application)

    // Report ID

    0x85, REPORT_ID,               // Report ID

    // Modifier byte (input)

    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x05, 0x07,                    //   Usage Page (Key codes)
    0x19, 0xe0,                    //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xe7,                    //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x01,                    //   Logical Maximum (1)
    0x81, 0x02,                    //   Input (Data, Variable, Absolute)

    // Reserved byte (input)

    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x81, 0x03,                    //   Input (Constant, Variable, Absolute)

    // LED report + padding (output)

    0x95, 0x05,                    //   Report Count (5)
    0x75, 0x01,                    //   Report Size (1)
    0x05, 0x08,                    //   Usage Page (LEDs)
    0x19, 0x01,                    //   Usage Minimum (Num Lock)
    0x29, 0x05,                    //   Usage Maximum (Kana)
    0x91, 0x02,                    //   Output (Data, Variable, Absolute)

    0x95, 0x01,                    //   Report Count (1)
    0x75, 0x03,                    //   Report Size (3)
    0x91, 0x03,                    //   Output (Constant, Variable, Absolute)

    // Keycodes (input)

    0x95, 0x06,                    //   Report Count (6)
    0x75, 0x08,                    //   Report Size (8)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0xff,                    //   Logical Maximum (1)
    0x05, 0x07,                    //   Usage Page (Key codes)
    0x19, 0x00,                    //   Usage Minimum (Reserved (no event indicated))
    0x29, 0xff,                    //   Usage Maximum (Reserved)
    0x81, 0x00,                    //   Input (Data, Array)

    0xc0,                          // End collection
};

// 
#define CHAR_ILLEGAL     0xff
#define CHAR_RETURN     '\n'
#define CHAR_ESCAPE      27
#define CHAR_TAB         '\t'
#define CHAR_BACKSPACE   0x7f

// Simplified US Keyboard with Shift modifier

/**
 * English (US)
 */
static const uint8_t keytable_us_none [] = {
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /*   0-3 */
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',                   /*  4-13 */
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',                   /* 14-23 */
    'u', 'v', 'w', 'x', 'y', 'z',                                       /* 24-29 */
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',                   /* 30-39 */
    CHAR_RETURN, CHAR_ESCAPE, CHAR_BACKSPACE, CHAR_TAB, ' ',            /* 40-44 */
    '-', '=', '[', ']', '\\', CHAR_ILLEGAL, ';', '\'', 0x60, ',',       /* 45-54 */
    '.', '/', CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,   /* 55-60 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 61-64 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 65-68 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 69-72 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 73-76 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 77-80 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 81-84 */
    '*', '-', '+', '\n', '1', '2', '3', '4', '5',                       /* 85-97 */
    '6', '7', '8', '9', '0', '.', 0xa7,                                 /* 97-100 */
}; 

static const uint8_t keytable_us_shift[] = {
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /*  0-3  */
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',                   /*  4-13 */
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',                   /* 14-23 */
    'U', 'V', 'W', 'X', 'Y', 'Z',                                       /* 24-29 */
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',                   /* 30-39 */
    CHAR_RETURN, CHAR_ESCAPE, CHAR_BACKSPACE, CHAR_TAB, ' ',            /* 40-44 */
    '_', '+', '{', '}', '|', CHAR_ILLEGAL, ':', '"', 0x7E, '<',         /* 45-54 */
    '>', '?', CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,   /* 55-60 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 61-64 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 65-68 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 69-72 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 73-76 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 77-80 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 81-84 */
    '*', '-', '+', '\n', '1', '2', '3', '4', '5',                       /* 85-97 */
    '6', '7', '8', '9', '0', '.', 0xb1,                                 /* 97-100 */
}; 

// STATE

static uint8_t hid_service_buffer[300];
static uint8_t device_id_sdp_service_buffer[100];
static const char hid_device_name[] = "BTstack HID Keyboard";
static btstack_packet_callback_registration_t hci_event_callback_registration;
static uint16_t hid_cid;
static uint8_t hid_boot_device = 0;

// HID Report sending
static uint8_t                send_buffer_storage[16];
static btstack_ring_buffer_t  send_buffer;
static btstack_timer_source_t send_timer;
static uint8_t                send_modifier;
static uint8_t                send_keycode;
static bool                   send_active;

static enum {
    APP_BOOTING,
    APP_NOT_CONNECTED,
    APP_CONNECTING,
    APP_CONNECTED
} app_state = APP_BOOTING;

// HID Keyboard lookup
static bool lookup_keycode(uint8_t character, const uint8_t * table, int size, uint8_t * keycode){
    int i;
    for (i=0;i<size;i++){
        if (table[i] != character) continue;
        *keycode = i;
        return true;
    }
    return false;
}

static bool keycode_and_modifer_us_for_character(uint8_t character, uint8_t * keycode, uint8_t * modifier){
    bool found;
    found = lookup_keycode(character, keytable_us_none, sizeof(keytable_us_none), keycode);
    if (found) {
        *modifier = 0;  // none
        return true;
    }
    found = lookup_keycode(character, keytable_us_shift, sizeof(keytable_us_shift), keycode);
    if (found) {
        *modifier = 2;  // shift
        return true;
    }
    return false;
}

static void send_report(int modifier, int keycode){
    // setup HID message: A1 = Input Report, Report ID, Payload
    uint8_t message[] = {0xa1, REPORT_ID, modifier, 0, keycode, 0, 0, 0, 0, 0};
    hid_device_send_interrupt_message(hid_cid, &message[0], sizeof(message));
}

static void trigger_key_up(btstack_timer_source_t * ts){
    UNUSED(ts);
    hid_device_request_can_send_now_event(hid_cid);
}

static void send_next(btstack_timer_source_t * ts) {
    // get next key from buffer
    uint8_t character;
    uint32_t num_bytes_read = 0;
    btstack_ring_buffer_read(&send_buffer, &character, 1, &num_bytes_read);
    if (num_bytes_read == 0) {
        // buffer empty, nothing to send
        send_active = false;
    } else {
        send_active = true;
        // lookup keycode and modifier using US layout
        bool found = keycode_and_modifer_us_for_character(character, &send_keycode, &send_modifier);
        if (found) {
            // request can send now
            hid_device_request_can_send_now_event(hid_cid);
        } else {
            // restart timer for next character
            btstack_run_loop_set_timer(ts, TYPING_DELAY_MS);
            btstack_run_loop_add_timer(ts);
        }
    }
}

static void queue_character(char character){
    btstack_ring_buffer_write(&send_buffer, (uint8_t *) &character, 1);
    if (send_active == false) {
        send_next(&send_timer);
    }
}

// Demo Application

#define TYPING_DEMO_PERIOD_MS 100

static const char * demo_text = "\n\nHello World!\n\nThis is the BTstack HID Keyboard Demo running on an Embedded Device.\n\n";
static int demo_pos;
static btstack_timer_source_t demo_text_timer;

static void demo_text_timer_handler(btstack_timer_source_t * ts){
    UNUSED(ts);

    // queue character
    uint8_t character = demo_text[demo_pos++];
    if (demo_text[demo_pos] == 0){
        demo_pos = 0;
    }
    queue_character(character);

    // set timer for next character
    btstack_run_loop_set_timer_handler(&demo_text_timer, demo_text_timer_handler);
    btstack_run_loop_set_timer(&demo_text_timer, TYPING_DEMO_PERIOD_MS);
    btstack_run_loop_add_timer(&demo_text_timer);
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t packet_size){
    /*
    程式碼摘要：
        HCI 事件處理：
        處理藍牙堆棧狀態改變的事件，如設備連接狀態變化。
        處理 SSP (安全簡單配對) 的用戶確認請求，並自動接受數字配對值。
        HID 元數據事件：
        當 HID 連接建立時，檢查連接狀態，如果成功，設置應用為已連接。
        如果 HID 連接關閉，重設狀態並移除發送計時器。
        當 HID 可以發送數據時，根據當前按鍵的狀態發送按鍵報告，並設置計時器來處理按鍵的按下和釋放操作。
        這段程式碼的主要作用是根據不同的藍牙事件處理 HID 鍵盤的連接與按鍵輸入。
    */
   
    UNUSED(channel); // 忽略 channel 參數
    UNUSED(packet_size); // 忽略 packet_size 參數
    uint8_t status;
    switch (packet_type){
        case HCI_EVENT_PACKET: // 處理 HCI 事件數據包
            switch (hci_event_packet_get_type(packet)){
                case BTSTACK_EVENT_STATE:
                    // 檢查藍牙堆棧是否處於工作狀態
                    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
                    app_state = APP_NOT_CONNECTED; // 設置應用狀態為未連接
                    break;

                case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                    // SSP: 用戶確認請求
                    log_info("SSP 用戶確認請求，數字值為 '%06"PRIu32"'\n", hci_event_user_confirmation_request_get_numeric_value(packet));
                    log_info("SSP 用戶自動接受\n");                   
                    break; 

                case HCI_EVENT_HID_META: // 處理 HID 元數據事件
                    switch (hci_event_hid_meta_get_subevent_code(packet)){
                        case HID_SUBEVENT_CONNECTION_OPENED:
                            status = hid_subevent_connection_opened_get_status(packet);
                            if (status != ERROR_CODE_SUCCESS) {
                                // 如果連接失敗，輸出錯誤信息
                                printf("連接失敗，狀態 0x%x\n", status);
                                app_state = APP_NOT_CONNECTED; // 設置應用狀態為未連接
                                hid_cid = 0; // 清空 HID 通道 ID
                                return;
                            }
                            app_state = APP_CONNECTED; // 連接成功，設置應用狀態為已連接
                            hid_cid = hid_subevent_connection_opened_get_hid_cid(packet); // 儲存 HID 通道 ID
                            break;

                        case HID_SUBEVENT_CONNECTION_CLOSED:
                            // 移除發送計時器，並設定應用狀態為未連接
                            btstack_run_loop_remove_timer(&send_timer);
                            printf("HID 連接已斷開\n");
                            app_state = APP_NOT_CONNECTED;
                            hid_cid = 0; // 清空 HID 通道 ID
                            break;

                        case HID_SUBEVENT_CAN_SEND_NOW:
                            // 檢查是否有按鍵代碼需要發送
                            if (send_keycode){
                                send_report(send_modifier, send_keycode); // 發送按鍵報告
                                // 計劃釋放按鍵的操作
                                send_keycode = 0;
                                send_modifier = 0;
                                btstack_run_loop_set_timer_handler(&send_timer, trigger_key_up); // 設定按鍵釋放計時器
                                btstack_run_loop_set_timer(&send_timer, TYPING_KEYDOWN_MS); // 設定按鍵釋放時間
                            } else {
                                send_report(0, 0); // 發送空白報告 (無按鍵按下)
                                // 計劃下一次按鍵按下的操作
                                btstack_run_loop_set_timer_handler(&send_timer, send_next); // 設定下一次按鍵按下計時器
                                btstack_run_loop_set_timer(&send_timer, TYPING_DELAY_MS); // 設定按鍵延遲時間
                            }
                            btstack_run_loop_add_timer(&send_timer); // 增加計時器到循環中
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


/* @section Main Application Setup
 *
 * @text Listing MainConfiguration shows main application code. 
 * To run a HID Device service you need to initialize the SDP, and to create and register HID Device record with it. 
 * At the end the Bluetooth stack is started.
 */

/* LISTING_START(MainConfiguration): Setup HID Device */

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_GPIO 18

#define HID_KEY_Q 0x14

void button_init(){
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

bool button_is_pressed(){
    return gpio_get_level(BUTTON_GPIO) == 0;
}

void send_key_q(){
    uint8_t keycode = 0;
    uint8_t modifier = 0;
    
    // 查找 'q' 對應的鍵碼和修飾符
    keycode_and_modifer_us_for_character('q', &keycode, &modifier);
    
    // 發送按鍵按下報告
    send_report(modifier, keycode);
}

void release_key(){
    uint8_t key_report[] = { 0xa1, 0x01, 0, 0, 0, 0, 0, 0, 0 };
    hid_device_send_interrupt_message(0, key_report, sizeof(key_report));
}
void task_button_monitor(void *arg){
    while (1){
        if (button_is_pressed()){
            printf("pressed\n");
            send_key_q(); 
        }
        else{
            printf("release\n");
            send_report(0, 0);
        } 
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){
    /*程式碼摘要：
    初始化按鈕監聽與創建按鈕監控任務。
    設定藍牙裝置為可發現模式，並設定角色切換與嗅探模式。
    初始化 L2CAP 協議和 SDP 伺服器，並創建並註冊 HID 鍵盤的 SDP 記錄。
    註冊藍牙 HCI 和 HID 事件的回調函數。
    初始化環狀緩衝區並開啟藍牙電源。*/


    (void)argc; // 忽略 argc
    (void)argv; // 忽略 argv
    button_init(); // 初始化按鈕

    // 創建一個按鈕監控的任務
    xTaskCreate(task_button_monitor, "button_monitor_task", 2048, NULL, 10, NULL);

    // 允許藍牙裝置被搜尋到
    gap_discoverable_control(1);
    // 設定為限時可發現模式，設置為周邊設備，並將類別設置為鍵盤
    gap_set_class_of_device(0x2540);
    // 設置本地名稱以便識別 - 後面的 00:00:00:00:00:00 會被實際的 BD 地址替換
    gap_set_local_name("HID Keyboard Demo 00:00:00:00:00:00");
    // 允許進行角色切換並啟用嗅探模式
    gap_set_default_link_policy_settings( LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE );
    // 允許在發起連接時角色切換 - 這允許 HID 主機在重新連接時成為主機
    gap_set_allow_role_switch(true);

    // 初始化 L2CAP 協議
    l2cap_init();

    // 初始化 SDP 伺服器
    sdp_init();
    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));

    uint8_t hid_virtual_cable = 0; // HID 虛擬連接
    uint8_t hid_remote_wake = 1;   // 遠程喚醒
    uint8_t hid_reconnect_initiate = 1; // 允許重新連接
    uint8_t hid_normally_connectable = 1; // 正常可連接

    // 設置 HID SDP 服務參數，包括 HID 服務的子類別、國別碼等
    hid_sdp_record_t hid_params = {
        // HID 服務子類別 2540 鍵盤，國別碼 33 (美國)
        0x2540, 33, 
        hid_virtual_cable, hid_remote_wake, 
        hid_reconnect_initiate, hid_normally_connectable,
        hid_boot_device,
        host_max_latency, host_min_timeout,
        3200,
        hid_descriptor_keyboard,
        sizeof(hid_descriptor_keyboard),
        hid_device_name
    };
    
    // 創建 HID SDP 記錄並註冊服務
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
    btstack_assert(de_get_len(hid_service_buffer) <= sizeof(hid_service_buffer));
    sdp_register_service(hid_service_buffer);

    // 如果沒有 USB 廠商 ID 並需要藍牙廠商 ID，請參考 https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers
    // 設備資訊: BlueKitchen GmbH，產品 1，版本 1
    device_id_create_sdp_record(device_id_sdp_service_buffer, sdp_create_service_record_handle(), DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    btstack_assert(de_get_len(device_id_sdp_service_buffer) <= sizeof(device_id_sdp_service_buffer));
    sdp_register_service(device_id_sdp_service_buffer);

    // 初始化 HID 設備
    hid_device_init(hid_boot_device, sizeof(hid_descriptor_keyboard), hid_descriptor_keyboard);
       
    // 註冊 HCI 事件的回調處理函數
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // 註冊 HID 事件的處理函數
    hid_device_register_packet_handler(&packet_handler);

    // 初始化發送緩衝區
    btstack_ring_buffer_init(&send_buffer, send_buffer_storage, sizeof(send_buffer_storage));

    // 開啟藍牙裝置電源
    hci_power_control(HCI_POWER_ON);
    return 0;
}

/* LISTING_END */
/* EXAMPLE_END */
