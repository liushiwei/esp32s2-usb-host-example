#include <stdio.h>
#include <string.h>
#include "common.h"
#include "adb_driver.h"
#include "hcd.h"


#define MAX_NUM_ENDP    2
#define EP1             0
#define EP2             1
#define EP3             2


hcd_pipe_handle_t adb_ep_pipe_hdl[MAX_NUM_ENDP];
uint8_t *adb_data_buffers[MAX_NUM_ENDP];
usb_irp_t *adb_ep_irps[MAX_NUM_ENDP];
usb_desc_ep_t endpoints[MAX_NUM_ENDP];
int bMaxPacketSize0;
extern hcd_pipe_handle_t ctrl_pipe_hdl;

static QueueHandle_t adb_pipe_evt_queue;
static ctrl_pipe_cb_t adb_pipe_cb;

static bool adb_pipe_callback(hcd_pipe_handle_t pipe_hdl, hcd_pipe_event_t pipe_event, void *user_arg, bool in_isr)
{
    QueueHandle_t pipe_evt_queue = (QueueHandle_t)user_arg;
    pipe_event_msg_t msg = {
        .pipe_hdl = pipe_hdl,
        .pipe_event = pipe_event,
    };
    if (in_isr) {
        BaseType_t xTaskWoken = pdFALSE;
        xQueueSendFromISR(pipe_evt_queue, &msg, &xTaskWoken);
        return (xTaskWoken == pdTRUE);
    } else {
        xQueueSend(pipe_evt_queue, &msg, portMAX_DELAY);
        return false;
    }
}

void adb_pipe_event_task(void* p)
{
    printf("start pipe event task\n");
    pipe_event_msg_t msg;
    adb_pipe_evt_queue = xQueueCreate(10, sizeof(pipe_event_msg_t));

    while(1){
        xQueueReceive(adb_pipe_evt_queue, &msg, portMAX_DELAY);
        usb_irp_t* irp = hcd_irp_dequeue(msg.pipe_hdl);
        hcd_pipe_handle_t pipe_hdl;
        if(irp == NULL) continue;
        void *context = irp->context;

        if (adb_pipe_cb != NULL)
        {
            adb_pipe_cb(msg, irp, context);
        }
    }
}

static void free_pipe_and_xfer_reqs(hcd_pipe_handle_t pipe_hdl,
                                    // hcd_xfer_req_handle_t *req_hdls,
                                    uint8_t **data_buffers,
                                    usb_irp_t **irps,
                                    int num_xfers)
{
    //Dequeue transfer requests
    do{
        usb_irp_t* irp = hcd_irp_dequeue(pipe_hdl);
        if(irp == NULL) break;
    }while(1);

    ESP_LOGD("", "Freeing transfer requets\n");
    //Free transfer requests (and their associated objects such as IRPs and data buffers)
    for (int i = 0; i < num_xfers; i++) {
        heap_caps_free(irps[i]);
        heap_caps_free(data_buffers[i]);
    }
    ESP_LOGD("", "Freeing pipe\n");
    //Delete the pipe
    if(ESP_OK != hcd_pipe_free(pipe_hdl)) {
        ESP_LOGE("", "err to free pipes");
    }
}

static void alloc_pipe_and_xfer_reqs_adb(hcd_port_handle_t port_hdl,
                                     QueueHandle_t pipe_evt_queue,
                                     hcd_pipe_handle_t *pipe_hdl,
                                     uint8_t **data_buffers,
                                     usb_irp_t **irps,
                                     int num_xfers,
                                     usb_desc_ep_t* ep)
{
    //We don't support hubs yet. Just get the speed of the port to determine the speed of the device
    usb_speed_t port_speed;
    if(ESP_OK == hcd_port_get_speed(port_hdl, &port_speed)){}

    hcd_pipe_config_t config = {
        .callback = adb_pipe_callback,
        .callback_arg = (void *)pipe_evt_queue,
        .context = NULL,
        .ep_desc = ep,
        .dev_addr = DEVICE_ADDR, // TODO
        .dev_speed = port_speed,
    };
    if(ESP_OK == hcd_pipe_alloc(port_hdl, &config, pipe_hdl)) {}
    if(NULL == pipe_hdl) {
        ESP_LOGE("", "NULL == pipe_hdl");
        return;
    }
    //Create transfer requests (and other required objects such as IRPs and data buffers)
    printf("Creating transfer requests\n");
    for (int i = 0; i < num_xfers; i++) {
        irps[i] = heap_caps_calloc(1, sizeof(usb_irp_t), MALLOC_CAP_DEFAULT);
        if(NULL == irps[i]) ESP_LOGE("", "err 6");
        //Allocate data buffers
        data_buffers = heap_caps_calloc(1, sizeof(usb_ctrl_req_t) + TRANSFER_DATA_MAX_BYTES, MALLOC_CAP_DMA);
        if(NULL == data_buffers) ESP_LOGE("", "err 5");
        //Initialize IRP and IRP list
        irps[i]->data_buffer = data_buffers;
        irps[i]->num_iso_packets = 0;
    }
}

void adb_create_pipe(usb_desc_ep_t* ep)
{
    if(adb_pipe_evt_queue == NULL)
        adb_pipe_evt_queue = xQueueCreate(10, sizeof(pipe_event_msg_t));
    if((USB_DESC_EP_GET_XFERTYPE(ep) == USB_XFER_TYPE_BULK) && USB_DESC_EP_GET_EP_DIR(ep)){
        memcpy(&endpoints[EP1], ep, sizeof(usb_desc_ep_t));
        alloc_pipe_and_xfer_reqs_adb(port_hdl, adb_pipe_evt_queue, &adb_ep_pipe_hdl[EP1], &adb_data_buffers[EP1], &adb_ep_irps[EP1], 1, ep);
    } else if((USB_DESC_EP_GET_XFERTYPE(ep) == USB_XFER_TYPE_BULK) && (!USB_DESC_EP_GET_EP_DIR(ep))){
        memcpy(&endpoints[EP2], ep, sizeof(usb_desc_ep_t));
        alloc_pipe_and_xfer_reqs_adb(port_hdl, adb_pipe_evt_queue, &adb_ep_pipe_hdl[EP2], &adb_data_buffers[EP2], &adb_ep_irps[EP2], 1, ep);
    }
}

void delete_pipes()
{
    for (size_t i = 0; i < MAX_NUM_ENDP; i++)
    {
        if(adb_ep_pipe_hdl[i] == NULL) continue;
        if (HCD_PIPE_STATE_INVALID == hcd_pipe_get_state(adb_ep_pipe_hdl[i]))
        {                
            ESP_LOGD("", "pipe state: %d", hcd_pipe_get_state(adb_ep_pipe_hdl[i]));
            free_pipe_and_xfer_reqs( adb_ep_pipe_hdl[i], &adb_data_buffers[i], &adb_ep_irps[i], 1);
            adb_ep_pipe_hdl[i] = NULL;
        }
    }
}



// ENDPOINTS
void xfer_intr_data()
{
    adb_ep_irps[EP1]->num_bytes = 8;    //1 worst case MPS
    adb_ep_irps[EP1]->data_buffer = adb_data_buffers[EP1];
    adb_ep_irps[EP1]->num_iso_packets = 0;
    adb_ep_irps[EP1]->num_bytes = 8;

    esp_err_t err;
    if(ESP_OK == (err = hcd_irp_enqueue(adb_ep_pipe_hdl[EP1], adb_ep_irps[EP1]))) {
        ESP_LOGI("", "INT ");
    } else {
        ESP_LOGE("", "INT err: 0x%02x", err);
    }
}

void xfer_in_data()
{
    ESP_LOGD("", "EP: 0x%02x", USB_DESC_EP_GET_ADDRESS(&endpoints[EP1]));
    adb_ep_irps[EP1]->num_bytes = bMaxPacketSize0;    //1 worst case MPS
    adb_ep_irps[EP1]->num_iso_packets = 0;
    adb_ep_irps[EP1]->num_bytes = 64;

    esp_err_t err;
    if(ESP_OK != (err = hcd_irp_enqueue(adb_ep_pipe_hdl[EP1], adb_ep_irps[EP1]))) {
        ESP_LOGW("", "BULK %s, dir: %d, err: 0x%x", "IN", USB_DESC_EP_GET_EP_DIR(&endpoints[EP1]), err);
    }
}

void xfer_out_data(uint8_t* data, size_t len)
{
    ESP_LOGD("", "EP: 0x%02x", USB_DESC_EP_GET_ADDRESS(&endpoints[EP2]));
    memcpy(adb_ep_irps[EP2]->data_buffer, data, len);
    adb_ep_irps[EP2]->num_bytes = bMaxPacketSize0;    //1 worst case MPS
    adb_ep_irps[EP2]->num_iso_packets = 0;
    adb_ep_irps[EP2]->num_bytes = len;

    esp_err_t err;
    if(ESP_OK != (err = hcd_irp_enqueue(adb_ep_pipe_hdl[EP2], adb_ep_irps[EP2]))) {
        ESP_LOGW("", "BULK %s, dir: %d, err: 0x%x", "OUT", USB_DESC_EP_GET_EP_DIR(&endpoints[EP2]), err);
    }
}

void adb_class_specific_ctrl_cb(usb_irp_t* irp)
{
    
}

void register_adb_pipe_callback(ctrl_pipe_cb_t cb)
{
    adb_pipe_cb = cb;
}
