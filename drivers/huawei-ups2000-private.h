/*
 * huawei-ups2000-private.h - internal data-quality policy for Huawei UPS2000
 *
 * Copyright (C) 2026 Yang Zun
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef HUAWEI_UPS2000_PRIVATE_H_SEEN
#define HUAWEI_UPS2000_PRIVATE_H_SEEN 1

#include <stdbool.h>
#include <stdint.h>

enum ups2000_register_datatype {
	UPS2000_REG_UINT16,
	UPS2000_REG_UINT32, /* occupies two registers */
	UPS2000_REG_FLOAT   /* fixed-point, named after the protocol datatype */
};

enum ups2000_update_result {
	UPS2000_UPDATE_OK = 0,
	UPS2000_UPDATE_BATTERY_DEGRADED = 1,
	UPS2000_UPDATE_FATAL = 2
};

enum ups2000_battery_transition {
	UPS2000_BATTERY_TRANSITION_NONE,
	UPS2000_BATTERY_TRANSITION_DEGRADED,
	UPS2000_BATTERY_TRANSITION_RECOVERED
};

static inline bool ups2000_is_optional_battery_register(uint16_t reg)
{
	switch (reg) {
	case 2000:
	case 2002:
	case 2003:
	case 2004:
	case 2007:
	case 2033:
		return true;
	default:
		return false;
	}
}

static inline bool ups2000_register_value_is_invalid(
	enum ups2000_register_datatype datatype, uint32_t value)
{
	switch (datatype) {
	case UPS2000_REG_FLOAT:
		return value == UINT32_C(0x7fff);
	case UPS2000_REG_UINT16:
		return value == UINT32_C(0xffff);
	case UPS2000_REG_UINT32:
		return value == UINT32_C(0xffffffff);
	default:
		return true;
	}
}

static inline enum ups2000_update_result ups2000_classify_register_value(
	uint16_t reg, enum ups2000_register_datatype datatype,
	bool read_succeeded, uint32_t value)
{
	if (datatype != UPS2000_REG_UINT16 && datatype != UPS2000_REG_UINT32 &&
	    datatype != UPS2000_REG_FLOAT)
		return UPS2000_UPDATE_FATAL;

	if (read_succeeded && reg == 1024 && value != 0 && value != 1 &&
	    value != 2 && value != 3 && value != 5)
		return UPS2000_UPDATE_FATAL;

	if (read_succeeded && reg == 2002 && (value < 2 || value > 5))
		return UPS2000_UPDATE_BATTERY_DEGRADED;

	if (read_succeeded &&
	    !ups2000_register_value_is_invalid(datatype, value))
		return UPS2000_UPDATE_OK;

	if (ups2000_is_optional_battery_register(reg))
		return UPS2000_UPDATE_BATTERY_DEGRADED;

	return UPS2000_UPDATE_FATAL;
}

static inline enum ups2000_update_result
ups2000_classify_battery_page_read(bool read_succeeded)
{
	return read_succeeded ? UPS2000_UPDATE_OK :
		UPS2000_UPDATE_BATTERY_DEGRADED;
}

static inline enum ups2000_battery_transition ups2000_battery_transition(
	bool was_degraded, bool is_degraded)
{
	if (was_degraded == is_degraded)
		return UPS2000_BATTERY_TRANSITION_NONE;

	return is_degraded ? UPS2000_BATTERY_TRANSITION_DEGRADED :
		UPS2000_BATTERY_TRANSITION_RECOVERED;
}

#endif /* HUAWEI_UPS2000_PRIVATE_H_SEEN */
