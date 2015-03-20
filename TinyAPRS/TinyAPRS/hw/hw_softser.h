/*
 * \file hw_soft_ser.h
 * <!--
 * This file is part of TinyAPRS.
 * Released under GPL License
 *
 * Copyright 2015 Shawn Chain (shawn.chain@gmail.com)
 *
 * -->
 *
 * \brief 
 *
 * \author shawn
 * \date 2015-3-13
 */

#ifndef HW_SOFT_SER_H_
#define HW_SOFT_SER_H_

#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>

#define _SS_MAX_RX_BUFF 64 // RX buffer size
#ifndef GCC_VERSION
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif

typedef struct {
	uint8_t _receivePin;
	uint8_t _receiveBitMask;
	volatile uint8_t *_receivePortRegister;
	uint8_t _transmitBitMask;
	volatile uint8_t *_transmitPortRegister;

	uint16_t _rx_delay_centering;
	uint16_t _rx_delay_intrabit;
	uint16_t _rx_delay_stopbit;
	uint16_t _tx_delay;

	bool _buffer_overflow;bool _inverse_logic;
} SoftSerial;

void hw_soft_ser_init(SoftSerial *pSSer, uint8_t pinRX, uint8_t pinTX);

void hw_soft_ser_start(SoftSerial *pSSer, long speed);
void hw_soft_ser_stop(SoftSerial *pSSer);
int hw_soft_ser_available(SoftSerial *pSSers);
int hw_soft_ser_read(SoftSerial *pSSers);
int hw_soft_ser_write(SoftSerial *pSSer, uint8_t b);
#endif /* HW_SOFT_SER_H_ */
