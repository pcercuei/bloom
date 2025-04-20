// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input code
 *
 * Copyright (C) 2025 Paul Cercueil <paul@crapouillou.net>
 */

#include <frontend/plugin_lib.h>
#include <psemu_plugin_defs.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <kos/regfield.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

unsigned short in_keystate[8];

static bool use_multitap;

int in_type[8] = {
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE,
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE,
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE,
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE
};

static void emu_attach_cont_cb(maple_device_t *dev)
{
	if (cont_has_capabilities(dev, 0xffff3f00)) {
		printf("Plugged a BlueRetro / usb4maple controller in port %u\n",
		       dev->port);
		in_type[dev->port] = PSE_PAD_TYPE_ANALOGPAD;
	} else {
		printf("Plugged a standard controller in port %u\n", dev->port);
		in_type[dev->port] = PSE_PAD_TYPE_STANDARD;
	}

	if (dev->port > 1) {
		/* Plugged in port C/D - enable multitap */
		if (!use_multitap)
			printf("Enabling multi-tap\n");
		use_multitap = true;
	}
}

static void emu_detach_cont_cb(maple_device_t *dev)
{
	printf("Unplugged a controller in port %u\n", dev->port);
	in_type[dev->port] = PSE_PAD_TYPE_NONE;

	if (dev->port > 1) {
		/* Unplugged from port C/D - check if the other one is unplugged
		 * as well, and if it is, disable multitap */
		if (in_type[dev->port ^ 1] == PSE_PAD_TYPE_NONE) {
			if (use_multitap)
				printf("Disabling multi-tap\n");
			use_multitap = false;
		}
	}
}

long PAD__init(long flags) {
        maple_device_t *dev;
	unsigned int i;

	maple_attach_callback(MAPLE_FUNC_CONTROLLER, emu_attach_cont_cb);
	maple_detach_callback(MAPLE_FUNC_CONTROLLER, emu_detach_cont_cb);

	for (i = 0; i < 4; i++) {
		dev = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
		if (dev)
			emu_attach_cont_cb(dev);
	}

	return PSE_PAD_ERR_SUCCESS;
}

long PAD__shutdown(void) {
	maple_attach_callback(MAPLE_FUNC_CONTROLLER, NULL);
	maple_detach_callback(MAPLE_FUNC_CONTROLLER, NULL);

	return PSE_PAD_ERR_SUCCESS;
}

long PAD__open(void)
{
	return PSE_PAD_ERR_SUCCESS;
}

long PAD__close(void) {
	return PSE_PAD_ERR_SUCCESS;
}

long PAD1__readPort1(PadDataS *pad) {
        maple_device_t *dev;
	cont_state_t *state;
	uint16_t buttons = 0;

	pad->controllerType = in_type[pad->requestPadIndex];
	if (pad->controllerType == PSE_PAD_TYPE_NONE)
		return 0;

	dev = maple_enum_dev(pad->requestPadIndex, 0);
	if (!dev)
		return 0;

	if (pad->requestPadIndex == 1)
		pad->portMultitap = use_multitap;

	if (!(dev->info.functions & MAPLE_FUNC_CONTROLLER))
		return 0;

	state = (cont_state_t *)maple_dev_status(dev);
	if (state->buttons & CONT_Z)
		buttons |= BIT(DKEY_SELECT);
	if (state->buttons & CONT_DPAD2_LEFT)
		buttons |= BIT(DKEY_L3);
	if (state->buttons & CONT_DPAD2_DOWN)
		buttons |= BIT(DKEY_R3);
	if (state->buttons & CONT_START)
		buttons |= BIT(DKEY_START);
	if (state->buttons & CONT_DPAD_UP)
		buttons |= BIT(DKEY_UP);
	if (state->buttons & CONT_DPAD_RIGHT)
		buttons |= BIT(DKEY_RIGHT);
	if (state->buttons & CONT_DPAD_DOWN)
		buttons |= BIT(DKEY_DOWN);
	if (state->buttons & CONT_DPAD_LEFT)
		buttons |= BIT(DKEY_LEFT);
	if (state->buttons & CONT_C)
		buttons |= BIT(DKEY_L2);
	if (state->buttons & CONT_D)
		buttons |= BIT(DKEY_R2);
	if (state->ltrig > 128)
		buttons |= BIT(DKEY_L1);
	if (state->rtrig > 128)
		buttons |= BIT(DKEY_R1);
	if (state->buttons & CONT_A)
		buttons |= BIT(DKEY_CROSS);
	if (state->buttons & CONT_B)
		buttons |= BIT(DKEY_CIRCLE);
	if (state->buttons & CONT_X)
		buttons |= BIT(DKEY_SQUARE);
	if (state->buttons & CONT_Y)
		buttons |= BIT(DKEY_TRIANGLE);

	pad->buttonStatus = ~buttons;

	if (pad->controllerType == PSE_PAD_TYPE_ANALOGPAD) {
		pad->rightJoyX = state->joy2x + 128;
		pad->rightJoyY = state->joy2y + 128;
		pad->leftJoyX = state->joyx + 128;
		pad->leftJoyY = state->joyy + 128;

		if (state->buttons & CONT_DPAD2_RIGHT)
			pad->ds.padMode ^= 1;
	}

	return 0;
}

long PAD2__readPort2(PadDataS *pad) {
	return PAD1__readPort1(pad);
}

void plat_trigger_vibrate(int pad, int low, int high) {
}

void pl_gun_byte2(int port, unsigned char byte)
{
}
