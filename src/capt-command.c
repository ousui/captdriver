/*
 * Copyright (C) 2013 Alexey Galakhov <agalakhov@gmail.com>
 * Copyright (C) 2020, 2022 Moses Chong
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "capt-command.h"
#include "printer-usb.h"

#include "std.h"
#include "word.h"

#include <stdio.h>
#include <stdlib.h>

#include <cups/cups.h>
#include <cups/sidechannel.h>

static uint8_t capt_iobuf[0x10000];
static unsigned char lusb_iobuf[0x10240];
static size_t  capt_iosize;
static cups_sc_status_t last_send_status = CUPS_SC_STATUS_NONE;
static bool sendrecv_started = false;

static void capt_debug_buf(const char *level, size_t size)
{
	size_t i;
	if (size > capt_iosize)
		size = capt_iosize;
	for (i = 0; i < size; ++i) {
		if (i != 0 && (i % 16) == 0)
			fprintf(stderr, "\n%s: CAPT:", level);
		fprintf(stderr, " %02X", capt_iobuf[i]);
	}
	if (size < capt_iosize)
		fprintf(stderr, "... (%u more)", (unsigned) (capt_iosize - size));
	fprintf(stderr, "\n");
}

static void capt_send_buf(void)
{
	const uint8_t *iopos = capt_iobuf;
	size_t iosize = capt_iosize;
	int n_sent;
	int code_r;

	if (debug) {
		fprintf(stderr, "DEBUG: CAPT: send ");
		capt_debug_buf("DEBUG", 128);
	}

	while (iosize) {
		uint8_t ep_out = 0x01; /* out of host into device */
		size_t sendsize = iosize;
		if (sendsize > 4096)
			sendsize = 4096;
		iopos += sendsize;
		iosize -= sendsize;
		code_r = libusb_bulk_transfer(device_handle, ep_out, (unsigned char*) iopos, iosize, &n_sent, 1000);
		if(code_r < 0)
		{
			fprintf(stderr, "DEBUG: CAPT: cannot send buffer (%s)\n", libusb_strerror(code_r));
			usb_cleanup();
			exit(1);
		}
	}
}

static void capt_recv_buf(size_t offset, size_t expected)
{
	uint8_t ep_in = 0x82; /* out of device into host */
	int timeout = 1000;
	int size;
	int code_r;
	if (offset + expected > sizeof(capt_iobuf)) {
		fprintf(stderr, "ALERT: bug in CAPT driver, input buffer overflow\n");
		usb_cleanup();
		exit(1);
	}
	while(1) {
		fprintf(stderr, "DEBUG: CAPT: waiting for %u bytes\n", (unsigned) expected);
		code_r = libusb_bulk_transfer(device_handle, ep_in, (unsigned char *) capt_iobuf + offset, expected, &size, timeout);

		if (code_r == LIBUSB_ERROR_TIMEOUT) {
			fprintf(stderr, "DEBUG: CAPT: capt_recv_buf() timeout after %d msec, retrying\n", timeout);
			sleep(1);
			timeout *= 2;
			continue;
		}
		else if (code_r < 0) {
			fprintf(stderr, "ERROR: CAPT: no reply from printer (%s)\n", libusb_strerror(code_r));
			usb_cleanup();
			exit(1);
		}
	}
	capt_iosize = offset + (size_t)size;
}

const char *capt_identify(void)
{
	if(device_handle == NULL) fprintf(stderr, "DEBUG: CAPT: device handle is null\n");
	int REQUEST_TYPE = 0xA1;
	int code;
	int iosize = sizeof(lusb_iobuf)-1;
	fprintf(stderr, "DEBUG: CAPT: attempt to get IEEE 1284 Device ID\n");
	code = libusb_control_transfer(device_handle, REQUEST_TYPE, 0x0, 0x0, 0x0, lusb_iobuf, iosize-1, 300);
	if(code < 0){
		fprintf(stderr, "DEBUG: CAPT: unable to get device ID string (%s)\n", libusb_strerror(code));
		usb_cleanup();
		exit(1);
	}
	lusb_iobuf[iosize] = '\0';
	fprintf(stderr, "DEBUG: CAPT: printer ID string: %s\n", lusb_iobuf+1);
	return (const char*)lusb_iobuf+1; // the ID string begins with a \0
}

static void capt_copy_cmd(uint16_t cmd, const void *buf, size_t size)
{
	if (capt_iosize + 4 + size > sizeof(capt_iobuf)) {
		fprintf(stderr, "ALERT: bug in CAPT driver, output buffer overflow\n");
		exit(1);
	}
	if (buf)
		memcpy(capt_iobuf + capt_iosize + 4, buf, size);
	else
		size = 0;
	capt_iobuf[capt_iosize + 0] = LO(cmd);
	capt_iobuf[capt_iosize + 1] = HI(cmd);
	capt_iobuf[capt_iosize + 2] = LO(size + 4);
	capt_iobuf[capt_iosize + 3] = HI(size + 4);
	capt_iosize += size + 4;
}

void capt_send(uint16_t cmd, const void *buf, size_t size)
{
	capt_iosize = 0;
	capt_copy_cmd(cmd, buf, size);
	capt_send_buf();
}

void capt_sendrecv(uint16_t cmd, const void *buf, size_t size, void *reply, size_t *reply_size)
{
	sendrecv_started = true;
	last_send_status = CUPS_SC_STATUS_NONE;

	capt_send(cmd, buf, size);
	capt_recv_buf(0, 6);
	if (capt_iosize != 6 || WORD(capt_iobuf[0], capt_iobuf[1]) != cmd) {
		fprintf(stderr, "ERROR: CAPT: bad reply from printer, "
				"expected %02X %02X xx xx xx xx, got", LO(cmd), HI(cmd));
		capt_debug_buf("ERROR", 6);
		exit(1);
	}
	while (1) {
		if (WORD(capt_iobuf[2], capt_iobuf[3]) == capt_iosize)
			break;
		if (BCD(capt_iobuf[2], capt_iobuf[3]) == capt_iosize)
			break;
		/* block at 64 byte boundary is not the last one */
		if (WORD(capt_iobuf[2], capt_iobuf[3]) > capt_iosize && capt_iosize % 64 == 6) {
			capt_recv_buf(capt_iosize, WORD(capt_iobuf[2], capt_iobuf[3]) - capt_iosize);
			continue;
		}
		/* we should never get here */
		fprintf(stderr, "ERROR: CAPT: bad reply from printer, "
				"expected size %02X %02X, got %02X %02X\n",
				capt_iobuf[2], capt_iobuf[3], LO(capt_iosize), HI(capt_iosize));
		capt_debug_buf("ERROR", capt_iosize);
		exit(1);
	}
	if (debug) {
		fprintf(stderr, "DEBUG: CAPT: recv ");
		capt_debug_buf("DEBUG", capt_iosize);
	}
	if (reply) {
		size_t copysize = reply_size ? *reply_size : capt_iosize;
		if (copysize > capt_iosize)
			copysize = capt_iosize;
		memcpy(reply, capt_iobuf + 4, copysize);
	}
	if (reply_size)
		*reply_size = capt_iosize;

	sendrecv_started = false;
	last_send_status = CUPS_SC_STATUS_NONE;
}

void capt_multi_begin(uint16_t cmd)
{
	capt_iobuf[0] = LO(cmd);
	capt_iobuf[1] = HI(cmd);
	capt_iosize = 4;
}

void capt_multi_add(uint16_t cmd, const void *buf, size_t size)
{
	capt_copy_cmd(cmd, buf, size);
}

void capt_multi_send(void)
{
	capt_iobuf[2] = LO(capt_iosize);
	capt_iobuf[3] = HI(capt_iosize);
	capt_send_buf();
}

void capt_cleanup(void)
{
	/* For use with handling job cancellations */
	if (sendrecv_started) {

		if (last_send_status != CUPS_SC_STATUS_OK) {
			capt_send_buf();
			fprintf(stderr, "DEBUG: CAPT: finished interrupted send\n");
		}

		/* not else because recv cleanup is needed after finishing send */
		if (last_send_status == CUPS_SC_STATUS_OK) {
			size_t bytes = 0x10000;
			size_t bs = 64;
			while(bytes > 0) {
				bytes -= bs;
				cupsBackChannelRead(NULL, bs, 0.01);
			}
			fprintf(stderr, "DEBUG: CAPT: finished interrupted recv\n");
		}

	capt_iosize = 0;
	sendrecv_started = false;
	}
}

