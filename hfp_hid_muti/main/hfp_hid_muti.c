#include "btstack_config.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "btstack.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sco_demo_util.h"
#include "btstack_run_loop.h"
#include "hid_device.h"

// 常量定义
#define REPORT_ID           0x01
#define BUTTON_GPIO         1
#define HID_KEY_Q           0x14

// HID 键盘描述符
const uint8_t hid_descriptor_keyboard[] = {
    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x06,                    // Usage (Keyboard)
    0xa1, 0x01,                    // Collection (Application)

    // Report ID
    0x85, REPORT_ID,               // Report ID

    // Modifier byte (input)
    0x75, 0x01,                    // Report size (1)
    0x95, 0x08,                    // Report count (8)
    0x05, 0x07,                    // Usage page (Keyboard)
    0x19, 0xe0,                    // Usage minimum (Left Control)
    0x29, 0xe7,                    // Usage maximum (Right GUI)
    0x15, 0x00,                    // Logical minimum (0)
    0x25, 0x01,                    // Logical maximum (1)
    0x81, 0x02,                    // Input (Data, Variable, Absolute)

    // Reserved byte (input)
    0x75, 0x01,                    // Report size (1)
    0x95, 0x08,                    // Report count (8)
    0x81, 0x03,                    // Input (Constant, Variable, Absolute)

    // LED report + padding (output)
    0x95, 0x05,                    // Report count (5)
    0x75, 0x01,                    // Report size (1)
    0x05, 0x08,                    // Usage page (LEDs)
    0x19, 0x01,                    // Usage minimum (Num Lock)
    0x29, 0x05,                    // Usage maximum (Kana)
    0x91, 0x02,                    // Output (Data, Variable, Absolute)

    0x95, 0x01,                    // Report count (1)
    0x75, 0x03,                    // Report size (3)
    0x91, 0x03,                    // Output (Constant, Variable, Absolute)

    // Key codes (input)
    0x95, 0x06,                    // Report count (6)
    0x75, 0x08,                    // Report size (8)
    0x15, 0x00,                    // Logical minimum (0)
    0x25, 0xff,                    // Logical maximum (255)
    0x05, 0x07,                    // Usage page (Keyboard)
    0x19, 0x00,                    // Usage minimum (Reserved)
    0x29, 0xff,                    // Usage maximum (Reserved)
    0x81, 0x00,                    // Input (Data, Array)

    0xc0                           // End Collection
};

// 全局 HID 变量
static uint8_t hid_service_buffer[300];
static btstack_packet_callback_registration_t hid_hci_event_callback_registration;
static uint16_t hid_cid;

// HID 应用状态
typedef enum {
    APP_BOOTING,
    APP_NOT_CONNECTED,
    APP_CONNECTING,
    APP_CONNECTED
} app_state_t;

static app_state_t app_state = APP_BOOTING;

// 发送 HID 报告
static void send_report(int modifier, int keycode) {
    uint8_t message[] = { 0xa1, REPORT_ID, modifier, 0, keycode, 0, 0, 0, 0, 0 };
    hid_device_send_interrupt_message(hid_cid, message, sizeof(message));
}

// 初始化按钮 GPIO
void button_init() {
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

// 检查按钮是否被按下
bool button_is_pressed() {
    return gpio_get_level(BUTTON_GPIO) == 0;
}

// 全局定时器
static btstack_timer_source_t button_monitor_timer;

// 按钮监控任务处理器
static void button_monitor_handler(btstack_timer_source_t *ts) {
    if (button_is_pressed()) {
        send_report(0, HID_KEY_Q);
    } else {
        send_report(0, 0);
    }

    // 重置定时器间隔为 10ms 后再次调用
    btstack_run_loop_set_timer(ts, 10);
    btstack_run_loop_add_timer(ts);
}

// 启动按钮监控定时器
static void start_button_monitor(void) {
    btstack_run_loop_set_timer_handler(&button_monitor_timer, button_monitor_handler);
    btstack_run_loop_set_timer(&button_monitor_timer, 10);  // 初始间隔 10ms
    btstack_run_loop_add_timer(&button_monitor_timer);
}

// HFP 相关变量和函数
uint8_t hfp_service_buffer[300];
const uint8_t rfcomm_channel_nr = 1;
const char hfp_hf_service_name[] = "HFP HF Demo";

static bd_addr_t device_addr;
static hci_con_handle_t acl_handle = HCI_CON_HANDLE_INVALID;
static hci_con_handle_t sco_handle = HCI_CON_HANDLE_INVALID;

static uint8_t codecs[] = {
    HFP_CODEC_CVSD,
#ifdef ENABLE_HFP_WIDE_BAND_SPEECH
    HFP_CODEC_MSBC,
#endif
#ifdef ENABLE_HFP_SUPER_WIDE_BAND_SPEECH
    HFP_CODEC_LC3_SWB,
#endif
};

static uint16_t indicators[1] = { 0x01 };
static uint8_t negotiated_codec = HFP_CODEC_CVSD;
static btstack_packet_callback_registration_t hfp_hci_event_callback_registration;
static char cmd;

static void dump_supported_codecs(void) {
    printf("Supported codecs: CVSD");
    if (hci_extended_sco_link_supported()) {
#ifdef ENABLE_HFP_WIDE_BAND_SPEECH
        printf(", mSBC");
#endif
#ifdef ENABLE_HFP_SUPER_WIDE_BAND_SPEECH
        printf(", LC3-SWB");
#endif
        printf("\n");
    } else {
        printf("\nmSBC and/or LC3-SWB disabled as eSCO not supported by local controller.\n");
    }
}

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * event, uint16_t event_size){
    UNUSED(channel);
    bd_addr_t event_addr;

    switch (packet_type){

        case HCI_SCO_DATA_PACKET:
            // 處理接收到的 SCO 音頻資料包，並轉發給 SCO 組件
            if (READ_SCO_CONNECTION_HANDLE(event) != sco_handle) break;
            sco_demo_receive(event, event_size);
            break;

        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(event)){
                case BTSTACK_EVENT_STATE:
                    // 在藍牙協議棧啟動後列出支持的編解碼器
                    if (btstack_event_state_get_state(event) != HCI_STATE_WORKING) break;
                    dump_supported_codecs();
                    break;

                case HCI_EVENT_PIN_CODE_REQUEST:
                    // 當收到 PIN 碼請求時，回應 "0000"
                    printf("Pin code request - using '0000'\n");
                    hci_event_pin_code_request_get_bd_addr(event, event_addr);
                    gap_pin_code_response(event_addr, "0000");
                    break;

                case HCI_EVENT_SCO_CAN_SEND_NOW:
                    // 當可以發送 SCO 音頻時，發送數據
                    sco_demo_send(sco_handle);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static void hfp_hf_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * event, uint16_t event_size){
    UNUSED(channel);
    uint8_t status;
    bd_addr_t event_addr;

    switch (packet_type){
        case HCI_SCO_DATA_PACKET:
            // 處理接收到的 SCO 音頻資料包
            if (READ_SCO_CONNECTION_HANDLE(event) != sco_handle) break;
            sco_demo_receive(event, event_size);
            break;

        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(event)){
                case BTSTACK_EVENT_STATE:
                    if (btstack_event_state_get_state(event) != HCI_STATE_WORKING) break;
                    dump_supported_codecs();
                    break;

                case HCI_EVENT_PIN_CODE_REQUEST:
                    printf("Pin code request - using '0000'\n");
                    hci_event_pin_code_request_get_bd_addr(event, event_addr);
                    gap_pin_code_response(event_addr, "0000");
                    break;

                case HCI_EVENT_SCO_CAN_SEND_NOW:
                    sco_demo_send(sco_handle);
                    break;

                case HCI_EVENT_HFP_META:
                    switch (hci_event_hfp_meta_get_subevent_code(event)) {
                        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_ESTABLISHED:
                            status = hfp_subevent_service_level_connection_established_get_status(event);
                            if (status != ERROR_CODE_SUCCESS){
                                printf("HFP Connection failed, status 0x%02x\n", status);
                                break;
                            }
                            // HFP 连接建立成功，保存设备地址
                            hfp_subevent_service_level_connection_established_get_bd_addr(event, device_addr);
                            printf("HFP Service level connection established with %s.\n", bd_addr_to_str(device_addr));
                            
                            // 这里检查 HID 是否已经连接，如果未连接则启动 HID 连接
                            if (app_state != APP_CONNECTED) {
                                printf("HID not connected. Initiating HID connection...\n");
                                hid_device_connect(device_addr, &hid_cid);
                            }
                            break;

                        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED:
                            acl_handle = HCI_CON_HANDLE_INVALID;
                            printf("HFP Service level connection released.\n\n");
                            break;

                        case HFP_SUBEVENT_AUDIO_CONNECTION_ESTABLISHED:
                            status = hfp_subevent_audio_connection_established_get_status(event);
                            if (status != ERROR_CODE_SUCCESS){
                                printf("HFP Audio connection failed with status 0x%02x\n", status);
                                break;
                            }
                            sco_handle = hfp_subevent_audio_connection_established_get_sco_handle(event);
                            printf("HFP Audio connection established with SCO handle 0x%04x.\n", sco_handle);
                            negotiated_codec = hfp_subevent_audio_connection_established_get_negotiated_codec(event);
                            sco_demo_set_codec(negotiated_codec);
                            hci_request_sco_can_send_now_event();
                            break;

                        case HFP_SUBEVENT_AUDIO_CONNECTION_RELEASED:
                            sco_handle = HCI_CON_HANDLE_INVALID;
                            printf("HFP Audio connection released\n");
                            sco_demo_close();
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

static void hid_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t packet_size){
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
                                printf("HID Connection failed.\n");
                                app_state = APP_NOT_CONNECTED;
                                hid_cid = 0;
                                return;
                            }
                            printf("HID Connection established.\n");
                            app_state = APP_CONNECTED;
                            hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
                            break;

                        case HID_SUBEVENT_CONNECTION_CLOSED:
                            printf("HID Connection closed.\n");
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


int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){
    (void)argc;
    (void)argv;

    // 初始化按鈕並創建監控任務
    button_init();

    // 初始化基本协议栈
    l2cap_init(); 
    rfcomm_init();
    sdp_init();
#ifdef ENABLE_BLE
    sm_init();  // 初始化 BLE 安全管理器
#endif

    // 初始化 HFP HF 支持的功能
    uint16_t hf_supported_features = 
        (1<<HFP_HFSF_ESCO_S4) |
        (1<<HFP_HFSF_CLI_PRESENTATION_CAPABILITY) |
        (1<<HFP_HFSF_HF_INDICATORS) |
        (1<<HFP_HFSF_CODEC_NEGOTIATION) |
        (1<<HFP_HFSF_ENHANCED_CALL_STATUS) |
        (1<<HFP_HFSF_VOICE_RECOGNITION_FUNCTION) |
        (1<<HFP_HFSF_ENHANCED_VOICE_RECOGNITION_STATUS) |
        (1<<HFP_HFSF_VOICE_RECOGNITION_TEXT) |
        (1<<HFP_HFSF_EC_NR_FUNCTION) |
        (1<<HFP_HFSF_REMOTE_VOLUME_CONTROL);

    // 初始化 HFP HF 服务
    hfp_hf_init(rfcomm_channel_nr);
    hfp_hf_init_supported_features(hf_supported_features);
    hfp_hf_init_hf_indicators(sizeof(indicators)/sizeof(uint16_t), indicators);
    hfp_hf_init_codecs(sizeof(codecs), codecs);
    hfp_hf_register_packet_handler(hfp_hf_packet_handler);

    // 注册 HFP SDP 记录
    memset(hfp_service_buffer, 0, sizeof(hfp_service_buffer));
    hfp_hf_create_sdp_record_with_codecs(hfp_service_buffer, sdp_create_service_record_handle(),
                                         rfcomm_channel_nr, hfp_hf_service_name, 
                                         hf_supported_features, sizeof(codecs), codecs);
    sdp_register_service(hfp_service_buffer);

    // 配置 HID 服务
    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));
    const char* hid_service_name = "HID Keyboard";  // HID 服务名称
    hid_sdp_record_t hid_params = {
        0x2540, 33,
        0, 1,
        1, 1,
        0,
        0, 0, 3200,
        hid_descriptor_keyboard,
        sizeof(hid_descriptor_keyboard),
        hid_service_name
    };
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
    sdp_register_service(hid_service_buffer);
    hid_device_init(0, sizeof(hid_descriptor_keyboard), hid_descriptor_keyboard);

    // 注册 HID 事件处理程序
    hid_hci_event_callback_registration.callback = &hid_packet_handler;  
    hci_add_event_handler(&hid_hci_event_callback_registration);
    hid_device_register_packet_handler(&hid_packet_handler);

    // 注册 HCI 事件和 SCO 包处理程序
    hfp_hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hfp_hci_event_callback_registration);
    hci_register_sco_packet_handler(&hci_packet_handler);

    // 初始化 SCO / HFP 音频处理
    sco_demo_init();

    // 启动按键监控
    start_button_monitor();

    // 配置 GAP 参数
    gap_set_local_name("HFP HF Demo 00:00:00:00:00:00");
    gap_discoverable_control(1);
    gap_set_class_of_device(0x200408);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);

    // 启动蓝牙模块
    hci_power_control(HCI_POWER_ON);  // 放在最后，确保所有服务和处理程序都已注册

    return 0;
}



/* LISTING_END */
/* EXAMPLE_END */
