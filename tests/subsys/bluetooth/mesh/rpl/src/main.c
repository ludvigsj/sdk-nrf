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

/* ======================== Forward declarations ============================ */
static void rx_ok(uint16_t addr, uint32_t seq);

/* ======================== Mocks =========================================== */

static struct {
	struct k_work_delayable *rpl_work;
	k_work_handler_t rpl_work_handler;
	struct zms_fs *zms_fs;
} mock_data = { 0 };

static bool verify_mock_calls = true;

/* Used to simulate race conditions with updates mid-write. */
static struct {
	uint16_t addr;
	uint32_t seq;
} rx_mid_write = { 0 };

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

	if (rx_mid_write.addr != 0) {
		/* Simulate a mid-write RX event. */
		rx_ok(rx_mid_write.addr, rx_mid_write.seq);
		rx_mid_write.addr = 0;
	}

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

#define PAIR_ID(first_addr, second_addr) (((uint32_t)(first_addr) << 16) | (second_addr))

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

static void expect_zms_clear(void)
{
	ztest_expect_value(zms_clear, fs, mock_data.zms_fs);
	ztest_returns_value(zms_clear, 0);
}

static void trigger_work_handler(void)
{
	zassert_not_null(mock_data.rpl_work_handler, "RPL work handler not set");
	zassert_not_null(mock_data.rpl_work, "RPL work not set");
	mock_data.rpl_work_handler(&mock_data.rpl_work->work);
}

struct rpl_val {
	uint32_t seq:24,
		 old_iv:1;
};

static struct rpl_val expected_rpl_data_pool[CONFIG_BT_MESH_CRPL][2];
static size_t expected_rpl_data_count;

static void expect_pair_write(uint16_t first_addr, uint32_t first_seq, bool first_old_iv,
			      uint16_t second_addr, uint32_t second_seq, bool second_old_iv)
{
	zms_id_t expected_id = PAIR_ID(first_addr, second_addr);
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

static void expect_pair_delete(uint16_t first_addr, uint16_t second_addr)
{
	zms_id_t expected_id = PAIR_ID(first_addr, second_addr);
	expect_zms_delete(expected_id);
}

static bool rpl_check(uint16_t addr, uint32_t seq, bool old_iv, uint8_t net_if,
		      bool local_match, bool bridge)
{
	struct bt_mesh_net_rx rx = {
		.net_if = net_if,
		.ctx.addr = addr,
		.seq = seq,
		.old_iv = old_iv,
		.local_match = local_match,
	};

	return bt_mesh_rpl_check(&rx, NULL, bridge);
}

static void rx(uint16_t addr, uint32_t seq, bool old_iv, bool should_reject)
{
	/*
	 * I had to debug a weird test bug because of this, helpful for the
	 * next person who makes the same mistake.
	 */
	zassert(BT_MESH_ADDR_IS_UNICAST(addr), "RX addr must be unicast");

	if (!should_reject) {
		expect_work_schedule();
	}

	zassert_equal(
		rpl_check(addr, seq, old_iv, BT_MESH_NET_IF_ADV, true, false),
		should_reject);
}

static void rx_ok(uint16_t addr, uint32_t seq)
{
	rx(addr, seq, false, false);
}

static void rx_expect_reject(uint16_t addr, uint32_t seq)
{
	rx(addr, seq, false, true);
}

static void rx_ok_old_iv(uint16_t addr, uint32_t seq)
{
	rx(addr, seq, true, false);
}

static void rx_expect_reject_old_iv(uint16_t addr, uint32_t seq)
{
	rx(addr, seq, true, true);
}

static void schedule_rx_mid_write(uint16_t addr, uint32_t seq)
{
	rx_mid_write.addr = addr;
	rx_mid_write.seq = seq;
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

/** Verify that the RPL correctly stores full pairs, i.e. that the write happens
 *  correctly when triggered when there is an even number of entries in the RPL.
 */
ZTEST(bt_mesh_rpl_ncs, test_full_pairs_stored)
{
	/* Add an even number of entries */
	rx_ok(1, 1);
	rx_ok_old_iv(2, 0xabba); /* Ensure old_iv bit stored properly. */
	rx_ok(0x42, 0xabcd);
	rx_ok(4, 0x123455);
	rx_ok(4, 0x123456); /* Updated twice, should only be written once. */

	/* Check that both pairs are written correctly. */
	expect_pair_write(1, 1, false, 2, 0xabba, true);
	expect_pair_write(0x42, 0xabcd, false, 4, 0x123456, false);
	trigger_work_handler();

	/* Check that extra write call does not re-write old pairs. */
	trigger_work_handler();
}

/** Verify that the RPL correctly stores half pairs, i.e. that the write happens
 *  correctly when there is an odd number of entries in the RPL.
 */
ZTEST(bt_mesh_rpl_ncs, test_half_pair_stored)
{
	/* Add entries to end up with an odd number of entries. */
	rx_ok(5, 0xabcdef);
	rx_ok(6, 0x42);
	rx_ok(0x1234, 0x101010);

	/* Check that half-pair is written correctly */
	expect_pair_write(5, 0xabcdef, false, 6, 0x42, false);
	expect_pair_write(0x1234, 0x101010, false, 0, 0, false);
	trigger_work_handler();
}

/** Verify that a half pair that is replaced with a full pair later is correctly
 *  deleted and replaced with the new full pair.
 */
ZTEST(bt_mesh_rpl_ncs, test_half_pair_deleted)
{
	/* Add entries to end up with an odd number of entries. */
	rx_ok(5, 0xabcdef);
	rx_ok(6, 0x42);
	rx_ok(0x1234, 0x101010);

	/* Store them. */
	expect_pair_write(5, 0xabcdef, false, 6, 0x42, false);
	expect_pair_write(0x1234, 0x101010, false, 0, 0, false);
	trigger_work_handler();

	/* Add several more entries to create an even number. */
	rx_ok(0x000f, 0x123);
	rx_ok(0x0009, 0x2);
	rx_ok(0x000a, 0x3);

	/* Check that the old half-pair is deleted, and new pairs are written */
	expect_pair_delete(0x1234, 0);
	expect_pair_write(0x1234, 0x101010, false, 0x000f, 0x123, false);
	expect_pair_write(0x0009, 0x2, false, 0x000a, 0x3, false);
	trigger_work_handler();

	/* Ensure that it is not re-deleted on next update. */
	rx_ok(0x1234, 0x808080);
	/* no expect_pair_delete! */
	expect_pair_write(0x1234, 0x808080, false, 0x000f, 0x123, false);
	trigger_work_handler();
}

/** Verify that no writing to ZMS happens when messages are rejected and the
 *  RPL is not updated.
 */
ZTEST(bt_mesh_rpl_ncs, test_no_write_on_replay)
{
	/* Add an entry and store it */
	rx_ok(0x1234, 10);
	expect_pair_write(0x1234, 10, false, 0, 0, false);
	trigger_work_handler();

	/* Perform replays and check that no write is scheduled. */
	rx_expect_reject(0x1234, 10);
	rx_expect_reject(0x1234, 9);
	trigger_work_handler(); /* No write expected */

	/* New sequence number, should not be rejected and should be stored */
	rx_ok(0x1234, 11);
	expect_pair_write(0x1234, 11, false, 0, 0, false);
	trigger_work_handler();
}

/** Verify that rejection and writing behavior is correct in cases involving old IVs. */
ZTEST(bt_mesh_rpl_ncs, test_old_iv_replay)
{
	/* Check that old IV entries are handled correctly. */
	rx_ok_old_iv(0x1234, 10);
	expect_pair_write(0x1234, 10, true, 0, 0, false);
	trigger_work_handler();
	rx_expect_reject_old_iv(0x1234, 10);
	rx_expect_reject_old_iv(0x1234, 9);
	trigger_work_handler(); /* Don't expect write */

	/* Check that messages with old_iv are rejected against new IV RPL entries. */
	rx_ok(0xabc, 10);
	expect_pair_delete(0x1234, 0); /* This action will fill the half pair. */
	expect_pair_write(0x1234, 10, true, 0xabc, 10, false);
	trigger_work_handler();
	rx_expect_reject_old_iv(0xabc, 11);
	trigger_work_handler(); /* Don't expect write */
}

/** Verify that a message with a lower seq but a newer IV is correctly accepted
 *  and stored in the RPL and ZMS.
 */
ZTEST(bt_mesh_rpl_ncs, test_iv_change_lower_seq)
{
	rx_ok_old_iv(0x42, 100);
	expect_pair_write(0x42, 100, true, 0, 0, false);
	trigger_work_handler();

	/* Ensure that a lower sequence number is stored when old_iv gets unset. */
	rx_ok(0x42, 50);
	expect_pair_write(0x42, 50, false, 0, 0, false);
	trigger_work_handler();
}

/** Verify that race conditions during write are handled correctly.
 *  In particular, check that we correctly handle the case where an RX event
 *  happens while we are executing the zms write.
 */
ZTEST(bt_mesh_rpl_ncs, test_race_update_during_write)
{
	rx_ok(0x1234, 10);

	/* Set up an update of the same entry to happen mid-write. */
	schedule_rx_mid_write(0x1234, 11);

	/* First store should complete as normal. */
	expect_pair_write(0x1234, 10, false, 0, 0, false);
	trigger_work_handler();

	/* Second store should store the mid-write value. */
	expect_pair_write(0x1234, 11, false, 0, 0, false);
	trigger_work_handler();
}

/** Verify various always reject/always accept scenarios. */
ZTEST(bt_mesh_rpl_ncs, test_special_rx_cases)
{
	/* Verify that the RPL ignores IF_LOCAL messages. */
	zassert_false(rpl_check(0x20, 10, false, BT_MESH_NET_IF_LOCAL, true, false));
	/* Don't expect work schedule. */
	trigger_work_handler(); /* Don't expect write on next work */
	zassert_false(rpl_check(0x20, 10, false, BT_MESH_NET_IF_LOCAL, true, false));

	/* Verify that the RPL ignores messages that are not a local match. */
	zassert_false(rpl_check(0x21, 15, false, BT_MESH_NET_IF_ADV, false, false));
	/* Don't expect work schedule. */
	trigger_work_handler(); /* Don't expect write on next work */
	zassert_false(rpl_check(0x21, 15, false, BT_MESH_NET_IF_ADV, false, false));

	/* ... except when bridge == true */
	expect_work_schedule();
	zassert_false(rpl_check(0x22, 20, false, BT_MESH_NET_IF_ADV, false, true));
	expect_work_schedule();
	/* Complete the pair to simplify remaining test. */
	zassert_false(rpl_check(0x23, 25, false, BT_MESH_NET_IF_ADV, false, true));
	expect_pair_write(0x22, 20, false, 0x23, 25, false);
	trigger_work_handler();

	/* Fill the rest of the RPL and verify that subsequent messages are rejected. */
	zassert_true(CONFIG_BT_MESH_CRPL % 2 == 0, "CRPL must be even for this test");
	zassert_true(CONFIG_BT_MESH_CRPL >= 4, "CRPL must be at least 4 for this test");
	for (int i = 0; i < CONFIG_BT_MESH_CRPL - 2; i++) {
		rx_ok(0x30 + i, 1);
		if (i % 2 == 0) {
			expect_pair_write(0x30 + i, 1, false, 0x30 + i + 1, 1, false);
		}
	}
	/* Next new src is always rejected because table is full. */
	rx_expect_reject(0x1, 1);
	trigger_work_handler(); /* Don't expect write of 0x1. */
	/* Existing src is accepted and stored even with full list. */
	rx_ok(0x30, 3);
	expect_pair_write(0x30, 3, false, 0x31, 1, false);
	trigger_work_handler();
}

/** Verify reset compacting behavior. */
ZTEST(bt_mesh_rpl_ncs, test_reset_compaction)
{
	/* ==== Set up RPL ==== */
	/* Two pairs, Should be re-paired on reset. */
	rx_ok(0x1, 1);
	rx_ok_old_iv(0x2, 2);
	rx_ok_old_iv(0x3, 3);
	rx_ok(0x4, 4);
	/* Pair which should turn into a half-pair upon reset. */
	rx_ok(0x5, 5);
	rx_ok_old_iv(0x6, 6);
	/* Trigger the write. */
	expect_pair_write(0x1, 1, false, 0x2, 2, true);
	expect_pair_write(0x3, 3, true, 0x4, 4, false);
	expect_pair_write(0x5, 5, false, 0x6, 6, true);
	trigger_work_handler();

	/* ==== Reset ==== */
	expect_work_schedule();
	bt_mesh_rpl_reset();

	/* ==== Verify correct compacted data in ZMS ==== */
	expect_zms_clear();
	expect_pair_write(0x1, 1, true, 0x4, 4, true); /* Re-paired from first two pairs. */
	expect_pair_write(0x5, 5, true, 0, 0, false); /* Half-pair remaining at the end. */
	trigger_work_handler();

	/* ==== Verify that RPL behaves correctly after reset ==== */
	/* Entries which survived */
	rx_expect_reject_old_iv(0x1, 1);
	rx_expect_reject_old_iv(0x4, 2);
	rx_expect_reject_old_iv(0x5, 5);
	/* Entries that should be gone and be re-added. */
	rx_ok_old_iv(0x2, 2);
	rx_ok_old_iv(0x3, 3);
	rx_ok_old_iv(0x6, 6);
	/* Ensure that list was compacted and can now fit new entries. */
	zassert_true(CONFIG_BT_MESH_CRPL == 8, "CRPL must be 8 for this test");
	rx_ok(0x7, 7);
	rx_ok(0x8, 8);
	/* List should be full now. */
	rx_expect_reject(0x9, 9);
}

ZTEST(bt_mesh_rpl_ncs, test_rpl_clear)
{
	/* Set up and store some RPL data. */
	rx_ok(0x1234, 10);
	rx_ok(0x5678, 20);
	rx_ok(0x1010, 30);
	expect_pair_write(0x1234, 10, false, 0x5678, 20, false);
	expect_pair_write(0x1010, 30, false, 0, 0, false);
	trigger_work_handler();

	/* Verify that clearing happens properly. */
	expect_work_schedule();
	bt_mesh_rpl_clear();
	expect_zms_clear();
	/* Don't expect any writes. */
	trigger_work_handler();
}

ZTEST_SUITE(bt_mesh_rpl_ncs, NULL, NULL, setup, NULL, NULL);
