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
#include "zephyr/logging/log.h"
LOG_MODULE_REGISTER(bt_mesh_rpl);

#include <mesh/net.h>
#include <mesh/rpl.h>

static struct bt_mesh_rpl replay_list[CONFIG_BT_MESH_CRPL];

#if defined(CONFIG_BT_MESH_RPL_STORAGE_MODE_EMDS)
#include <emds/emds.h>

EMDS_STATIC_ENTRY_DEFINE(rpl_store, CONFIG_BT_MESH_RPL_INDEX, replay_list, sizeof(replay_list));
#endif /* CONFIG_BT_MESH_RPL_STORAGE_MODE_EMDS */

#if defined(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)
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
	.sector_count = MAX(2U,
		PARTITION_SIZE(rpl_partition) / DT_PROP(DT_CHOSEN(zephyr_flash), erase_block_size)),
};

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
static struct k_work_delayable store_work;

static inline int rpl_pair_idx(struct bt_mesh_rpl *rpl)
{
	return (rpl - &replay_list[0]) / 2;
}

static void store_pair(int i)
{
	uint32_t pair_id = (replay_list[i * 2].src << 16) | (replay_list[(i * 2) + 1].src);
	struct rpl_val pair[2] = { 0 };

	if (replay_list[(i * 2) + 1].src == 0) {
		most_recent_half_pair_idx = i;
	}

	pair[0].seq = replay_list[i * 2].seq;
	pair[0].old_iv = replay_list[i * 2].old_iv;
	pair[1].seq = replay_list[(i * 2) + 1].seq;
	pair[1].old_iv = replay_list[(i * 2) + 1].old_iv;

	zms_write(&rpl_zms_fs, pair_id, pair, sizeof(pair));
}

static void remove_half_pair(int i)
{
	uint32_t pair_id = replay_list[i * 2].src << 16;

	zms_delete(&rpl_zms_fs, pair_id);
}

static int clear_storage(void) {
	int err;

	err = zms_clear(&rpl_zms_fs);
	if (err) {
		LOG_ERR("Failed to clear ZMS, err %d", err);
		return err;
	}

	/* zms_clear unmounts the filesystem, so we need to remount it */
	err = zms_mount(&rpl_zms_fs);
	if (err) {
		LOG_ERR("Failed to remount ZMS, err %d", err);
		return err;
	}

	most_recent_half_pair_idx = -1;
	return 0;
}

static void store_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (atomic_test_and_clear_bit(flags, FLAGS_CLEAR_PENDING)) {
		int err = clear_storage();

		if (err) {
			LOG_ERR("Failed to clear RPL ZMS storage, err %d", err);
			return;
		}
	}

	for (int i = 0; i < PAIR_COUNT; i++) {
		if (atomic_test_and_clear_bit(store_pairs, i)) {
			bool is_half_pair = replay_list[(i * 2) + 1].src == 0;

			if (most_recent_half_pair_idx == i && !is_half_pair) {
				remove_half_pair(i);
				most_recent_half_pair_idx = -1;
			}
			store_pair(i);
		}
	}
}

static void schedule_store_work(void)
{
	k_work_schedule(&store_work, K_SECONDS(CONFIG_BT_MESH_RPL_ZMS_STORE_TIMEOUT));
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

	if (IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)) {
		atomic_set_bit(store_pairs, rpl_pair_idx(rpl));
		schedule_store_work();
	}
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
	if (IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)) {
		atomic_clear(store_pairs);
		atomic_set_bit(flags, FLAGS_CLEAR_PENDING);
		schedule_store_work();
	}
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

	if (IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)) {
		atomic_clear(store_pairs);

		/* Schedule rewrite of every pair after clearing. */
		for (int i = 0; i < PAIR_COUNT; i++) {
			if (!replay_list[i * 2].src) {
				break;
			}
			atomic_set_bit(store_pairs, i);
		}

		atomic_set_bit(flags, FLAGS_CLEAR_PENDING);
		schedule_store_work();
	}
}

void bt_mesh_rpl_pending_store(uint16_t addr)
{}

void bt_mesh_rpl_pending_store_all_nodes(void)
{}

static int bt_mesh_rpl_init(void)
{
	if (IS_ENABLED(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)) {
		int err = zms_mount(&rpl_zms_fs);

		if (err) {
			LOG_ERR("Failed to mount ZMS for RPL storage, err %d", err);
			return err;
		}

		k_work_init_delayable(&store_work, store_work_handler);

		/* TODO: This is where the reload should probably happen. */
	}

	return 0;
}

/*
 * TODO: Figure out if this is the best way of doing this.
 * The reason why I did it this way is that we cannot easilly add a new _init
 * call to the mesh main.c, since this lives upstream and there is no existing
 * rpl_init we can hook into.
 *
 * Some drawbacks with this is that it complicates the testing a bit, since this
 * will be called before the test suite starts running, and it might be a bit
 * difficult to test the RPL restore situation because of this. We can
 * for instance only check this _once_ since the test cannot call the init
 * again.
 *
 * If we decide to keep the SYS_INIT, we need to double-check that the priority
 * is correct here. The current APPLICATION_INIT priority was picked just to
 * get something that works while I'm developing.
 */
#if defined(CONFIG_BT_MESH_RPL_STORAGE_MODE_ZMS)
SYS_INIT(bt_mesh_rpl_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif
