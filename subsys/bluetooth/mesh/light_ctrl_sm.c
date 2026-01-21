/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdlib.h>
#include <zephyr/smf.h>
#include <bluetooth/mesh/light_ctrl_sm.h>
#include "light_ctrl_sm_internal.h"

static const struct smf_state states[];

static void output_changed(struct bt_mesh_light_ctrl_sm *sm)
{
	if (sm->cb && sm->cb->output_changed) {
		sm->cb->output_changed(sm, &sm->output.status);
	}
}

static void publish_onoff(struct bt_mesh_light_ctrl_sm *sm)
{
	if (!sm->cb || !sm->cb->publish_onoff) {
		return;
	}

	struct bt_mesh_light_ctrl_sm_onoff_status status;

	status.present_onoff = sm->output.status.present_onoff;
	status.target_onoff = sm->output.status.target_onoff;
	status.is_fade = sm->output.status.is_fade;
	status.remaining_time = sm->output.status.transition_time;

	sm->cb->publish_onoff(sm, &status);
}

static void event_process(struct bt_mesh_light_ctrl_sm *sm, enum bt_mesh_light_ctrl_sm_evt evt,
			  uint32_t *transition_time)
{
	sm->evt.type = evt;
	sm->evt.transition_time = transition_time;

	smf_run_state(SMF_CTX(sm));
}

static void output_fade(struct bt_mesh_light_ctrl_sm *sm,
			const struct bt_mesh_light_ctrl_sm_output *target_output,
			bool present_onoff, bool target_onoff, uint32_t transition_time)
{
	struct bt_mesh_light_ctrl_sm_output current_output;

	bt_mesh_light_ctrl_sm_output_get(sm, &current_output);
	sm->output.initial_output = current_output;

	sm->output.status.target_output = target_output;
	sm->output.status.present_onoff = present_onoff;
	sm->output.status.target_onoff = target_onoff;
	sm->output.status.transition_time = transition_time;
	sm->output.status.is_fade = true;

	output_changed(sm);
}

static void output_set(struct bt_mesh_light_ctrl_sm *sm,
		       const struct bt_mesh_light_ctrl_sm_output *output, bool onoff)
{
	sm->output.initial_output = *output;

	sm->output.status.target_output = output;
	sm->output.status.present_onoff = onoff;
	sm->output.status.target_onoff = onoff;
	sm->output.status.transition_time = 0;
	sm->output.status.is_fade = false;

	output_changed(sm);
}

static uint32_t remaining_time_get(const struct bt_mesh_light_ctrl_sm *sm)
{
	return k_ticks_to_ms_ceil32(k_work_delayable_remaining_get(&sm->timer));
}

static void timer_set(struct bt_mesh_light_ctrl_sm *sm, uint32_t time_ms)
{
	k_work_reschedule(&sm->timer, K_MSEC(time_ms));
}

static void timer_cancel(struct bt_mesh_light_ctrl_sm *sm)
{
	k_work_cancel_delayable(&sm->timer);
}

static void timeout(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct bt_mesh_light_ctrl_sm *sm =
		CONTAINER_OF(dwork, struct bt_mesh_light_ctrl_sm, timer);

	event_process(sm, BT_MESH_LIGHT_CTRL_SM_EVT_TIMER_OFF, NULL);
	output_changed(sm);
}

/* ========================================================================== */
/* State machine definition:                                                  */
/* ========================================================================== */

static void state_off_entry(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	timer_cancel(sm);
	output_set(sm, NULL, false);
}

static void state_standby_entry(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	timer_cancel(sm);
	output_set(sm, &sm->cfg.output_levels[BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_STANDBY], false);
}

static void state_fade_on_entry(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;
	uint32_t transition_time = sm->evt.transition_time == NULL ?
		sm->cfg.time_fade_on : *(sm->evt.transition_time);

	output_fade(sm, &sm->cfg.output_levels[BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_ON], true, true,
		    transition_time);
	timer_set(sm, transition_time);
}

static void state_run_entry(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	output_set(sm, &sm->cfg.output_levels[BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_ON], true);
	timer_set(sm, sm->cfg.time_run_on);
}

static void state_fade_entry(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	output_fade(sm, &sm->cfg.output_levels[BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_PROLONG], true,
		    true, sm->cfg.time_fade);
	timer_set(sm, sm->cfg.time_fade);
}

static void state_prolong_entry(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	output_set(sm, &sm->cfg.output_levels[BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_PROLONG], true);
	timer_set(sm, sm->cfg.time_prolong);
}

static void state_fade_standby_auto_entry(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	output_fade(sm, &sm->cfg.output_levels[BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_STANDBY], true,
		    false, sm->cfg.time_fade_standby_auto);
	timer_set(sm, sm->cfg.time_fade_standby_auto);
}

static void state_fade_standby_manual_entry(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;
	uint32_t transition_time = sm->evt.transition_time == NULL ?
		sm->cfg.time_fade_standby_manual : *(sm->evt.transition_time);

	output_fade(sm, &sm->cfg.output_levels[BT_MESH_LIGHT_CTRL_SM_OUTPUT_LEVEL_STANDBY], true,
		    false, transition_time);
	timer_set(sm, transition_time);
}

static enum smf_state_result state_off_run(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	if (sm->evt.type == BT_MESH_LIGHT_CTRL_SM_EVT_MODE_ON) {
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_STANDBY]);
	}

	return SMF_EVENT_HANDLED;
}

static enum smf_state_result state_standby_run(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	switch (sm->evt.type) {
	case BT_MESH_LIGHT_CTRL_SM_EVT_MODE_OFF:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_OFF]);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_ON:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_ON]);
		publish_onoff(sm);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_OCCUPANCY_ON:
		if (sm->cfg.occupancy_mode) {
			smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_ON]);
			publish_onoff(sm);
		}
		break;
	}

	return SMF_EVENT_HANDLED;
}

static enum smf_state_result state_fade_on_run(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	switch (sm->evt.type) {
	case BT_MESH_LIGHT_CTRL_SM_EVT_MODE_OFF:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_OFF]);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_OFF:
		smf_set_state(SMF_CTX(sm),
			      &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_MANUAL]);
		publish_onoff(sm);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_TIMER_OFF:
		uint32_t prev_transition_time = sm->output.status.transition_time;

		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_RUN]);
		if (prev_transition_time > 0) {
			publish_onoff(sm);
		}
		break;
	}

	return SMF_EVENT_HANDLED;
}

static enum smf_state_result state_run_run(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	switch (sm->evt.type) {
	case BT_MESH_LIGHT_CTRL_SM_EVT_MODE_OFF:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_OFF]);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_OFF:
		smf_set_state(SMF_CTX(sm),
			      &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_MANUAL]);
		publish_onoff(sm);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_OCCUPANCY_ON:
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_ON:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_RUN]);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_TIMER_OFF:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE]);
		break;
	}

	return SMF_EVENT_HANDLED;
}

static enum smf_state_result state_fade_run(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	switch (sm->evt.type) {
	case BT_MESH_LIGHT_CTRL_SM_EVT_MODE_OFF:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_OFF]);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_OFF:
		smf_set_state(SMF_CTX(sm),
			      &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_MANUAL]);
		publish_onoff(sm);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_OCCUPANCY_ON:
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_ON:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_ON]);
		publish_onoff(sm);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_TIMER_OFF:
		uint32_t prev_transition_time = sm->output.status.transition_time;

		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_PROLONG]);
		if (prev_transition_time > 0) {
			publish_onoff(sm);
		}
		break;
	}

	return SMF_EVENT_HANDLED;
}

static enum smf_state_result state_prolong_run(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	switch (sm->evt.type) {
	case BT_MESH_LIGHT_CTRL_SM_EVT_MODE_OFF:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_OFF]);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_OFF:
		smf_set_state(SMF_CTX(sm),
			      &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_MANUAL]);
		publish_onoff(sm);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_OCCUPANCY_ON:
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_ON:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_ON]);
		publish_onoff(sm);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_TIMER_OFF:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_AUTO]);
		publish_onoff(sm);
		break;
	}

	return SMF_EVENT_HANDLED;
}

static enum smf_state_result state_fade_standby_auto_run(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	switch (sm->evt.type) {
	case BT_MESH_LIGHT_CTRL_SM_EVT_MODE_OFF:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_OFF]);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_OFF:
		smf_set_state(SMF_CTX(sm),
			      &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_MANUAL]);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_OCCUPANCY_ON:
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_ON:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_ON]);
		publish_onoff(sm);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_TIMER_OFF:
		uint32_t prev_transition_time = sm->output.status.transition_time;

		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_STANDBY]);
		if (prev_transition_time > 0) {
			publish_onoff(sm);
		}
		break;
	}

	return SMF_EVENT_HANDLED;
}

static enum smf_state_result state_fade_standby_manual_run(void *user_data)
{
	struct bt_mesh_light_ctrl_sm *sm = user_data;

	switch (sm->evt.type) {
	case BT_MESH_LIGHT_CTRL_SM_EVT_MODE_OFF:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_OFF]);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_ON:
		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_ON]);
		publish_onoff(sm);
		break;
	case BT_MESH_LIGHT_CTRL_SM_EVT_TIMER_OFF:
		uint32_t prev_transition_time = sm->output.status.transition_time;

		smf_set_state(SMF_CTX(sm), &states[BT_MESH_LIGHT_CTRL_SM_STATE_STANDBY]);
		if (prev_transition_time > 0) {
			publish_onoff(sm);
		}
		break;
	}

	return SMF_EVENT_HANDLED;
}

static const struct smf_state states[] = {
	[BT_MESH_LIGHT_CTRL_SM_STATE_OFF] = SMF_CREATE_STATE(
		state_off_entry, state_off_run, NULL, NULL, NULL),
	[BT_MESH_LIGHT_CTRL_SM_STATE_STANDBY] = SMF_CREATE_STATE(
		state_standby_entry, state_standby_run, NULL, NULL, NULL),
	[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_ON] = SMF_CREATE_STATE(
		state_fade_on_entry, state_fade_on_run, NULL, NULL, NULL),
	[BT_MESH_LIGHT_CTRL_SM_STATE_RUN] = SMF_CREATE_STATE(
		state_run_entry, state_run_run, NULL, NULL, NULL),
	[BT_MESH_LIGHT_CTRL_SM_STATE_FADE] = SMF_CREATE_STATE(
		state_fade_entry, state_fade_run, NULL, NULL, NULL),
	[BT_MESH_LIGHT_CTRL_SM_STATE_PROLONG] = SMF_CREATE_STATE(
		state_prolong_entry, state_prolong_run, NULL, NULL, NULL),
	[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_AUTO] = SMF_CREATE_STATE(
		state_fade_standby_auto_entry, state_fade_standby_auto_run, NULL, NULL, NULL),
	[BT_MESH_LIGHT_CTRL_SM_STATE_FADE_STANDBY_MANUAL] = SMF_CREATE_STATE(
		state_fade_standby_manual_entry, state_fade_standby_manual_run, NULL, NULL, NULL)
};

/* ========================================================================== */
/* API functions:                                                             */
/* ========================================================================== */

void bt_mesh_light_ctrl_sm_light_onoff_set(struct bt_mesh_light_ctrl_sm *sm, bool onoff,
					   uint32_t *transition_time,
					   struct bt_mesh_light_ctrl_sm_status *status)
{
	event_process(sm, onoff ? BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_ON :
		      BT_MESH_LIGHT_CTRL_SM_EVT_LIGHT_OFF,
		      transition_time);

	*status = sm->output.status;
}

void bt_mesh_light_ctrl_sm_light_onoff_get(const struct bt_mesh_light_ctrl_sm *sm,
					   struct bt_mesh_light_ctrl_sm_onoff_status *status)
{
	status->is_fade = sm->output.status.is_fade;
	if (sm->output.status.is_fade) {
		/* Binary transitions are always 0b1 mid-transition. */
		status->present_onoff = sm->output.status.present_onoff ||
				sm->output.status.target_onoff;

		status->remaining_time = remaining_time_get(sm);
		status->target_onoff = sm->output.status.target_onoff;
	} else {
		status->present_onoff = sm->output.status.present_onoff;
	}
}

void bt_mesh_light_ctrl_sm_mode_set(struct bt_mesh_light_ctrl_sm *sm, bool mode)
{
	event_process(sm, mode ? BT_MESH_LIGHT_CTRL_SM_EVT_MODE_ON :
		      BT_MESH_LIGHT_CTRL_SM_EVT_MODE_OFF,
		      NULL);
}

void bt_mesh_light_ctrl_sm_occupancy_on(struct bt_mesh_light_ctrl_sm *sm)
{
	event_process(sm, BT_MESH_LIGHT_CTRL_SM_EVT_OCCUPANCY_ON, NULL);
}

void bt_mesh_light_ctrl_sm_output_get(const struct bt_mesh_light_ctrl_sm *sm,
				      struct bt_mesh_light_ctrl_sm_output *output)
{
	struct bt_mesh_light_ctrl_sm_output initial = sm->output.initial_output;
	struct bt_mesh_light_ctrl_sm_output target = sm->output.status.target_output;
	uint32_t total_time = sm->output.status.transition_time;

	if (!(sm->output.status.is_fade)) {
		*output = initial;
		return;
	}

	if (total_time == 0) {
		*output = target;
		return;
	}

	uint32_t remaining_time = remaining_time_get(sm);

	if (remaining_time >= total_time) {
		*output = initial;
		return;
	}

	int32_t delta_lightness = (int32_t)(target.lightness) - (int32_t)(initial.lightness);
	int32_t delta_centilux = (int32_t)(target.centilux) - (int32_t)(initial.centilux);
	int64_t elapsed = (int64_t)total_time - (int64_t)remaining_time;

	/* Interpolate output. 64-bit math because `delta * elapsed` may overflow 32 bits. */
	output->lightness = initial.lightness + (delta_lightness * elapsed) / total_time;
	output->centilux = initial.centilux + (delta_centilux * elapsed) / total_time;
}

void bt_mesh_light_ctrl_sm_output_lightness_set(struct bt_mesh_light_ctrl_sm *sm,
						enum bt_mesh_light_ctrl_sm_output_level level,
						uint16_t lightness)
{
	sm->cfg.output_levels[level].lightness = lightness;
	if (sm->output.status.target_output == &sm->cfg.output_levels[level]) {
		output_fade(sm, &sm->cfg.output_levels[level],
			    sm->output.status.present_onoff || sm->output.status.target_onoff,
			    sm->output.status.target_onoff, remaining_time_get(sm));
		output_changed(sm);
	}
}

void bt_mesh_light_ctrl_sm_output_centilux_set(struct bt_mesh_light_ctrl_sm *sm,
					       enum bt_mesh_light_ctrl_sm_output_level level,
					       uint32_t centilux)
{
	sm->cfg.output_levels[level].centilux = centilux;
	if (sm->output.status.target_output == &sm->cfg.output_levels[level]) {
		output_fade(sm, &sm->cfg.output_levels[level],
			    sm->output.status.present_onoff || sm->output.status.target_onoff,
			    sm->output.status.target_onoff, remaining_time_get(sm));
		output_changed(sm);
	}
}

int bt_mesh_light_ctrl_sm_init(struct bt_mesh_light_ctrl_sm *sm,
			       enum bt_mesh_light_ctrl_sm_state initial_state,
			       struct bt_mesh_light_ctrl_sm_cb *cb)
{
	if (!sm) {
		return -EINVAL;
	}

	sm->cb = cb;
	k_work_init_delayable(&sm->timer, timeout);
	smf_set_initial(SMF_CTX(sm), &states[initial_state]);

	return 0;
}
