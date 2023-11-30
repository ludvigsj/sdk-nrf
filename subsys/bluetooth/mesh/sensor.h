/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Internal sensor API
 */

#ifndef BT_MESH_INTERNAL_SENSOR_H__
#define BT_MESH_INTERNAL_SENSOR_H__

#include <bluetooth/mesh/sensor.h>
#include <bluetooth/mesh/sensor_cli.h>
#include <bluetooth/mesh/sensor_srv.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup BT_MESH_SENSOR_FORMATS Sensor channel formats
 * @brief All available sensor channel formats in the mesh device properties
 * Specification.
 * @{
 */

/* Percentage formats */
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_percentage_8;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_percentage_16;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_percentage_delta_trigger;

/* Environmental formats */
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_temp_8;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_temp;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_co2_concentration;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_noise;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_voc_concentration;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_wind_speed;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_temp_8_wide;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_gust_factor;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_magnetic_flux_density;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_pollen_concentration;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_pressure;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_rainfall;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_uv_index;

/* Time formats */
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_time_decihour_8;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_time_hour_24;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_time_second_16;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_time_millisecond_24;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_time_exp_8;

/* Electrical formats */
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_electric_current;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_voltage;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_energy;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_energy32;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_power;

/* Lighting formats */
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_chromatic_distance;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_chromaticity_coordinate;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_correlated_color_temp;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_illuminance;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_luminous_efficacy;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_luminous_energy;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_luminous_exposure;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_luminous_flux;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_perceived_lightness;

/* Miscellaneous formats */
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_direction_16;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_count_16;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_gen_lvl;
extern const struct bt_mesh_sensor_format
	bt_mesh_sensor_format_cos_of_the_angle;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_boolean;
extern const struct bt_mesh_sensor_format bt_mesh_sensor_format_coefficient;

/** @} */

/**
 * @defgroup BT_MESH_SENSOR_UNITS Sensor value units
 * @brief All available sensor value units in the mesh device properties
 * specification.
 * @{
 */
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_hours;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_seconds;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_celsius;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_kelvin;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_percent;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_ppm;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_ppb;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_volt;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_ampere;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_watt;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_kwh;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_db;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_lux;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_lux_hour;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_lumen;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_lumen_per_watt;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_lumen_hour;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_gram_per_sec;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_litre_per_sec;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_degrees;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_mps;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_microtesla;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_concentration;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_pascal;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_metre;
extern const struct bt_mesh_sensor_unit bt_mesh_sensor_unit_unitless;

/** @} */

/* Temporary typedef while we need to support two different sensor value types
 * in the deprecation period. Can be removed once support for
 * struct sensor_value is removed, and internal APIs changed to use
 * struct bt_mesh_sensor_value.
 */
#ifdef CONFIG_BT_MESH_SENSOR_USE_SENSOR_VALUE
typedef struct sensor_value sensor_value_type;
#else
typedef struct bt_mesh_sensor_value sensor_value_type;
#endif

#ifdef CONFIG_BT_MESH_SENSOR_USE_SENSOR_VALUE
#define SENSOR_VALUE_IN_RANGE(_value, _start, _end) (                          \
		((_value)->val1 > (_start)->val1 ||                            \
		 ((_value)->val1 == (_start)->val1 &&                          \
		  (_value)->val2 >= (_start)->val2)) &&                        \
		((_value)->val1 < (_end)->val1 ||                              \
		 ((_value)->val1 == (_end)->val1 &&                            \
		  (_value)->val2 <= (_end)->val2)))
#else
#define SENSOR_VALUE_IN_RANGE(_value, _start, _end) (                          \
		(_value)->format->cb->compare((_value), (_start)) >= 0 &&      \
		(_value)->format->cb->compare((_end), (_value)) >= 0)

struct bt_mesh_sensor_format_cb {
	/** @brief Perform a delta check between two @ref bt_mesh_sensor_value
	 *         instances.
	 *
	 *  @c current and @c previous must have the same format.
	 *
	 *  @param[in] current  The current value.
	 *  @param[in] previous The previous sensor value to compare against.
	 *  @param[in] delta    The delta to use when checking.
	 *
	 *  @return @c true if the difference between @c current and @c previous
	 *          is bigger than the relevant delta specified in @c delta, @c
	 *          false otherwise.
	 */
	bool (*const delta_check)(const struct bt_mesh_sensor_value *current,
				  const struct bt_mesh_sensor_value *previous,
				  const struct bt_mesh_sensor_deltas *delta);

	/** @brief Compare two @ref bt_mesh_sensor_value instances.
	 *
	 *  @c op1 and @c op1 must have the same format.
	 *
	 *  @param[in] op1 The first value to compare.
	 *  @param[in] op2 The second value to compare.
	 *
	 *  @return 0 if @c op1 == @c op2, 1 if @c op1 > @c op2, -1 otherwise
	 *          (including if @c op1 and @c op2 are not comparable).
	 */
	int (*const compare)(const struct bt_mesh_sensor_value *op1,
			     const struct bt_mesh_sensor_value *op2);

	/** @brief Convert a @ref bt_mesh_sensor_value instance to a
	 *         @ref sensor_value.
	 *
	 *  If this function returns a status other than
	 *  @c BT_MESH_SENSOR_VALUE_NUMBER, @c val is not modified.
	 *
	 *  @param[in]  sensor_val The @ref bt_mesh_sensor_value to convert.
	 *  @param[out] val        The resulting @ref sensor_value.
	 *
	 *  @return The status of the conversion.
	 */
	enum bt_mesh_sensor_value_status (*const to_sensor_value)(
		const struct bt_mesh_sensor_value *sensor_val,
		struct sensor_value *val);

	/** @brief Convert a @ref sensor_value instance to a
	 *         @ref bt_mesh_sensor_value.
	 *
	 *  If @c val has a value that cannot be represented by the format,
	 *  @c sensor_val will be set to the value clamped to the range
	 *  supported by the format, and this function will return -ERANGE.
	 *  This will clamp to "Greater than the maximum value" and
	 *  "Less than the minimum value" if these are supported by the format.
	 *
	 *  If this function returns an error code other than -ERANGE,
	 *  @c sensor_val is not modified.
	 *
	 *  @param[in]  format     Format to use when encoding the sensor value.
	 *  @param[in]  val        The @ref sensor_value to convert.
	 *  @param[out] sensor_val The resulting @ref bt_mesh_sensor_value.
	 *
	 *  @return 0 on success, (negative) error code otherwise.
	 */
	int (*const from_sensor_value)(
		const struct bt_mesh_sensor_format *format,
		const struct sensor_value *val,
		struct bt_mesh_sensor_value *sensor_val);

	/** @brief Convert a @ref bt_mesh_sensor_value to a @c float.
	 *
	 *  If this function returns a status for which
	 *  @ref bt_mesh_sensor_value_status_is_numeric returns false, @c val
	 *  is not modified.
	 *
	 *  @param[in]  sensor_val The @ref bt_mesh_sensor_value to convert.
	 *  @param[out] val        The resulting @c float.
	 *
	 *  @return The status of the conversion.
	 */
	enum bt_mesh_sensor_value_status (*const to_float)(
		const struct bt_mesh_sensor_value *sensor_val, float *val);

	/** @brief Convert a @c float to a @ref bt_mesh_sensor_value.
	 *
	 *  If @c val has a value that cannot be represented by the format,
	 *  @c sensor_val will be set to the value clamped to the range
	 *  supported by the format, and this function will return -ERANGE.
	 *  This will clamp to "Greater than the maximum value" and
	 *  "Less than the minimum value" if these are supported by the format.
	 *
	 *  If this function returns an error code other than -ERANGE,
	 *  @c sensor_val is not modified.
	 *
	 *  @param[in]  format     Format to use when encoding the sensor value.
	 *  @param[in]  val        The @c float to convert.
	 *  @param[out] sensor_val The resulting @ref bt_mesh_sensor_value.
	 *
	 *  @return 0 on success, (negative) error code otherwise.
	 */
	int (*const from_float)(const struct bt_mesh_sensor_format *format,
				float val,
				struct bt_mesh_sensor_value *sensor_val);

	/** @brief Convert a @ref bt_mesh_sensor_value_status value to a
	 *         @ref bt_mesh_sensor_value.
	 *
	 *  @param[in]  format     Format to use when encoding the sensor value.
	 *  @param[in]  val        The @ref bt_mesh_sensor_value_special value
	 *                         to convert.
	 *  @param[out] sensor_val The resulting @ref bt_mesh_sensor_value on
	 *                         success. Undefined otherwise.
	 *
	 *  @return 0 on success, (negative) error code otherwise.
	 */
	int (*const from_special)(const struct bt_mesh_sensor_format *format,
				  enum bt_mesh_sensor_value_status val,
				  struct bt_mesh_sensor_value *sensor_val);

	/** @brief Get a human readable representation of a
	 *         @ref bt_mesh_sensor_value.
	 *
	 *  @param[in]  sensor_val Sensor value to represent.
	 *  @param[out] str        String buffer to fill. Should be
	 *                         @ref BT_MESH_SENSOR_CH_STR_LEN bytes long.
	 *  @param[in]  len        Length of @c str buffer.
	 *
	 *  @return 0 if string successfully written, (negative) error code
	 *          otherwise.
	 */
	int (*const to_string)(const struct bt_mesh_sensor_value *sensor_val,
			       char *str, size_t len);
};

/** Sensor channel value format. */
struct bt_mesh_sensor_format {
	/** Callbacks used for this format. */
	struct bt_mesh_sensor_format_cb *cb;
	/** User data pointer. Used internally by the sensor types. */
	void *user_data;
	/** Size of the encoded data in bytes. */
	size_t size;

#ifdef CONFIG_BT_MESH_SENSOR_LABELS
	/** Pointer to the unit associated with this format. */
	const struct bt_mesh_sensor_unit *unit;
#endif
};

/** @brief Check if a value change breaks the delta threshold.
 *
 *  Sensors should publish their value if the measured sample is outside the
 *  delta threshold compared to the previously published value. This function
 *  checks the threshold and the previously published value for this sensor,
 *  and returns whether the sensor should publish its value.
 *
 *  @note Only single-channel sensors support cadence. Multi-channel sensors are
 *        always considered out of their threshold range, and will always return
 *        true from this function. Single-channel sensors that haven't been
 *        assigned a threshold will return true if the value is different.
 *
 *  @param[in] sensor    The sensor instance.
 *  @param[in] value     Sensor value.
 *
 *  @return true if the difference between the measurements exceeds the delta
 *          threshold, false otherwise.
 */
bool bt_mesh_sensor_delta_threshold(const struct bt_mesh_sensor *sensor,
				    const struct bt_mesh_sensor_value *value);
#endif /* CONFIG_BT_MESH_SENSOR_USE_SENSOR_VALUE */

int sensor_status_encode(struct net_buf_simple *buf,
			 const struct bt_mesh_sensor *sensor,
			 const sensor_value_type *values);

int sensor_status_id_encode(struct net_buf_simple *buf, uint8_t len, uint16_t id);
void sensor_status_id_decode(struct net_buf_simple *buf, uint8_t *len, uint16_t *id);

void sensor_descriptor_decode(struct net_buf_simple *buf,
			      struct bt_mesh_sensor_info *sensor);
void sensor_descriptor_encode(struct net_buf_simple *buf,
			      struct bt_mesh_sensor *sensor);

int sensor_value_encode(struct net_buf_simple *buf,
			const struct bt_mesh_sensor_type *type,
			const sensor_value_type *values);
int sensor_value_decode(struct net_buf_simple *buf,
			const struct bt_mesh_sensor_type *type,
			sensor_value_type *values);

int sensor_ch_encode(struct net_buf_simple *buf,
		     const struct bt_mesh_sensor_format *format,
		     const sensor_value_type *value);
int sensor_ch_decode(struct net_buf_simple *buf,
		     const struct bt_mesh_sensor_format *format,
		     sensor_value_type *value);

int sensor_column_value_encode(struct net_buf_simple *buf,
			       struct bt_mesh_sensor_srv *srv,
			       struct bt_mesh_sensor *sensor,
			       struct bt_mesh_msg_ctx *ctx,
			       uint32_t column_index);
int sensor_column_encode(struct net_buf_simple *buf,
			 struct bt_mesh_sensor_srv *srv,
			 struct bt_mesh_sensor *sensor,
			 struct bt_mesh_msg_ctx *ctx,
			 const struct bt_mesh_sensor_column *col);
int sensor_column_decode(
	struct net_buf_simple *buf, const struct bt_mesh_sensor_type *type,
	struct bt_mesh_sensor_column *col,
	sensor_value_type value[CONFIG_BT_MESH_SENSOR_CHANNELS_MAX]);

int sensor_cadence_encode(struct net_buf_simple *buf,
			  const struct bt_mesh_sensor_type *sensor_type,
			  uint8_t fast_period_div, uint8_t min_int,
			  const struct bt_mesh_sensor_threshold *threshold);
int sensor_cadence_decode(struct net_buf_simple *buf,
			  const struct bt_mesh_sensor_type *sensor_type,
			  uint8_t *fast_period_div, uint8_t *min_int,
			  struct bt_mesh_sensor_threshold *threshold);
uint8_t sensor_value_len(const struct bt_mesh_sensor_type *type);

uint8_t sensor_powtime_encode(uint64_t raw);
uint64_t sensor_powtime_decode(uint8_t encoded);
uint64_t sensor_powtime_decode_us(uint8_t val);

uint8_t sensor_pub_div_get(const struct bt_mesh_sensor *s, uint32_t base_period);

void sensor_cadence_update(struct bt_mesh_sensor *sensor,
			   const sensor_value_type *value);

#ifdef __cplusplus
}
#endif

#endif /* BT_MESH_INTERNAL_SENSOR_H__ */
