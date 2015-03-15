/**
 * \file
 * <!--
 * This file is part of BeRTOS.
 *
 * Bertos is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As a special exception, you may use this file as part of a free software
 * library without restriction.  Specifically, if other files instantiate
 * templates or use macros or inline functions from this file, or you compile
 * this file and link it with other files to produce an executable, this
 * file does not by itself cause the resulting executable to be covered by
 * the GNU General Public License.  This exception does not however
 * invalidate any other reasons why the executable file might be covered by
 * the GNU General Public License.
 *
 * Copyright 2010 Develer S.r.l. (http://www.develer.com/)
 *
 * -->
 *
 * \author Francesco Sacchi <batt@develer.com>
 * \author Luca Ottaviano <lottaviano@develer.com>
 * \author Daniele Basile <asterix@develer.com>
 *
 * \brief Arduino APRS radio demo.
 *
 * This example shows how to read and decode APRS radio packets.
 * It uses the following modules:
 * afsk
 * ax25
 * ser
 *
 * You will see how to use a serial port to output messages, init the afsk demodulator and
 * how to parse input messages using ax25 module.
 */

#include <cfg/compiler.h>

#include <cpu/irq.h>

#include <net/afsk.h>
#include <net/ax25.h>
#include <net/kiss.h>

#include <drv/ser.h>
#include <drv/timer.h>

#include <stdio.h>
#include <string.h>


#include "console.h"

#include "config.h"
#if SERIAL_DEBUG
#include <cfg/debug.h>
#endif

#include "settings.h"
#include <ctype.h>

#include <cpu/pgm.h>     /* PROGMEM */
#include <avr/pgmspace.h>

#include "sys_utils.h"

#include "beacon.h"

// TEST for SoftSerial
#include "hw_soft_ser.h"


static Afsk afsk;
static AX25Ctx ax25;
static Serial ser;

#define ADC_CH 0
#define SER_BAUD_RATE_9600 9600L
#define SER_BAUD_RATE_115200 115200L

typedef enum{
	MODE_CFG  = 0,
	MODE_KISS = 1,
	MODE_DIGI = 2,
	MODE_TRACKER_BEACON = 3,
	MODE_TEST_BEACON = 0xf
}RunMode;


static RunMode currentMode = MODE_CFG;

///////////////////////////////////////////////////////////////////////////////////
// Message Callbacks
static void print_call_P(KFile *ch, const AX25Call *call)
{
	char buf[16];
	sprintf_P(buf,PSTR("%.6s"),call->call);
	kfile_print(ch, buf);
	if (call->ssid){
		sprintf_P(buf,PSTR("-%d"),call->ssid);
		kfile_print(ch, buf);
	}
}

INLINE void print_ax25_message(Serial *pSer, AX25Msg *msg){
	#if 0
		ax25_print(&(pSer->fd),msg); // less code but need 16 bytes of ram
	#else
		KFile *ch = &(pSer->fd);
		print_call_P(ch, &msg->src);
		kfile_putc('>', ch);
		print_call_P(ch, &msg->dst);

		#if CONFIG_AX25_RPT_LST
		for (int i = 0; i < msg->rpt_cnt; i++)
		{
			kfile_putc(',', ch);
			print_call_P(ch, &msg->rpt_lst[i]);
			/* Print a '*' if packet has already been transmitted
			 * by this repeater */
			if (AX25_REPEATED(msg, i))
				kfile_putc('*', ch);
		}
		#endif
		// DATA PART
		SERIAL_PRINTF_P(pSer, PSTR(":%.*s\n"), msg->len, msg->info);
	#endif
}

/*
 * Print on console the message that we have received.
 */
static void ax25_msg_callback(struct AX25Msg *msg){
	switch(currentMode){
	case MODE_CFG:{
		// Print received message to serial
		print_ax25_message(&ser,msg);
		break;
	}
	case MODE_KISS:
		kiss_send_host(0x00/*kiss port id*/,ax25.buf,ax25.frm_len - 2);
		break;

	default:
		break;

	}
}

static void kiss_mode_exit_callback(void){
	currentMode = MODE_CFG;
	SERIAL_PRINT_P((&ser),PSTR("Exit KISS mode\r\n"));
}

///////////////////////////////////////////////////////////////////////////////////
// Command handlers
/*
 * AT+MODE=[0|1|2]
 */
static bool cmd_switch_mode(Serial* pSer, char* value, size_t len){
	bool modeOK = false;
	if(len > 0 ){
		int i = atoi(value);
		if(i == (int)currentMode){
			// already in this mode, bail out.
			return true;
		}

		modeOK = true;
		switch(i){
		case MODE_CFG:
			// COMMAND/CONFIG MODE
			currentMode = MODE_CFG;
			ax25.pass_through = 0;		// parse ax25 frames
			beacon_set_enabled(false); 	// beacon off
			ser_purge(pSer);  			// clear all rx/tx buffer
			break;

		case MODE_KISS:
			// KISS MODE
			currentMode = MODE_KISS;
			ax25.pass_through = 1;		// don't parse ax25 frames
			beacon_set_enabled(false); 	// beacon off
			ser_purge(pSer);  			// clear serial rx/tx buffer
			SERIAL_PRINT_P(pSer,PSTR("Enter KISS mode\r\n"));
			break;
/*
		case MODE_DIGI:
			// DIGI MODE
			runMode = MODE_DIGI;
			ax25.pass_through = 0;		// parse ax25 frames
			kiss_set_enabled(false);	// kiss off
			beacon_set_enabled(true);	// beacon on
			ser_purge(pSer);  			// clear serial rx/tx buffer
			break;

		case MODE_TRACKER_BEACON:
			// TRACKER MODE
			runMode = MODE_TRAC;
			// disable the ax25 module
			ax25.pass_through = 0;		// parse ax25 frames
			kiss_set_enabled(false);	// kiss off
			tracker_set_enabled(true);	// gps on
			break;
*/
		default:
			// unknown mode
			modeOK = false;
			break;
		}
	}


	if(!modeOK){
		SERIAL_PRINTF_P(pSer,PSTR("Invalid value %s, only int value [0|1|2] is accepted\r\n"),value);
	}

	return true;
}



/*
 * AT+KISS=1 - enable the KISS mode
 */
static bool cmd_enter_kiss_mode(Serial* pSer, char* value, size_t len){
	if(len > 0 && value[0] == '1'){
		currentMode = MODE_KISS;
		ax25.pass_through = 1;
		ser_purge(pSer);  // clear all rx/tx buffer

		// disable beacon mode
		beacon_set_enabled(false);
		SERIAL_PRINT_P(pSer,PSTR("Enter KISS mode\r\n"));
	}else{
		SERIAL_PRINTF_P(pSer,PSTR("Invalid value %s, only value 1 is accepted\r\n"),value);
	}
	return true;
}

static SoftSerial softSer;

static void init(void)
{

    IRQ_ENABLE;

	kdbg_init();
	timer_init();

	/* Initialize serial port, we are going to use it to show APRS messages*/
	ser_init(&ser, SER_UART0);
	ser_setbaudrate(&ser, SER_BAUD_RATE_9600);
    // For some reason BertOS sets the serial
    // to 7 bit characters by default. We set
    // it to 8 instead.
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);

	/*
	 * Init afsk demodulator. We need to implement the macros defined in hw_afsk.h, which
	 * is the hardware abstraction layer.
	 * We do not need transmission for now, so we set transmission DAC channel to 0.
	 */
	afsk_init(&afsk, ADC_CH, 0);

	/*
	 * Here we initialize AX25 context, the channel (KFile) we are going to read messages
	 * from and the callback that will be called on incoming messages.
	 */
	ax25_init(&ax25, &afsk.fd, ax25_msg_callback);
	ax25.pass_through = false;

	// Initialize the kiss module
	kiss_init(&ser,&ax25,&afsk,kiss_mode_exit_callback);

	//TODO refactoring me
#if CONFIG_BEACON_ENABLED
    beacon_init(&ax25);
#endif

    // Load settings first
    settings_load();

    //////////////////////////////////////////////////////////////
    // Initialize the console & commands
    console_init(&ser);
    console_add_command(PSTR("MODE"),cmd_switch_mode);			// setup tnc run mode
    console_add_command(PSTR("KISS"),cmd_enter_kiss_mode);		// enable KISS mode

    hw_soft_ser_init(&softSer, 10,11);
    hw_soft_ser_start(&softSer,9600);
}

// Free ram test
INLINE uint16_t freeRam (void) {
  extern int __heap_start, *__brkval;
  uint8_t v;
  uint16_t vaddr = (uint16_t)(&v);
  return (uint16_t) (vaddr - (__brkval == 0 ? (uint16_t) &__heap_start : (uint16_t) __brkval));
}

int main(void)
{

	init();

	while (1)
	{
		/*
		 * This function will look for new messages from the AFSK channel.
		 * It will call the message_callback() function when a new message is received.
		 * If there's nothing to do, this function will call cpu_relax()
		 */
		ax25_poll(&ax25);

		switch(currentMode){

		case MODE_CFG:{
			console_poll();
			break;
		}

		case MODE_KISS:{
			kiss_serial_poll();
			kiss_queue_process();
			break;
		}

		case MODE_DIGI:{
			break;
		}

		case MODE_TEST_BEACON:{
			break;
		}

		case MODE_TRACKER_BEACON:{
			break;
		}

		default:
			break;
		}// end of switch(runMode)

// BEACON ROUTINS
#if CONFIG_BEACON_ENABLED
		beacon_poll();
#endif

#define FREE_RAM_DEBUG 1
#if FREE_RAM_DEBUG
		{
			static uint32_t i = 0;
			if(i++ == 100000){
				i = 0;
				// log the stack size
				uint16_t ram = freeRam();
				SERIAL_PRINTF((&ser),"%u\r\n",ram);
			}
		}
#endif

		// Dump the isr changes
		{
			static uint32_t i = 0;
			//static uint32_t j = 0;
			if(i++ == 30000){
				i = 0;

//				// log the stack size
				char c;
				while(hw_soft_ser_available(&softSer)){
					c = hw_soft_ser_read(&softSer);
					kfile_putc(c,&(ser.fd));
//					j++;
				}
//				c = 'B';
				hw_soft_ser_write(&softSer,'0');
				hw_soft_ser_write(&softSer,'K');
				hw_soft_ser_write(&softSer,'\n');
				hw_soft_ser_write(&softSer,'\r');
				/*
				c = 'K';
				hw_soft_ser_write(&softSer,c);
//				hw_soft_ser_write(&softSer,'\r');
				c = '\n';
				hw_soft_ser_write(&softSer,c);
				*/
//				if(hw_soft_ser_available(&softSer)){
//					int8_t c = hw_soft_ser_read(&softSer);
//					SERIAL_PRINTF_P((&ser),PSTR("softser read 0x%x! \r\n"),c);
//					//j = 0;
//				}
//				uint16_t avail = hw_soft_ser_available();
//				if(avail){
//
//					SERIAL_PRINTF((&ser),"changed! \r\n");
//
//					if(++j == 10){
//						SERIAL_PRINTF((&ser),"stopped! \r\n");
//						hw_soft_ser_stop(&softSer);
//					}
//				}
//				else{
//					SERIAL_PRINTF((&ser),"not changed \r\n");
//				}

			}
		}

	} // end of while(1)
	return 0;
}
