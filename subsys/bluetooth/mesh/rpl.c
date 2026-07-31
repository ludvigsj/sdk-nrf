/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <zephyr/bluetooth/mesh.h>

#define LOG_LEVEL CONFIG_BT_MESH_RPL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_mesh_rpl);

#include <mesh/net.h>
#include <mesh/rpl.h>
#include <bluetooth/mesh/rpl.h>

/* This is a Zephyr Mesh internal header, not a public header. There is no
 * public API to schedule a settings store, so we use a path relative to
 * `zephyr/include` and escape up one directory to get to the root of the
 * Zephyr project.
 */
#include "../subsys/bluetooth/mesh/settings.h"

static struct bt_mesh_rpl replay_list[CONFIG_BT_MESH_CRPL];

#if IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_EMDS)
#include <emds/emds.h>

EMDS_STATIC_ENTRY_DEFINE(rpl_store, CONFIG_BT_MESH_RPL_INDEX, replay_list, sizeof(replay_list));
#endif /* CONFIG_BT_MESH_RPL_STORAGE_MODE_EMDS */

#if IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)
#include <zephyr/kvss/zms.h>
#include <zephyr/storage/flash_map.h>

#if !PARTITION_EXISTS(rpl_partition)
#error "CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS requires a dedicated rpl_partition"
#endif

#define PAIR_COUNT DIV_ROUND_UP(CONFIG_BT_MESH_CRPL, 2)

static struct zms_fs rpl_zms_fs = {
	.flash_device = PARTITION_DEVICE(rpl_partition),
	.offset = PARTITION_OFFSET(rpl_partition),
	.sector_size = DT_PROP(DT_CHOSEN(zephyr_flash), erase_block_size),
	.sector_count = PARTITION_SIZE(rpl_partition) /
			DT_PROP(DT_CHOSEN(zephyr_flash), erase_block_size),
};

BUILD_ASSERT(PARTITION_SIZE(rpl_partition) >=
	     (2U * DT_PROP(DT_CHOSEN(zephyr_flash), erase_block_size)),
	     "rpl_partition must be at least 2 erase blocks in size when using ZMS storage mode");

struct rpl_val {
	uint32_t seq:24,
	      old_iv:1;
};

enum {
	FLAGS_CLEAR_PENDING = 0,
	FLAGS_COUNT,
};
static ATOMIC_DEFINE(flags, FLAGS_COUNT);

static ATOMIC_DEFINE(store_pairs, PAIR_COUNT);
static int most_recent_half_pair_idx = -1;

static uint32_t rpl_zms_id(uint16_t src1, uint16_t src2)
{
	return ((uint32_t)src1 << 16) | src2;
}

/* RPL entries are divided into pairs of sequential (even, odd) RPL entries. */
static int pair_idx_of(const struct bt_mesh_rpl *rpl)
{
	return ARRAY_INDEX(replay_list, rpl) / 2;
}

static const struct bt_mesh_rpl *pair_first(int pair_idx)
{
	return &replay_list[pair_idx * 2];
}

/* Returns the second entry of a pair, or NULL when CONFIG_BT_MESH_CRPL is odd
 * and this is the trailing pair, which has no second slot.
 */
static const struct bt_mesh_rpl *pair_second(int pair_idx)
{
	int idx = pair_idx * 2 + 1;

	if (idx >= ARRAY_SIZE(replay_list)) {
		return NULL;
	}

	return &replay_list[idx];
}

static void store_pair(int pair_idx)
{
	const struct bt_mesh_rpl *first = pair_first(pair_idx);
	const struct bt_mesh_rpl *second = pair_second(pair_idx);
	uint32_t zms_id = rpl_zms_id(first->src, second == NULL ? 0 : second->src);
	struct rpl_val pair[2] = { 0 };
	ssize_t rc;

	if (second == NULL || second->src == 0) {
		most_recent_half_pair_idx = pair_idx;
	}

	pair[0].seq = first->seq;
	pair[0].old_iv = first->old_iv;

	if (second != NULL) {
		pair[1].seq = second->seq;
		pair[1].old_iv = second->old_iv;
	}

	rc = zms_write(&rpl_zms_fs, zms_id, pair, sizeof(pair));

	if (rc != sizeof(pair)) {
		LOG_ERR("Failed to write ZMS entry for id 0x%08x, rc %d", zms_id, rc);
	}
}

static void delete_half_pair(int pair_idx)
{
	uint32_t zms_id = rpl_zms_id(pair_first(pair_idx)->src, 0);

	int err = zms_delete(&rpl_zms_fs, zms_id);

	if (err) {
		LOG_ERR("Failed to delete half pair, err %d", err);
	}
}

static void clear_storage(void)
{
	int err;

	err = zms_clear(&rpl_zms_fs);
	if (err) {
		LOG_ERR("Failed to clear ZMS, err %d", err);
		return;
	}

	/* zms_clear unmounts the filesystem, so we need to remount it */
	err = zms_mount(&rpl_zms_fs);
	if (err) {
		LOG_ERR("Failed to remount ZMS, err %d", err);
	}

	most_recent_half_pair_idx = -1;
}

#endif /* CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS */

void bt_mesh_rpl_update(struct bt_mesh_rpl *rpl,
		struct bt_mesh_net_rx *rx)
{
	/* If this is the first message on the new IV index, we should reset it
	 * to zero to avoid invalid combinations of IV index and seg.
	 */
	if (rpl->old_iv && !rx->old_iv) {
		rpl->seg = 0;
	}

	rpl->src = rx->ctx.addr;
	rpl->seq = rx->seq;
	rpl->old_iv = rx->old_iv;

#if IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)
	atomic_set_bit(store_pairs, pair_idx_of(rpl));
	bt_mesh_settings_store_schedule(BT_MESH_SETTINGS_RPL_PENDING);
#endif /* CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS */
}

/* Check the Replay Protection List for a replay attempt. If non-NULL match
 * parameter is given the RPL slot is returned but it is not immediately
 * updated (needed for segmented messages), whereas if a NULL match is given
 * the RPL is immediately updated (used for unsegmented messages).
 */
bool bt_mesh_rpl_check(struct bt_mesh_net_rx *rx,
		struct bt_mesh_rpl **match, bool bridge)
{
	int i;

	/* Don't bother checking messages from ourselves */
	if (rx->net_if == BT_MESH_NET_IF_LOCAL) {
		return false;
	}

	/* The RPL is used only for the local node or Subnet Bridge. */
	if (!rx->local_match && !bridge) {
		return false;
	}

	for (i = 0; i < ARRAY_SIZE(replay_list); i++) {
		struct bt_mesh_rpl *rpl = &replay_list[i];

		/* Empty slot */
		if (!rpl->src) {
			if (match) {
				*match = rpl;
			} else {
				bt_mesh_rpl_update(rpl, rx);
			}

			return false;
		}

		/* Existing slot for given address */
		if (rpl->src == rx->ctx.addr) {
			if (rx->old_iv && !rpl->old_iv) {
				return true;
			}

			if ((!rx->old_iv && rpl->old_iv) ||
			    rpl->seq < rx->seq) {
				if (match) {
					*match = rpl;
				} else {
					bt_mesh_rpl_update(rpl, rx);
				}

				return false;
			} else {
				return true;
			}
		}
	}

	LOG_ERR("RPL is full!");
	return true;
}

void bt_mesh_rpl_clear(void)
{
	(void)memset(replay_list, 0, sizeof(replay_list));
#if IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)
	for (int i = 0; i < PAIR_COUNT; i++) {
		atomic_clear_bit(store_pairs, i);
	}
	atomic_set_bit(flags, FLAGS_CLEAR_PENDING);
	bt_mesh_settings_store_schedule(BT_MESH_SETTINGS_RPL_PENDING);
#endif /* CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS */
}

void bt_mesh_rpl_reset(void)
{
	int shift = 0;
	int last = 0;

	/* Discard "old" IV Index entries from RPL and flag
	 * any other ones (which are valid) as old.
	 */
	for (int i = 0; i < ARRAY_SIZE(replay_list); i++) {
		struct bt_mesh_rpl *rpl = &replay_list[i];

		if (rpl->src) {
			if (rpl->old_iv) {
				(void)memset(rpl, 0, sizeof(*rpl));

				shift++;
			} else {
				rpl->old_iv = true;

				if (shift > 0) {
					replay_list[i - shift] = *rpl;
				}
			}

			last = i;
		}
	}

	(void) memset(&replay_list[last - shift + 1], 0, sizeof(struct bt_mesh_rpl) * shift);

#if IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)
	for (int i = 0; i < PAIR_COUNT; i++) {
		atomic_clear_bit(store_pairs, i);
	}

	/* Schedule rewrite of every pair after clearing. */
	for (int i = 0; i < PAIR_COUNT; i++) {
		if (!pair_first(i)->src) {
			break;
		}
		atomic_set_bit(store_pairs, i);
	}

	atomic_set_bit(flags, FLAGS_CLEAR_PENDING);
	bt_mesh_settings_store_schedule(BT_MESH_SETTINGS_RPL_PENDING);
#endif /* CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS */
}

void bt_mesh_rpl_pending_store(uint16_t addr)
{
#if IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)
	bool store_all_pending = (addr == BT_MESH_ADDR_ALL_NODES);

	if (atomic_test_and_clear_bit(flags, FLAGS_CLEAR_PENDING)) {
		clear_storage();
		/*
		 * Need to always store all pairs after a clear, even if a
		 * specific address was requested.
		 */
		store_all_pending = true;
	}

	for (int i = 0; i < PAIR_COUNT; i++) {
		const struct bt_mesh_rpl *first = pair_first(i);
		const struct bt_mesh_rpl *second = pair_second(i);

		if (!(store_all_pending ||
		      first->src == addr ||
		      (second != NULL && second->src == addr))) {
			continue;
		}
		if (atomic_test_and_clear_bit(store_pairs, i)) {
			store_pair(i);

			bool is_half_pair = (second == NULL) || (second->src == 0);

			if (most_recent_half_pair_idx == i && !is_half_pair) {
				delete_half_pair(i);
				most_recent_half_pair_idx = -1;
			}
		}
	}
#endif /* CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS */
}

void bt_mesh_rpl_pending_store_all_nodes(void)
{
	bt_mesh_rpl_pending_store(BT_MESH_ADDR_ALL_NODES);
}

int bt_mesh_rpl_init(void)
{
#if IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)
	int err = zms_mount(&rpl_zms_fs);

	if (err) {
		LOG_ERR("Failed to mount ZMS for RPL storage, err %d", err);
		return -EACCES;
	}

	/* TODO: This is where the reload should happen. */
#endif /* CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS */

	return 0;
}
