/* USB host example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_intr_alloc.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_pins.h"
#include "soc/gpio_sig_map.h"
#include "hal/usbh_ll.h"
#include "hcd.h"
#include "esp_log.h"
#include "ctrl_pipe.h"
#include "usb_host_port.h"
#include "adb_driver.h"

// #define USE_ALTERNATIVE_CALLBACKS

uint8_t device_state = 0;
uint8_t conf_num;
static hcd_port_handle_t _port_hdl;

hcd_pipe_handle_t ctrl_pipe_hdl;
#define DEVICE_ADDR 1

uint8_t bMaxPacketSize0 = 64;
uint8_t conf_num = 0;
static bool ready = false;

void parse_cfg_descriptor(uint8_t *data_buffer, usb_transfer_status_t status, uint8_t len, uint8_t *conf_num);

static void utf16_to_utf8(char *in, char *out, uint8_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        out[i / 2] = in[i];
        i++;
    }
}

/*------------------------------- USBH EP0 CTRL PIPE CALLBACKS -------------------------------*/
void usbh_get_device_desc_cb(uint8_t *data_buffer, size_t num_bytes, void *context)
{
    ESP_LOG_BUFFER_HEX_LEVEL("DEVICE descriptor", data_buffer, num_bytes, ESP_LOG_INFO);
    parse_cfg_descriptor(data_buffer, 0, num_bytes, &conf_num);

    ESP_LOGI("", "port handle: %p", _port_hdl);
    xfer_set_address(port_hdl, ctrl_pipe_hdl, DEVICE_ADDR);
}

void usbh_set_address_cb(uint16_t addr, void *context)
{
    ESP_LOGI("ADDRESS", "%d", addr);
    if (ESP_OK != hcd_pipe_update_dev_addr(ctrl_pipe_hdl, DEVICE_ADDR))
        ESP_LOGE("", "failed to update device address");
    xfer_set_configuration(port_hdl, ctrl_pipe_hdl, 0);
}
void usbh_ctrl_pipe_class_specific_cb(pipe_event_msg_t msg, usb_irp_t *irp)
{
    adb_class_specific_ctrl_cb(irp);
    ready = true;
}

void usbh_get_config_desc_cb(uint8_t *data_buffer, size_t num_bytes, void *context)
{
    ESP_LOG_BUFFER_HEX_LEVEL("CONFIG descriptor", data_buffer, num_bytes, ESP_LOG_INFO);
    parse_cfg_descriptor(data_buffer, 0, num_bytes, &conf_num);

    xfer_get_current_config(port_hdl, ctrl_pipe_hdl);
    xfer_get_string(port_hdl, ctrl_pipe_hdl, 1);
    xfer_get_string(port_hdl, ctrl_pipe_hdl, 2);
    xfer_get_string(port_hdl, ctrl_pipe_hdl, 3);
}

void usbh_set_config_desc_cb(uint16_t data, void *context)
{
    ESP_LOGI("SET CONFIG", "%d", data);
    xfer_get_desc(port_hdl, ctrl_pipe_hdl);
}

void usbh_get_string_cb(uint8_t *data, size_t num_bytes, void *context)
{
    char out[64] = {};
    utf16_to_utf8((char *)data, out, num_bytes);
    ESP_LOGI("STRING CB", "[%d] %s", num_bytes, out);
    parse_cfg_descriptor(data, 0, num_bytes, &conf_num);
}

void usbh_ctrl_pipe_stalled_cb(usb_ctrl_req_t *ctrl)
{
    ESP_LOG_BUFFER_HEX_LEVEL("STALLED", ctrl, 8, ESP_LOG_WARN);
}

void usbh_ctrl_pipe_error_cb(usb_ctrl_req_t *ctrl)
{
    ESP_LOG_BUFFER_HEX_LEVEL("ERROR", ctrl, 8, ESP_LOG_WARN);
}
// extern static _Bool connected;
void usbh_get_configuration_cb(uint8_t addr, void *context)
{
    ESP_LOGI("GET CONFIG", "%d", addr);
    printf("write A_CNXN\n");
    writeStringMessage(A_CNXN, 0x01000000, 4096, (char*)"host::microbridge");
}

/*------------------------------- USBH EP0 CTRL PIPE CALLBACKS -------------------------------*/

/*------------------------------- USBH PORT CALLBACKS -------------------------------*/

void usbh_port_connection_cb(port_event_msg_t msg)
{
    hcd_port_state_t state;
    ESP_LOGI("", "HCD_PORT_EVENT_CONNECTION");
    if (HCD_PORT_STATE_DISABLED == hcd_port_get_state(msg.port_hdl))
        ESP_LOGI("", "HCD_PORT_STATE_DISABLED");
    if (ESP_OK == hcd_port_command(msg.port_hdl, HCD_PORT_CMD_RESET))
        ESP_LOGI("", "USB device reset");
    else
        return;
    if (HCD_PORT_STATE_ENABLED == hcd_port_get_state(msg.port_hdl))
    {
        ESP_LOGI("", "HCD_PORT_STATE_ENABLED");
        // we are already physically connected and ready, now we can perform software connection steps
        allocate_ctrl_pipe(msg.port_hdl, &ctrl_pipe_hdl);
        // get device descriptor on EP0, this is first mandatory step
        xfer_get_device_desc(msg.port_hdl, ctrl_pipe_hdl);
        port_hdl = msg.port_hdl;
        ESP_LOGW("", "port handle: %p, %p", _port_hdl, msg.port_hdl);
    }
}


static void adb_pipe_cb(pipe_event_msg_t msg, usb_irp_t *irp, void *context)
{
    ESP_LOGD("", "\t-> Pipe [%d] event: %d\n", (uint8_t)context, msg.pipe_event);

    switch (msg.pipe_event)
    {
        case HCD_PIPE_EVENT_NONE:
            break;

        case HCD_PIPE_EVENT_IRP_DONE:
            ESP_LOGD("Pipe cdc: ", "XFER status: %d, num bytes: %d, actual bytes: %d", irp->status, irp->num_bytes, irp->actual_num_bytes);
            // we are getting only IN data here
            if(irp->data_buffer[0] == 0x3f)
                xfer_out_data((uint8_t*)"test\n", 5);
            else
                ESP_LOGI("", "%.*s", irp->actual_num_bytes, irp->data_buffer);

            ready = true;
            break;

        case HCD_PIPE_EVENT_ERROR_XFER:
            ESP_LOGW("", "adb XFER error: %d", irp->status);
            hcd_pipe_command(msg.pipe_hdl, HCD_PIPE_CMD_RESET);
            break;
        
        case HCD_PIPE_EVENT_ERROR_STALL:
            ESP_LOGW("", "Device stalled: %s pipe, state: %d", "BULK", hcd_pipe_get_state(msg.pipe_hdl));
            hcd_pipe_command(msg.pipe_hdl, HCD_PIPE_CMD_RESET);
            break;
        
        default:
            ESP_LOGW("", "not handled pipe event: %d", msg.pipe_event);
            break;
    }
}


void usbh_port_disconnection_cb(port_event_msg_t msg) {}
void usbh_port_error_cb(port_event_msg_t msg) {}
void usbh_port_overcurrent_cb(port_event_msg_t msg) {}

void usbh_port_sudden_disconn_cb(port_event_msg_t msg)
{
    hcd_port_state_t state;
    if (ctrl_pipe_hdl != NULL && HCD_PIPE_STATE_INVALID == hcd_pipe_get_state(ctrl_pipe_hdl))
    {
        ESP_LOGW("", "pipe state: %d", hcd_pipe_get_state(ctrl_pipe_hdl));
        free_pipe_and_irp_list(ctrl_pipe_hdl);
        ctrl_pipe_hdl = NULL;
        ready = false;
        esp_err_t err;
        if (HCD_PORT_STATE_RECOVERY == (state = hcd_port_get_state(msg.port_hdl)))
        {
            if (ESP_OK != (err = hcd_port_recover(msg.port_hdl)))
                ESP_LOGE("recovery", "should be not powered state %d => (%d)", state, err);
        }
        else
        {
            ESP_LOGE("", "hcd_port_state_t: %d", state);
        }
        if (ESP_OK == hcd_port_command(msg.port_hdl, HCD_PORT_CMD_POWER_ON))
            ESP_LOGI("", "Port powered ON");
    }
}

/*------------------------------- USBH PORT CALLBACKS -------------------------------*/

void app_main(void)
{
    printf("Hello world USB host!\n");
    if (setup_usb_host())
    {
        xTaskCreate(ctrl_pipe_event_task, "pipe_task", 4 * 1024, NULL, 10, NULL);
        xTaskCreate(adb_pipe_event_task, "pipe_task", 4*1024, NULL, 10, NULL);
        register_adb_pipe_callback(adb_pipe_cb);
    }

    while (1)
    {
        vTaskDelay(200);
        printf("write A_CNXN\n");
        writeStringMessage(A_CNXN, 0x01000000, 4096, (char*)"host::microbridge");
        xfer_get_string(port_hdl, ctrl_pipe_hdl, 1);
        if(ready){
            ready = false;
            xfer_in_data();
        }
    }
}
