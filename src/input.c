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
#include <dc/maple/lightgun.h>
#include <dc/maple/mouse.h>
#include <dc/maple/purupuru.h>
#include <kos/regfield.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include <libpcsxcore/r3000a.h>

#include "bloom-config.h"
#include "emu.h"

/* Scale factor of analog sticks / 128.
 * sqrtf(128^2 + 128^2) == ~181.02f */
#define SCALE_FACTOR 181

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
	} else {
		printf("Plugged a standard controller in port %u\n", dev->port);
	}

	in_type[dev->port] = PSE_PAD_TYPE_ANALOGPAD;

	if (dev->port > 1) {
		/* Plugged in port C/D - enable multitap */
		if (!use_multitap)
			printf("Enabling multi-tap\n");
		use_multitap = true;
	}
}

static void emu_detach_cb(maple_device_t *dev)
{
	printf("Unplugged input device from port %u\n", dev->port);
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

static void emu_attach_mouse_cb(maple_device_t *dev)
{
	printf("Plugged a mouse in port %u\n", dev->port);
	in_type[dev->port] = PSE_PAD_TYPE_MOUSE;
}

static void emu_attach_lightgun_cb(maple_device_t *dev)
{
	printf("Plugged a lightgun in port %u\n", dev->port);
	in_type[dev->port] = PSE_PAD_TYPE_GUN;

	maple_gun_enable(dev->port);
}

void input_init(void) {
        maple_device_t *dev;
	unsigned int i;

	maple_attach_callback(MAPLE_FUNC_CONTROLLER, emu_attach_cont_cb);
	maple_attach_callback(MAPLE_FUNC_MOUSE, emu_attach_mouse_cb);
	maple_attach_callback(MAPLE_FUNC_LIGHTGUN, emu_attach_lightgun_cb);

	maple_detach_callback(MAPLE_FUNC_CONTROLLER, emu_detach_cb);
	maple_detach_callback(MAPLE_FUNC_MOUSE, emu_detach_cb);
	maple_detach_callback(MAPLE_FUNC_LIGHTGUN, emu_detach_cb);

	for (i = 0; i < 4; i++) {
		dev = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
		if (dev)
			emu_attach_cont_cb(dev);

		dev = maple_enum_type(i, MAPLE_FUNC_MOUSE);
		if (dev)
			emu_attach_mouse_cb(dev);

		dev = maple_enum_type(i, MAPLE_FUNC_LIGHTGUN);
		if (dev)
			emu_attach_lightgun_cb(dev);
	}
}

void input_shutdown(void) {
	maple_attach_callback(MAPLE_FUNC_CONTROLLER, NULL);
	maple_detach_callback(MAPLE_FUNC_CONTROLLER, NULL);
	maple_attach_callback(MAPLE_FUNC_MOUSE, NULL);
	maple_detach_callback(MAPLE_FUNC_MOUSE, NULL);
}

long PAD__open(void)
{
	return PSE_PAD_ERR_SUCCESS;
}

long PAD__close(void) {
	return PSE_PAD_ERR_SUCCESS;
}

static long reportMouse(maple_device_t *dev, PadDataS *pad)
{
	mouse_state_t *state = (mouse_state_t *)maple_dev_status(dev);
	uint16_t buttons = 0;

	if (state->buttons & MOUSE_RIGHTBUTTON)
		buttons |= BIT(10);
	if (state->buttons & MOUSE_LEFTBUTTON)
		buttons |= BIT(11);

	pad->moveX = state->dx;
	pad->moveY = state->dy;
	pad->buttonStatus = ~buttons;

	return 0;
}

static inline uint8_t clamp8(int value)
{
	if (value < 0)
		return 0;
	if (value > 255)
		return 255;

	return (uint8_t)value;
}

static inline uint8_t analog_scale(uint8_t val)
{
	return clamp8((uint32_t)val * SCALE_FACTOR / 128 + 128 - SCALE_FACTOR);
}

long PAD1_readPort(PadDataS *pad) {
        maple_device_t *dev;
	cont_state_t *state;
	uint16_t buttons = 0;
	int x, y;

	pad->controllerType = in_type[pad->requestPadIndex];
	if (pad->controllerType == PSE_PAD_TYPE_NONE)
		return 0;

	dev = maple_enum_dev(pad->requestPadIndex, 0);
	if (!dev)
		return 0;

	if (pad->requestPadIndex == 1)
		pad->portMultitap = use_multitap;

	if (dev->info.functions & MAPLE_FUNC_MOUSE)
		return reportMouse(dev, pad);

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
	if (state->buttons & CONT_A) {
		if (pad->controllerType == PSE_PAD_TYPE_GUN)
			buttons |= BIT(DKEY_SQUARE);
		else
			buttons |= BIT(DKEY_CROSS);
	}
	if (state->buttons & CONT_B)
		buttons |= BIT(DKEY_CIRCLE);
	if (state->buttons & CONT_X)
		buttons |= BIT(DKEY_SQUARE);
	if (state->buttons & CONT_Y)
		buttons |= BIT(DKEY_TRIANGLE);

	if (pad->controllerType == PSE_PAD_TYPE_ANALOGPAD) {
		pad->rightJoyX = analog_scale(state->joy2x + 128);
		pad->rightJoyY = analog_scale(state->joy2y + 128);
		pad->leftJoyX = analog_scale(state->joyx + 128);
		pad->leftJoyY = analog_scale(state->joyy + 128);

		if (state->buttons & CONT_DPAD2_RIGHT)
			pad->ds.padMode ^= 1;
	} else if(pad->controllerType == PSE_PAD_TYPE_GUN) {
		maple_gun_read_pos(&x, &y);

		psxScheduleIrq10(4, x * 1629 / SCREEN_WIDTH,
				 y * screen_h / SCREEN_HEIGHT);
		maple_gun_enable(pad->requestPadIndex);
	}

	pad->buttonStatus = ~buttons;

	return 0;
}

long PAD2_readPort(PadDataS *pad) {
	return PAD1_readPort(pad);
}

void plat_trigger_vibrate(int pad, int low, int high) {
	maple_device_t *dev;
	unsigned int i;

	for (i = 0; i < MAPLE_UNIT_COUNT; i++) {
		dev = maple_enum_dev(pad, i);

		if (dev && (dev->info.functions & MAPLE_FUNC_PURUPURU)) {
			purupuru_rumble(dev, &(purupuru_effect_t){
				.cont   =  true,
				.motor  =  1,
				.fpow   =  low ? 1 : (uint8_t)high >> 5,
				.freq   =  21,
				.inc    =  38,
			});

			return;
		}
	}
}

void pl_gun_byte2(int port, unsigned char byte)
{
}
