/*
 * huawei-ups2000-test.c - focused policy tests for Huawei UPS2000 data updates
 *
 * Copyright (C) 2026 Yang Zun
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "huawei-ups2000-private.h"

static int failures = 0;

#define CHECK_EQ(actual, expected, description) \
	do { \
		int actual_value = (int)(actual); \
		int expected_value = (int)(expected); \
		if (actual_value != expected_value) { \
			fprintf(stderr, "FAIL: %s (got %d, expected %d)\n", \
				description, actual_value, expected_value); \
			failures++; \
		} \
	} while (0)

static void test_incident_values_degrade_only_battery_data(void)
{
	CHECK_EQ(ups2000_classify_register_value(2000, UPS2000_REG_FLOAT,
		true, UINT32_C(0x7fff)), UPS2000_UPDATE_BATTERY_DEGRADED,
		"register 2000 value 0x7fff is optional battery degradation");
	CHECK_EQ(ups2000_classify_register_value(2002, UPS2000_REG_UINT16,
		true, UINT32_C(0xffff)), UPS2000_UPDATE_BATTERY_DEGRADED,
		"register 2002 value 0xffff is optional battery degradation");
}

static void test_all_battery_register_failures_are_optional(void)
{
	static const struct {
		uint16_t reg;
		enum ups2000_register_datatype datatype;
		uint32_t invalid_value;
	} cases[] = {
		{ 2000, UPS2000_REG_FLOAT,  UINT32_C(0x7fff) },
		{ 2002, UPS2000_REG_UINT16, UINT32_C(0xffff) },
		{ 2003, UPS2000_REG_UINT16, UINT32_C(0xffff) },
		{ 2004, UPS2000_REG_UINT32, UINT32_C(0xffffffff) },
		{ 2007, UPS2000_REG_UINT16, UINT32_C(0xffff) },
		{ 2033, UPS2000_REG_UINT16, UINT32_C(0xffff) },
	};
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		CHECK_EQ(ups2000_classify_register_value(cases[i].reg,
			cases[i].datatype, false, 0),
			UPS2000_UPDATE_BATTERY_DEGRADED,
			"battery register read failure remains non-fatal");
		CHECK_EQ(ups2000_classify_register_value(cases[i].reg,
			cases[i].datatype, true, cases[i].invalid_value),
			UPS2000_UPDATE_BATTERY_DEGRADED,
			"battery register invalid sentinel remains non-fatal");
	}
}

static void test_critical_register_failures_remain_fatal(void)
{
	CHECK_EQ(ups2000_classify_register_value(1000, UPS2000_REG_FLOAT,
		false, 0), UPS2000_UPDATE_FATAL,
		"input telemetry read failure is fatal");
	CHECK_EQ(ups2000_classify_register_value(1000, UPS2000_REG_FLOAT,
		true, UINT32_C(0x7fff)), UPS2000_UPDATE_FATAL,
		"input telemetry invalid sentinel is fatal");
	CHECK_EQ(ups2000_classify_register_value(1024, UPS2000_REG_UINT16,
		false, 0), UPS2000_UPDATE_FATAL,
		"register 1024 read failure is fatal");
	CHECK_EQ(ups2000_classify_register_value(1024, UPS2000_REG_UINT16,
		true, UINT32_C(0xffff)), UPS2000_UPDATE_FATAL,
		"register 1024 invalid sentinel is fatal");
	CHECK_EQ(ups2000_classify_register_value(1043, UPS2000_REG_UINT16,
		false, 0), UPS2000_UPDATE_FATAL,
		"register 1043 read failure is fatal");
	CHECK_EQ(ups2000_classify_register_value(1043, UPS2000_REG_UINT16,
		true, UINT32_C(0xffff)), UPS2000_UPDATE_FATAL,
		"register 1043 invalid sentinel is fatal");
	CHECK_EQ(ups2000_classify_register_value(40156, UPS2000_REG_UINT16,
		false, 0), UPS2000_UPDATE_FATAL,
		"alarm register read failure is fatal");
}

static void test_battery_page_failure_requires_fresh_critical_status(void)
{
	int update_result = ups2000_classify_battery_page_read(false);

	update_result |= ups2000_classify_register_value(1024,
		UPS2000_REG_UINT16, true, UINT32_C(2));

	CHECK_EQ(update_result & UPS2000_UPDATE_BATTERY_DEGRADED,
		UPS2000_UPDATE_BATTERY_DEGRADED,
		"battery page read failure degrades battery data");
	CHECK_EQ(update_result & UPS2000_UPDATE_FATAL, 0,
		"battery page read failure does not add a fatal result");

	update_result |= ups2000_classify_register_value(1043,
		UPS2000_REG_UINT16, false, 0);
	CHECK_EQ(update_result & UPS2000_UPDATE_FATAL, UPS2000_UPDATE_FATAL,
		"critical status failure still makes the aggregate update fatal");
}

static void test_protocol_boundaries_preserve_safety(void)
{
	static const uint32_t valid_source_values[] = { 0, 1, 2, 3, 5 };
	static const uint32_t valid_charger_values[] = { 2, 3, 4, 5 };
	size_t i;

	/* Protocol boundary cases, not captured production readings. */
	for (i = 0; i < sizeof(valid_source_values) / sizeof(valid_source_values[0]); i++)
		CHECK_EQ(ups2000_classify_register_value(1024, UPS2000_REG_UINT16,
			true, valid_source_values[i]), UPS2000_UPDATE_OK,
			"all documented power-source states are publishable");
	for (i = 0; i < sizeof(valid_charger_values) / sizeof(valid_charger_values[0]); i++)
		CHECK_EQ(ups2000_classify_register_value(2002, UPS2000_REG_UINT16,
			true, valid_charger_values[i]), UPS2000_UPDATE_OK,
			"all documented charger states are publishable");
	CHECK_EQ(ups2000_classify_register_value(2002, UPS2000_REG_UINT16,
		true, UINT32_C(0)), UPS2000_UPDATE_BATTERY_DEGRADED,
		"charger status below the supported range is degraded");
	CHECK_EQ(ups2000_classify_register_value(2002, UPS2000_REG_UINT16,
		true, UINT32_C(1)), UPS2000_UPDATE_BATTERY_DEGRADED,
		"charger status immediately below the supported range is degraded");
	CHECK_EQ(ups2000_classify_register_value(2002, UPS2000_REG_UINT16,
		true, UINT32_C(6)), UPS2000_UPDATE_BATTERY_DEGRADED,
		"charger status above the supported range is degraded");
	CHECK_EQ(ups2000_classify_register_value(1024, UPS2000_REG_UINT16,
		true, UINT32_C(4)), UPS2000_UPDATE_FATAL,
		"unknown power-source status is fatal");
	CHECK_EQ(ups2000_classify_register_value(2000,
		(enum ups2000_register_datatype)-1, true, UINT32_C(821)),
		UPS2000_UPDATE_FATAL,
		"unknown datatype is fatal even for an optional battery register");
	CHECK_EQ(ups2000_register_value_is_invalid(
		(enum ups2000_register_datatype)-1, UINT32_C(821)), true,
		"unknown datatype cannot produce a valid decoded value");
	CHECK_EQ(ups2000_classify_register_value(2001, UPS2000_REG_UINT16,
		false, 0), UPS2000_UPDATE_FATAL,
		"unlisted battery-page registers are not implicitly optional");
	CHECK_EQ(ups2000_classify_battery_page_read(true), UPS2000_UPDATE_OK,
		"complete battery-page read is publishable");
}

static void test_valid_values_are_publishable(void)
{
	CHECK_EQ(ups2000_classify_register_value(2000, UPS2000_REG_FLOAT,
		true, UINT32_C(821)), UPS2000_UPDATE_OK,
		"valid battery voltage is publishable after recovery");
	CHECK_EQ(ups2000_classify_register_value(2002, UPS2000_REG_UINT16,
		true, UINT32_C(3)), UPS2000_UPDATE_OK,
		"valid battery charger status is publishable after recovery");
	CHECK_EQ(ups2000_classify_register_value(2003, UPS2000_REG_UINT16,
		true, UINT32_C(94)), UPS2000_UPDATE_OK,
		"valid battery charge is publishable after recovery");
	CHECK_EQ(ups2000_classify_register_value(2004, UPS2000_REG_UINT32,
		true, UINT32_C(1352)), UPS2000_UPDATE_OK,
		"valid battery runtime is publishable after recovery");
	CHECK_EQ(ups2000_classify_register_value(2007, UPS2000_REG_UINT16,
		true, UINT32_C(6)), UPS2000_UPDATE_OK,
		"valid battery pack count is publishable after recovery");
	CHECK_EQ(ups2000_classify_register_value(2033, UPS2000_REG_UINT16,
		true, UINT32_C(9)), UPS2000_UPDATE_OK,
		"valid battery capacity is publishable after recovery");
	CHECK_EQ(ups2000_classify_register_value(1024, UPS2000_REG_UINT16,
		true, UINT32_C(2)), UPS2000_UPDATE_OK,
		"valid line-power status remains publishable");
}

static void test_degradation_logging_only_follows_transitions(void)
{
	CHECK_EQ(ups2000_battery_transition(false, false),
		UPS2000_BATTERY_TRANSITION_NONE,
		"healthy polling does not log");
	CHECK_EQ(ups2000_battery_transition(false, true),
		UPS2000_BATTERY_TRANSITION_DEGRADED,
		"first degraded poll logs once");
	CHECK_EQ(ups2000_battery_transition(true, true),
		UPS2000_BATTERY_TRANSITION_NONE,
		"repeated degraded poll does not spam logs");
	CHECK_EQ(ups2000_battery_transition(true, false),
		UPS2000_BATTERY_TRANSITION_RECOVERED,
		"first healthy poll after degradation logs recovery once");
}

int main(void)
{
	test_incident_values_degrade_only_battery_data();
	test_all_battery_register_failures_are_optional();
	test_critical_register_failures_remain_fatal();
	test_battery_page_failure_requires_fresh_critical_status();
	test_protocol_boundaries_preserve_safety();
	test_valid_values_are_publishable();
	test_degradation_logging_only_follows_transitions();

	if (failures != 0)
		fprintf(stderr, "%d Huawei UPS2000 policy test(s) failed\n", failures);
	else
		printf("All Huawei UPS2000 policy tests passed\n");

	return failures != 0;
}
