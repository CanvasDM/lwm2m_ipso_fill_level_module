/**
 * @file lcz_lwm2m_fill_level.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(fill_level, CONFIG_LCZ_LWM2M_FILL_LEVEL_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr.h>
#include <init.h>
#include <lcz_lwm2m.h>

#include "lwm2m_resource_ids.h"
#include "ipso_filling_sensor.h"
#include "lcz_lwm2m.h"
#include "lcz_snprintk.h"
#include "lcz_lwm2m_util.h"
#include "lcz_lwm2m_fill_level.h"

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
static struct lwm2m_obj_agent fill_level_create_agent;

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static int create_fill_level_sensor(int idx, uint16_t type, uint16_t instance, void *context);

static int fill_sensor_write_cb(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id,
				uint8_t *data, uint16_t data_len, bool last_block,
				size_t total_size);

/**************************************************************************************************/
/* SYS INIT                                                                                       */
/**************************************************************************************************/
static int lcz_lwm2m_fill_level_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	fill_level_create_agent.type = IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID;
	fill_level_create_agent.create = create_fill_level_sensor;
	lcz_lwm2m_util_register_agent(&fill_level_create_agent);

	return 0;
}

SYS_INIT(lcz_lwm2m_fill_level_init, APPLICATION, LCZ_LWM2M_UTIL_USER_INIT_PRIORITY);

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
#if defined(CONFIG_LCZ_LWM2M_UTIL_MANAGE_OBJ_INST)
int lcz_lwm2m_managed_fill_level_set(int idx, uint16_t offset, double value)
{
	uint16_t type = IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID;
	int instance;
	int r;

	instance = lcz_lwm2m_util_manage_obj_instance(type, idx, offset);
	if (instance < 0) {
		LOG_ERR("Unable to manage instance");
		return instance;
	}

	r = lcz_lwm2m_fill_level_set(instance, value);
	lcz_lwm2m_util_manage_obj_deletion(r, type, idx, instance);
	return r;
}
#endif

int lcz_lwm2m_fill_level_set(uint16_t instance, double value)
{
	uint16_t type = IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID;
	char path[LWM2M_MAX_PATH_STR_LEN];
	double fill_percent;
	uint32_t distance = 10.0;
	uint32_t height = 0;
	uint32_t level;
	uint16_t resource;
	int r;

	do {
		/* Read the height so that the fill level can be calculated */
		resource = CONTAINER_HEIGHT_FILLING_SENSOR_RID;
		r = LCZ_SNPRINTK(path, "%u/%u/%u", type, instance, resource);
		if (r < 0) {
			break;
		}
		r = lwm2m_engine_get_u32(path, &height);
		if (r != 0) {
			LOG_ERR("Unable to read height");
			r = -ENOENT;
			break;
		}

		/* Don't allow a negative level (height of substance) to be reported */
		if (distance >= height) {
			level = 0;
		} else {
			level = height - distance;
		}
		fill_percent = (double)(((float)level / (float)height) * 100.0);

		/* The suggested sensor has a minimum range of 50 cm */
		LOG_DBG("height: %u level: %u measured distance: %u percent: %d", height, level,
			distance, (uint32_t)fill_percent);

		/* Write optional resource (don't care if it fails) */
		resource = ACTUAL_FILL_LEVEL_FILLING_SENSOR_RID;
		r = LCZ_SNPRINTK(path, "%u/%u/%u", type, instance, resource);
		if (r < 0) {
			break;
		}
		lwm2m_engine_set_u32(path, level);

		/* Writing this resource will cause full/empty to be re-evaluated */
		resource = ACTUAL_FILL_PERCENTAGE_FILLING_SENSOR_RID;
		r = LCZ_SNPRINTK(path, "%u/%u/%u", type, instance, resource);
		if (r < 0) {
			break;
		}
		r = lwm2m_engine_set_float(path, &fill_percent);

	} while (0);

	return r;
}

int lcz_lwm2m_fill_level_create(uint16_t instance)
{
	return lcz_lwm2m_util_create_obj_inst(IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID, instance);
}

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
/* Callback occurs after the object instance has been created.
 *
 * Save and load filling sensor config to the file system
 */
static int create_fill_level_sensor(int idx, uint16_t type, uint16_t instance, void *context)
{
	ARG_UNUSED(idx);
	ARG_UNUSED(context);

	if (IS_ENABLED(CONFIG_LCZ_LWM2M_UTIL_CONFIG_DATA)) {
		/* If it exists, restore configuration */
		lcz_lwm2m_util_load_config(type, instance, CONTAINER_HEIGHT_FILLING_SENSOR_RID,
					   sizeof(uint32_t));
		lcz_lwm2m_util_load_config(type, instance,
					   HIGH_THRESHOLD_PERCENTAGE_FILLING_SENSOR_RID,
					   sizeof(double));
		lcz_lwm2m_util_load_config(type, instance,
					   LOW_THRESHOLD_PERCENTAGE_FILLING_SENSOR_RID,
					   sizeof(double));

		/* Callback is used to save config to nv */
		lcz_lwm2m_util_reg_post_write_cb(
			type, instance, CONTAINER_HEIGHT_FILLING_SENSOR_RID, fill_sensor_write_cb);
		lcz_lwm2m_util_reg_post_write_cb(type, instance,
						 HIGH_THRESHOLD_PERCENTAGE_FILLING_SENSOR_RID,
						 fill_sensor_write_cb);
		lcz_lwm2m_util_reg_post_write_cb(type, instance,
						 LOW_THRESHOLD_PERCENTAGE_FILLING_SENSOR_RID,
						 fill_sensor_write_cb);
	}

	/* Delete unused resources so they don't show up in Cumulocity. */
	lcz_lwm2m_util_del_res_inst(type, instance, AVERAGE_FILL_SPEED_FILLING_SENSOR_RID, 0);
	lcz_lwm2m_util_del_res_inst(type, instance, FORECAST_FULL_DATE_FILLING_SENSOR_RID, 0);
	lcz_lwm2m_util_del_res_inst(type, instance, FORECAST_EMPTY_DATE_FILLING_SENSOR_RID, 0);
	lcz_lwm2m_util_del_res_inst(type, instance, CONTAINER_OUT_OF_LOCATION_FILLING_SENSOR_RID,
				    0);
	lcz_lwm2m_util_del_res_inst(type, instance, CONTAINER_OUT_OF_POSITION_FILLING_SENSOR_RID,
				    0);

	return 0;
}

static int fill_sensor_write_cb(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id,
				uint8_t *data, uint16_t data_len, bool last_block,
				size_t total_size)
{
	if (IS_ENABLED(CONFIG_LCZ_LWM2M_UTIL_CONFIG_DATA)) {
		lcz_lwm2m_util_save_config(IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID, obj_inst_id, res_id,
					   data, data_len);
	}

	return 0;
}
