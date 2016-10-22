#include "kiss.h"

#include <cfg/compiler.h>
#include <algo/rand.h>

#define LOG_LEVEL  KISS_LOG_LEVEL
#define LOG_FORMAT KISS_LOG_FORMAT
#include <cfg/log.h>

#include <drv/ser.h>

#include "global.h"
#include "settings.h"
#include "utils.h"

#include <io/kfile.h>
#include <net/afsk.h>
#include <net/ax25.h>
#include <drv/ser.h>
#include "reader.h"


#include <algo/crc_ccitt.h>


#define KISS_FEND  0xc0
#define KISS_FESC  0xdb
#define KISS_TFEND 0xdc
#define KISS_TFESC 0xdd

enum {
	KISS_CMD_DATA = 0,
	KISS_CMD_TXDELAY,
	KISS_CMD_P,
	KISS_CMD_SlotTime,
	KISS_CMD_TXtail,
	KISS_CMD_FullDuplex,
	KISS_CMD_SetHardware,
	KISS_CMD_CONFIG_CALL = 0x0C,
	KISS_CMD_CONFIG_PARAMS = 0x0D,
	KISS_CMD_CONFIG_TEXT = 0x0E,
	KISS_CMD_CONFIG_COMMIT = 0x0F,
	KISS_CMD_Return = 0xFF
};

enum {
	KISS_QUEUE_IDLE = 0,
	KISS_QUEUE_DELAYED,
};

static KissCtx kiss;

static bool verify_config_data(uint8_t *frame,uint16_t size);
static void kiss_handle_frame(uint8_t *frame, uint16_t size);
static void kiss_handle_config_params_cmd(uint8_t *frame, uint16_t size);
static void kiss_handle_config_text_cmd(uint8_t *frame, uint16_t size);
static void kiss_handle_config_call_cmd(uint8_t *frame, uint16_t size);
static void kiss_handle_config_commit_cmd(uint8_t *frame, uint16_t size);

void kiss_init(struct SerialReader *serialReader,struct AX25Ctx *modem){
	memset(&kiss,0,sizeof(KissCtx));
	kiss.serialReader = serialReader;
	kiss.modem = modem;

	//kiss.serial = serialReader->ser;
	//NOTE - Atmega328P has limited 2048 RAM, so here we have to use shared read buffer to save memory
	//kiss.rxBuf = serialReader->buf;			// Shared buffer in SerialReader
	//kiss.rxBufLen = serialReader->bufLen; 	// buffer length, should be >= CONFIG_AX25_FRAME_BUF_LEN
}

static void kiss_poll_serial(void){
	SerialReader *reader = kiss.serialReader;

	int c = ser_getchar(reader->ser); // Make sure CONFIG_SERIAL_RXTIMEOUT = 0
	if (c == EOF) {
		return;
	}

	static bool escaped = false;
	// sanity checks
	// no serial input in last 2 secs?
	if ((reader->readLen != 0)
			&& (timer_clock() - kiss.rxTick > ms_to_ticks(2000L))) {
		LOG_INFO("Serial - Timeout\n");
		reader->readLen = 0;
	}

	// about to overflow buffer? reset
	if (reader->readLen >= (reader->bufLen - 2)) {
		LOG_INFO("Serial - Packet too long %d >= %d\n", reader->readLen,
				reader->bufLen - 2);
		reader->readLen = 0;
	}

	if (c == KISS_FEND) {
		if ((!escaped) && (reader->readLen > 0)) {
			kiss_handle_frame(reader->buf, reader->readLen);
		}
		reader->readLen = 0;
		escaped = false;
		return;
	} else if (c == KISS_FESC) {
		escaped = true;
		return;
	} else if (c == KISS_TFESC) {
		if (escaped) {
			escaped = false;
			c = KISS_FESC;
		}
	} else if (c == KISS_TFEND) {
		if (escaped) {
			escaped = false;
			c = KISS_FEND;
		}
	} else if (escaped) {
		escaped = false;
	}

	reader->buf[reader->readLen++] = c & 0xff;
	kiss.rxTick = timer_clock();
}

void kiss_poll() {
	kiss_poll_serial();
}

static void kiss_handle_frame(uint8_t *frame, uint16_t size) {
	if (size == 0)
		return;

	// Check return command
	if (size == 1 && frame[0] == KISS_CMD_Return) {
		//LOG_INFO("Kiss - exiting");
		return;
	}

	if (size < 2) {
		LOG_INFO("Kiss - discarding packet - too short\n");
		return;
	}

	// the first byte of KISS message is for command and port
	uint8_t cmd = frame[0] & 0x0f;
	uint8_t port = frame[0] >> 4 & 0x0f;
	uint8_t *payload = frame + 1;

	if (port > 0) {
		//WARN: ignore the port id ?
		return;
	}

	switch (cmd) {
	case KISS_CMD_DATA:
		//LOG_INFO("Kiss - handle frame message\n");
		kiss_send_to_modem(payload, size - 1);
		break;

	case KISS_CMD_CONFIG_PARAMS:
		if(verify_config_data(payload,size -1)){
			kiss_handle_config_params_cmd(payload, size - 2);
		}
		break;

	case KISS_CMD_CONFIG_TEXT:
		if(verify_config_data(payload,size -1)){
			kiss_handle_config_text_cmd(payload, size - 2);
		}
		break;

	case KISS_CMD_CONFIG_CALL:
		if(verify_config_data(payload,size - 1)){
			kiss_handle_config_call_cmd(payload, size - 2);
		}
		break;

	case KISS_CMD_CONFIG_COMMIT:
		if(verify_config_data(payload,size - 1)){
			kiss_handle_config_commit_cmd(payload, size - 2);
		}
		break;
		/*
		 case KISS_CMD_TXDELAY:{
		 //LOG_INFO("Kiss - setting txdelay %d\n", k->buf[1]);
		 if(kiss.rxBuf[1] > 0){
		 kiss_txdelay = kiss.rxBuf[1];
		 }
		 break;
		 }
		 case KISS_CMD_P:{
		 //LOG_INFO("Kiss - setting persistence %d\n", k->buf[1]);
		 if(kiss.rxBuf[1] > 0){
		 kiss_persistence = kiss.rxBuf[1];
		 }
		 break;
		 }
		 case KISS_CMD_SlotTime:{
		 //LOG_INFO("Kiss - setting slot_time %d\n", k->buf[1]);
		 if(kiss.rxBuf[1] > 0){
		 kiss_slot_time = kiss.rxBuf[1];
		 }
		 break;
		 }
		 case KISS_CMD_TXtail:{
		 LOG_INFO("Kiss - setting txtail %d\n", k->buf[1]);
		 if(k->buf[1] > 0){
		 kiss_txtail = k->buf[1];
		 }
		 break;
		 }
		 case KISS_CMD_FullDuplex:{
		 LOG_INFO("Kiss - setting duplex %d\n", k->buf[1]);
		 kiss_duplex = k->buf[1];
		 break;
		 }
		 */

	default:
		// Unsupported command
		break;
	}			// end of switch(cmd)
}

/*
 * send to modem/rf
 */
void kiss_send_to_modem(/*channel = 0*/uint8_t *buf, size_t len) {
	bool sent = false;
	Afsk *afsk = AFSK_CAST(kiss.modem->ch);

	if (g_settings.rf.duplex == RF_DUPLEX_FULL) {
		ax25_sendRaw(kiss.modem, buf, len);
		return;
	}

	// Perform CSMA check under HALF_DUPLEX mode,
	// FIXME - blocking send currently.
	while (!sent) {
		if (!/*ctx->dcd*/(afsk)->hdlc.rxstart) {
			uint16_t i = rand();
			uint8_t tp = ((i >> 8) ^ (i & 0xff));
			if (tp < g_settings.rf.persistence) {
				ax25_sendRaw(kiss.modem, buf, len);
				sent = true;
			} else {
				//TEST ONLY -
#if 0
				kfile_printf_P(kiss.serial,PSTR("send backoff 100ms, because %d > persistence \n"),tp);
#endif
				timer_delay(g_settings.rf.slot_time * 10); // block waiting 100ms by default.
			}
		} else {
			while (!sent && /*kiss_ax25->dcd*/(afsk)->hdlc.rxstart) {
				// Continously poll the modem for data
				// while waiting, so we don't overrun
				// receive buffers
				ax25_poll(kiss.modem);
				if ((afsk)->status != 0) {
					// If an overflow or other error
					// occurs, we'll back off and drop
					// this packet silently.
					(afsk)->status = 0;
					sent = true;
				}
			}
		}
	}
}

#if 0
void kiss_send_to_serial(uint8_t port, uint8_t cmd, uint8_t *buf, size_t len) {
	size_t i;

	kfile_putc(KISS_FEND, kiss.serial);
	kfile_putc(((port << 4) & 0xf0) | (cmd & 0x0f), kiss.serial);

	for (i = 0; i < len; i++) {

		uint8_t c = buf[i];

		if (c == KISS_FEND) {
			kfile_putc(KISS_FESC, kiss.serial);
			kfile_putc(KISS_TFEND, kiss.serial);
			continue;
		}

		kfile_putc(c, kiss.serial);

		if (c == KISS_FESC) {
			kfile_putc(KISS_TFESC, kiss.serial);
		}
	}

	kfile_putc(KISS_FEND, kiss.serial);
}
#else
void kiss_send_to_serial(uint8_t port, uint8_t cmd, uint8_t *buf, size_t len) {
	size_t i;
	Serial *serial = kiss.serialReader->ser;
	ser_putchar(KISS_FEND, serial);
	ser_putchar(((port << 4) & 0xf0) | (cmd & 0x0f), serial);

	for (i = 0; i < len; i++) {
		uint8_t c = buf[i];
		if (c == KISS_FEND) {
			ser_putchar(KISS_FESC, serial);
			ser_putchar(KISS_TFEND, serial);
			continue;
		}
		ser_putchar(c, serial);
		if (c == KISS_FESC) {
			ser_putchar(KISS_TFESC, serial);
		}
	}
	ser_putchar(KISS_FEND, serial);
}

#endif


/*
 * check the last byte of the frame as verify checksum
 *
 * config data is encapsulated in the kiss frame with following format
 *
 * DATA (4096) | SUM
 * where:
 *   DATA(4096) = config data, (byte order is CPU specific, AVR/X86 is little-endian, BRCM63xx is big-endian)
 *   SUM(1)  = ~(sum of each data bytes)
 *
 * KISS request: C0 0D FF C0
 *
 */
static bool verify_config_data(uint8_t *frame,uint16_t size){
	if(size == 0) {
		// at least 1 bytes, Check sum 0xFF
		return false;
	}
	uint16_t len = size -1; // excluding the last byte of checksum
	return frame[len]/*sum*/ == calc_crc(frame,len);
}

INLINE void kiss_handle_config_params_cmd(uint8_t *data, uint16_t len) {
	if(len == 0){
		//read g_settings and write to serial
		kiss_send_to_serial(0,KISS_CMD_CONFIG_PARAMS,(uint8_t*)&g_settings,sizeof(SettingsData));
	}else if(len == sizeof(SettingsData)){
		// set g_settings
		settings_set_params_bytes(data,len);
	}
}

INLINE void kiss_handle_config_text_cmd(uint8_t *data, uint16_t len) {
	if(len == 0){
		// read beacon text and write to serial
		// NOTE: *TRICK* here: reuse the serial reader buffer, so assuming kiss.serialReader->readLen == 0
		kiss.serialReader->readLen = 0; // force discard the received data in buffer as we'll use the buffer now.
		uint8_t len = settings_get_beacon_text((char*)kiss.serialReader->buf,kiss.serialReader->bufLen);
		if(len > 0){
			kiss_send_to_serial(0,KISS_CMD_CONFIG_TEXT,kiss.serialReader->buf,kiss.serialReader->bufLen);
		}
	}else{
		settings_set_beacon_text((char*)data,len);
	}
}

INLINE void kiss_handle_config_call_cmd(uint8_t *data, uint16_t len) {
	if(len == 0){
		//read g_settings and write to serial
		AX25Call calls[4];
		settings_get_call(SETTINGS_MY_CALL,&calls[0]);
		settings_get_call(SETTINGS_DEST_CALL,&calls[1]);
		settings_get_call(SETTINGS_PATH1_CALL,&calls[2]);
		settings_get_call(SETTINGS_PATH2_CALL,&calls[3]);
		kiss_send_to_serial(0,KISS_CMD_CONFIG_CALL,(uint8_t*)calls,sizeof(calls));
	}else if(len == 7 * 4){
		AX25Call call;
		// set the call
		memcpy(&call,data,7);
		settings_set_call(SETTINGS_MY_CALL,&call);
		memcpy(&call,data+7,7);
		settings_set_call(SETTINGS_DEST_CALL,&call);
		memcpy(&call,data+14,7);
		settings_set_call(SETTINGS_PATH1_CALL,&call);
		memcpy(&call,data+21,7);
		settings_set_call(SETTINGS_PATH2_CALL,&call);
	}
}

INLINE void kiss_handle_config_commit_cmd(uint8_t *data, uint16_t len) {
	(void)data;
	(void)len;
	settings_save();
}
