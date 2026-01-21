/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Light Lightness Control State Machine internal API
 */

#ifndef LIGHT_CTRL_SM_H__
#define LIGHT_CTRL_SM_H__

#include <zephyr/smf.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL_HIDDEN */
enum bt_mesh_light_ctrl_sm_evt {
	BT_MESH_LIGHT_CTRL_SM_EVT_MODE_ON,
	BT_MESH_LIGHT_CTRL_SM_EVT_MODE_OFF,
	BT_MESH_LIGHT_CTRL_SM_EVT_OCCUPANCY_ON,
	BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_ON,
	BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_OFF,
	BT_MESH_LIGHT_CTRL_SM_EVT_TIMER_OFF
};

enum bt_mesh_light_ctrl_sm_output_level {
	BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_STANDBY,
	BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_ON,
	BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_PROLONG,
	BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_COUNT,
};

struct bt_mesh_light_ctrl_sm_output {
	uint16_t lightness;
	uint32_t centilux;
};

struct bt_mesh_light_ctrl_sm_status {
	const struct bt_mesh_light_ctrl_sm_output *target_output;
	bool present_onoff;
	bool target_onoff;
	uint32_t transition_time;
	bool is_fade;
};

struct bt_mesh_light_ctrl_sm_onoff_status {
	bool present_onoff;
	bool target_onoff;
	uint32_t remaining_time;
	bool is_fade;
};

struct bt_mesh_light_ctrl_sm;

struct bt_mesh_light_ctrl_sm_cb {
	/* Called whenever the OnOff state should be published */
	void (*publish_onoff)(struct bt_mesh_light_ctrl_sm *sm,
			      const struct bt_mesh_light_ctrl_sm_onoff_status *status);
	/* Called whenever a transition starts or ends because of the internal timer. */
	void (*output_changed)(struct bt_mesh_light_ctrl_sm *sm,
			       const struct bt_mesh_light_ctrl_sm_status *status);
};

struct bt_mesh_light_ctrl_sm {
	struct smf_ctx smf_ctx;
	struct {
		enum bt_mesh_light_ctrl_sm_evt type;
		uint32_t *transition_time;
	} evt;
	struct k_work_delayable timer;
	struct bt_mesh_light_ctrl_sm_cb *cb;
	struct {
		struct bt_mesh_light_ctrl_sm_output initial_output;
		struct bt_mesh_light_ctrl_sm_status status;
	} output;

	/* Config fields to be set from server model. */
	struct {
		bool occupancy_mode;
		uint32_t time_fade_on;
		uint32_t time_run_on;
		uint32_t time_fade;
		uint32_t time_prolong;
		uint32_t time_fade_standby_auto;
		uint32_t time_fade_standby_manual;
		struct bt_mesh_light_ctrl_sm_output output_levels[
			BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_COUNT];
	} cfg;
};

/* @endcond */

#ifdef __cplusplus
}
#endif

#endif /* LIGHT_CTRL_SM_H__ */
