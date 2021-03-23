

#pragma once
#include "pipes.h"

#define USB_DESC_EP_GET_ADDRESS(desc_ptr) ((desc_ptr)->bEndpointAddress & 0x7F)

#define SET_VALUE       0x21
#define GET_VALUE       0xA1

// DTR/RTS control in SET_CONTROL_LINE_STATE
#define ENABLE_DTR(val)      (val<<0)
#define ENABLE_RTS(val)      (val<<1)

#define SET_LINE_CODING 0x20
#define GET_LINE_CODING 0x21
#define SET_CONTROL_LINE_STATE 0x22
#define SERIAL_STATE    0x20

typedef union {
    struct {
        uint8_t bRequestType;
        uint8_t bRequest;
        uint16_t wValue;
        uint16_t wIndex;
        uint16_t wLength;
        uint32_t dwDTERate;
        uint8_t bCharFormat;
        uint8_t bParityType;
        uint8_t bDataBits;
    } USB_CTRL_REQ_ATTR;
    uint8_t val[USB_CTRL_REQ_SIZE + 7];
} cdc_ctrl_line_t;

/**
 * @brief Initializer for a SET_ADDRESS request
 *
 * Sets the address of a connected device
 */
#define USB_CTRL_REQ_CDC_SET_LINE_CODING(ctrl_req_ptr, index, bitrate, cf, parity, bits) ({  \
    (ctrl_req_ptr)->bRequestType = SET_VALUE;   \
    (ctrl_req_ptr)->bRequest = SET_LINE_CODING;  \
    (ctrl_req_ptr)->wValue = 0;   \
    (ctrl_req_ptr)->wIndex = (index);    \
    (ctrl_req_ptr)->wLength = (7);   \
    (ctrl_req_ptr)->dwDTERate = (bitrate);   \
    (ctrl_req_ptr)->bCharFormat = (cf);   \
    (ctrl_req_ptr)->bParityType = (parity);   \
    (ctrl_req_ptr)->bDataBits = (bits);   \
})

#define USB_CTRL_REQ_CDC_GET_LINE_CODING(ctrl_req_ptr, index) ({  \
    (ctrl_req_ptr)->bRequestType = GET_VALUE;   \
    (ctrl_req_ptr)->bRequest = GET_LINE_CODING;  \
    (ctrl_req_ptr)->wValue = 0;   \
    (ctrl_req_ptr)->wIndex = (index);    \
    (ctrl_req_ptr)->wLength = (7);   \
})

#define USB_CTRL_REQ_CDC_SET_CONTROL_LINE_STATE(ctrl_req_ptr, index, dtr, rts) ({  \
    (ctrl_req_ptr)->bRequestType = SET_VALUE;   \
    (ctrl_req_ptr)->bRequest = SET_CONTROL_LINE_STATE;  \
    (ctrl_req_ptr)->wValue = ENABLE_DTR(dtr) | ENABLE_RTS(rts);   \
    (ctrl_req_ptr)->wIndex = (index);    \
    (ctrl_req_ptr)->wLength = (0);   \
})

void xfer_set_line_coding(uint32_t);
void xfer_set_control_line(bool dtr, bool rts);
void xfer_get_line_coding();
void xfer_intr_data();
void xfer_in_data();
void xfer_out_data();
void delete_pipes();
