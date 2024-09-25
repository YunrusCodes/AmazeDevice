
#define BTSTACK_FILE__ "hfp_hf_muti.c"

#include "btstack_config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "btstack.h"
#include <inttypes.h>

#include "sco_demo_util.h"

// timing of keypresses
#define TYPING_KEYDOWN_MS  20
#define TYPING_DELAY_MS    20

// When not set to 0xffff, sniff and sniff subrating are enabled
static uint16_t host_max_latency = 1600;
static uint16_t host_min_timeout = 3200;

uint8_t hfp_service_buffer[150];
const uint8_t   rfcomm_channel_nr = 1;
const char hfp_hf_service_name[] = "HFP HF Demo";


#ifdef HAVE_BTSTACK_STDIN
// static const char * device_addr_string = "6C:72:E7:10:22:EE";
static const char * device_addr_string = "00:02:72:DC:31:C1";
#endif

static bd_addr_t device_addr;

#ifdef HAVE_BTSTACK_STDIN
// 80:BE:05:D5:28:48
// prototypes
static void show_usage(void);
#endif
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

static uint16_t indicators[1] = {0x01};
static uint8_t  negotiated_codec = HFP_CODEC_CVSD;
static btstack_packet_callback_registration_t hci_event_callback_registration;
static char cmd;

static void dump_supported_codecs(void){
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

static void report_status(uint8_t status, const char * message){
    if (status != ERROR_CODE_SUCCESS){
        printf("%s command failed, status 0x%02x\n", message, status);
    } else {
        printf("%s command successful\n", message);
    }
}

#ifdef HAVE_BTSTACK_STDIN

// Testig User Interface 
static void show_usage(void){
    bd_addr_t iut_address;
    gap_local_bd_addr(iut_address);

    printf("\n--- Bluetooth HFP Hands-Free (HF) unit Test Console %s ---\n", bd_addr_to_str(iut_address));
    printf("\n");
    printf("a - establish SLC to %s     | ", bd_addr_to_str(device_addr));
    printf("A - release SLC connection to device\n");
    printf("b - establish Audio connection             | B - release Audio connection\n");
    printf("d - query network operator                 | D - Enable HFP AG registration status update via bitmap(IIA)\n");
    printf("f - answer incoming call                   | F - Hangup call\n");
    printf("g - query network operator name            | G - reject incoming call\n");
    printf("i - dial 1234567                           | I - dial 7654321\n");
    printf("j - dial #1                                | J - dial #99\n");
    printf("o - Set speaker volume to 0  (minimum)     | O - Set speaker volume to 9  (default)\n");
    printf("p - Set speaker volume to 12 (higher)      | P - Set speaker volume to 15 (maximum)\n");
    printf("q - Set microphone gain to 0  (minimum)    | Q - Set microphone gain to 9  (default)\n");
    printf("s - Set microphone gain to 12 (higher)     | S - Set microphone gain to 15 (maximum)\n");
    printf("t - terminate connection\n");
    printf("u - send 'user busy' (TWC 0)\n");
    printf("U - end active call and accept other call' (TWC 1)\n");
    printf("v - Swap active call call (TWC 2)          | V - Join held call (TWC 3)\n");
    printf("w - Connect calls (TWC 4)                  | W - redial\n");
    printf("m - deactivate echo canceling and noise reduction\n");
    printf("c/C - disable/enable registration status update for all AG indicators\n");
    printf("e/E - disable/enable reporting of the extended AG error result code\n");
    printf("k/K - deactivate/activate call waiting notification\n");
    printf("l/L - deactivate/activate calling line notification\n");
    printf("n/N - deactivate/activate voice recognition\n");
    
    printf("0123456789#*-+ - send DTMF dial tones\n");
    printf("x - request phone number for voice tag     | X - current call status (ECS)\n");
    printf("y - release call with index 2 (ECC)        | Y - private consultation with call 2(ECC)\n");
    printf("[ - Query Response and Hold status (RHH ?) | ] - Place call in a response and held state(RHH 0)\n");
    printf("{ - Accept held call(RHH 1)                | } - Reject held call(RHH 2)\n");
    printf("? - Query Subscriber Number (NUM)\n");
    printf("! - Update HF indicator with assigned number 1 (HFI)\n");
    printf("\n");
}

static void stdin_process(char c){
    uint8_t status = ERROR_CODE_SUCCESS;

    cmd = c;    // used in packet handler

    if (cmd >= '0' && cmd <= '9'){
        printf("DTMF Code: %c\n", cmd);
        status = hfp_hf_send_dtmf_code(acl_handle, cmd);
        return;
    }

    switch (cmd){
        case '#':
        case '-':
        case '+':
        case '*':
            log_info("USER:\'%c\'", cmd);
            printf("DTMF Code: %c\n", cmd);
            status = hfp_hf_send_dtmf_code(acl_handle, cmd);
            break;
        case 'a':
            log_info("USER:\'%c\'", cmd);
            printf("Establish Service level connection to device with Bluetooth address %s...\n", bd_addr_to_str(device_addr));
            status = hfp_hf_establish_service_level_connection(device_addr);
            break;
        case 'A':
            log_info("USER:\'%c\'", cmd);
            printf("Release Service level connection.\n");
            status = hfp_hf_release_service_level_connection(acl_handle);
            break;
        case 'b':
            log_info("USER:\'%c\'", cmd);
            printf("Establish Audio connection to device with Bluetooth address %s...\n", bd_addr_to_str(device_addr));
            status = hfp_hf_establish_audio_connection(acl_handle);
            break;
        case 'B':
            log_info("USER:\'%c\'", cmd);
            printf("Release Audio service level connection.\n");
            status = hfp_hf_release_audio_connection(acl_handle);
            break;
        case 'C':
            log_info("USER:\'%c\'", cmd);
            printf("Enable registration status update for all AG indicators.\n");
            status = hfp_hf_enable_status_update_for_all_ag_indicators(acl_handle);
            break;
        case 'c':
            log_info("USER:\'%c\'", cmd);
            printf("Disable registration status update for all AG indicators.\n");
            status = hfp_hf_disable_status_update_for_all_ag_indicators(acl_handle);
            break;
        case 'D':
            log_info("USER:\'%c\'", cmd);
            printf("Set HFP AG registration status update for individual indicators (0111111).\n");
            status = hfp_hf_set_status_update_for_individual_ag_indicators(acl_handle, 63);
            break;
        case 'd':
            log_info("USER:\'%c\'", cmd);
            printf("Query network operator.\n");
            status = hfp_hf_query_operator_selection(acl_handle);
            break;
        case 'E':
            log_info("USER:\'%c\'", cmd);
            printf("Enable reporting of the extended AG error result code.\n");
            status = hfp_hf_enable_report_extended_audio_gateway_error_result_code(acl_handle);
            break;
        case 'e':
            log_info("USER:\'%c\'", cmd);
            printf("Disable reporting of the extended AG error result code.\n");
            status = hfp_hf_disable_report_extended_audio_gateway_error_result_code(acl_handle);
            break;
        case 'f':
            log_info("USER:\'%c\'", cmd);
            printf("Answer incoming call.\n");
            status = hfp_hf_answer_incoming_call(acl_handle);
            break;
        case 'F':
            log_info("USER:\'%c\'", cmd);
            printf("Hangup call.\n");
            status = hfp_hf_terminate_call(acl_handle);
            break;
        case 'G':
            log_info("USER:\'%c\'", cmd);
            printf("Reject incoming call.\n");
            status = hfp_hf_reject_incoming_call(acl_handle);
            break;
        case 'g':
            log_info("USER:\'%c\'", cmd);
            printf("Query operator.\n");
            status = hfp_hf_query_operator_selection(acl_handle);
            break;
        case 't':
            log_info("USER:\'%c\'", cmd);
            printf("Terminate HCI connection.\n");
            gap_disconnect(acl_handle);
            break;
        case 'i':
            log_info("USER:\'%c\'", cmd);
            printf("Dial 1234567\n");
            status = hfp_hf_dial_number(acl_handle, "1234567");
            break;
        case 'I':
            log_info("USER:\'%c\'", cmd);
            printf("Dial 7654321\n");
            status = hfp_hf_dial_number(acl_handle, "7654321");
            break;
        case 'j':
            log_info("USER:\'%c\'", cmd);
            printf("Dial #1\n");
            status = hfp_hf_dial_memory(acl_handle,1);
            break;
        case 'J':
            log_info("USER:\'%c\'", cmd);
            printf("Dial #99\n");
            status = hfp_hf_dial_memory(acl_handle,99);
            break;
        case 'k':
            log_info("USER:\'%c\'", cmd);
            printf("Deactivate call waiting notification\n");
            status = hfp_hf_deactivate_call_waiting_notification(acl_handle);
            break;
        case 'K':
            log_info("USER:\'%c\'", cmd);
            printf("Activate call waiting notification\n");
            status = hfp_hf_activate_call_waiting_notification(acl_handle);
            break;
        case 'l':
            log_info("USER:\'%c\'", cmd);
            printf("Deactivate calling line notification\n");
            status = hfp_hf_deactivate_calling_line_notification(acl_handle);
            break;
        case 'L':
            log_info("USER:\'%c\'", cmd);
            printf("Activate calling line notification\n");
            status = hfp_hf_activate_calling_line_notification(acl_handle);
            break;
        case 'm':
            log_info("USER:\'%c\'", cmd);
            printf("Deactivate echo canceling and noise reduction\n");
            status = hfp_hf_deactivate_echo_canceling_and_noise_reduction(acl_handle);
            break;
        case 'n':
            log_info("USER:\'%c\'", cmd);
            printf("Deactivate voice recognition\n");
            status = hfp_hf_deactivate_voice_recognition(acl_handle);
            break;
        case 'N':
            log_info("USER:\'%c\'", cmd);
            printf("Activate voice recognition %s\n", bd_addr_to_str(device_addr));
            status = hfp_hf_activate_voice_recognition(acl_handle);
            break;
        case 'o':
            log_info("USER:\'%c\'", cmd);
            printf("Set speaker gain to 0 (minimum)\n");
            status = hfp_hf_set_speaker_gain(acl_handle, 0);
            break;
        case 'O':
            log_info("USER:\'%c\'", cmd);
            printf("Set speaker gain to 9 (default)\n");
            status = hfp_hf_set_speaker_gain(acl_handle, 9);
            break;
        case 'p':
            log_info("USER:\'%c\'", cmd);
            printf("Set speaker gain to 12 (higher)\n");
            status = hfp_hf_set_speaker_gain(acl_handle, 12);
            break;
        case 'P':
            log_info("USER:\'%c\'", cmd);
            printf("Set speaker gain to 15 (maximum)\n");
            status = hfp_hf_set_speaker_gain(acl_handle, 15);
            break;
        case 'q':
            log_info("USER:\'%c\'", cmd);
            printf("Set microphone gain to 0\n");
            status = hfp_hf_set_microphone_gain(acl_handle, 0);
            break;
        case 'Q':
            log_info("USER:\'%c\'", cmd);
            printf("Set microphone gain to 9\n");
            status = hfp_hf_set_microphone_gain(acl_handle, 9);
            break;
        case 's':
            log_info("USER:\'%c\'", cmd);
            printf("Set microphone gain to 12\n");
            status = hfp_hf_set_microphone_gain(acl_handle, 12);
            break;
        case 'S':
            log_info("USER:\'%c\'", cmd);
            printf("Set microphone gain to 15\n");
            status = hfp_hf_set_microphone_gain(acl_handle, 15);
            break;
        case 'u':
            log_info("USER:\'%c\'", cmd);
            printf("Send 'user busy' (Three-Way Call 0)\n");
            status = hfp_hf_user_busy(acl_handle);
            break;
        case 'U':
            log_info("USER:\'%c\'", cmd);
            printf("End active call and accept waiting/held call (Three-Way Call 1)\n");
            status = hfp_hf_end_active_and_accept_other(acl_handle);
            break;
        case 'v':
            log_info("USER:\'%c\'", cmd);
            printf("Swap active call and hold/waiting call (Three-Way Call 2)\n");
            status = hfp_hf_swap_calls(acl_handle);
            break;
        case 'V':
            log_info("USER:\'%c\'", cmd);
            printf("Join hold call (Three-Way Call 3)\n");
            status = hfp_hf_join_held_call(acl_handle);
            break;
        case 'w':
            log_info("USER:\'%c\'", cmd);
            printf("Connect calls (Three-Way Call 4)\n");
            status = hfp_hf_connect_calls(acl_handle);
            break;
        case 'W':
            log_info("USER:\'%c\'", cmd);
            printf("Redial\n");
            status = hfp_hf_redial_last_number(acl_handle);
            break;
        case 'x':
            log_info("USER:\'%c\'", cmd);
            printf("Request phone number for voice tag\n");
            status = hfp_hf_request_phone_number_for_voice_tag(acl_handle);
            break;
        case 'X':
            log_info("USER:\'%c\'", cmd);
            printf("Query current call status\n");
            status = hfp_hf_query_current_call_status(acl_handle);
            break;
        case 'y':
            log_info("USER:\'%c\'", cmd);
            printf("Release call with index 2\n");
            status = hfp_hf_release_call_with_index(acl_handle, 2);
            break;
        case 'Y':
            log_info("USER:\'%c\'", cmd);
            printf("Private consultation with call 2\n");
            status = hfp_hf_private_consultation_with_call(acl_handle, 2);
            break;
        case '[':
            log_info("USER:\'%c\'", cmd);
            printf("Query Response and Hold status (RHH ?)\n");
            status = hfp_hf_rrh_query_status(acl_handle);
            break;
        case ']':
            log_info("USER:\'%c\'", cmd);
            printf("Place call in a response and held state (RHH 0)\n");
            status = hfp_hf_rrh_hold_call(acl_handle);
           break;
        case '{':
            log_info("USER:\'%c\'", cmd);
            printf("Accept held call (RHH 1)\n");
            status = hfp_hf_rrh_accept_held_call(acl_handle);
            break;
        case '}':
            log_info("USER:\'%c\'", cmd);
            printf("Reject held call (RHH 2)\n");
            status = hfp_hf_rrh_reject_held_call(acl_handle);
            break;
        case '?':
            log_info("USER:\'%c\'", cmd);
            printf("Query Subscriber Number\n");
            status = hfp_hf_query_subscriber_number(acl_handle);
            break;
        case '!':
            log_info("USER:\'%c\'", cmd);
            printf("Update HF indicator with assigned number 1 (HFI)\n");
            status = hfp_hf_set_hf_indicator(acl_handle, 1, 1);
            break;
        default:
            show_usage();
            break;
    }

    if (status != ERROR_CODE_SUCCESS){
        printf("Could not perform command, status 0x%02x\n", status);
    }
}
#endif

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * event, uint16_t event_size){
    UNUSED(channel);
    bd_addr_t event_addr;

    switch (packet_type){

        case HCI_SCO_DATA_PACKET:
            // forward received SCO / audio packets to SCO component
            if (READ_SCO_CONNECTION_HANDLE(event) != sco_handle) break;
            sco_demo_receive(event, event_size);
            break;

        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(event)){
                case BTSTACK_EVENT_STATE:
                    // list supported codecs after stack has started up
                    if (btstack_event_state_get_state(event) != HCI_STATE_WORKING) break;
                    dump_supported_codecs();
                    break;

                case HCI_EVENT_PIN_CODE_REQUEST:
                    // inform about pin code request and respond with '0000'
                    printf("Pin code request - using '0000'\n");
                    hci_event_pin_code_request_get_bd_addr(event, event_addr);
                    gap_pin_code_response(event_addr, "0000");
                    break;

                case HCI_EVENT_SCO_CAN_SEND_NOW:
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
                    // inform about pin code request
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
                                printf("Connection failed, status 0x%02x\n", status);
                                break;
                            }
                            acl_handle = hfp_subevent_service_level_connection_established_get_acl_handle(event);
                            hfp_subevent_service_level_connection_established_get_bd_addr(event, device_addr);
                            printf("Service level connection established %s.\n\n", bd_addr_to_str(device_addr));
                            break;
                        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED:
                            acl_handle = HCI_CON_HANDLE_INVALID;
                            printf("Service level connection released.\n\n");
                            break;
                        case HFP_SUBEVENT_AUDIO_CONNECTION_ESTABLISHED:
                            status = hfp_subevent_audio_connection_established_get_status(event);
                            if (status != ERROR_CODE_SUCCESS){
                                printf("Audio connection establishment failed with status 0x%02x\n", status);
                                break;
                            } 
                            sco_handle = hfp_subevent_audio_connection_established_get_sco_handle(event);
                            printf("Audio connection established with SCO handle 0x%04x.\n", sco_handle);
                            negotiated_codec = hfp_subevent_audio_connection_established_get_negotiated_codec(event);
                            switch (negotiated_codec){
                                case HFP_CODEC_CVSD:
                                    printf("Using CVSD codec.\n");
                                    break;
                                case HFP_CODEC_MSBC:
                                    printf("Using mSBC codec.\n");
                                    break;
                                case HFP_CODEC_LC3_SWB:
                                    printf("Using LC3-SWB codec.\n");
                                    break;
                                default:
                                    printf("Using unknown codec 0x%02x.\n", negotiated_codec);
                                    break;
                            }
                            sco_demo_set_codec(negotiated_codec);
                            hci_request_sco_can_send_now_event();
                            break;

                        case HFP_SUBEVENT_CALL_ANSWERED:
                            printf("Call answered\n");
                            break;

                        case HFP_SUBEVENT_CALL_TERMINATED:
                            printf("Call terminated\n");
                            break;

                        case HFP_SUBEVENT_AUDIO_CONNECTION_RELEASED:
                            sco_handle = HCI_CON_HANDLE_INVALID;
                            printf("Audio connection released\n");
                            sco_demo_close();
                            break;
                        case  HFP_SUBEVENT_COMPLETE:
                            status = hfp_subevent_complete_get_status(event);
                            if (status == ERROR_CODE_SUCCESS){
                                printf("Cmd \'%c\' succeeded\n", cmd);
                            } else {
                                printf("Cmd \'%c\' failed with status 0x%02x\n", cmd, status);
                            }
                            break;

                        case HFP_SUBEVENT_AG_INDICATOR_MAPPING:
                            printf("AG Indicator Mapping | INDEX %d: range [%d, %d], name '%s'\n", 
                                hfp_subevent_ag_indicator_mapping_get_indicator_index(event), 
                                hfp_subevent_ag_indicator_mapping_get_indicator_min_range(event),
                                hfp_subevent_ag_indicator_mapping_get_indicator_max_range(event),
                                (const char*) hfp_subevent_ag_indicator_mapping_get_indicator_name(event));
                            break;

                        case HFP_SUBEVENT_AG_INDICATOR_STATUS_CHANGED:
                            printf("AG Indicator Status  | INDEX %d: status 0x%02x, '%s'\n", 
                                hfp_subevent_ag_indicator_status_changed_get_indicator_index(event), 
                                hfp_subevent_ag_indicator_status_changed_get_indicator_status(event),
                                (const char*) hfp_subevent_ag_indicator_status_changed_get_indicator_name(event));
                            break;
                        case HFP_SUBEVENT_NETWORK_OPERATOR_CHANGED:
                            printf("NETWORK_OPERATOR_CHANGED, operator mode %d, format %d, name %s\n", 
                                hfp_subevent_network_operator_changed_get_network_operator_mode(event), 
                                hfp_subevent_network_operator_changed_get_network_operator_format(event), 
                                (char *) hfp_subevent_network_operator_changed_get_network_operator_name(event));          
                            break;
                        case HFP_SUBEVENT_EXTENDED_AUDIO_GATEWAY_ERROR:
                            printf("EXTENDED_AUDIO_GATEWAY_ERROR_REPORT, status 0x%02x\n",
                                hfp_subevent_extended_audio_gateway_error_get_error(event));
                            break;
                        case HFP_SUBEVENT_START_RINGING:
                            printf("** START Ringing **\n");
                            break;
                        case HFP_SUBEVENT_RING:
                            printf("** Ring **\n");
                            break;
                        case HFP_SUBEVENT_STOP_RINGING:
                            printf("** STOP Ringing **\n");
                            break;
                        case HFP_SUBEVENT_NUMBER_FOR_VOICE_TAG:
                            printf("Phone number for voice tag: %s\n", 
                                (const char *) hfp_subevent_number_for_voice_tag_get_number(event));
                            break;
                        case HFP_SUBEVENT_SPEAKER_VOLUME:
                            printf("Speaker volume: gain %u\n",
                                hfp_subevent_speaker_volume_get_gain(event));
                            break;
                        case HFP_SUBEVENT_MICROPHONE_VOLUME:
                            printf("Microphone volume: gain %u\n",
                            hfp_subevent_microphone_volume_get_gain(event));
                            break;
                        case HFP_SUBEVENT_CALLING_LINE_IDENTIFICATION_NOTIFICATION:
                            printf("Caller ID, number '%s', alpha '%s'\n", (const char *) hfp_subevent_calling_line_identification_notification_get_number(event),
                                   (const char *) hfp_subevent_calling_line_identification_notification_get_alpha(event));
                            break;
                        case HFP_SUBEVENT_ENHANCED_CALL_STATUS:
                            printf("Enhanced call status:\n");
                            printf("  - call index: %d \n", hfp_subevent_enhanced_call_status_get_clcc_idx(event));
                            printf("  - direction : %s \n", hfp_enhanced_call_dir2str(hfp_subevent_enhanced_call_status_get_clcc_dir(event)));
                            printf("  - status    : %s \n", hfp_enhanced_call_status2str(hfp_subevent_enhanced_call_status_get_clcc_status(event)));
                            printf("  - mode      : %s \n", hfp_enhanced_call_mode2str(hfp_subevent_enhanced_call_status_get_clcc_mode(event)));
                            printf("  - multipart : %s \n", hfp_enhanced_call_mpty2str(hfp_subevent_enhanced_call_status_get_clcc_mpty(event)));
                            printf("  - type      : %d \n", hfp_subevent_enhanced_call_status_get_bnip_type(event));
                            printf("  - number    : %s \n", hfp_subevent_enhanced_call_status_get_bnip_number(event));
                            break;
                        
                        case HFP_SUBEVENT_VOICE_RECOGNITION_ACTIVATED:
                            status = hfp_subevent_voice_recognition_activated_get_status(event);
                            if (status != ERROR_CODE_SUCCESS){
                                printf("Voice Recognition Activate command failed, status 0x%02x\n", status);
                                break;
                            }
                            
                            switch (hfp_subevent_voice_recognition_activated_get_enhanced(event)){
                                case 0: 
                                    printf("\nVoice recognition ACTIVATED\n\n");
                                    break;
                                default:
                                    printf("\nEnhanced voice recognition ACTIVATED.\n");
                                    printf("Start new audio enhanced voice recognition session %s\n\n", bd_addr_to_str(device_addr));
                                    status = hfp_hf_enhanced_voice_recognition_report_ready_for_audio(acl_handle);
                                    break;
                            }
                            break;
            
                        case HFP_SUBEVENT_VOICE_RECOGNITION_DEACTIVATED:
                            status = hfp_subevent_voice_recognition_deactivated_get_status(event);
                            if (status != ERROR_CODE_SUCCESS){
                                printf("Voice Recognition Deactivate command failed, status 0x%02x\n", status);
                                break;
                            }
                            printf("\nVoice Recognition DEACTIVATED\n\n");
                            break;

                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_HF_READY_FOR_AUDIO:
                            status = hfp_subevent_enhanced_voice_recognition_hf_ready_for_audio_get_status(event);
                            report_status(status, "Enhanced Voice recognition: READY FOR AUDIO");
                            break;

                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_AG_READY_TO_ACCEPT_AUDIO_INPUT:
                            printf("\nEnhanced Voice recognition AG status: AG READY TO ACCEPT AUDIO INPUT\n\n");                            
                            break;
                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_AG_IS_STARTING_SOUND:
                            printf("\nEnhanced Voice recognition AG status: AG IS STARTING SOUND\n\n");                            
                            break;
                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_AG_IS_PROCESSING_AUDIO_INPUT:
                            printf("\nEnhanced Voice recognition AG status: AG IS PROCESSING AUDIO INPUT\n\n");                            
                            break;
                        
                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_AG_MESSAGE:
                            printf("\nEnhanced Voice recognition AG message: \'%s\'\n", hfp_subevent_enhanced_voice_recognition_ag_message_get_text(event));                            
                            break;

                        case HFP_SUBEVENT_ECHO_CANCELING_AND_NOISE_REDUCTION_DEACTIVATE:
                            status = hfp_subevent_echo_canceling_and_noise_reduction_deactivate_get_status(event);
                            report_status(status, "Echo Canceling and Noise Reduction Deactivate");
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
    UNUSED(channel);
    UNUSED(packet_size);
    uint8_t status;
    switch (packet_type){
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)){
                case BTSTACK_EVENT_STATE:
                    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
                    app_state = APP_NOT_CONNECTED;
                    break;

                case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                    // ssp: inform about user confirmation request
                    log_info("SSP User Confirmation Request with numeric value '%06"PRIu32"'\n", hci_event_user_confirmation_request_get_numeric_value(packet));
                    log_info("SSP User Confirmation Auto accept\n");                   
                    break; 

                case HCI_EVENT_HID_META:
                    switch (hci_event_hid_meta_get_subevent_code(packet)){
                        case HID_SUBEVENT_CONNECTION_OPENED:
                            status = hid_subevent_connection_opened_get_status(packet);
                            if (status != ERROR_CODE_SUCCESS) {
                                // outgoing connection failed
                                printf("Connection failed, status 0x%x\n", status);
                                app_state = APP_NOT_CONNECTED;
                                hid_cid = 0;
                                return;
                            }
                            app_state = APP_CONNECTED;
                            hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);

                            break;
                        case HID_SUBEVENT_CONNECTION_CLOSED:
                            btstack_run_loop_remove_timer(&send_timer);
                            printf("HID Disconnected\n");
                            app_state = APP_NOT_CONNECTED;
                            hid_cid = 0;
                            break;
                        case HID_SUBEVENT_CAN_SEND_NOW:
                            if (send_keycode){
                                send_report(send_modifier, send_keycode);
                                // schedule key up
                                send_keycode = 0;
                                send_modifier = 0;
                                btstack_run_loop_set_timer_handler(&send_timer, trigger_key_up);
                                btstack_run_loop_set_timer(&send_timer, TYPING_KEYDOWN_MS);
                            } else {
                                send_report(0, 0);
                                // schedule next key down
                                btstack_run_loop_set_timer_handler(&send_timer, send_next);
                                btstack_run_loop_set_timer(&send_timer, TYPING_DELAY_MS);
                            }
                            btstack_run_loop_add_timer(&send_timer);
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
 * To run a HFP HF service you need to initialize the SDP, and to create and register HFP HF record with it. 
 * The packet_handler is used for sending commands to the HFP AG. It also receives the HFP AG's answers.
 * The stdin_process callback allows for sending commands to the HFP AG. 
 * At the end the Bluetooth stack is started.
 */

/* LISTING_START(MainConfiguration): Setup HFP Hands-Free unit */

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
            send_key_q(); 
        }
        else{
            send_report(0, 0);
        } 
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){
    (void)argc;
    (void)argv;

    // 初始化協議
    // 初始化 L2CAP
    l2cap_init();
    rfcomm_init();
    sdp_init();
#ifdef ENABLE_BLE
    // 初始化 LE 安全管理器，必要時用於跨傳輸密鑰推導
    sm_init();
#endif

    // 初始化免提設備支持的功能
    uint16_t hf_supported_features          =
        (1<<HFP_HFSF_ESCO_S4)               |  // eSCO S4支持
        (1<<HFP_HFSF_CLI_PRESENTATION_CAPABILITY) |  // CLI 顯示支持
        (1<<HFP_HFSF_HF_INDICATORS)         |  // 免提設備指示器支持
        (1<<HFP_HFSF_CODEC_NEGOTIATION)     |  // 編解碼器協商支持
        (1<<HFP_HFSF_ENHANCED_CALL_STATUS)  |  // 增強的通話狀態支持
        (1<<HFP_HFSF_VOICE_RECOGNITION_FUNCTION)  |  // 語音識別功能支持
        (1<<HFP_HFSF_ENHANCED_VOICE_RECOGNITION_STATUS) |  // 增強的語音識別狀態支持
        (1<<HFP_HFSF_VOICE_RECOGNITION_TEXT) |  // 語音識別文本支持
        (1<<HFP_HFSF_EC_NR_FUNCTION) |  // 回聲消除和噪音抑制功能
        (1<<HFP_HFSF_REMOTE_VOLUME_CONTROL);  // 遠程音量控制支持

    // 初始化 HFP HF 服務
    hfp_hf_init(rfcomm_channel_nr);
    hfp_hf_init_supported_features(hf_supported_features);
    hfp_hf_init_hf_indicators(sizeof(indicators)/sizeof(uint16_t), indicators);
    hfp_hf_init_codecs(sizeof(codecs), codecs);
    hfp_hf_register_packet_handler(hfp_hf_packet_handler);

    // 配置 SDP（服務發現協議）

    // - 創建並註冊 HFP HF 服務記錄
    memset(hfp_service_buffer, 0, sizeof(hfp_service_buffer));
    hfp_hf_create_sdp_record_with_codecs(hfp_service_buffer, sdp_create_service_record_handle(),
                             rfcomm_channel_nr, hfp_hf_service_name, hf_supported_features, sizeof(codecs), codecs);
    btstack_assert(de_get_len(hfp_service_buffer) <= sizeof(hfp_service_buffer));
    sdp_register_service(hfp_service_buffer);

    // 配置 GAP（通用訪問協議） - 發現/連接

    // - 設置本地名稱，當藍牙地址可用時自動替換
    gap_set_local_name("HFP HF Demo 00:00:00:00:00:00");

    // - 允許在藍牙設備查詢中顯示
    gap_discoverable_control(1);

    // - 設置設備類別：服務類別：音頻，主要設備類別：音頻，次要設備類別：免提設備
    gap_set_class_of_device(0x200408);

    // - 允許角色切換和休眠模式
    gap_set_default_link_policy_settings( LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE );

    // - 允許出站連接的角色切換，這樣當我們重新連接到 HFP AG 時，它可以成為主設備
    gap_set_allow_role_switch(true);

    // 註冊 HCI 事件和 SCO 包處理程序
    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    hci_register_sco_packet_handler(&hci_packet_handler);

    // 初始化 SCO / HFP 音頻處理
    sco_demo_init();

#ifdef HAVE_BTSTACK_STDIN
    // 解析人類可讀的藍牙地址
    sscanf_bd_addr(device_addr_string, device_addr);
    btstack_stdin_setup(stdin_process);
#endif

    // 開啟藍牙模塊！
    hci_power_control(HCI_POWER_ON);
    return 0;    
    
    return 0;
}









/* LISTING_END */
/* EXAMPLE_END */
