/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kvss/zms.h>
#include <zephyr/kernel.h>

#include "net.h"
#include "rpl.h"


/* ======================== Mocks =========================================== */

static struct {
	struct k_work_delayable *rpl_work;
	k_work_handler_t rpl_work_handler;
	struct zms_fs *zms_fs;
} mock_data = { 0 };

bool verify_mock_calls = true;

int zms_mount(struct zms_fs *fs)
{
	zassert_is_null(mock_data.zms_fs, "zms_mount called twice");
	mock_data.zms_fs = fs;
	return 0;
}

int zms_clear(struct zms_fs *fs)
{
	mock_data.zms_fs = NULL;

	if (!verify_mock_calls) {
		return 0;
	}

	ztest_check_expected_value(fs);
	return ztest_get_return_value();
}

ssize_t zms_write(struct zms_fs *fs, zms_id_t id, const void *data, size_t len)
{
	if (!verify_mock_calls) {
		return len;
	}

	ztest_check_expected_value(fs);
	ztest_check_expected_value(id);
	ztest_check_expected_value(len);
	ztest_check_expected_data(data, len);

	return ztest_get_return_value();
}

int zms_delete(struct zms_fs *fs, zms_id_t id)
{
	if (!verify_mock_calls) {
		return 0;
	}

	ztest_check_expected_value(fs);
	ztest_check_expected_value(id);

	return ztest_get_return_value();
}

void k_work_init_delayable(struct k_work_delayable *dwork,
				  k_work_handler_t handler)
{
	zassert_is_null(mock_data.rpl_work, "k_work_init_delayable called twice");
	zassert_is_null(mock_data.rpl_work_handler, "k_work_init_delayable called twice");
	mock_data.rpl_work = dwork;
	mock_data.rpl_work_handler = handler;
}

int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	if (!verify_mock_calls) {
		return 0;
	}

	ztest_check_expected_value(dwork);
	ztest_check_expected_value(delay.ticks);
	return ztest_get_return_value();
}

/* ======================== End Mocks ======================================= */

static void expect_work_schedule(void)
{
	ztest_expect_value(k_work_schedule, dwork, mock_data.rpl_work);
	ztest_expect_value(k_work_schedule,
			   delay.ticks,
			   K_SECONDS(CONFIG_BT_MESH_RPL_ZMS_STORE_TIMEOUT).ticks);
	ztest_returns_value(k_work_schedule, 0);
}

static void expect_zms_delete(zms_id_t expected_id)
{
	ztest_expect_value(zms_delete, fs, mock_data.zms_fs);
	ztest_expect_value(zms_delete, id, expected_id);
	ztest_returns_value(zms_delete, 0);
}

static void trigger_work_handler(void)
{
	zassert_not_null(mock_data.rpl_work_handler, "RPL work handler not set");
	zassert_not_null(mock_data.rpl_work, "RPL work not set");
	mock_data.rpl_work_handler(&mock_data.rpl_work->work);
}

struct rpl_test_data {
	uint16_t addr;
	uint32_t seq;
	bool old_iv;
};

struct rpl_val {
	uint32_t seq:24,
		 old_iv:1;
};

static struct rpl_val expected_rpl_data_pool[CONFIG_BT_MESH_CRPL][2];
static size_t expected_rpl_data_count;

static void expect_pair_write(uint16_t first_addr, uint32_t first_seq, bool first_old_iv,
			      uint16_t second_addr, uint32_t second_seq, bool second_old_iv)
{
	zms_id_t expected_id = ((uint32_t)first_addr << 16) | second_addr;
	struct rpl_val *expected_data = expected_rpl_data_pool[expected_rpl_data_count++];

	expected_data[0] = (struct rpl_val) {
		.seq = first_seq,
		.old_iv = first_old_iv,
	};
	expected_data[1] = (struct rpl_val) {
		.seq = second_seq,
		.old_iv = second_old_iv,
	};

	ztest_expect_value(zms_write, fs, mock_data.zms_fs);
	ztest_expect_value(zms_write, id, expected_id);
	ztest_expect_data(zms_write, data, expected_data);
	ztest_expect_value(zms_write, len, sizeof(expected_rpl_data_pool[0]));
	ztest_returns_value(zms_write, sizeof(expected_rpl_data_pool[0]));
}

static inline void expect_pair_write_cur_iv(uint16_t first_addr, uint32_t first_seq,
					 uint16_t second_addr, uint32_t second_seq)
{
	expect_pair_write(first_addr, first_seq, false,
			  second_addr, second_seq, false);
}

static void expect_pair_delete(uint16_t first_addr, uint16_t second_addr)
{
	zms_id_t expected_id = ((uint32_t)first_addr << 16) | second_addr;
	expect_zms_delete(expected_id);
}

static void rx(uint16_t addr, uint32_t seq, bool old_iv, bool expected_return)
{
	struct bt_mesh_net_rx rx = {
		.ctx.addr = addr,
		.seq = seq,
		.old_iv = old_iv,
		.local_match = true,
	};

	expect_work_schedule();
	zassert_equal(bt_mesh_rpl_check(&rx, NULL, false), expected_return);
}

static void rx_ok(uint16_t addr, uint32_t seq)
{
	rx(addr, seq, false, false);
}

/* TODO: Remove all __maybe_unused when test suite is finised. */
static __maybe_unused void rx_replay(uint16_t addr, uint32_t seq)
{
	rx(addr, seq, false, true);
}

static __maybe_unused void rx_ok_old_iv(uint16_t addr, uint32_t seq)
{
	rx(addr, seq, true, false);
}

static __maybe_unused void rx_replay_old_iv(uint16_t addr, uint32_t seq)
{
	rx(addr, seq, true, true);
}

static void setup(void *f)
{
	ARG_UNUSED(f);

	/* Temporarily disable argument checking. */
	verify_mock_calls = false;

	bt_mesh_rpl_clear();
	trigger_work_handler();
	expected_rpl_data_count = 0;

	/* Re-enable argument checking. */
	verify_mock_calls = true;
}

ZTEST(bt_mesh_rpl_ncs, test_full_pairs_stored)
{
	/* Add an even number of entries */
	rx_ok(1, 1);
	rx_ok(2, 0xabba);
	rx_ok(0x42, 0xabcd);
	/* Updated twice, should only be written once */
	rx_ok(4, 0x123455);
	rx_ok(4, 0x123456);

	/* Check that both pairs are written correctly. */
	expect_pair_write_cur_iv(1, 1, 2, 0xabba);
	expect_pair_write_cur_iv(0x42, 0xabcd, 4, 0x123456);
	trigger_work_handler();

	/* Check that extra write call does not re-write old pairs. */
	trigger_work_handler();
}

ZTEST(bt_mesh_rpl_ncs, test_half_pair_stored)
{
	/* Add entries to end up with an odd number of entries. */
	rx_ok(5, 0xabcdef);
	rx_ok(6, 0x42);
	rx_ok(0x1234, 0x101010);

	/* Check that half-pair is written correctly */
	expect_pair_write_cur_iv(5, 0xabcdef, 6, 0x42);
	expect_pair_write_cur_iv(0x1234, 0x101010, 0, 0);
	trigger_work_handler();
}

ZTEST(bt_mesh_rpl_ncs, test_half_pair_deleted)
{
	/* Add entries to end up with an odd number of entries. */
	rx_ok(5, 0xabcdef);
	rx_ok(6, 0x42);
	rx_ok(0x1234, 0x101010);

	/* Store them. */
	expect_pair_write_cur_iv(5, 0xabcdef, 6, 0x42);
	expect_pair_write_cur_iv(0x1234, 0x101010, 0, 0);
	trigger_work_handler();

	/* Add several more entries to create an even number. */
	rx_ok(0x000f, 0x123);
	rx_ok(0x0009, 0x2);
	rx_ok(0x000a, 0x3);

	/* Check that the old half-pair is deleted, and new pairs are written */
	expect_pair_delete(0x1234, 0);
	expect_pair_write_cur_iv(0x1234, 0x101010, 0x000f, 0x123);
	expect_pair_write_cur_iv(0x0009, 0x2, 0x000a, 0x3);
	trigger_work_handler();
}

/*
 * TODO: Go through upstream RPL unit tests, replicate any of them that make
 * sense with the new ZMS storage implementation. Also add new tests related
 * to clearing and resetting the RPL, and entries with old_iv=1.
 */

ZTEST_SUITE(bt_mesh_rpl_ncs, NULL, NULL, setup, NULL, NULL);
