/*
 * huawei-ups2000-driver-test.c - hardware-free Huawei UPS2000 regressions
 *
 * Copyright (C) 2026 Yang Zun
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include "main.h"
#include "serial.h"
#include "nut_stdint.h"
#include <modbus.h>

static int test_modbus_read_registers(modbus_t *ctx, int addr, int nb,
	uint16_t *dest);
static int test_modbus_write_registers(modbus_t *ctx, int addr, int nb,
	const uint16_t *src);
static int test_modbus_flush(modbus_t *ctx);
static modbus_t *test_modbus_new_rtu(const char *device, int baud, char parity,
	int data_bit, int stop_bit);
static TYPE_FD_SER test_ser_open(const char *port);
static unsigned int test_sleep(unsigned int seconds);
static void test_upslogx(int priority, const char *fmt, ...)
	__attribute__ ((__format__ (__printf__, 2, 3)));

/* No test can open a serial device or send a real Modbus request. */
#define modbus_read_registers test_modbus_read_registers
#define modbus_write_registers test_modbus_write_registers
#define modbus_flush test_modbus_flush
#define modbus_new_rtu test_modbus_new_rtu
#define ser_open test_ser_open
#define sleep test_sleep
#define upslogx test_upslogx
#include "huawei-ups2000.c"
#undef modbus_read_registers
#undef modbus_write_registers
#undef modbus_flush
#undef modbus_new_rtu
#undef ser_open
#undef sleep
#undef upslogx

static struct {
	uint16_t info[28];
	uint16_t battery[34];
	uint16_t alarms[27];
	uint16_t nominal_power;
	uint16_t source;
	uint16_t safety;
	uint16_t charger;
	uint16_t beeper;
	int failed_addr;
	int second_failed_addr;
	bool fail_all;
} fixture;

static int failures = 0;
static int writes = 0;
static int sleeps = 0;
static int battery_page_reads = 0;
static int charger_reads = 0;
static int source_reads = 0;
static int safety_reads = 0;
static int degraded_logs = 0;
static int recovered_logs = 0;

#define CHECK(condition, description) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "FAIL: %s\n", description); \
			failures++; \
		} \
	} while (0)

static int test_modbus_read_registers(modbus_t *ctx, int addr, int nb,
	uint16_t *dest)
{
	const uint16_t *values = NULL;
	int count = 0;

	NUT_UNUSED_VARIABLE(ctx);
	if (addr == 12000)
		battery_page_reads++;
	else if (addr == 12002)
		charger_reads++;
	else if (addr == 11024)
		source_reads++;
	else if (addr == 11043)
		safety_reads++;

	if (fixture.fail_all || fixture.failed_addr == addr ||
	    fixture.second_failed_addr == addr) {
		errno = EIO;
		return -1;
	}

	switch (addr) {
	case 11000:
		values = fixture.info;
		count = 28;
		break;
	case 12000:
		values = fixture.battery;
		count = 34;
		break;
	case 19009:
		values = &fixture.nominal_power;
		count = 1;
		break;
	case 11024:
		values = &fixture.source;
		count = 1;
		break;
	case 11043:
		values = &fixture.safety;
		count = 1;
		break;
	case 12002:
		values = &fixture.charger;
		count = 1;
		break;
	case 11046:
		values = &fixture.beeper;
		count = 1;
		break;
	case 41180:
		values = fixture.alarms;
		count = 27;
		break;
	default:
		fprintf(stderr, "Unexpected test register read: %d\n", addr);
		abort();
	}

	if (nb != count) {
		fprintf(stderr, "Unexpected test register count: %d at %d\n", nb, addr);
		abort();
	}
	memcpy(dest, values, (size_t)count * sizeof(*dest));
	return count;
}

static int test_modbus_write_registers(modbus_t *ctx, int addr, int nb,
	const uint16_t *src)
{
	NUT_UNUSED_VARIABLE(ctx);
	NUT_UNUSED_VARIABLE(addr);
	NUT_UNUSED_VARIABLE(nb);
	NUT_UNUSED_VARIABLE(src);
	writes++;
	errno = EPERM;
	return -1;
}

static int test_modbus_flush(modbus_t *ctx)
{
	NUT_UNUSED_VARIABLE(ctx);
	return 0;
}

static modbus_t *test_modbus_new_rtu(const char *device, int baud, char parity,
	int data_bit, int stop_bit)
{
	NUT_UNUSED_VARIABLE(device);
	NUT_UNUSED_VARIABLE(baud);
	NUT_UNUSED_VARIABLE(parity);
	NUT_UNUSED_VARIABLE(data_bit);
	NUT_UNUSED_VARIABLE(stop_bit);
	fprintf(stderr, "Hardware initialization is forbidden in this test\n");
	abort();
}

static TYPE_FD_SER test_ser_open(const char *port)
{
	NUT_UNUSED_VARIABLE(port);
	fprintf(stderr, "Opening a serial device is forbidden in this test\n");
	abort();
}

static unsigned int test_sleep(unsigned int seconds)
{
	NUT_UNUSED_VARIABLE(seconds);
	sleeps++;
	return 0;
}

static void test_upslogx(int priority, const char *fmt, ...)
{
	char message[1024];
	va_list ap;

	NUT_UNUSED_VARIABLE(priority);
	va_start(ap, fmt);
	vsnprintf(message, sizeof(message), fmt, ap);
	va_end(ap);
	if (strstr(message, "Battery data is degraded"))
		degraded_logs++;
	if (strstr(message, "Battery data has recovered"))
		recovered_logs++;
}

static void reset_counters(void)
{
	writes = 0;
	sleeps = 0;
	battery_page_reads = 0;
	charger_reads = 0;
	source_reads = 0;
	safety_reads = 0;
	degraded_logs = 0;
	recovered_logs = 0;
}

static void restore_battery_fixture(void)
{
	/* Captured healthy battery readings; these are not estimated values. */
	fixture.battery[0] = 821;
	fixture.battery[3] = 94;
	fixture.battery[4] = 0;
	fixture.battery[5] = 1352;
	fixture.battery[7] = 6;
	fixture.battery[33] = 9;
	fixture.charger = 3;
}

static void begin_test(void)
{
	dstate_free();
	dstate_datastale();
	memset(&fixture, 0, sizeof(fixture));
	fixture.failed_addr = -1;
	fixture.second_failed_addr = -1;
	fixture.source = 2; /* Protocol OL enum matching the observed OL status. */
	/*
	 * Protocol boundary fixtures, NOT captured raw register 1043 data:
	 * CAL is bit 2 and LB is bit 6. Unrelated telemetry uses zero-valued
	 * protocol fixtures solely to exercise the update path without hardware.
	 */
	fixture.safety = (1U << 2) | (1U << 6);
	fixture.alarms[40170 - 40156] = 1U << 4; /* Alarm 22: disconnected. */
	restore_battery_fixture();
	retry_status = RETRY_ENABLE;
	upsdrv_updateinfo();
	CHECK(!dstate_is_stale(), "healthy baseline is dataok");
	reset_counters();
}

static bool value_equals(const char *name, const char *expected)
{
	const char *value = dstate_getinfo(name);
	return value && !strcmp(value, expected);
}

static bool status_contains(const char *flag)
{
	const char *value = dstate_getinfo("ups.status");
	return value && strstr(value, flag);
}

static void check_safety_and_alarm(void)
{
	const char *alarm = dstate_getinfo("ups.alarm");
	CHECK(status_contains("OL"), "fresh OL status is retained");
	CHECK(status_contains("CAL"), "protocol CAL flag is retained");
	CHECK(status_contains("LB"), "protocol LB flag is retained");
	CHECK(alarm && strstr(alarm, "Battery disconnected"),
		"Battery disconnected alarm remains published");
	CHECK(writes == 0, "monitoring never sends a control write");
}

static void test_incident_degradation_and_recovery(void)
{
	begin_test();
	/* The two raw invalid values recorded in the incident. */
	fixture.battery[0] = 0x7fff;
	fixture.charger = 0xffff;
	upsdrv_updateinfo();
	CHECK(!dstate_is_stale(), "incident battery values do not make the UPS stale");
	CHECK(!dstate_getinfo("battery.voltage"), "invalid battery voltage is removed");
	CHECK(!dstate_getinfo("battery.charger.status"), "invalid charger state is removed");
	CHECK(value_equals("battery.charge", "94.0"), "valid battery charge remains available");
	CHECK(!status_contains("CHRG"), "invalid charger state does not publish CHRG or DISCHRG");
	CHECK(battery_page_reads == 1 && charger_reads == 1 && sleeps == 0,
		"optional reads are single attempts without retry sleeps");
	CHECK(source_reads == 1 && safety_reads == 1,
		"both critical status registers are freshly read");
	CHECK(degraded_logs == 1, "degradation is logged once");
	check_safety_and_alarm();

	upsdrv_updateinfo();
	CHECK(!dstate_is_stale(), "repeated incident values remain dataok");
	CHECK(degraded_logs == 1 && recovered_logs == 0,
		"repeated degradation does not spam transition logs");

	restore_battery_fixture();
	upsdrv_updateinfo();
	CHECK(!dstate_is_stale(), "recovered battery values remain dataok");
	CHECK(value_equals("battery.voltage", "82.1"), "battery voltage recovers");
	CHECK(value_equals("battery.charger.status", "charging"), "charger state recovers");
	CHECK(status_contains("CHRG"), "valid charger state restores CHRG");
	CHECK(recovered_logs == 1, "recovery is logged once");
	check_safety_and_alarm();
}

static void test_battery_page_failure_and_field_recovery(void)
{
	static const char *names[] = {
		"battery.voltage", "battery.charge", "battery.runtime",
		"battery.packs", "battery.capacity"
	};
	size_t i;

	begin_test();
	fixture.failed_addr = 12000;
	upsdrv_updateinfo();
	CHECK(!dstate_is_stale(), "battery-page read failure alone remains dataok");
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		CHECK(!dstate_getinfo(names[i]), "unread battery-page field is removed");
	CHECK(value_equals("battery.charger.status", "charging"),
		"independently read charger state remains available");
	CHECK(battery_page_reads == 1 && sleeps == 0,
		"battery-page failure has no retry delay");
	check_safety_and_alarm();

	fixture.failed_addr = -1;
	upsdrv_updateinfo();
	CHECK(value_equals("battery.voltage", "82.1"), "battery voltage returns after page recovery");
	CHECK(value_equals("battery.charge", "94.0"), "battery charge returns after page recovery");
	CHECK(value_equals("battery.runtime", "1352"), "battery runtime returns after page recovery");
	CHECK(value_equals("battery.packs", "6"), "battery packs return after page recovery");
	CHECK(value_equals("battery.capacity", "9"), "battery capacity returns after page recovery");

	fixture.failed_addr = 12000;
	fixture.second_failed_addr = 11043;
	upsdrv_updateinfo();
	CHECK(dstate_is_stale(), "battery-page failure cannot hide a critical safety-read failure");
}

static void test_charger_read_failure_is_degraded(void)
{
	begin_test();
	fixture.failed_addr = 12002;
	upsdrv_updateinfo();
	CHECK(!dstate_is_stale(), "charger read failure alone remains dataok");
	CHECK(!dstate_getinfo("battery.charger.status"), "unread charger state is removed");
	CHECK(!status_contains("CHRG"), "unread charger state adds no charging flag");
	CHECK(value_equals("battery.voltage", "82.1"), "independent battery telemetry remains available");
	CHECK(charger_reads == 1 && sleeps == 0, "charger read failure has no retry delay");
	check_safety_and_alarm();
}

static void test_other_battery_sentinels_are_removed(void)
{
	begin_test();
	/* Protocol invalid sentinels for the remaining optional registers. */
	fixture.battery[3] = 0xffff;
	fixture.battery[4] = 0xffff;
	fixture.battery[5] = 0xffff;
	fixture.battery[7] = 0xffff;
	fixture.battery[33] = 0xffff;
	upsdrv_updateinfo();
	CHECK(!dstate_is_stale(), "other invalid battery fields remain dataok");
	CHECK(!dstate_getinfo("battery.charge"), "invalid charge is removed");
	CHECK(!dstate_getinfo("battery.runtime"), "invalid runtime is removed");
	CHECK(!dstate_getinfo("battery.packs"), "invalid pack count is removed");
	CHECK(!dstate_getinfo("battery.capacity"), "invalid capacity is removed");
	CHECK(value_equals("battery.voltage", "82.1"), "valid voltage is not removed");
	check_safety_and_alarm();
}

static void test_required_read_failures_remain_stale(void)
{
	static const int required_addresses[] = {
		41180, 11000, 19009, 11024, 11043, 11046
	};
	size_t i;

	for (i = 0; i < sizeof(required_addresses) / sizeof(required_addresses[0]); i++) {
		begin_test();
		fixture.failed_addr = required_addresses[i];
		upsdrv_updateinfo();
		CHECK(dstate_is_stale(), "required register read failure remains stale");
		CHECK(writes == 0, "required read failure does not send control writes");
	}

	begin_test();
	fixture.fail_all = true;
	upsdrv_updateinfo();
	CHECK(dstate_is_stale(), "complete transport failure remains stale");
	CHECK(writes == 0, "complete transport failure does not send control writes");
}

static void test_status_boundaries(void)
{
	static const uint16_t invalid_charger_values[] = { 0, 1, 6 };
	size_t i;

	for (i = 0; i < sizeof(invalid_charger_values) / sizeof(invalid_charger_values[0]); i++) {
		begin_test();
		fixture.charger = invalid_charger_values[i];
		upsdrv_updateinfo();
		CHECK(!dstate_is_stale(), "unknown charger state remains dataok");
		CHECK(!dstate_getinfo("battery.charger.status"), "unknown charger state is removed");
		CHECK(!status_contains("CHRG"), "unknown charger state adds no charging flag");
		check_safety_and_alarm();
	}

	begin_test();
	fixture.info[0] = 0x7fff; /* Protocol invalid input-voltage sentinel. */
	upsdrv_updateinfo();
	CHECK(dstate_is_stale(), "invalid required input telemetry remains stale");

	begin_test();
	fixture.source = 4; /* Protocol boundary: unsupported power-source enum. */
	upsdrv_updateinfo();
	CHECK(dstate_is_stale(), "unknown power-source status remains stale");

	begin_test();
	fixture.safety = 0xffff; /* Invalid sentinel, not a captured healthy sample. */
	upsdrv_updateinfo();
	CHECK(dstate_is_stale(), "invalid safety status remains stale");

	begin_test();
	fixture.source = 3; /* Protocol OB enum, not an incident raw sample. */
	fixture.battery[0] = 0x7fff;
	fixture.charger = 0xffff;
	fixture.alarms[40164 - 40156] = 1U << 1; /* Protocol replacement alarm. */
	upsdrv_updateinfo();
	CHECK(!dstate_is_stale(), "OB with optional battery degradation remains dataok");
	CHECK(status_contains("OB") && status_contains("LB") && status_contains("CAL"),
		"OB, LB, and CAL remain available during battery degradation");
	CHECK(status_contains("RB"), "battery replacement alarm flag remains available");
	CHECK(writes == 0, "status-boundary tests send no control writes");
}

static void test_load_on_rejects_missing_safety_status(void)
{
	static const int required_addresses[] = { 11024, 11043 };
	size_t i;

	for (i = 0; i < sizeof(required_addresses) / sizeof(required_addresses[0]); i++) {
		begin_test();
		fixture.failed_addr = required_addresses[i];
		CHECK(ups2000_instcmd_load_on(1029) == STAT_INSTCMD_FAILED,
			"load.on rejects a missing critical status register");
		CHECK(dstate_is_stale(), "load.on marks missing critical status stale");
		CHECK(writes == 0, "rejected load.on never reaches a control write");
	}

	begin_test();
	fixture.source = 4;
	CHECK(ups2000_instcmd_load_on(1029) == STAT_INSTCMD_FAILED,
		"load.on rejects an unknown power-source value");
	CHECK(writes == 0, "unknown source prevents a load.on control write");

	begin_test();
	fixture.safety = 0xffff;
	CHECK(ups2000_instcmd_load_on(1029) == STAT_INSTCMD_FAILED,
		"load.on rejects an invalid safety-status value");
	CHECK(writes == 0, "invalid safety status prevents a load.on control write");
}

int main(void)
{
	test_incident_degradation_and_recovery();
	test_battery_page_failure_and_field_recovery();
	test_charger_read_failure_is_degraded();
	test_other_battery_sentinels_are_removed();
	test_required_read_failures_remain_stale();
	test_status_boundaries();
	test_load_on_rejects_missing_safety_status();
	dstate_free();

	if (failures != 0)
		fprintf(stderr, "%d Huawei UPS2000 driver test(s) failed\n", failures);
	else
		printf("All hardware-free Huawei UPS2000 driver tests passed\n");
	return failures != 0;
}
