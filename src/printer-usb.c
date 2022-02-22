/*
 * Copyright (C) 2022 Moses Chong
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

struct libusb_device_handle *device_handle;
struct libusb_context* ctxt = NULL;
struct libusb_device** devlist;

void usb_cleanup(void)
{
	fprintf(stderr, "DEBUG: CAPT: freeing libusb handle and list\n");
	libusb_close(device_handle);
	libusb_exit(ctxt);
	libusb_free_device_list(devlist, 1);
	return;
}

int set_device_handle(char* sn)
{
	// Please remember to call usb_cleanup() when the handle is no
	// longer in use.
	struct libusb_device_descriptor usbdesc; 
	struct libusb_device* dev;
	int code;
	int n_dev;  // number of devices found

	code = libusb_init(&ctxt);
	if(code < 0)
	{
		fprintf(stderr, "DEBUG: CAPT: libusb could not get context (%s)\n", libusb_strerror(code));
		return 1;
	}
	n_dev = libusb_get_device_list(ctxt, &devlist);

	if(n_dev == LIBUSB_ERROR_ACCESS)
	{
		fprintf(stderr, "DEBUG: CAPT: libusb access error getting device list\n");
		return 1;
	}

	else if(n_dev < 1)
	{
		fprintf(stderr, "DEBUG: CAPT: libusb device list empty\n");
		return 1;
	}
	
	for(int i=0; i < n_dev; i++)
	{
		dev = devlist[i];
		code = libusb_get_device_descriptor(dev, &usbdesc);
		if(code < 0)
		{
			fprintf(stderr, "DEBUG: CAPT: libusb can't get USB descriptor (%s)\n", libusb_strerror(code));
			fprintf(stderr, "DEBUG: CAPT: libusb skipping device\n");
			continue;
		}

		int isn = usbdesc.iSerialNumber;
		code = libusb_open(dev, &device_handle);
		if(code < 0)
		{
			fprintf(stderr, "DEBUG: CAPT: libusb can't get handle for device %04x:%04x, skipping device (%s)\n", usbdesc.idVendor, usbdesc.idProduct, libusb_strerror(code));
			continue;
		}
		int dlen = 13;
		unsigned char sn_d[dlen];
		libusb_get_string_descriptor_ascii(device_handle, isn, sn_d, dlen);
		if(!strncmp(sn, (char*)sn_d, dlen))
		{
			fprintf(stderr, "DEBUG: CAPT: libusb found handle with serial number matching %s\n", sn);
			break;
		}
	}

	if(libusb_kernel_driver_active(device_handle, 0) == 1)
	{
		fprintf(stderr, "DEBUG: CAPT: detaching kernel driver from printer device\n");
		code = libusb_detach_kernel_driver(device_handle, 0);
		if(!code) fprintf(stderr, "DEBUG: CAPT: successfully detached kernel driver from printer device\n");
	}

	code = libusb_claim_interface(device_handle, 0);
	if(code < 0)
	{
		fprintf(stderr, "DEBUG: CAPT: unable to claim USB interface (%s)\n", libusb_strerror(code));
		usb_cleanup();
		return 1;
	}
	else fprintf(stderr, "DEBUG: CAPT: interface claimed\n");

	return 0;
}

