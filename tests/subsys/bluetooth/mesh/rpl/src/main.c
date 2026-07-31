/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kvss/zms.h>
#include <zephyr/kernel.h>
#include <bluetooth/mesh/rpl.h>

#include "net.h"
#include "rpl.h"
#include "settings.h"

struct rpl_val {
	uint32_t seq:24,
		 old_iv:1;
};

typedef struct rpl_val pair_t[2];

/* ======================== Forward declarations ============================ */
static void rx_ok(uint16_t addr, uint32_t seq);

/* ======================== Mocks =========================================== */
static struct {
	struct zms_fs *zms_fs;
} global_mock_data;

#define MOCK_DATA_POOL_SIZE 16

static struct {
	pair_t pool[MOCK_DATA_POOL_SIZE];
	size_t count;
} mock_write_data;

/* Used to simulate a concurrent update mid-write. */
static struct {
	uint16_t addr;
	uint32_t seq;
} rx_mid_write;

int zms_mount(struct zms_fs *fs)
{
	zassert_is_null(global_mock_data.zms_fs, "zms_mount called twice");
	global_mock_data.zms_fs = fs;
	return ztest_get_return_value();
}

int zms_clear(struct zms_fs *fs)
{
	zassert_equal(fs, global_mock_data.zms_fs);
	global_mock_data.zms_fs = NULL;

	return ztest_get_return_value();
}

ssize_t zms_write(struct zms_fs *fs, zms_id_t id, const void *data, size_t len)
{
	zassert_equal(fs, global_mock_data.zms_fs);
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
	zassert_equal(fs, global_mock_data.zms_fs);
	ztest_check_expected_value(id);

	return ztest_get_return_value();
}

void bt_mesh_settings_store_schedule(enum bt_mesh_settings_flag flag)
{
	ztest_check_expected_value(flag);
}

/* ======================== End Mocks ======================================= */

#define PAIR_ID(first_addr, second_addr) (((uint32_t)(first_addr) << 16) | (second_addr))

static void expect_store_schedule(void)
{
	ztest_expect_value(bt_mesh_settings_store_schedule, flag, BT_MESH_SETTINGS_RPL_PENDING);
}

static void expect_zms_delete(zms_id_t expected_id)
{
	ztest_expect_value(zms_delete, id, expected_id);
	ztest_returns_value(zms_delete, 0);
}

static void expect_zms_clear(void)
{
	ztest_returns_value(zms_clear, 0);
}

static void expect_zms_mount(int err)
{
	ztest_returns_value(zms_mount, err);
}

/* Mirrors what the real mesh settings work handler does for a pending RPL
 * store (settings.c dispatches BT_MESH_SETTINGS_RPL_PENDING here).
 */
static void trigger_store(void)
{
	bt_mesh_rpl_pending_store_all_nodes();
}

static void expect_pair_write(uint16_t first_addr, uint32_t first_seq, bool first_old_iv,
			      uint16_t second_addr, uint32_t second_seq, bool second_old_iv)
{
	zms_id_t expected_id = PAIR_ID(first_addr, second_addr);

	zassert(mock_write_data.count < ARRAY_SIZE(mock_write_data.pool),
		"Can't fit mock zms write data");
	struct rpl_val *expected_data = mock_write_data.pool[mock_write_data.count++];

	expected_data[0] = (struct rpl_val) {
		.seq = first_seq,
		.old_iv = first_old_iv,
	};
	expected_data[1] = (struct rpl_val) {
		.seq = second_seq,
		.old_iv = second_old_iv,
	};

	ztest_expect_value(zms_write, id, expected_id);
	ztest_expect_data(zms_write, data, expected_data);
	ztest_expect_value(zms_write, len, sizeof(pair_t));
	ztest_returns_value(zms_write, sizeof(pair_t));
}

static void expect_half_pair_delete(uint16_t first_addr)
{
	zms_id_t expected_id = PAIR_ID(first_addr, 0);

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
	/* Avoid test authors accidentally testing RPL with group addresses. */
	zassert(BT_MESH_ADDR_IS_UNICAST(addr), "RX addr must be unicast");

	if (!should_reject) {
		expect_store_schedule();
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

static void clear_rpl(void)
{
	expect_store_schedule();
	bt_mesh_rpl_clear();
	expect_zms_clear();
	expect_zms_mount(0);
	trigger_store();
}

static void reset_mock_data(void)
{
	mock_write_data.count = 0;
	rx_mid_write.addr = 0;
	rx_mid_write.seq = 0;
}

static void *setup(void)
{
	expect_zms_mount(0);

	zassert_ok(bt_mesh_rpl_init());

	return NULL;
}

static void after(void *f)
{
	ARG_UNUSED(f);

	clear_rpl();
	reset_mock_data();
}

/** Verify that the RPL correctly stores full pairs (an even number of entries):
 *  Adds four entries in the RPL, checks that both pairs are written, and that
 *  an entry updated multiple times before a store is only written once, and a
 *  store with no new changes writes nothing.
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
	trigger_store();

	/* Check that extra write call does not re-write old pairs. */
	trigger_store();
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
	trigger_store();
}

/** Verify that a stored half pair that later becomes a full pair has its
 *  half-pair ZMS entry deleted and is rewritten as a full pair, and that the
 *  delete is not repeated on subsequent updates to that pair.
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
	trigger_store();

	/* Add several more entries to create an even number. */
	rx_ok(0x000f, 0x123);
	rx_ok(0x0009, 0x2);
	rx_ok(0x000a, 0x3);

	/* Check that the old half-pair is deleted, and new pairs are written */
	expect_half_pair_delete(0x1234);
	expect_pair_write(0x1234, 0x101010, false, 0x000f, 0x123, false);
	expect_pair_write(0x0009, 0x2, false, 0x000a, 0x3, false);
	trigger_store();

	/* Ensure that it is not re-deleted on next update. */
	rx_ok(0x1234, 0x808080);
	/* no expect_half_pair_delete! */
	expect_pair_write(0x1234, 0x808080, false, 0x000f, 0x123, false);
	trigger_store();
}

/** Verify that no writing to ZMS happens when messages are rejected and the
 *  RPL is not updated.
 */
ZTEST(bt_mesh_rpl_ncs, test_no_write_on_replay)
{
	/* Add an entry and store it */
	rx_ok(0x1234, 10);
	expect_pair_write(0x1234, 10, false, 0, 0, false);
	trigger_store();

	/* Perform replays and check that no write is scheduled. */
	rx_expect_reject(0x1234, 10);
	rx_expect_reject(0x1234, 9);
	trigger_store(); /* No write expected */

	/* New sequence number, should not be rejected and should be stored */
	rx_ok(0x1234, 11);
	expect_pair_write(0x1234, 11, false, 0, 0, false);
	trigger_store();
}

/** Verify old-IV handling: old-IV entries are stored, replays at or below a
 *  stored old-IV seq are rejected, and an old-IV message is rejected against a
 *  newer-IV RPL entry for the same source.
 */
ZTEST(bt_mesh_rpl_ncs, test_old_iv_replay)
{
	/* Check that old IV entries are handled correctly. */
	rx_ok_old_iv(0x1234, 10);
	expect_pair_write(0x1234, 10, true, 0, 0, false);
	trigger_store();
	rx_expect_reject_old_iv(0x1234, 10);
	rx_expect_reject_old_iv(0x1234, 9);
	trigger_store(); /* Don't expect write */

	/* Check that messages with old_iv are rejected against new IV RPL entries. */
	rx_ok(0xabc, 10);
	expect_half_pair_delete(0x1234); /* This action will fill the half pair. */
	expect_pair_write(0x1234, 10, true, 0xabc, 10, false);
	trigger_store();
	rx_expect_reject_old_iv(0xabc, 11);
	trigger_store(); /* Don't expect write */
}

/** Verify that a message with a lower seq but a newer IV is correctly accepted
 *  and stored in the RPL and ZMS.
 */
ZTEST(bt_mesh_rpl_ncs, test_iv_change_lower_seq)
{
	rx_ok_old_iv(0x42, 100);
	expect_pair_write(0x42, 100, true, 0, 0, false);
	trigger_store();

	/* Ensure that a lower sequence number is stored when old_iv gets unset. */
	rx_ok(0x42, 50);
	expect_pair_write(0x42, 50, false, 0, 0, false);
	trigger_store();
}

/** Verify that concurrent updates during write are handled correctly.
 *  In particular, check that we correctly handle the case where an RX event
 *  happens while we are executing the zms write.
 */
ZTEST(bt_mesh_rpl_ncs, test_concurrent_update_during_write)
{
	rx_ok(0x1234, 10);

	/* Set up an update of the same entry to happen mid-write. */
	schedule_rx_mid_write(0x1234, 11);

	/* First store should complete as normal. */
	expect_pair_write(0x1234, 10, false, 0, 0, false);
	trigger_store();

	/* Second store should store the mid-write value. */
	expect_pair_write(0x1234, 11, false, 0, 0, false);
	trigger_store();
}

/** Verify that the RPL ignores IF_LOCAL messages. */
ZTEST(bt_mesh_rpl_ncs, test_rx_ignores_local_messages)
{
	zassert_false(rpl_check(0x20, 10, false, BT_MESH_NET_IF_LOCAL, true, false));
	/* Don't expect a store schedule. */
	trigger_store(); /* Don't expect write on next store */
	zassert_false(rpl_check(0x20, 10, false, BT_MESH_NET_IF_LOCAL, true, false));
}

/** Verify that the RPL ignores messages that are not a local match, except
 *  when bridge == true.
 */
ZTEST(bt_mesh_rpl_ncs, test_rx_ignores_non_local_match_unless_bridge)
{
	zassert_false(rpl_check(0x21, 15, false, BT_MESH_NET_IF_ADV, false, false));
	/* Don't expect a store schedule. */
	trigger_store(); /* Don't expect write on next store */
	zassert_false(rpl_check(0x21, 15, false, BT_MESH_NET_IF_ADV, false, false));

	/* ... except when bridge == true */
	expect_store_schedule();
	zassert_false(rpl_check(0x22, 20, false, BT_MESH_NET_IF_ADV, false, true));
	expect_pair_write(0x22, 20, false, 0, 0, false);
	trigger_store();
}

/** Verify that once the RPL is full, messages from new sources are rejected,
 *  while an existing source is still accepted and stored.
 */
ZTEST(bt_mesh_rpl_ncs, test_rpl_full_rejects_new_src)
{
	zassert_true(CONFIG_BT_MESH_CRPL % 2 == 0, "CRPL must be even for this test");
	zassert_true(CONFIG_BT_MESH_CRPL >= 2, "CRPL must be at least 2 for this test");
	for (int i = 0; i < CONFIG_BT_MESH_CRPL; i++) {
		rx_ok(0x30 + i, 1);
		if (i % 2 == 0) {
			expect_pair_write(0x30 + i, 1, false, 0x30 + i + 1, 1, false);
		}
	}
	/* Next new src is always rejected because table is full. */
	rx_expect_reject(0x1, 1);
	trigger_store(); /* Don't expect write of 0x1. */
	/* Existing src is accepted and stored even with full list. */
	rx_ok(0x30, 3);
	expect_pair_write(0x30, 3, false, 0x31, 1, false);
	trigger_store();
}

/** Verify RPL reset behavior: bt_mesh_rpl_reset() discards old-IV entries,
 *  marks the surviving entries as old-IV, and compacts them; ZMS is then
 *  cleared and the compacted set is rewritten (re-paired, with a trailing
 *  half pair as needed), and the reset frees space so that new entries can be
 *  added afterwards.
 */
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
	trigger_store();

	/* ==== Reset ==== */
	expect_store_schedule();
	bt_mesh_rpl_reset();

	/* ==== Verify correct compacted data in ZMS ==== */
	expect_zms_clear();
	expect_zms_mount(0);
	expect_pair_write(0x1, 1, true, 0x4, 4, true); /* Re-paired from first two pairs. */
	expect_pair_write(0x5, 5, true, 0, 0, false); /* Half-pair remaining at the end. */
	trigger_store();

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

/** Verify that the RPL is cleared correctly when bt_mesh_rpl_clear() is called.
 */
ZTEST(bt_mesh_rpl_ncs, test_rpl_clear)
{
	/* Set up and store some RPL data. */
	rx_ok(0x1234, 10);
	rx_ok(0x5678, 20);
	rx_ok(0x1010, 30);
	expect_pair_write(0x1234, 10, false, 0x5678, 20, false);
	expect_pair_write(0x1010, 30, false, 0, 0, false);
	trigger_store();

	/* Verify that clearing happens properly. */
	expect_store_schedule();
	bt_mesh_rpl_clear();
	/* Ensure that entries are no longer in the RPL. */
	rx_ok(0x1234, 1);
	rx_ok(0x5678, 1);
	rx_ok(0x1010, 1);
	/* Check that ZMS is cleared on next work handler trigger. */
	expect_zms_clear();
	expect_zms_mount(0);
	expect_pair_write(0x1234, 1, false, 0x5678, 1, false);
	expect_pair_write(0x1010, 1, false, 0, 0, false);
	trigger_store();
}

ZTEST_SUITE(bt_mesh_rpl_ncs, NULL, setup, NULL, after, NULL);
