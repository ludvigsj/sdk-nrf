/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Light Lightness Control State Machine internal API
 */

#ifndef LIGHT_CTRL_SM_INTERNAL_H__
#define LIGHT_CTRL_SM_INTERNAL_H__

#include <zephyr/smf.h>
#include <bluetooth/mesh/light_ctrl_sm.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum bt_mesh_light_ctrl_sm_state {
	BT_MESH_LIGHT_CTRL_SM_STATE_OFF,
	BT_MESH_LIGHT_CTRL_SM_STATE_STANDBY,
	BT_MESH_LIGHT_CTRL_SM_STATE_FADE_ON,
	BT_MESH_LIGHT_CTRL_SM_STATE_RUN,
	BT_MESH_LIGHT_CTRL_SM_STATE_FADE,
	BT_MESH_LIGHT_CTRL_SM_STATE_PROLONG,
	BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_AUTO,
	BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_MANUAL,
};

void bt_mesh_light_ctrl_sm_light_onoff_set(struct bt_mesh_light_ctrl_sm *sm, bool onoff,
					   uint32_t *transition_time,
					   struct bt_mesh_light_ctrl_sm_status *status);

void bt_mesh_light_ctrl_sm_light_onoff_get(const struct bt_mesh_light_ctrl_sm *sm,
					   struct bt_mesh_light_ctrl_sm_onoff_status *status);

void bt_mesh_light_ctrl_sm_mode_set(struct bt_mesh_light_ctrl_sm *sm, bool mode);

void bt_mesh_light_ctrl_sm_occupancy_on(struct bt_mesh_light_ctrl_sm *sm);

void bt_mesh_light_ctrl_sm_output_get(const struct bt_mesh_light_ctrl_sm *sm,
				      struct bt_mesh_light_ctrl_sm_output *output);

void bt_mesh_light_ctrl_sm_output_lightness_set(struct bt_mesh_light_ctrl_sm *sm,
						enum bt_mesh_light_ctrl_sm_output_level level,
						uint16_t lightness);

void bt_mesh_light_ctrl_sm_output_centilux_set(struct bt_mesh_light_ctrl_sm *sm,
					       enum bt_mesh_light_ctrl_sm_output_level level,
					       uint32_t centilux);

/**
 * @brief Initialize the Light Lightness Control State Machine.
 *
 * @param sm Pointer to a state machine instance.
 * @param initial_state Initial state of the state machine.
 * @param output_changed_cb Callback function invoked whenever the output
 * changes because of the internal timer. NOT called for external events.
 */
int bt_mesh_light_ctrl_sm_init(struct bt_mesh_light_ctrl_sm *sm,
			       enum bt_mesh_light_ctrl_sm_state initial_state,
			       struct bt_mesh_light_ctrl_sm_cb *cb);
#ifdef __cplusplus
}
#endif

#endif /* LIGHT_CTRL_SM_INTERNAL_H__ */
