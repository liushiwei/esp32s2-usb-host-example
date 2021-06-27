#pragma once
#include "hcd.h"
#include "ctrl_pipe.h"
#include "usb_host_port.h"

// #define XFER_DATA_MAX_LEN       1023     //Just assume that will only IN/OUT 1023 bytes for now, which is max length for full speed
#define DEVICE_ADDR             1

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


#define MAX_PAYLOAD 4096;

#define A_SYNC 0x434e5953
#define A_CNXN 0x4e584e43
#define A_OPEN 0x4e45504f
#define A_OKAY 0x59414b4f
#define A_CLSE 0x45534c43
#define A_WRTE 0x45545257

// https://code.google.com/p/microbridge/issues/detail?id=21
#define A_AUTH 0x48545541

#define ADB_CLASS 0xff
#define ADB_SUBCLASS 0x42
#define ADB_PROTOCOL 0x1

#define ADB_USB_PACKETSIZE 0x40
#define ADB_CONNECTION_RETRY_TIME 1000

// https://code.google.com/p/microbridge/issues/detail?id=21
/* AUTH packets first argument */
/* Request */
#define ADB_AUTH_TOKEN         1
/* Response */
#define ADB_AUTH_SIGNATURE     2
#define ADB_AUTH_RSAPUBLICKEY  3


typedef struct
{
	uint8_t address;
	uint8_t configuration;
	uint8_t interface;
	uint8_t inputEndPointAddress;
	uint8_t outputEndPointAddress;
} adb_usbConfiguration;

typedef struct
{
	// Command identifier constant
	uint32_t command;

	// First argument
	uint32_t arg0;

	// Second argument
	uint32_t arg1;

	// Payload length (0 is allowed)
	uint32_t data_length;

	// Checksum of data payload
	uint32_t data_check;

	// Command ^ 0xffffffff
	uint32_t magic;

} adb_message;

// https://code.google.com/p/microbridge/issues/detail?id=21
typedef struct
{
	uint16_t a;
	uint16_t b;
} rsa_key;

typedef enum
{
	ADB_UNUSED = 0,
	ADB_CLOSED,
	ADB_OPEN,
	ADB_OPENING,
	ADB_RECEIVING,
	ADB_WRITING
} ConnectionStatus;

typedef enum
{
	ADB_CONNECT = 0,
	ADB_DISCONNECT,
	ADB_CONNECTION_OPEN,
	ADB_CONNECTION_CLOSE,
	ADB_CONNECTION_FAILED,
	ADB_CONNECTION_RECEIVE
} adb_eventType;


// void xfer_set_line_coding(uint32_t bitrate, uint8_t cf, uint8_t parity, uint8_t bits);
// void xfer_set_control_line(bool dtr, bool rts);
// void xfer_get_line_coding();
// void xfer_intr_data();
void xfer_in_data();
void xfer_out_data(uint8_t* data, size_t len);
void delete_pipes();
void adb_create_pipe(usb_desc_ep_t* ep);
void register_adb_pipe_callback(ctrl_pipe_cb_t cb);
void adb_class_specific_ctrl_cb(usb_irp_t* irp);
