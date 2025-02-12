/*
 * drivers/inputhub/sensor_detect.c
 *
 * sensors sysfs driver
 *
 * Copyright (c) 2012-2019 Huawei Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mtd/hisi_nve_interface.h>
#include <linux/of.h>
#include <linux/rtc.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/completion.h>
#include <linux/hwspinlock.h>
#ifdef CONFIG_HUAWEI_DSM
#include <dsm/dsm_pub.h>
#endif
#include "contexthub_route.h"
#include "protocol.h"
#include "sensor_sysfs.h"
#include "contexthub_pm.h"
#include "contexthub_recovery.h"

/* Wait 100ms for data transmitting */
#define FINGERSENSE_TRANS_TOUT  msecs_to_jiffies(100)
 /* We think the data is fresh if it is collected in 10ms */
#define FINGERSENSE_FRESH_TIME  msecs_to_jiffies(10)
#define FINGERSENSE_DISABLE 0
#define FINGERSENSE_ENABLE_ACCEL 1
#define FINGERSENSE_ENABLE_ACC1 2
#define CALIBRATE_DATA_LENGTH 15
#define ACC_GYRO_OFFSET_CALIBRATE_LENGTH 3
#define MAX_VALUE 4294967296
#define MAX_AIRPRESS_OFFSET 500
#define MIN_AIRPRESS_OFFSET (-500)
#define GYRO_RANGE_1000DPS 1000
#define GYRO_DEFAULT_RANGE 2000
#define GYRO_RANGE_FROM_2000DPS_TO_1000DPS 2
#define SAR_SEMTECH_USE_PH_NUM 2
#define LAYA_PHONE_TYPE 37
#define TO_DECIMALISM 10

#ifdef SENSOR_DATA_ACQUISITION
static char *acc_test_name[ACC_CAL_NUM] = {
	"ACC_CALI_X_OFFSET_MSG", "ACC_CALI_Y_OFFSET_MSG", "ACC_CALI_Z_OFFSET_MSG",
	"ACC_CALI_X_SEN_MSG", "ACC_CALI_Y_SEN_MSG", "ACC_CALI_Z_SEN_MSG",
	"ACC_CALI_XIS_00_MSG", "ACC_CALI_XIS_01_MSG", "ACC_CALI_XIS_02_MSG",
	"ACC_CALI_XIS_10_MSG", "ACC_CALI_XIS_11_MSG", "ACC_CALI_XIS_12_MSG",
	"ACC_CALI_XIS_20_MSG", "ACC_CALI_XIS_21_MSG", "ACC_CALI_XIS_22_MSG"
};

static char *gyro_test_name[GYRO_CAL_NUM] = {
	"GYRO_CALI_X_OFFSET_MSG", "GYRO_CALI_Y_OFFSET_MSG", "GYRO_CALI_Z_OFFSET_MSG",
	"GYRO_CALI_X_SEN_MSG", "GYRO_CALI_Y_SEN_MSG", "GYRO_CALI_Z_SEN_MSG",
	"GYRO_CALI_XIS_00_MSG", "GYRO_CALI_XIS_01_MSG", "GYRO_CALI_XIS_02_MSG",
	"GYRO_CALI_XIS_10_MSG", "GYRO_CALI_XIS_11_MSG", "GYRO_CALI_XIS_12_MSG",
	"GYRO_CALI_XIS_20_MSG", "GYRO_CALI_XIS_21_MSG", "GYRO_CALI_XIS_22_MSG"
};

static char *als_test_name[ALS_CAL_NUM] = {
	"ALS_CALI_R_MSG", "ALS_CALI_G_MSG", "ALS_CALI_B_MSG",
	"ALS_CALI_C_MSG", "ALS_CALI_LUX_MSG", "ALS_CALI_CCT_MSG"
};

static char *ps_test_name[PS_CAL_NUM] = {
	"PS_CALI_XTALK_MSG", "PS_FAR_PDATA_MSG",
	"PS_NEAR_PDATA_MSG", "PS_OFFSET_PDATA_MSG",
	"PS_SCREEN_ON_CALI_XTALK_MSG", "PS_SCREEN_ON_FAR_PDATA_MSG",
	"PS_SCREEN_ON_NEAR_PDATA_MSG", "PS_SCREEN_ON_OFFSET_PDATA_MSG"
};

static char *ps_add_test_name = "PS_DIGITAL_OFFSET_MSG";
static char *cap_prox_offset_test_name[CAP_PROX_CAL_NUM] = {
	"SAR_SENSOR_PH1_OFFSET_MSG", "SAR_SENSOR_PH2_OFFSET_MSG"
};

static char *cap_prox_diff_test_name[CAP_PROX_CAL_NUM] = {
	"SAR_SENSOR_DIFF1_MSG", "SAR_SENSOR_DIFF2_MSG"
};

static char *cap_prox1_offset_test_name[CAP_PROX_CAL_NUM] = {
	"SAR_SENSOR1_PH1_OFFSET_MSG",
	"SAR_SENSOR1_PH2_OFFSET_MSG"
};

static char *cap_prox1_diff_test_name[CAP_PROX_CAL_NUM] = {
	"SAR_SENSOR1_DIFF1_MSG",
	"SAR_SENSOR1_DIFF2_MSG"
};

static char *airpress_test_name[AIRPRESS_CAL_NUM] = {
	"AIRPRESS_CALI_OFFSET_MSG", "AIRPRESS_TOUCH_CALI_SLOPE_MSG",
	"AIRPRESS_TOUCH_CALI_BASEP_MSG", "AIRPRESS_TOUCH_CALI_MAXP_MSG",
	"AIRPRESS_TOUCH_CALI_RAISEP_MSG", "AIRPRESS_TOUCH_CALI_MINP_MSG",
	"AIRPRESS_TOUCH_CALI_TEMP_MSG", "AIRPRESS_TOUCH_CALI_SPEED_MSG",
	"AIRPRESS_TOUCH_CALI_RFLAG_MSG", "AIRPRESS_TOUCH_TEST_SLOPE_MSG",
	"AIRPRESS_TOUCH_TEST_BASEP_MSG", "AIRPRESS_TOUCH_TEST_MAXP_MSG",
	"AIRPRESS_TOUCH_TEST_RAISEP_MSG", "AIRPRESS_TOUCH_TEST_MINP_MSG",
	"AIRPRESS_TOUCH_TEST_TEMP_MSG", "AIRPRESS_TOUCH_TEST_SPEED_MSG",
	"AIRPRESS_TOUCH_TEST_RFLAG_MSG"
};
#endif

uint16_t sensorlist[SENSOR_LIST_NUM];
unsigned int sensor_read_number[TAG_END];
char *sar_calibrate_order;
char *acc_sensor_id;
char *gyro_sensor_id;
char *mag_sensor_id;
char *cap_sensor_id;
char *sensor_in_board_status;
volatile int hall_value;
int hifi_supported;
int fingersense_enabled;
int stop_auto_motion;
int flag_for_sensor_test; /* fordden sensor cmd from HAL */
int stop_auto_accel;
int stop_auto_als;
int stop_auto_ps;
int gyro_range = GYRO_DEFAULT_RANGE;
static uint8_t debug_read_data_buf[DEBUG_DATA_LENGTH];
static uint8_t i2c_rw16_data_buf[2]; /* 2: i2c data buf length */
static uint8_t i2c_rw_16bit_reg_data_buf[DEBUG_DATA_LENGTH];
static RET_TYPE return_calibration = RET_INIT; /* acc calibrate result */
static int acc_close_after_calibrate = 1;
static RET_TYPE gyro_calibration_res = RET_INIT; /* gyro calibrate result */
static RET_TYPE ps_calibration_res = RET_INIT; /* ps calibrate result */
static RET_TYPE als_calibration_res = RET_INIT; /* als calibrate result */
/* handpress calibrate result */
static RET_TYPE handpress_calibration_res = RET_INIT;
static RET_TYPE return_cap_prox_calibration = RET_INIT;
static RET_TYPE return_cap_prox1_calibration = RET_INIT;

static int als_close_after_calibrate = true;
static int gyro_close_after_calibrate = true;
extern int32_t ps_sensor_offset[PS_CALIBRATE_DATA_LENGTH];
static int32_t gyro_calib_data[15]; /* 15: gyro calib data default length */
static int32_t set_gyro_calib_data[15];
static int32_t set_acc_calib_data[15];
static int cap_prox_calibrate_len;
static int cap_prox1_calibrate_len;
static uint8_t cap_prox_calibrate_data[CAP_PROX_CALIDATA_NV_SIZE];
static uint8_t cap_prox1_calibrate_data[CAP_PROX1_CALIDATA_NV_SIZE];

static struct work_struct cap_prox_calibrate_work;
static struct work_struct cap_prox1_calibrate_work;
static int airpress_cali_flag;
static struct work_struct handpress_calibrate_work;
static int is_gsensor_gather_enable;
extern uint8_t tof_register_value;
static int32_t ps_calib_data_for_data_collect[8]; /* 8: ps calib data length */
extern int hall_sen_type;
static RET_TYPE acc1_return_calibration = RET_INIT;
static int acc1_close_after_calibrate = true;
static RET_TYPE gyro1_calibration_res = RET_INIT;
static int gyro1_close_after_calibrate = true;
static int32_t gyro1_calib_data[15]; /* 15: gyro calib data default length */
struct acc_gyr_offset_threshold acc_calib_threshold[CALIBRATE_DATA_LENGTH] = {
	{ -320, 320 },   /* x-offset unit:mg */
	{ -320, 320 },   /* y-offset unit:mg */
	{ -320, 320 },   /* z-offset unit:mg */
	{ 6500, 13500 }, /* x-sensitivity */
	{ 6500, 13500 }, /* y-sensitivity */
	{ 6500, 13500 }, /* z-sensitivity */
	/* The following is the interaxial interference */
	{ 6500, 13500 },
	{ -2000, 2000 },
	{ -2000, 2000 },
	{ -2000, 2000 },
	{ 6500, 13500 },
	{ -2000, 2000 },
	{ -2000, 2000 },
	{ -2000, 2000 },
	{ 6500, 13500 },
};

struct acc_gyr_offset_threshold gyro_calib_threshold[CALIBRATE_DATA_LENGTH] = {
	{ -572, 572 },   /* x-offset */
	{ -572, 572 },   /* y-offset */
	{ -572, 572 },   /* z-offset */
	{ 6500, 13500 }, /* x-sensitivity */
	{ 6500, 13500 }, /* y-sensitivity */
	{ 6500, 13500 }, /* z-sensitivity */
	/* The following is the interaxial interference */
	{ 6500, 13500 },
	{ -2000, 2000 },
	{ -2000, 2000 },
	{ -2000, 2000 },
	{ 6500, 13500 },
	{ -2000, 2000 },
	{ -2000, 2000 },
	{ -2000, 2000 },
	{ 6500, 13500 },
};

/* pass mark as NA */
static unsigned long fingersense_data_ts; /* timestamp for the data */

extern char sensor_chip_info[SENSOR_MAX][MAX_CHIP_INFO_LEN];
extern int sensor_ps_flag;
extern int sensor_tof_flag;
extern int apds9253_006_ps_flag;
extern u8 tplcd_manufacture;
extern u8 phone_color;
extern int is_cali_supported;
extern int ps_cali_supported;
extern int rohm_rgb_flag;
extern int avago_rgb_flag;
extern int ams_tmd3725_rgb_flag;
extern int liteon_ltr582_rgb_flag;
extern int apds9999_rgb_flag;
extern int  ams_tmd3702_rgb_flag;
extern int ams_tcs3707_rgb_flag;
extern int apds9253_rgb_flag;
extern int vishay_vcnl36658_als_flag;
extern int ams_tcs3701_rgb_flag;
extern int vd6281_als_flag;

extern struct hisi_nve_info_user user_info;
extern struct airpress_platform_data airpress_data;
extern union sar_calibrate_data sar_calibrate_datas;
extern struct sar_platform_data sar_pdata;
extern struct g_sensor_platform_data gsensor_data;
extern struct gyro_platform_data gyro_data;
extern struct ps_platform_data ps_data;
extern struct tof_platform_data tof_data;
extern struct als_platform_data als_data;

extern int get_airpress_data;
extern int get_temperature_data;
extern uint8_t hp_offset[HANDPRESS_CALIDATA_NV_SIZE];
extern sys_status_t iom3_sr_status;
extern volatile int vibrator_shake;
extern uint8_t ps_sensor_calibrate_data[MAX_SENSOR_CALIBRATE_DATA_LENGTH];
extern uint8_t als_sensor_calibrate_data[MAX_SENSOR_CALIBRATE_DATA_LENGTH];
extern spinlock_t fsdata_lock;
extern bool fingersense_data_intrans;
extern bool fingersense_data_ready;
extern s16 fingersense_data[FINGERSENSE_DATA_NSAMPLES];
extern uint8_t gyro_cali_way;
extern uint8_t acc_cali_way;
extern s16 min_threshold_als_para;
extern s16 max_threshold_als_para;
extern uint8_t tof_sensor_calibrate_data[TOF_CALIDATA_NV_SIZE];

#ifdef CONFIG_HUAWEI_DSM
extern struct dsm_client *shb_dclient;
#endif
extern int get_tpcolor_from_nv(void);
extern void enable_motions_when_recovery_iom3(void);
extern void disable_motions_when_sysreboot(void);
extern int send_calibrate_data_to_mcu(int tag, uint32_t subcmd,
	const void *data, int length, bool is_recovery);

static ssize_t show_sensor_list_info(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i;

	hwlog_info("sensor list: ");
	for (i = 0; i <= sensorlist[0]; i++)
		hwlog_info(" %d  ", sensorlist[i]);
	hwlog_info("\n");
	memcpy(buf, sensorlist, ((sensorlist[0] + 1) * sizeof(uint16_t)));
	return (sensorlist[0] + 1) * sizeof(uint16_t);
}
static DEVICE_ATTR(sensor_list_info, 0444, show_sensor_list_info, NULL);

#define SENSOR_SHOW_INFO(TAG)\
static ssize_t sensor_show_##TAG##_info(struct device *dev, \
	struct device_attribute *attr, char *buf)\
{\
	return snprintf(buf, MAX_STR_SIZE, "%s\n", sensor_chip_info[TAG]);\
}

SENSOR_SHOW_INFO(PS);
static DEVICE_ATTR(ps_info, 0444, sensor_show_PS_info, NULL);

SENSOR_SHOW_INFO(ALS);
static DEVICE_ATTR(als_info, 0444, sensor_show_ALS_info, NULL);

SENSOR_SHOW_INFO(GYRO);
static DEVICE_ATTR(gyro_info, 0444, sensor_show_GYRO_info, NULL);

SENSOR_SHOW_INFO(MAG);
static DEVICE_ATTR(mag_info, 0444, sensor_show_MAG_info, NULL);

SENSOR_SHOW_INFO(ACC);
static DEVICE_ATTR(acc_info, 0444, sensor_show_ACC_info, NULL);

SENSOR_SHOW_INFO(AIRPRESS);
static DEVICE_ATTR(airpress_info, 0444, sensor_show_AIRPRESS_info, NULL);

SENSOR_SHOW_INFO(HANDPRESS);
static DEVICE_ATTR(handpress_info, 0444, sensor_show_HANDPRESS_info, NULL);

#define SENSOR_SHOW_VALUE(TAG) \
static ssize_t sensor_show_##TAG##_read_data(struct device *dev, \
	struct device_attribute *attr, char *buf) \
{\
	hwlog_info("[sensorhub_test], %s return %d\n", __func__, \
		sensor_read_number[TAG]);\
	return snprintf(buf, MAX_STR_SIZE, "%d\n", sensor_read_number[TAG]);\
}

SENSOR_SHOW_VALUE(TAG_ACCEL);
static DEVICE_ATTR(acc_read_data, 0664, sensor_show_TAG_ACCEL_read_data, NULL);

SENSOR_SHOW_VALUE(TAG_MAG);
static DEVICE_ATTR(mag_read_data, 0664, sensor_show_TAG_MAG_read_data, NULL);

SENSOR_SHOW_VALUE(TAG_GYRO);
static DEVICE_ATTR(gyro_read_data, 0664, sensor_show_TAG_GYRO_read_data, NULL);

SENSOR_SHOW_VALUE(TAG_ALS);
static DEVICE_ATTR(als_read_data, 0664, sensor_show_TAG_ALS_read_data, NULL);

SENSOR_SHOW_VALUE(TAG_PS);
static DEVICE_ATTR(ps_read_data, 0664, sensor_show_TAG_PS_read_data, NULL);

SENSOR_SHOW_VALUE(TAG_PRESSURE);
static DEVICE_ATTR(airpress_read_data, 0664,
	sensor_show_TAG_PRESSURE_read_data, NULL);

SENSOR_SHOW_VALUE(TAG_HANDPRESS);
static DEVICE_ATTR(handpress_read_data, 0664,
	sensor_show_TAG_HANDPRESS_read_data, NULL);

#define SHOW_SELFTEST_RESULT(TAG) \
static ssize_t show_##TAG##_selfTest_result(struct device *dev, \
	struct device_attribute *attr, char *buf)\
{\
	return snprintf(buf, MAX_STR_SIZE, "%s\n", \
		sensor_status.TAG##_selfTest_result);\
}

SHOW_SELFTEST_RESULT(gyro);
SHOW_SELFTEST_RESULT(mag);
SHOW_SELFTEST_RESULT(accel);
SHOW_SELFTEST_RESULT(connectivity);

static ssize_t show_handpress_selfTest_result(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int result;

	if (strncmp(sensor_status.handpress_selfTest_result, "1", strlen("1")))
		result = 0;
	else
		result = 1;
	return snprintf(buf, MAX_STR_SIZE, "%d\n", result);
}

#ifdef CONFIG_HUAWEI_DSM
#define SET_SENSOR_SELFTEST(TAGUP, TAGLOW) \
static ssize_t attr_set_##TAGLOW##_selftest(struct device *dev, \
	struct device_attribute *attr, const char *buf, size_t size)\
{\
	unsigned long val = 0;\
	int err = -1;\
	write_info_t	pkg_ap;\
	read_info_t	pkg_mcu;\
	uint32_t subcmd;\
	memset(&pkg_ap, 0, sizeof(pkg_ap));\
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));\
	if (strict_strtoul(buf, 10, &val))\
		return -EINVAL;\
	if (val == 1) {\
		pkg_ap.tag = TAG_##TAGUP;\
		pkg_ap.cmd = CMD_CMN_CONFIG_REQ;\
		subcmd = SUB_CMD_SELFTEST_REQ;\
		pkg_ap.wr_buf = &subcmd;\
		pkg_ap.wr_len = SUBCMD_LEN;\
		err = write_customize_cmd(&pkg_ap,  &pkg_mcu, true);\
		if (err) {\
			hwlog_err("send %s selftest cmd to mcu fail,ret=%d\n", \
				#TAGUP, err);\
			memcpy(sensor_status.TAGLOW##_selfTest_result, "0", 2);\
			return size;\
		} \
		if (pkg_mcu.errno != 0) {\
			hwlog_err("%s selftest fail\n", #TAGUP);\
			memcpy(sensor_status.TAGLOW##_selfTest_result, "0", 2);\
		} else {\
			hwlog_info("%s selftest  success, data len=%d\n", \
				#TAGUP, pkg_mcu.data_length);\
			memcpy(sensor_status.TAGLOW##_selfTest_result, "1", 2);\
		} \
	} \
	return size;\
}
#else
#define SET_SENSOR_SELFTEST(TAGUP, TAGLOW) \
static ssize_t attr_set_##TAGLOW##_selftest(struct device *dev, \
	struct device_attribute *attr, const char *buf, size_t size)\
{\
	unsigned long val = 0;\
	int err = -1;\
	write_info_t	pkg_ap;\
	read_info_t	pkg_mcu;\
	memset(&pkg_ap, 0, sizeof(pkg_ap));\
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));\
	if (strict_strtoul(buf, 10, &val))\
		return -EINVAL;\
	if (val == 1) {\
		pkg_ap.tag = TAG_##TAGUP;\
		pkg_ap.cmd = CMD_##TAGUP##_SELFTEST_REQ;\
		pkg_ap.wr_buf = NULL;\
		pkg_ap.wr_len = 0;\
		err = write_customize_cmd(&pkg_ap,  &pkg_mcu, true);\
		if (err) {\
			hwlog_err("send %s selftest cmd to mcu fail,ret=%d\n", \
				#TAGUP, err);\
			memcpy(sensor_status.TAGLOW##_selfTest_result, "0", 2);\
			return size;\
		} \
		if (pkg_mcu.errno != 0) {\
			hwlog_err("%s selftest fail\n", #TAGUP);\
			memcpy(sensor_status.TAGLOW##_selfTest_result, "0", 2);\
		} else {\
			hwlog_info("%s selftest  success, data len=%d\n", \
				#TAGUP, pkg_mcu.data_length);\
			memcpy(sensor_status.TAGLOW##_selfTest_result, "1", 2);\
		} \
	} \
	return size;\
}
#endif

SET_SENSOR_SELFTEST(GYRO, gyro);
static DEVICE_ATTR(gyro_selfTest, 0664,
	show_gyro_selfTest_result, attr_set_gyro_selftest);

SET_SENSOR_SELFTEST(MAG, mag);
static DEVICE_ATTR(mag_selfTest, 0664,
	show_mag_selfTest_result, attr_set_mag_selftest);

SET_SENSOR_SELFTEST(ACCEL, accel);
static DEVICE_ATTR(acc_selfTest, 0664,
	show_accel_selfTest_result, attr_set_accel_selftest);

SET_SENSOR_SELFTEST(CONNECTIVITY, connectivity);
static DEVICE_ATTR(connectivity_selfTest, 0664,
	show_connectivity_selfTest_result, attr_set_connectivity_selftest);

SET_SENSOR_SELFTEST(HANDPRESS, handpress);
static DEVICE_ATTR(handpress_selfTest, 0664,
	show_handpress_selfTest_result, attr_set_handpress_selftest);

#ifdef SENSOR_DATA_ACQUISITION
static int init_msg_for_enq(struct message *msg)
{
	if (!msg)
		return -1;

	memset(msg, 0, sizeof(struct message));
	msg->data_source = DATA_FROM_KERNEL;
	msg->num_events = 0;
	msg->version = 1;
	return 0;
}

static int init_event_of_msg(struct event *events,
	struct sensor_eng_cal_test sensor_test)
{
	if (!events)
		return -1;
	memset(events, 0, sizeof(struct event));
	events->error_code = 0;
	events->cycle = 0;
	memcpy(events->station, NA, sizeof(NA));
	memcpy(events->device_name, sensor_test.name, sizeof(sensor_test.name));
	memcpy(events->bsn, NA, sizeof(NA));
	memcpy(events->min_threshold, NA, sizeof(NA));
	memcpy(events->max_threshold, NA, sizeof(NA));
	memcpy(events->result, sensor_test.result, sizeof(sensor_test.result));
	memcpy(events->firmware, NA, sizeof(NA));
	memcpy(events->description, NA, sizeof(NA));
	return 0;
}

static int enq_msg_data_in_sensorhub_single(struct event events)
{
	struct message *msg = NULL;
	int ret = -1;

	msg = (struct message *)kzalloc(sizeof(struct message), GFP_KERNEL);
	if (!msg) {
		hwlog_err("alloc message failed\n");
		return ret;
	}

	msg->data_source = DATA_FROM_KERNEL;
	msg->num_events = 1;
	msg->version = 1;
	events.error_code = 0;
	events.cycle = 0;
	memcpy(events.station, NA, sizeof(NA));
	memcpy(events.bsn, NA, sizeof(NA));
	memcpy(events.firmware, NA, sizeof(NA));
	memcpy(events.description, NA, sizeof(NA));
	memcpy(&(msg->events[0]), &events, sizeof(events));

	if (!dsm_client_ocuppy(shb_dclient)) {
		ret = dsm_client_copy_ext(shb_dclient, msg,
			sizeof(struct message));
		if (ret > 0)
			dsm_client_notify(shb_dclient, DA_SENSOR_HUB_ERROR_NO);
		else
			hwlog_err("dsm_client_notify failed");
	} else {
		hwlog_info("%s:dsm_client_ocuppy fail\n", __func__);
		dsm_client_unocuppy(shb_dclient);
		if (!dsm_client_ocuppy(shb_dclient)) {
			ret = dsm_client_copy_ext(shb_dclient, msg,
				sizeof(struct message));
			if (ret > 0)
				dsm_client_notify(shb_dclient,
					DA_SENSOR_HUB_ERROR_NO);
			else
				hwlog_err("dsm_client_notify failed");
		}
	}

	if (msg)
		kfree(msg);

	return ret;
}

static int enq_msg_data_in_sensorhub(struct sensor_eng_cal_test sensor_test)
{
	struct message *msg = NULL;
	int ret = -1;
	int pCalValue = 0;
	int pEvents = 0;

	hwlog_info("%s:sart\n", __func__);
	if (sensor_test.value_num > MAX_COPY_EVENT_SIZE ||
	    !sensor_test.cal_value) {
		hwlog_info("enq_msg_data_in_sensorhub bad value!!\n");
		return ret;
	}

	while (pCalValue < sensor_test.value_num) {
		msg = (struct message *)kzalloc(sizeof(struct message),
			GFP_KERNEL);
		ret = init_msg_for_enq(msg);
		if (ret) {
			hwlog_err("alloc mesage failed\n");
			return ret;
		}

		for (pEvents = 0; (pEvents < MAX_MSG_EVENT_NUM &&
			pCalValue < sensor_test.value_num);
			pEvents++, pCalValue++) {
			ret = init_event_of_msg(&(msg->events[pEvents]),
				sensor_test);
			if (ret) {
				hwlog_err("init_event_of_msg failed\n");
				kfree(msg);
				return ret;
			}
			snprintf(msg->events[pEvents].value, MAX_VAL_LEN, "%d",
				*(sensor_test.cal_value + pCalValue));
			if (pCalValue < sensor_test.threshold_num) {
				snprintf(msg->events[pEvents].min_threshold,
					MAX_VAL_LEN, "%d",
					*(sensor_test.min_threshold + pCalValue));
				snprintf(msg->events[pEvents].max_threshold,
					MAX_VAL_LEN, "%d",
					*(sensor_test.max_threshold + pCalValue));
			}
			memcpy(msg->events[pEvents].test_name,
				sensor_test.test_name[pCalValue],
				strlen(sensor_test.test_name[pCalValue]) + 1);
			msg->events[pEvents].item_id =
				sensor_test.first_item + pCalValue;
		}
		msg->num_events = pEvents;
		ret = dsm_client_copy_ext(shb_dclient, msg, sizeof(struct message));
		if (ret <= 0) {
			ret = -1;
			hwlog_err("%s dsm_client_copy_ext for sensor failed\n",
				sensor_test.name);
			kfree(msg);
			return ret;
		} else {
			ret = 0;
			if (msg)
				kfree(msg);
		}
	}
	hwlog_info("%s enq_msg_data_in_sensorhub succ\n", sensor_test.name);
	return ret;
}


static void enq_notify_work_sensor(struct sensor_eng_cal_test sensor_test)
{
	int ret;

	if (!dsm_client_ocuppy(shb_dclient)) {
		ret = enq_msg_data_in_sensorhub(sensor_test);
		if (!ret)
			dsm_client_notify(shb_dclient, DA_SENSOR_HUB_ERROR_NO);
	} else {
		hwlog_info("%s:dsm_client_ocuppy fail\n", __func__);
		dsm_client_unocuppy(shb_dclient);
		if (!dsm_client_ocuppy(shb_dclient)) {
			ret = enq_msg_data_in_sensorhub(sensor_test);
			if (!ret)
				dsm_client_notify(shb_dclient,
					DA_SENSOR_HUB_ERROR_NO);
		}
	}
}


static void cap_prox_do_enq_work(int calibrate_index,
	int32_t *cap_prox_data, int32_t *min_threshold,
	int32_t *max_threshold, int sar_type)
{
	struct sensor_eng_cal_test air_test;
	int i;

	if (!cap_prox_data || !min_threshold || !max_threshold) {
		hwlog_err("%s: input error\n", __func__);
		return;
	}
	memset(&air_test, 0, sizeof(air_test));
	air_test.cal_value = cap_prox_data;
	air_test.value_num = CAP_PROX_CAL_NUM;
	air_test.threshold_num = CAP_PROX_THRESHOLD_NUM;
	air_test.min_threshold = min_threshold;
	air_test.max_threshold = max_threshold;
	memcpy(air_test.name, CAP_PROX_TEST_CAL,
		sizeof(CAP_PROX_TEST_CAL));
	memcpy(air_test.result, CAP_PROX_RESULT,
		strlen(CAP_PROX_RESULT) + 1);
	switch (calibrate_index) {
	case NEAR_CALIBRATE:
		air_test.first_item = SAR_SENSOR_DIFF1_MSG + sar_type;
		for (i = 0; i < CAP_PROX_CAL_NUM; i++) {
			if (sar_type == HW_CAP_PROX_SENSOR)
				air_test.test_name[i] =
					cap_prox_diff_test_name[i];
			else
				air_test.test_name[i] =
					cap_prox1_diff_test_name[i];
		}
		enq_notify_work_sensor(air_test);
		break;
	case FAR_CALIBRATE:
		air_test.first_item = SAR_SENSOR_PH1_OFFSET_MSG + sar_type;
		for (i = 0; i < CAP_PROX_CAL_NUM; i++) {
			if (sar_type == HW_CAP_PROX_SENSOR)
				air_test.test_name[i] =
					cap_prox_offset_test_name[i];
			else
				air_test.test_name[i] =
					cap_prox1_offset_test_name[i];
		}
		enq_notify_work_sensor(air_test);
		break;
	default:
		hwlog_err("%s: error calibrate_index = %d\n",
			__func__, calibrate_index);
		break;
	}
}

/* add ps special data to factory big data */
static void ps_add_do_enq_work(int32_t *ps_digital_offset)
{
	int32_t min_threshold = 0;
	int32_t max_threshold = (int32_t)ps_data.digital_offset_max;
	struct sensor_eng_cal_test ps_test;

	memset(&ps_test, 0, sizeof(ps_test));
	ps_test.cal_value = ps_digital_offset;
	ps_test.value_num = PS_ADD_CAL_NUM;
	ps_test.threshold_num = PS_ADD_THRESHOLD_NUM;
	ps_test.first_item = PS_DIGITAL_OFFSET_PDATA;
	ps_test.min_threshold = &min_threshold;
	ps_test.max_threshold = &max_threshold;
	memcpy(ps_test.name, PS_TEST_CAL, sizeof(PS_TEST_CAL));
	memcpy(ps_test.result, PS_CAL_RESULT,
		(strlen(PS_CAL_RESULT) + 1));
	ps_test.test_name[0] = ps_add_test_name;
	enq_notify_work_sensor(ps_test);
}
#endif

static ssize_t i2c_rw_pi(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	uint64_t val = 0;
	int ret = 0;
	uint8_t bus_num;
	uint8_t i2c_address;
	uint8_t reg_add;
	uint8_t len;
	uint8_t rw;
	uint8_t buf_temp[DEBUG_DATA_LENGTH] = { 0 };

	if (strict_strtoull(buf, 16, &val))
		return -EINVAL;
	/* ##(bus_num)##(i2c_addr)##(reg_addr)##(len) */
	bus_num = (val >> 40) & 0xff;
	i2c_address = (val >> 32) & 0xff;
	reg_add = (val >> 24) & 0xff;
	len = (val >> 16) & 0xff;
	if (len > DEBUG_DATA_LENGTH - 1) {
		hwlog_err("len exceed %d\n", len);
		len = DEBUG_DATA_LENGTH - 1;
	}
	rw = (val >> 8) & 0xff;
	buf_temp[0] = reg_add;
	buf_temp[1] = (uint8_t)(val & 0xff);

	if (rw)
		ret = mcu_i2c_rw(bus_num, i2c_address, &buf_temp[0], 1,
			&buf_temp[1], len);
	else
		ret = mcu_i2c_rw(bus_num, i2c_address, buf_temp, 2, NULL, 0);

	if (ret < 0)
		hwlog_err("oper %d(1/32:r 0/31:w) i2c reg fail\n", rw);
	if (rw) {
		hwlog_err("i2c reg %x value %x %x %x %x\n", reg_add,
			buf_temp[1], buf_temp[2], buf_temp[3], buf_temp[4]);
		memcpy(debug_read_data_buf, &buf_temp[1], len);
	}
	return count;
}

static ssize_t i2c_rw_pi_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	unsigned int i;
	unsigned int len = 0;
	char *p = buf;

	if (!buf)
		return -1;

	/* 4,5,6 used to show i2c debug data */
	for (i = 0; i < DEBUG_DATA_LENGTH; i++) {
		snprintf(p, 6, "0x%x,", debug_read_data_buf[i]);
		if (debug_read_data_buf[i] > 0xf) {
			p += 5;
			len += 5;
		} else {
			p += 4;
			len += 4;
		}
	}

	p = buf;
	*(p + len - 1) = 0;

	p = NULL;
	return len;
}

static DEVICE_ATTR(i2c_rw, 0664, i2c_rw_pi_show, i2c_rw_pi);

static ssize_t i2c_rw16_pi(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	uint64_t val = 0;
	int ret = 0;
	uint8_t bus_num;
	uint8_t i2c_address;
	uint8_t reg_add;
	uint8_t len;
	uint8_t rw;
	uint8_t buf_temp[3] = { 0 };

	if (strict_strtoull(buf, 16, &val))
		return -EINVAL;
	/* ##(bus_num)##(i2c_addr)##(reg_addr)##(len) */
	bus_num = (val >> 48) & 0xff;
	i2c_address = (val >> 40) & 0xff;
	reg_add = (val >> 32) & 0xff;
	len = (val >> 24) & 0xff;
	if (len > 2) {
		hwlog_err("len exceed %d\n", len);
		len = 2;
	}
	rw = (val >> 16) & 0xff;
	buf_temp[0] = reg_add;
	buf_temp[1] = (uint8_t)(val >> 8);
	buf_temp[2] = (uint8_t)(val & 0xff);

	if (rw)
		ret = mcu_i2c_rw(bus_num, i2c_address, buf_temp,
				1, &buf_temp[1], (uint32_t)len);
	else
		ret = mcu_i2c_rw(bus_num, i2c_address, buf_temp,
				1 + len, NULL, 0);
	if (ret < 0)
		hwlog_err("oper %d(1:r 0:w) i2c reg fail\n", rw);

	if (rw) {
		hwlog_err("i2c reg %x value %x %x\n",
			reg_add, buf_temp[1], buf_temp[2]);
		memcpy(i2c_rw16_data_buf, &buf_temp[1], 2);
	}
	return count;
}

static ssize_t i2c_rw16_pi_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char *p = buf;

	if (!buf)
		return -1;

	snprintf(p, 8, "0x%02x%02x\n",
		i2c_rw16_data_buf[0], i2c_rw16_data_buf[1]);
	*(p + 7) = 0;

	p = NULL;
	return 8;
}
static DEVICE_ATTR(i2c_rw16, 0664, i2c_rw16_pi_show, i2c_rw16_pi);

static ssize_t i2c_rw_16bit_reg_pi(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	uint64_t val = 0;
	int ret;
	uint8_t bus_num;
	uint8_t i2c_address;
	uint16_t reg_add;
	uint8_t len;
	uint8_t rw;
	uint8_t rw_info;
	uint8_t tx_buf_len;
	uint8_t buf_temp[DEBUG_DATA_LENGTH] = { 0 };

	if (strict_strtoull(buf, 16, &val))
		return -EINVAL;
	/*
	 * ##(bus_num 4bit)##(i2c_addr 8bit)##(reg_addr 16bit )
	 * ##(rw 4bit)##(write data 32bit)
	 */
	bus_num = (uint8_t) (val >> 60) & 0xf;
	i2c_address = (val >> 52) & 0xff;
	reg_add = (val >> 36) & 0xffff;
	/* rw_info 4bit,highest bit rw,left is buf length */
	rw_info = (uint8_t)(val >> 32) & 0xf;
	rw = rw_info & 0x8;
	len = rw_info & 0x7;
	buf_temp[0] = (uint8_t)(reg_add >> 8) & 0xff;
	buf_temp[1] = (uint8_t) (reg_add & 0xff);
	buf_temp[2] = (uint8_t)(val >> 24) & 0xff;
	buf_temp[3] = (uint8_t)(val >> 16) & 0xff;
	buf_temp[4] = (uint8_t)(val >> 8) & 0xff;
	buf_temp[5] = (uint8_t)val & 0xff;
	if (len > 4) {
		hwlog_err("buf len is too long\n");
		return -EINVAL;
	}

	if (rw) {
		ret = mcu_i2c_rw(bus_num, i2c_address,
			&buf_temp[0], 2, &buf_temp[2], len);
	} else {
		hwlog_info("reg %x write value is:%2x %2x %2x %2x\n", reg_add,
			buf_temp[2], buf_temp[3], buf_temp[4], buf_temp[5]);
		/* set highest bit 1 to indicate 16bit reg */
		tx_buf_len = (uint8_t)((2 + len) | 0x80);
		ret = mcu_i2c_rw(bus_num, i2c_address, buf_temp,
			tx_buf_len, NULL, 0);
	}
	if (ret < 0)
		hwlog_err("oper %d(1/32:r 0/31:w) i2c reg fail\n", rw);
	if (rw) {
		hwlog_info("i2c reg %04x value %x %x %x %x\n", reg_add,
			buf_temp[2], buf_temp[3], buf_temp[4], buf_temp[5]);
		memcpy(i2c_rw_16bit_reg_data_buf, &buf_temp[2], len);
	}
	return count;
}

static ssize_t i2c_rw_16bit_reg_pi_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	unsigned int i;
	unsigned int len = 0;
	char *p = buf;

	if (!buf)
		return -1;

	for (i = 0; i < DEBUG_DATA_LENGTH; i++) {
		snprintf(p, 12, "0x%x,", i2c_rw_16bit_reg_data_buf[i]);
		if (i2c_rw_16bit_reg_data_buf[i] > 0xf) {
			p += 5;
			len += 5;
		} else {
			p += 4;
			len += 4;
		}
	}
	p = buf;
	*(p + len - 1) = 0;
	p = NULL;
	return len;
}

static DEVICE_ATTR(i2c_rw_16bit_reg, 0664,
	i2c_rw_16bit_reg_pi_show, i2c_rw_16bit_reg_pi);

static ssize_t attr_acc_calibrate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int val = return_calibration;

	hwlog_info("acc_calibrate_show is old way,result=%d\n", val);
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static int acc1_calibrate_save(const void *buf, int length)
{
	const int32_t *poffset_data = (const int32_t *)buf;
	int ret;
	int i;

	if (!buf || length <= 0) {
		hwlog_err("%s invalid argument", __func__);
		acc1_return_calibration = EXEC_FAIL;
		return -1;
	}
	hwlog_info("%s:acc1 calibrate ok, %d  %d  %d\n",
		__func__, *poffset_data, *(poffset_data + 1), *(poffset_data + 2));

	for (i = 0; i < ACC_GYRO_OFFSET_CALIBRATE_LENGTH; i++) {
		if (*(poffset_data + i) < acc_calib_threshold[i].low_threshold ||
			*(poffset_data + i) > acc_calib_threshold[i].high_threshold) {
			hwlog_err("%s: acc1 calibrated_data is out of range. i = %d, num = %d\n",
				__FUNCTION__, i, *(poffset_data + i));
			acc1_return_calibration = NV_FAIL;
			return -1;
		}
	}

	ret = write_gsensor1_offset_to_nv((char *)buf, length);
	if (ret) {
		hwlog_err("nv write fail\n");
		acc1_return_calibration = NV_FAIL;
		return -1;
	}
	acc1_return_calibration = SUC;
	return 0;
}

static int acc_calibrate_save(const void *buf, int length)
{
	const int32_t *poffset_data = (const int32_t *)buf;
	int ret;
	int i;

	if (!buf || length <= 0) {
		hwlog_err("%s invalid argument", __func__);
		return_calibration = EXEC_FAIL;
		return -1;
	}
	hwlog_info("%s:gsensor calibrate ok, %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
		__func__, *poffset_data, *(poffset_data + 1),
		*(poffset_data + 2), *(poffset_data + 3), *(poffset_data + 4),
		*(poffset_data + 5), *(poffset_data + 6), *(poffset_data + 7),
		*(poffset_data + 8), *(poffset_data + 9), *(poffset_data + 10),
		*(poffset_data + 11), *(poffset_data + 12),
		*(poffset_data + 13), *(poffset_data + 14));

	for (i = 0; i < ACC_GYRO_OFFSET_CALIBRATE_LENGTH; i++) {
		if (*(poffset_data + i) < acc_calib_threshold[i].low_threshold ||
		    *(poffset_data + i) > acc_calib_threshold[i].high_threshold) {
			hwlog_err("%s: acc calibrated_data is out of range. i = %d, num = %d\n",
				__func__, i, *(poffset_data + i));
			return_calibration = NV_FAIL;
			return -1;
		}
	}
	ret = write_gsensor_offset_to_nv((char *)buf, length);
	if (ret) {
		hwlog_err("nv write fail\n");
		return_calibration = NV_FAIL;
		return -1;
	}
	return_calibration = SUC;
	return 0;
}

static read_info_t send_calibrate_cmd(uint8_t tag, unsigned long val,
	RET_TYPE *rtype)
{
	int ret;
	write_info_t pkg_ap;
	read_info_t pkg_mcu;
	pkt_parameter_req_t spkt;
	pkt_header_t *shd = (pkt_header_t *)&spkt;

	memset(&pkg_ap, 0, sizeof(pkg_ap));
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));

	pkg_ap.tag = tag;
	pkg_ap.cmd = CMD_CMN_CONFIG_REQ;
	spkt.subcmd = SUB_CMD_SELFCALI_REQ;
	pkg_ap.wr_buf = &shd[1];
	pkg_ap.wr_len = sizeof(val) + SUBCMD_LEN;
	memcpy(spkt.para, &val, sizeof(val));
	hwlog_err("tag %d calibrator val is %lu len is %lu\n",
		tag, val, sizeof(val));
	ret = write_customize_cmd(&pkg_ap, &pkg_mcu, true);
	if (ret) {
		*rtype = COMMU_FAIL;
		hwlog_err("send tag %d calibrate cmd to mcu fail,ret=%d\n",
			tag, ret);
	} else if (pkg_mcu.errno != 0) {
		hwlog_err("send tag %d calibrate fail,%d\n", tag, pkg_mcu.errno);
		*rtype = EXEC_FAIL;
	} else {
		hwlog_info("send tag %d calibrate  success, data len=%d\n",
			tag, pkg_mcu.data_length);
		*rtype = SUC;
	}
	return pkg_mcu;
}

read_info_t send_airpress_calibrate_cmd(uint8_t tag, unsigned long val,
	RET_TYPE *rtype)
{
	return send_calibrate_cmd(tag, val, rtype);
}

static ssize_t attr_acc1_calibrate_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	int ret;
	interval_param_t param;
	read_info_t read_pkg;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val != 1) {
		hwlog_err("send acc1 calibrate value is error value=%lu\n", val);
		return count;
	}
	/* if assistant_acc is not opened, open first */
	if (sensor_status.opened[TAG_ACC1] == 0) {
		acc1_close_after_calibrate = true;
		hwlog_info("send acc1 open cmd(during calibrate) to mcu.\n");
		ret = inputhub_sensor_enable(TAG_ACC1, true);
		if (ret) {
			acc1_return_calibration = COMMU_FAIL;
			hwlog_err("send acc1 open cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	} else {
		acc1_close_after_calibrate = false;
	}
	/* period must <= 100 ms */
	if ((sensor_status.delay[TAG_ACC1] == 0) ||
	    (sensor_status.delay[TAG_ACCEL] > 20)) {
		hwlog_info("send assistant_acc setdelay cmd(during calibrate) to mcu\n");
		memset(&param, 0, sizeof(param));
		param.period = 20;
		ret = inputhub_sensor_setdelay(TAG_ACC1, &param);
		if (ret) {
			acc1_return_calibration = COMMU_FAIL;
			hwlog_err("send assistant_acc set delay cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}

	msleep(300);
	/* send calibrate command, need set delay first */
	read_pkg = send_calibrate_cmd(TAG_ACC1, val, &acc1_return_calibration);
	if (acc1_return_calibration == COMMU_FAIL)
		return count;
	else if (read_pkg.errno == 0)
		acc1_calibrate_save(read_pkg.data, read_pkg.data_length);

	if (acc1_close_after_calibrate == true) {
		acc1_close_after_calibrate = false;
		hwlog_info("send acc1 close cmd(during calibrate) to mcu\n");
		ret = inputhub_sensor_enable(TAG_ACC1, false);
		if (ret) {
			acc1_return_calibration = COMMU_FAIL;
			hwlog_err("send acc1 close cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}
	return count;
}

static ssize_t attr_acc_calibrate_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	int ret = 0;
	int32_t *acc_cali_data = NULL;
	interval_param_t param;
	read_info_t read_pkg;
	#ifdef SENSOR_DATA_ACQUISITION
	int32_t *minThreshold = NULL;
	int32_t *maxThreshold = NULL;
	int32_t minThreshold_acc[ACC_THRESHOLD_NUM] = {
		-gsensor_data.x_calibrate_thredhold,
		-gsensor_data.y_calibrate_thredhold,
		-gsensor_data.z_calibrate_thredhold
	};
	int32_t maxThreshold_acc[ACC_THRESHOLD_NUM] = {
		gsensor_data.x_calibrate_thredhold,
		gsensor_data.y_calibrate_thredhold,
		gsensor_data.z_calibrate_thredhold
	};
	struct sensor_eng_cal_test acc_test;
	int pAccTest = 0;
	#endif

	#ifdef SENSOR_DATA_ACQUISITION
	memset(&acc_test, 0, sizeof(acc_test));
	#endif

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val < 1 || val > 6)
		return count;

	/* if acc is not opened, open first */
	if (sensor_status.opened[TAG_ACCEL] == 0) {
		acc_close_after_calibrate = 1;
		hwlog_info("send acc open cmd(during calibrate) to mcu\n");
		ret = inputhub_sensor_enable(TAG_ACCEL, true);
		if (ret) {
			return_calibration = COMMU_FAIL;
			hwlog_err("send acc open cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	} else {
		acc_close_after_calibrate = 0;
	}
	/* period must <= 100 ms */
	if ((sensor_status.delay[TAG_ACCEL] == 0) ||
	    (sensor_status.delay[TAG_ACCEL] > 20)) {
		hwlog_info("send acc setdelay cmd(during calibrate) to mcu\n");
		memset(&param, 0, sizeof(param));
		param.period = 20;
		ret = inputhub_sensor_setdelay(TAG_ACCEL, &param);
		if (ret) {
			return_calibration = COMMU_FAIL;
			hwlog_err("send acc set delay cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}
	msleep(300);
	/* send calibrate command, need set delay first */
	read_pkg = send_calibrate_cmd(TAG_ACCEL, val, &return_calibration);
	if (return_calibration == COMMU_FAIL)
		return count;
	else if (read_pkg.errno == 0)
		acc_calibrate_save(read_pkg.data, read_pkg.data_length);

	acc_cali_data = (int32_t *)read_pkg.data;
	#ifdef SENSOR_DATA_ACQUISITION
	hwlog_info("%s:is sport data collect\n", __func__);
	minThreshold = minThreshold_acc;
	maxThreshold = maxThreshold_acc;
	acc_test.cal_value = acc_cali_data;
	acc_test.value_num = ACC_CAL_NUM;
	acc_test.threshold_num = ACC_THRESHOLD_NUM;
	acc_test.first_item = ACC_CALI_X_OFFSET_MSG;
	acc_test.min_threshold = minThreshold;
	acc_test.max_threshold = maxThreshold;
	memcpy(acc_test.name, ACC_TEST_CAL, sizeof(ACC_TEST_CAL));
	memcpy(acc_test.result, ACC_CAL_RESULT, (strlen(ACC_CAL_RESULT) + 1));
	for (pAccTest = 0; pAccTest < ACC_CAL_NUM; pAccTest++)
		acc_test.test_name[pAccTest] = acc_test_name[pAccTest];

	enq_notify_work_sensor(acc_test);
	#endif

	if (acc_close_after_calibrate == 1) {
		acc_close_after_calibrate = 0;
		hwlog_info("send acc close cmd(during calibrate) to mcu\n");
		ret = inputhub_sensor_enable(TAG_ACCEL, false);
		if (ret) {
			return_calibration = COMMU_FAIL;
			hwlog_err("send acc close cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}
	return count;
}

static DEVICE_ATTR(acc_calibrate, 0664,
	attr_acc_calibrate_show, attr_acc_calibrate_write);

static ssize_t attr_gyro_calibrate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int val = gyro_calibration_res;

	hwlog_info("gyro_calibrate_show is old way,result=%d\n", val);
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static int gyro1_calibrate_save(const void *buf, int length)
{
	const int32_t *poffset_data = (const int32_t *)buf;
	int ret;
	int i;

	if (!buf || length <= 0) {
		hwlog_err("%s invalid argument", __func__);
		gyro1_calibration_res = EXEC_FAIL;
		return -1;
	}
	hwlog_info("%s:gyro1 calibrate ok, %d %d %d\n", __func__,
		*poffset_data, *(poffset_data + 1), *(poffset_data + 2));

	for (i = 0; i < ACC_GYRO_OFFSET_CALIBRATE_LENGTH; i++) {
		if (*(poffset_data + i) < gyro_calib_threshold[i].low_threshold ||
			*(poffset_data + i) > gyro_calib_threshold[i].high_threshold) {
			hwlog_err("%s: gyro1 calibrated_data is out of range. i = %d, num = %d\n",
				__func__, i, *(poffset_data + i));
			gyro1_calibration_res = NV_FAIL;
			return -1;
		}
	}
	ret = write_gyro1_sensor_offset_to_nv((char *)buf, length);
	if (ret) {
		hwlog_err("nv write fail\n");
		gyro1_calibration_res = NV_FAIL;
		return -1;
	}
	gyro1_calibration_res = SUC;
	return 0;
}

static int gyro_calibrate_save(const void *buf, int length)
{
	const int32_t *poffset_data = (const int32_t *)buf;
	int ret;
	int gyro_range_factor = 1;
	int i;

	if (!buf || length <= 0) {
		hwlog_err("%s invalid argument", __func__);
		gyro_calibration_res = EXEC_FAIL;
		return -1;
	}
	hwlog_info("%s:gyro_sensor calibrate ok, %d  %d  %d %d  %d  %d %d  %d  %d %d  %d  %d  %d  %d  %d\n",
		__func__, *poffset_data, *(poffset_data + 1),
		*(poffset_data + 2), *(poffset_data + 3), *(poffset_data + 4),
		*(poffset_data + 5), *(poffset_data + 6), *(poffset_data + 7),
		*(poffset_data + 8), *(poffset_data + 9), *(poffset_data + 10),
		*(poffset_data + 11), *(poffset_data + 12),
		*(poffset_data + 13), *(poffset_data + 14));

	if (gyro_range == GYRO_RANGE_1000DPS)
		gyro_range_factor = GYRO_RANGE_FROM_2000DPS_TO_1000DPS;

	for (i = 0; i < ACC_GYRO_OFFSET_CALIBRATE_LENGTH; i++) {
		if ((*(poffset_data + i) <
			gyro_calib_threshold[i].low_threshold * gyro_range_factor) ||
			(*(poffset_data + i) >
			gyro_calib_threshold[i].high_threshold * gyro_range_factor)) {
			hwlog_err("%s: gyro calibrated_data is out of range. i = %d, num = %d\n",
				__func__, i, *(poffset_data + i));
			gyro_calibration_res = NV_FAIL;
			return -1;
		}
	}

	ret = write_gyro_sensor_offset_to_nv((char *)buf, length);
	if (ret) {
		hwlog_err("nv write fail.\n");
		gyro_calibration_res = NV_FAIL;
		return -1;
	}
	gyro_calibration_res = SUC;
	return 0;
}

static ssize_t attr_gyro1_calibrate_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	int ret = 0;
	read_info_t read_pkg;
	const int32_t *gyro_cali_data = NULL;
	interval_param_t param;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val != 1) {
		hwlog_err("set gyro1 calibrate val invalid,val=%lu\n", val);
		return count;
	}

	/* if gyro1 is not opened, open first */
	if (sensor_status.opened[TAG_GYRO1] == 0) {
		gyro1_close_after_calibrate = true;
		hwlog_info("send gyro1 open cmd(during calibrate) to mcu.\n");
		ret = inputhub_sensor_enable(TAG_GYRO1, true);
		if (ret) {
			gyro1_calibration_res = COMMU_FAIL;
			hwlog_err("send gyro open cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	} else {
		gyro1_close_after_calibrate = false;
	}

	if ((sensor_status.delay[TAG_GYRO1] == 0) ||
		(sensor_status.delay[TAG_GYRO1] > 10)) {
		hwlog_info("send gyro1 setdelay cmd(during calibrate) to mcu\n");
		memset(&param, 0, sizeof(param));
		param.period = 10;
		ret = inputhub_sensor_setdelay(TAG_GYRO1, &param);
		if (ret) {
			gyro1_calibration_res = COMMU_FAIL;
			hwlog_err("send gyro1 set delay cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}
	msleep(300);
	/* send calibrate command, need set delay first */
	read_pkg = send_calibrate_cmd(TAG_GYRO1, val, &gyro1_calibration_res);
	if (gyro1_calibration_res == COMMU_FAIL) {
		return count;
	} else if (read_pkg.errno == 0) {
		gyro_cali_data = (const int32_t *)read_pkg.data;
		gyro1_calib_data[0] = *gyro_cali_data;
		gyro1_calib_data[1] = *(gyro_cali_data + 1);
		gyro1_calib_data[2] = *(gyro_cali_data + 2);
		gyro1_calibrate_save(gyro1_calib_data, sizeof(gyro1_calib_data));
		hwlog_info("gyro calibrate success, val=%lu data=%d %d %d,len=%d\n",
			val, gyro1_calib_data[0], gyro1_calib_data[1],
			gyro1_calib_data[2], read_pkg.data_length);

	}

	if (gyro1_close_after_calibrate == true) {
		gyro1_close_after_calibrate = false;
		hwlog_info("send gyro close cmd(during calibrate) to mcu.\n");
		ret = inputhub_sensor_enable(TAG_GYRO1, false);
		if (ret) {
			gyro1_calibration_res = COMMU_FAIL;
			hwlog_err("send gyro close cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}
	return count;
}

static ssize_t attr_gyro_calibrate_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	int ret = 0;
	read_info_t read_pkg;
	int32_t *gyro_cali_data = NULL;
	interval_param_t param;
	#ifdef SENSOR_DATA_ACQUISITION
	int32_t *minThreshold = NULL;
	int32_t *maxThreshold = NULL;
	int32_t minThreshold_gyro[GYRO_THRESHOLD_NUM] = {
		-gyro_data.calibrate_thredhold,
		-gyro_data.calibrate_thredhold,
		-gyro_data.calibrate_thredhold
	}; /* -40dps */
	int32_t maxThreshold_gyro[GYRO_THRESHOLD_NUM] = {
		gyro_data.calibrate_thredhold,
		gyro_data.calibrate_thredhold,
		gyro_data.calibrate_thredhold
	}; /* 40dps */
	struct sensor_eng_cal_test gyro_test;
	int pGyroTest = 0;
	#endif

	#ifdef SENSOR_DATA_ACQUISITION
	memset(&gyro_test, 0, sizeof(gyro_test));
	#endif

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if ((val < 1) || (val > 7)) {
		hwlog_err("set gyro calibrate val invalid,val=%lu\n", val);
		return count;
	}

	/* if gyro is not opened, open first */
	if (sensor_status.opened[TAG_GYRO] == 0) {
		gyro_close_after_calibrate = true;
		hwlog_info("send gyro open cmd(during calibrate) to mcu\n");
		ret = inputhub_sensor_enable(TAG_GYRO, true);
		if (ret) {
			gyro_calibration_res = COMMU_FAIL;
			hwlog_err("send gyro open cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	} else {
		gyro_close_after_calibrate = false;
	}

	if ((sensor_status.delay[TAG_GYRO] == 0) ||
		(sensor_status.delay[TAG_GYRO] > 10)) {
		hwlog_info("send gyro setdelay cmd(during calibrate) to mcu\n");
		memset(&param, 0, sizeof(param));
		param.period = 10;
		ret = inputhub_sensor_setdelay(TAG_GYRO, &param);
		if (ret) {
			gyro_calibration_res = COMMU_FAIL;
			hwlog_err("send gyro set delay cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}
	msleep(300);
	/* send calibrate command, need set delay first */
	read_pkg = send_calibrate_cmd(TAG_GYRO, val, &gyro_calibration_res);
	if (gyro_calibration_res == COMMU_FAIL) {
		return count;
	} else if (read_pkg.errno == 0) {
		gyro_cali_data = (int32_t *)read_pkg.data;
		if (val == 1) {
			gyro_calib_data[0] = *gyro_cali_data;
			gyro_calib_data[1] = *(gyro_cali_data + 1);
			gyro_calib_data[2] = *(gyro_cali_data + 2);
			gyro_calibrate_save(gyro_calib_data, sizeof(gyro_calib_data));
		} else if (val == GYRO_DYN_CALIBRATE_END_ORDER) {
			gyro_calib_data[3] = *(gyro_cali_data + 3);
			gyro_calib_data[4] = *(gyro_cali_data + 4);
			gyro_calib_data[5] = *(gyro_cali_data + 5);
			gyro_calib_data[6] = *(gyro_cali_data + 6);
			gyro_calib_data[7] = *(gyro_cali_data + 7);
			gyro_calib_data[8] = *(gyro_cali_data + 8);
			gyro_calib_data[9] = *(gyro_cali_data + 9);
			gyro_calib_data[10] = *(gyro_cali_data + 10);
			gyro_calib_data[11] = *(gyro_cali_data + 11);
			gyro_calib_data[12] = *(gyro_cali_data + 12);
			gyro_calib_data[13] = *(gyro_cali_data + 13);
			gyro_calib_data[14] = *(gyro_cali_data + 14);
			gyro_calibrate_save(gyro_calib_data, sizeof(gyro_calib_data));
		} else {
			gyro_calibration_res = SUC;
		}
		hwlog_info("gyro calibrate success, val=%lu data=%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d, len=%d\n",
			val, gyro_calib_data[0], gyro_calib_data[1],
			gyro_calib_data[2], gyro_calib_data[3], gyro_calib_data[4],
			gyro_calib_data[5], gyro_calib_data[6], gyro_calib_data[7],
			gyro_calib_data[8], gyro_calib_data[9], gyro_calib_data[10],
			gyro_calib_data[11], gyro_calib_data[12], gyro_calib_data[13],
			gyro_calib_data[14], read_pkg.data_length);
	}

	if (val == 1 || val == GYRO_DYN_CALIBRATE_END_ORDER) {
		#ifdef SENSOR_DATA_ACQUISITION
		hwlog_info("%s:is sport data collect\n", __func__);
		minThreshold = minThreshold_gyro;
		maxThreshold = maxThreshold_gyro;
		gyro_test.cal_value = gyro_calib_data;
		gyro_test.first_item = GYRO_CALI_X_OFFSET_MSG;
		gyro_test.value_num = GYRO_CAL_NUM;
		gyro_test.threshold_num = GYRO_THRESHOLD_NUM;
		gyro_test.min_threshold = minThreshold;
		gyro_test.max_threshold = maxThreshold;
		memcpy(gyro_test.name, GYRO_TEST_CAL, sizeof(GYRO_TEST_CAL));
		memcpy(gyro_test.result, GYRO_CAL_RESULT,
			(strlen(GYRO_CAL_RESULT) + 1));
		for (pGyroTest = 0; pGyroTest < GYRO_CAL_NUM; pGyroTest++)
			gyro_test.test_name[pGyroTest] = gyro_test_name[pGyroTest];

		enq_notify_work_sensor(gyro_test);
		#endif
	}

	if (gyro_close_after_calibrate == true) {
		gyro_close_after_calibrate = false;
		hwlog_info("send gyro close cmd(during calibrate) to mcu\n");
		ret = inputhub_sensor_enable(TAG_GYRO, false);
		if (ret) {
			gyro_calibration_res = COMMU_FAIL;
			hwlog_err("send gyro close cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}
	return count;
}
static DEVICE_ATTR(gyro_calibrate, 0664,
	attr_gyro_calibrate_show, attr_gyro_calibrate_write);

static ssize_t attr_ps_calibrate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int val = ps_calibration_res;

	hwlog_info("attr_ps_calibrate_show result=%d\n", val);
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

int write_ps_offset_to_nv(int *temp, uint16_t length)
{
	int ret = 0;
	const int *poffset_data = (const int *)user_info.nv_data;

	if (!temp) {
		hwlog_err("write_ps_offset_to_nv fail, invalid para\n");
		return -1;
	}

	if (length > PS_CALIDATA_NV_SIZE)
		return -1;

	if (write_calibrate_data_to_nv(PS_CALIDATA_NV_NUM, PS_CALIDATA_NV_SIZE,
		"PSENSOR", (char *)temp))
		return -1;

	hwlog_info("write_ps_offset_to_nv temp: %d,%d,%d\n",
		temp[0], temp[1], temp[2]);
	memcpy(&ps_sensor_calibrate_data, temp, PS_CALIDATA_NV_SIZE);
	hwlog_info("nve_direct_access write temp %d,%d,%d\n",
		*poffset_data, *(poffset_data + 1), *(poffset_data + 2));
	msleep(10);
	if (read_calibrate_data_from_nv(PS_CALIDATA_NV_NUM,
		PS_CALIDATA_NV_SIZE, "PSENSOR"))
		return -1;

	if ((*poffset_data != temp[0]) || (*(poffset_data+1) != temp[1]) ||
	    (*(poffset_data + 2) != temp[2])) {
		hwlog_err("nv write fail, %d,%d,%d\n",
			*poffset_data, *(poffset_data + 1), *(poffset_data + 2));
		return -1;
	}
	return ret;
}

int write_tof_offset_to_nv(uint8_t *temp, uint16_t length)
{
	int ret = 0;

	if (!temp) {
		hwlog_err("write_tof_offset_to_nv fail, invalid para\n");
		return -1;
	}

	if (length > TOF_CALIDATA_NV_SIZE)
		return -1;

	if (write_calibrate_data_to_nv(PS_CALIDATA_NV_NUM,
		TOF_CALIDATA_NV_SIZE, "PSENSOR", (char *)temp))
		return -1;

	hwlog_info("write_ps_offset_to_nv temp: temp[0]=%d,temp[9]=%d,temp[19]=%d temp[27]=%d temp[29]=%d temp[44]=%d temp[45]=%d temp[46]=%d\n",
		temp[0], temp[9], temp[19], temp[27], temp[29],
		temp[44], temp[45], temp[TOF_CALIDATA_NV_SIZE - 1]);
	memcpy(&tof_sensor_calibrate_data, temp, TOF_CALIDATA_NV_SIZE);
	return ret;
}

static void ps_calibrate_process(unsigned long val, read_info_t *pkg_mcu)
{
	int ps_calibate_offset_0;
	int ps_calibate_offset_3;
	read_info_t *p = pkg_mcu;

	if (!p) {
		hwlog_info("ps calibrate process error, pkg is null\n");
		return;
	}

	if (val == PS_XTALK_CALIBRATE) {
		ps_sensor_offset[val - 1] = *((int32_t *)p->data);
		ps_calibate_offset_0 =
			(int16_t)(ps_sensor_offset[val - 1] & 0x0000ffff);
		ps_calibate_offset_3 =
			(int16_t)((ps_sensor_offset[val - 1] & 0xffff0000) >> 16);
		ps_calib_data_for_data_collect[0] = (int32_t)ps_calibate_offset_0;
		ps_calib_data_for_data_collect[3] = (int32_t)ps_calibate_offset_3;
		/* clear NV 5cm calibrated value */
		ps_sensor_offset[1] = 0;
		/* clear NV 3cm calibrated value */
		ps_sensor_offset[2] = 0;
		hwlog_info("ps calibrate success, ps_calibate_offset_0=%d, ps_calibate_offset_3=%d\n",
			ps_calibate_offset_0, ps_calibate_offset_3);
		hwlog_info("ps calibrate success, data=%d, len=%d val=%lu\n",
			ps_sensor_offset[val - 1], p->data_length, val);
	} else if (val == PS_SAVE_MINPDATA) {
		ps_sensor_offset[0] = *((int32_t *)p->data);
		ps_calib_data_for_data_collect[0] = ps_sensor_offset[0];
		hwlog_info("ps calibrate success, data=%d, len=%d val=%lu\n",
			ps_sensor_offset[0], p->data_length, val);
	} else if (val == PS_SCREEN_ON_XTALK_CALIBRATE) {
		ps_sensor_offset[3] = *((int32_t *)p->data);
		ps_calib_data_for_data_collect[4] =
			(int32_t)((uint32_t)ps_sensor_offset[3] & 0x0000ffff);
		ps_calib_data_for_data_collect[7] =
			(int32_t)(((uint32_t)ps_sensor_offset[3] & 0xffff0000) >> 16);
		/* clear NV 5cm screen_on calibrated value */
		ps_sensor_offset[4] = 0;
		/* clear NV 3cm screen_on calibrated value */
		ps_sensor_offset[5] = 0;
		hwlog_info("ps calibrate success, data=%d, len=%d val=%lu\n",
			ps_sensor_offset[3], p->data_length, val);
	} else if (val == PS_SCREEN_ON_5CM_CALIBRATE) {
		ps_sensor_offset[4] = *((int32_t *)p->data);
		ps_calib_data_for_data_collect[5] = *((int32_t *)p->data);
		hwlog_info("ps calibrate success, data=%d, len=%d val=%lu\n",
			ps_sensor_offset[4], p->data_length, val);
	} else if (val == PS_SCREEN_ON_3CM_CALIBRATE) {
		ps_sensor_offset[5] = *((int32_t *)p->data);
		ps_calib_data_for_data_collect[6] = *((int32_t *)p->data);
		hwlog_info("ps calibrate success, data=%d, len=%d val=%lu\n",
			ps_sensor_offset[5], p->data_length, val);
	} else if (val != PS_MINPDATA_MODE) {
		ps_sensor_offset[val - 1] = *((int32_t *)p->data);
		ps_calib_data_for_data_collect[val - 1] = *((int32_t *)p->data);
		hwlog_info("ps calibrate success, data=%d, len=%d val=%lu\n",
			ps_sensor_offset[val-1], p->data_length, val);
	}
}

static int ps_calibrate_save(const void *buf, int length)
{
	int32_t temp_buf[PS_CALIBRATE_DATA_LENGTH] = { 0 };
	int ret;

	if (!buf || length <= 0 || length > sizeof(ps_sensor_offset)) {
		hwlog_err("%s invalid argument", __func__);
		ps_calibration_res = EXEC_FAIL;
		return -1;
	}
	memcpy(temp_buf, buf, length);
	hwlog_info("%s:psensor calibrate ok, %d,%d,%d,%d,%d,%d\n", __func__,
		temp_buf[0], temp_buf[1], temp_buf[2],
		temp_buf[3], temp_buf[4], temp_buf[5]);
	ret = write_ps_offset_to_nv(temp_buf, PS_CALIDATA_NV_SIZE);
	if (ret) {
		ret = mcu_save_calidata_to_nv(TAG_PS, temp_buf);
		if (ret != 0) {
			hwlog_err("nv write fail\n");
			ps_calibration_res = NV_FAIL;
			return -1;
		}
	}
	ps_calibration_res = SUC;
	return 0;
}

static int tof_calibrate_save(const void *buf, int length,
	uint8_t calibrate_order)
{
	uint8_t temp_buf[TOF_CALIDATA_NV_SIZE] = {0};

	if (!buf || (length <= 0 || length > TOF_CALIDATA_NV_SIZE)) {
		hwlog_err("%s invalid argument\n", __func__);
		ps_calibration_res = EXEC_FAIL;
		return -1;
	}
	memcpy(temp_buf, buf, length);
	hwlog_info("%s:calibrate ok,buf[0]=%d,buf[9]=%d,buf[19]=%d,buf[27]=%d\n",
		__func__, temp_buf[0], temp_buf[9], temp_buf[19],
		temp_buf[TOF_CALIDATA_NV_SIZE - 1]);

	if (sensor_tof_flag == 1) {
		/*
		 * 28: pkg_mcu.data[]  start site
		 * write_info_t        : len 20
		 * pkg_mcu.errno       : len 4
		 * pkg_mcu.data_length : len 4
		 */
		switch (calibrate_order) {
		case TOF_ZERO_CALIBRATE:
			memcpy(&ps_calib_data_for_data_collect[0],
				&temp_buf[28],
				sizeof(ps_calib_data_for_data_collect[0]));
			break;
		case TOF_6CM_CALIBRATE:
		case PS_XTALK_CALIBRATE:
			memcpy(&ps_calib_data_for_data_collect[1],
				&temp_buf[28 + 4],
				sizeof(ps_calib_data_for_data_collect[1]));
			break;
		case TOF_10CM_CALIBRATE:
			memcpy(&ps_calib_data_for_data_collect[2],
				&temp_buf[28 + 8],
				sizeof(ps_calib_data_for_data_collect[2]));
			break;
		case TOF_60CM_CALIBRATE:
			memcpy(&ps_calib_data_for_data_collect[3],
				&temp_buf[28 + 12],
				sizeof(ps_calib_data_for_data_collect[3]));
			break;
		default:
			hwlog_info("%s:order no need collect data\n", __func__);
		}
		hwlog_info("%s order is %d data_collect %d %d %d %d\n",
			__func__, calibrate_order,
			ps_calib_data_for_data_collect[0],
			ps_calib_data_for_data_collect[1],
			ps_calib_data_for_data_collect[2],
			ps_calib_data_for_data_collect[3]);
	}
	if (write_tof_offset_to_nv(temp_buf, TOF_CALIDATA_NV_SIZE)) {
		hwlog_err("nv write fail\n");
		ps_calibration_res = NV_FAIL;
		return -1;
	}
	ps_calibration_res = SUC;
	return 0;
}

static ssize_t attr_ps_calibrate_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	read_info_t pkg_mcu;
	uint8_t calibrate_order;
	int32_t *ps_cali_data = NULL;
	#ifdef SENSOR_DATA_ACQUISITION
	int32_t *minThreshold = NULL;
	int32_t *maxThreshold = NULL;
	struct sensor_eng_cal_test ps_test;
	int pPsTest = 0;
	int32_t digital_offset = 0;
	int32_t minThreshold_ps[PS_CAL_NUM] = {
		0, 0, 0, -ps_data.offset_min, 0, 0, 0, -ps_data.offset_min
	};
	int32_t maxThreshold_ps[PS_CAL_NUM] = {
		ps_data.ps_calib_20cm_threshold, ps_data.ps_calib_5cm_threshold,
		ps_data.ps_calib_3cm_threshold, ps_data.offset_max,
		ps_data.ps_calib_20cm_threshold, ps_data.ps_calib_5cm_threshold,
		ps_data.ps_calib_3cm_threshold, ps_data.offset_max
	};

	if (sensor_tof_flag == 1) {
		minThreshold_ps[0] = 0;
		minThreshold_ps[1] = 0;
		minThreshold_ps[2] = 0;
		minThreshold_ps[3] = 0;
		maxThreshold_ps[0] = tof_data.tof_calib_zero_threshold;
		maxThreshold_ps[1] = tof_data.tof_calib_6cm_threshold;
		maxThreshold_ps[2] = tof_data.tof_calib_10cm_threshold;
		maxThreshold_ps[3] = tof_data.tof_calib_60cm_threshold;
		hwlog_info("tof sensor maxThreshold %d %d %d %d\n",
			tof_data.tof_calib_zero_threshold,
			tof_data.tof_calib_6cm_threshold,
			tof_data.tof_calib_10cm_threshold,
			tof_data.tof_calib_60cm_threshold);
	}
	hwlog_info("ps sensor offset_min=%d,offset_max=%d\n",
		-ps_data.offset_min, ps_data.offset_max);
	#endif

	#ifdef SENSOR_DATA_ACQUISITION
	memset(&ps_test, 0, sizeof(ps_test));
	#endif
	if ((sensor_tof_flag != 1) && (sensor_ps_flag != 1)) {
		hwlog_info("ps sensor is not txc_ps_224 or ams_tmd2620 or avago_apds9110 or ams_tmd3725 or liteon_ltr582 or ps_cali_supported,no need calibrate\n");
		return count;
	}

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;
	hwlog_info("ps or tof calibrate order is %lu\n", val);
	if ((val < PS_XTALK_CALIBRATE) || (val > PS_SCREEN_ON_3CM_CALIBRATE)) {
		hwlog_err("set ps or tof calibrate val invalid,val=%lu\n", val);
		ps_calibration_res = EXEC_FAIL;
		return count;
	}
	calibrate_order = (uint8_t)val;
	/* ps calibrate */
	if (((val >= PS_XTALK_CALIBRATE && val <= PS_3CM_CALIBRATE) ||
		val == PS_MINPDATA_MODE || val == PS_SAVE_MINPDATA ||
		val >= PS_SCREEN_ON_XTALK_CALIBRATE) && (sensor_tof_flag != 1)) {
		pkg_mcu = send_calibrate_cmd(TAG_PS, val, &ps_calibration_res);
		/* COMMU_FAIL=4	EXEC_FAIL=2 */
		if (ps_calibration_res == COMMU_FAIL ||
		    ps_calibration_res == EXEC_FAIL)
			goto save_log;
		else if (pkg_mcu.errno == 0)
			ps_calibrate_process(val, &pkg_mcu);

		if ((val == PS_5CM_CALIBRATE) || (val == PS_MINPDATA_MODE) ||
		    (val == PS_SCREEN_ON_5CM_CALIBRATE))
			ps_calibration_res = SUC;
		else
			ps_calibrate_save(ps_sensor_offset,
				sizeof(ps_sensor_offset));
	} else {
		/* tof calibrate */
		pkg_mcu = send_calibrate_cmd(TAG_TOF, val, &ps_calibration_res);
		if (ps_calibration_res == COMMU_FAIL ||
			ps_calibration_res == EXEC_FAIL)
			goto save_log;
		else if (pkg_mcu.errno == 0) {
			if (val == TOF_CALI_FOR_FIX) {
				hwlog_info("tof fix calibrate succ\n");
				ps_calibration_res = SUC;
				goto save_log;
			}
			tof_calibrate_save(pkg_mcu.data, pkg_mcu.data_length,
				calibrate_order);
		}
	}
save_log:
	ps_cali_data = ps_calib_data_for_data_collect;
	#ifdef SENSOR_DATA_ACQUISITION
	hwlog_info("%s:is sport data collect\n", __func__);
	if (apds9253_006_ps_flag == 1) {
		digital_offset = PS_GET_DIGITAL_OFFSET(ps_cali_data[3]);
		ps_cali_data[3] = PS_GET_ANALOGOU_OFFSET(ps_cali_data[3]);
		ps_add_do_enq_work(&digital_offset);
		msleep(5);
	}
	minThreshold = minThreshold_ps;
	maxThreshold = maxThreshold_ps;
	ps_test.cal_value = ps_cali_data;
	ps_test.first_item = PS_CALI_XTALK;
	ps_test.value_num = PS_CAL_NUM;
	ps_test.threshold_num = PS_THRESHOLD_NUM;
	ps_test.min_threshold = minThreshold;
	ps_test.max_threshold = maxThreshold;
	memcpy(ps_test.name, PS_TEST_CAL, sizeof(PS_TEST_CAL));
	memcpy(ps_test.result, PS_CAL_RESULT, (strlen(PS_CAL_RESULT)+1));
	for (pPsTest = 0; pPsTest < PS_CAL_NUM; pPsTest++)
		ps_test.test_name[pPsTest] = ps_test_name[pPsTest];

	enq_notify_work_sensor(ps_test);
	#endif
	return count;
}
static DEVICE_ATTR(ps_calibrate, 0664,
	attr_ps_calibrate_show, attr_ps_calibrate_write);

static ssize_t attr_als_calibrate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int val = als_calibration_res;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static int write_als_offset_to_nv(const char *temp)
{
	int ret = 0;
	const uint16_t *poffset_data = (const uint16_t *)user_info.nv_data;

	if (!temp) {
		hwlog_err("write_als_offset_to_nv fail, invalid para\n");
		return -1;
	}

	if (write_calibrate_data_to_nv(ALS_CALIDATA_NV_NUM,
		ALS_CALIDATA_NV_SIZE, "LSENSOR", temp))
		return -1;
	memcpy((void *)als_sensor_calibrate_data,
		(void *)temp, MAX_SENSOR_CALIBRATE_DATA_LENGTH);
	hwlog_info("nve_direct_access als write temp %d %d %d %d %d %d\n",
		   *poffset_data, *(poffset_data + 1), *(poffset_data + 2),
		   *(poffset_data + 3), *(poffset_data + 4), *(poffset_data + 5));
	return ret;
}

int write_als_dark_noise_offset_to_nv(uint16_t *dark_noise_offset)
{
	const uint16_t *poffset_data = (uint16_t *)als_sensor_calibrate_data;

	if (read_calibrate_data_from_nv(ALS_CALIDATA_NV_NUM,
		ALS_CALIDATA_NV_SIZE, "LSENSOR"))
		return -1;
	memcpy(als_sensor_calibrate_data, user_info.nv_data,
		ALS_CALIDATA_NV_SIZE);
	memcpy(als_sensor_calibrate_data + ALS_CALIDATA_NV_SIZE,
		dark_noise_offset, sizeof(uint16_t));
	msleep(10);
	if (write_calibrate_data_to_nv(ALS_CALIDATA_NV_NUM,
		ALS_CALIDATA_NV_SIZE_WITH_DARK_NOISE_OFFSET,
		"LSENSOR", als_sensor_calibrate_data))
		return -1;
	hwlog_info("write_als_dark_noise_offset_to_nv %d %d %d %d %d %d %d\n",
		   *poffset_data, *(poffset_data + 1), *(poffset_data + 2),
		   *(poffset_data + 3), *(poffset_data + 4),
		   *(poffset_data + 5), *(poffset_data + 6));
	return 0;
}

#ifdef SENSOR_DATA_ACQUISITION
static void als_dark_noise_offset_enq_notify_work(const int item_id,
	uint16_t value, uint16_t min_threshold, uint16_t max_threshold)
{
	int ret;
	struct event als_dark_noise_offset_event;

	memset(&als_dark_noise_offset_event, 0,
		sizeof(als_dark_noise_offset_event));

	als_dark_noise_offset_event.item_id = item_id;
	memcpy(als_dark_noise_offset_event.device_name,
		ALS_TEST_CAL, sizeof(ALS_TEST_CAL));
	memcpy(als_dark_noise_offset_event.result,
		ALS_CAL_RESULT, strlen(ALS_CAL_RESULT) + 1);
	memcpy(als_dark_noise_offset_event.test_name,
		ALS_DARK_CALI_NAME, sizeof(ALS_DARK_CALI_NAME));
	snprintf(als_dark_noise_offset_event.value,
		MAX_VAL_LEN, "%d", value);
	snprintf(als_dark_noise_offset_event.min_threshold,
		MAX_VAL_LEN, "%u", min_threshold);
	snprintf(als_dark_noise_offset_event.max_threshold,
		MAX_VAL_LEN, "%u", max_threshold);

	ret = enq_msg_data_in_sensorhub_single(als_dark_noise_offset_event);
	if (ret > 0)
		hwlog_info("als_dark_noise_offset_enq_notify_work succ!!item_id=%d\n",
			als_dark_noise_offset_event.item_id);
	else
		hwlog_info("als_dark_noise_offset_enq_notify_work failed\n");

}
#endif

static int als_calibrate_save(const void *buf, int length)
{
	const uint16_t *poffset_data = (const uint16_t *)buf;
	int ret;

	if (!buf || length <= 0) {
		hwlog_err("%s invalid argument", __func__);
		als_calibration_res = EXEC_FAIL;
		return -1;
	}

	hwlog_info("%s:als calibrate ok, %d  %d  %d %d  %d  %d\n", __func__,
		   *poffset_data, *(poffset_data + 1), *(poffset_data + 2),
		   *(poffset_data + 3), *(poffset_data + 4), *(poffset_data + 5));
	ret = write_als_offset_to_nv((char *)buf);
	if (ret) {
		hwlog_err("nv write fail\n");
		als_calibration_res = NV_FAIL;
		return -1;
	}
	als_calibration_res = SUC;
	return 0;
}

#define APDS9253_DARK_NOISE_OFFSET_MAX 10
#define APDS9253_DARK_NOISE_OFFSET_MIN 0
static ssize_t attr_als_calibrate_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	int ret;
	read_info_t pkg_mcu;
#ifdef SENSOR_DATA_ACQUISITION
	uint16_t *cali_data_u16 = NULL;
	int32_t *minThreshold = NULL;
	int32_t *maxThreshold = NULL;
#endif
	interval_param_t param;
	int bh1749_flag;
	#ifdef SENSOR_DATA_ACQUISITION
	int32_t als_cali_data_int32[ALS_CAL_NUM] = { 0 };
	int32_t minThreshold_als[ALS_CAL_NUM] = {
		min_threshold_als_para, min_threshold_als_para,
		min_threshold_als_para, min_threshold_als_para,
		min_threshold_als_para, min_threshold_als_para
	};
	int32_t maxThreshold_als[ALS_CAL_NUM] = {
		max_threshold_als_para, max_threshold_als_para,
		max_threshold_als_para, max_threshold_als_para,
		max_threshold_als_para, max_threshold_als_para
	};
	struct sensor_eng_cal_test als_test;
	int pAlsTest = 0;
	#endif

	#ifdef SENSOR_DATA_ACQUISITION
	memset(&als_test, 0, sizeof(als_test));
	#endif
	bh1749_flag = sensor_get_als_bh1749_flag();
	if (rohm_rgb_flag != 1 && avago_rgb_flag != 1 &&
	    ams_tmd3725_rgb_flag != 1 && liteon_ltr582_rgb_flag != 1 &&
	    is_cali_supported != 1 && apds9999_rgb_flag != 1 &&
	    ams_tmd3702_rgb_flag != 1 && apds9253_rgb_flag != 1 &&
	    vishay_vcnl36658_als_flag != 1 && ams_tcs3701_rgb_flag != 1 &&
	    bh1749_flag != 1 && vd6281_als_flag != 1 &&
	    ams_tcs3707_rgb_flag != 1) {
		hwlog_info("als sensor is not rohm_bh1745 or avago apds9251 or ams_tmd3725 or liteon_ltr582 , is_cali_supported = %d, no need calibrate\n",
			   is_cali_supported);
		return count;
	}

	ret = get_tpcolor_from_nv();
	if (ret)
		hwlog_err("cali write, get_tpcolor_from_nv fail, ret=%d", ret);
	else
		hwlog_info("cali write, get_tpcolor_from_nv success\n");

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (!(val == 2 && (als_data.als_phone_type == LAYA_PHONE_TYPE ||
		is_cali_supported)) && val != 1)
		return count;

	/* if ALS is not opened, open first */
	if (sensor_status.opened[TAG_ALS] == 0) {
		als_close_after_calibrate = true;
		hwlog_info("send als open cmd(during calibrate) to mcu\n");
		ret = inputhub_sensor_enable(TAG_ALS, true);
		if (ret) {
			als_calibration_res = COMMU_FAIL;
			hwlog_err("send ALS open cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	} else {
		als_close_after_calibrate = false;
	}
	/* period must <= 100 ms */
	if ((sensor_status.delay[TAG_ALS] == 0) ||
	    (sensor_status.delay[TAG_ALS] > 100)) {
		hwlog_info("send als setdelay cmd(during calibrate) to mcu\n");
		memset(&param, 0, sizeof(param));
		param.period = 100;
		ret = inputhub_sensor_setdelay(TAG_ALS, &param);
		if (ret) {
			als_calibration_res = COMMU_FAIL;
			hwlog_err("send ALS set delay cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}
	msleep(350);
	/* send calibrate command, need set delay first */
	pkg_mcu = send_calibrate_cmd(TAG_ALS, val, &als_calibration_res);
	if (als_calibration_res == COMMU_FAIL) {
		return count;
	} else if (pkg_mcu.errno == 0) {
		if (val == 2 && als_data.als_phone_type == LAYA_PHONE_TYPE)
			write_als_dark_noise_offset_to_nv((uint16_t *)pkg_mcu.data);
		else
			als_calibrate_save(pkg_mcu.data, pkg_mcu.data_length);
	}

	if (val == 2 &&
		(als_data.als_phone_type == LAYA_PHONE_TYPE || is_cali_supported)) {
		#ifdef SENSOR_DATA_ACQUISITION
		cali_data_u16 = (uint16_t *)pkg_mcu.data;
		als_dark_noise_offset_enq_notify_work(ALS_CALI_DARK_OFFSET_MSG,
			*cali_data_u16, APDS9253_DARK_NOISE_OFFSET_MIN,
			APDS9253_DARK_NOISE_OFFSET_MAX);
		#endif
	} else {
		#ifdef SENSOR_DATA_ACQUISITION
		cali_data_u16 = (uint16_t *)pkg_mcu.data;
		als_cali_data_int32[0] = *cali_data_u16;
		als_cali_data_int32[1] = *(cali_data_u16 + 1);
		als_cali_data_int32[2] = *(cali_data_u16 + 2);
		als_cali_data_int32[3] = *(cali_data_u16 + 3);
		als_cali_data_int32[4] = *(cali_data_u16 + 4);
		als_cali_data_int32[5] = *(cali_data_u16 + 5);
		hwlog_info("als calibrate data for collect, %d %d %d %d %d %d\n",
			*cali_data_u16, *(cali_data_u16 + 1),
			*(cali_data_u16 + 2), *(cali_data_u16 + 3),
			*(cali_data_u16 + 4), *(cali_data_u16 + 5));

		minThreshold = minThreshold_als;
		maxThreshold = maxThreshold_als;
		als_test.cal_value = als_cali_data_int32;
		als_test.first_item = ALS_CALI_R_MSG;
		als_test.value_num = ALS_CAL_NUM;
		als_test.threshold_num = ALS_THRESHOLD_NUM;
		als_test.min_threshold = minThreshold;
		als_test.max_threshold = maxThreshold;

		memcpy(als_test.name, ALS_TEST_CAL, sizeof(ALS_TEST_CAL));
		memcpy(als_test.result, ALS_CAL_RESULT,
			(strlen(ALS_CAL_RESULT) + 1));
		for (pAlsTest = 0; pAlsTest < ALS_CAL_NUM; pAlsTest++)
			als_test.test_name[pAlsTest] = als_test_name[pAlsTest];
		enq_notify_work_sensor(als_test);
		#endif
	}

	if (als_close_after_calibrate == true) {
		als_close_after_calibrate = false;
		hwlog_info("send als close cmd(during calibrate) to mcu.\n");
		ret = inputhub_sensor_enable(TAG_ALS, false);
		if (ret) {
			als_calibration_res = COMMU_FAIL;
			hwlog_err("send ALS close cmd(during calibrate) to mcu fail,ret=%d\n",
				ret);
			return count;
		}
	}
	return count;
}

static DEVICE_ATTR(als_calibrate, 0664, attr_als_calibrate_show, attr_als_calibrate_write);

static int cap_prox1_calibrate_save(void *buf, int length)
{
	int ret = 0;
	u32 *pcaldata = NULL;

	if (!buf) {
		hwlog_err("%s invalid argument", __func__);
		return_cap_prox1_calibration = EXEC_FAIL;
		return -1;
	}
	pcaldata = (u32 *)buf;
	hwlog_err("%s:cap_prox1 calibrate ok, %08x  %08x  %08x\n", __func__,
		pcaldata[0], pcaldata[1], pcaldata[2]);

	if (read_calibrate_data_from_nv(CAP_PROX1_CALIDATA_NV_NUM,
		CAP_PROX1_CALIDATA_NV_SIZE, "CSENSOR1")) {
		return_cap_prox1_calibration = EXEC_FAIL;
		hwlog_err("nve_direct_access read error %d\n", ret);
		return -2;
	}

	if (write_calibrate_data_to_nv(CAP_PROX1_CALIDATA_NV_NUM, length,
		"CSENSOR1", buf)) {
		return_cap_prox1_calibration = EXEC_FAIL;
		hwlog_err("nve_direct_access write error %d\n", ret);
		return -3;
	}
	return_cap_prox1_calibration = SUC;
	return 0;
}

static void cap_prox1_calibrate_work_func(struct work_struct *work)
{
	int ret;

	hwlog_info("cap_prox1 calibrate work enter ++\n");
	ret = cap_prox1_calibrate_save(cap_prox1_calibrate_data,
		cap_prox1_calibrate_len);
	if (ret < 0)
		hwlog_err("nv write failed\n");
	hwlog_info("cap_prox1 calibrate work enter --\n");
}

static void cap_prox_calibrate_data_process(int calibrate_index,
	const char *data, uint16_t *diff_value, uint16_t *offset_value,
	uint16_t *calibrate_thred, int sar_type)
{
	int32_t min_threshold[CAP_PROX_THRESHOLD_NUM] = { 0 };
	int32_t max_threshold[CAP_PROX_THRESHOLD_NUM] = { 0 };
	int32_t cap_prox_data[CAP_PROX_CAL_NUM] = { 0 };

	if (!diff_value || !offset_value || !calibrate_thred || !data) {
		hwlog_err("%s: input error\n", __func__);
		return;
	}
	switch (calibrate_index) {
	case NEAR_CALIBRATE:
		memcpy(diff_value, data,
			sizeof(uint16_t) * SAR_SEMTECH_USE_PH_NUM);
		cap_prox_data[0] = diff_value[0];
		cap_prox_data[1] = diff_value[1];
		min_threshold[0] = calibrate_thred[DIFF_MIN_THREDHOLD];
		max_threshold[0] = calibrate_thred[DIFF_MAX_THREDHOLD];
		break;
	case FAR_CALIBRATE:
		memcpy(offset_value, data,
			sizeof(uint16_t) * SAR_SEMTECH_USE_PH_NUM);
		cap_prox_data[0] = offset_value[0];
		cap_prox_data[1] = offset_value[1];
		min_threshold[0] = calibrate_thred[OFFSET_MIN_THREDHOLD];
		max_threshold[0] = calibrate_thred[OFFSET_MAX_THREDHOLD];
		break;
	default:
		hwlog_err("%s: calibrate_index error\n", __func__);
		break;
	}
	hwlog_info("%s: offset1=%d offset2=%d diff1=%d diff2=%d\n", __func__,
		offset_value[0], offset_value[1],
		diff_value[0], diff_value[1]);

#ifdef SENSOR_DATA_ACQUISITION
	cap_prox_do_enq_work(calibrate_index, cap_prox_data,
		min_threshold, max_threshold, sar_type);
#endif
}

static ssize_t attr_cap_prox1_calibrate_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val;
	int calibrate_index;
	read_info_t pkg_mcu;
	uint16_t *diff_value = NULL;
	uint16_t *offset_value = NULL;
	uint16_t *calibrate_thred = NULL;

	if (strlen(sensor_chip_info[CAP_PROX1]) == 0) {
		hwlog_err("%s: no sar sensor\n", __func__);
		return -EINVAL;
	}
	if (kstrtoul(buf, TO_DECIMALISM, &val))
		return -EINVAL;
	if (val >= SAR_CALIBRATE_END || val < NEAR_CALIBRATE) {
		hwlog_err("%s: val = %lu\n", __func__, val);
		return -EINVAL;
	}
	return_cap_prox1_calibration = EXEC_FAIL;
	calibrate_index = (int)val;
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));
	pkg_mcu = send_calibrate_cmd(TAG_CAP_PROX1, val,
		&return_cap_prox1_calibration);
	if (return_cap_prox1_calibration == COMMU_FAIL || pkg_mcu.errno != 0) {
		hwlog_err("%s: errno = %d\n", __func__, pkg_mcu.errno);
		return count;
	}
	if (!strncmp(sensor_chip_info[CAP_PROX1], "huawei,semtech-sx9323",
		strlen("huawei,semtech-sx9323"))) {
		diff_value = g_sar_calibrate_datas.semtech_cali_data.diff;
		offset_value = g_sar_calibrate_datas.semtech_cali_data.offset;
		calibrate_thred = sar1_pdata.sar_datas.semteck_data.calibrate_thred;
	} else if (!strncmp(sensor_chip_info[CAP_PROX1],
		"huawei,semtech-sx9335", strlen("huawei,semtech-sx9335"))) {
		diff_value = g_sar_calibrate_datas.semtech_9335_cali_data.diff;
		offset_value = g_sar_calibrate_datas.semtech_9335_cali_data.offset;
		calibrate_thred = sar1_pdata.sar_datas.semteck_data.calibrate_thred;
	} else if (!strncmp(sensor_chip_info[CAP_PROX1],
		"huawei,abov-a96t3x6", strlen("huawei,abov-a96t3x6"))) {
		diff_value = sar_calibrate_datas.abov_cali_data.diff;
		offset_value = sar_calibrate_datas.abov_cali_data.offset;
		calibrate_thred = sar1_pdata.sar_datas.abov_data.calibrate_thred;
	}

	cap_prox_calibrate_data_process(calibrate_index, pkg_mcu.data,
		diff_value, offset_value,
		calibrate_thred, HW_CAP_PROX1_SENSOR);
	cap_prox1_calibrate_len = sizeof(g_sar_calibrate_datas);
	if (cap_prox1_calibrate_len > sizeof(cap_prox1_calibrate_data))
		cap_prox1_calibrate_len = sizeof(cap_prox1_calibrate_data);
	memset(cap_prox1_calibrate_data, 0, cap_prox1_calibrate_len);
	memcpy(cap_prox1_calibrate_data, &g_sar_calibrate_datas,
		cap_prox1_calibrate_len);
	INIT_WORK(&cap_prox1_calibrate_work, cap_prox1_calibrate_work_func);
	queue_work(system_power_efficient_wq, &cap_prox1_calibrate_work);

	return count;
}

static ssize_t attr_cap_prox_calibrate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	u32 *pcaldata = NULL;

	if (read_calibrate_data_from_nv(CAP_PROX_CALIDATA_NV_NUM,
		CAP_PROX_CALIDATA_NV_SIZE, "Csensor")) {
		hwlog_err("nve_direct_access read error %d\n", ret);
		return snprintf(buf, PAGE_SIZE, "%d\n",
			return_cap_prox_calibration);
	}

	pcaldata = (u32 *)(user_info.nv_data + 4);
	return snprintf(buf, PAGE_SIZE, "%d:%08x %08x %08x\n",
		return_cap_prox_calibration,
		pcaldata[0], pcaldata[1], pcaldata[2]);
}

static int cap_prox_calibrate_save(void *buf, int length)
{
	int ret = 0;
	u32 *pcaldata = NULL;

	if (!buf) {
		hwlog_err("%s invalid argument", __func__);
		return_cap_prox_calibration = EXEC_FAIL;
		return -1;
	}
	pcaldata = (u32 *)buf;
	hwlog_err("%s:cap_prox calibrate ok, %08x  %08x  %08x\n",
		__func__, pcaldata[0], pcaldata[1], pcaldata[2]);

	if (read_calibrate_data_from_nv(CAP_PROX_CALIDATA_NV_NUM,
		CAP_PROX_CALIDATA_NV_SIZE, "Csensor")) {
		return_cap_prox_calibration = EXEC_FAIL;
		hwlog_err("nve_direct_access read error %d\n", ret);
		return -2;
	}
	if (write_calibrate_data_to_nv(CAP_PROX_CALIDATA_NV_NUM,
		length, "Csensor", buf)) {
		return_cap_prox_calibration = EXEC_FAIL;
		hwlog_err("nve_direct_access write error %d\n", ret);
		return -3;
	}
	return_cap_prox_calibration = SUC;
	return 0;
}

static void cap_prox_calibrate_work_func(struct work_struct *work)
{
	int ret;

	hwlog_info("cap_prox calibrate work enter ++\n");
	ret = cap_prox_calibrate_save(cap_prox_calibrate_data,
		cap_prox_calibrate_len);
	if (ret < 0)
		hwlog_err("nv write faild\n");
	hwlog_info("cap_prox calibrate work enter --\n");
}


static ssize_t attr_cap_prox_calibrate_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val;
	int calibrate_index;
	read_info_t pkg_mcu;
	uint16_t *diff_value = NULL;
	uint16_t *offset_value = NULL;
	uint16_t *calibrate_thred = NULL;

	if (strlen(sensor_chip_info[CAP_PROX]) == 0) {
		hwlog_err("%s: no sar sensor\n", __func__);
		return -EINVAL;
	}
	if (kstrtoul(buf, TO_DECIMALISM, &val))
		return -EINVAL;
	if (val >= SAR_CALIBRATE_END || val < NEAR_CALIBRATE) {
		hwlog_err("%s: val = %lu\n", __func__, val);
		return -EINVAL;
	}
	return_cap_prox_calibration = EXEC_FAIL;
	calibrate_index = (int)val;
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));
	pkg_mcu = send_calibrate_cmd(TAG_CAP_PROX, val,
		&return_cap_prox_calibration);
	if (return_cap_prox_calibration == COMMU_FAIL || pkg_mcu.errno != 0) {
		hwlog_err("%s: errno = %d\n", __func__, pkg_mcu.errno);
		return count;
	}
	if (!strncmp(sensor_chip_info[CAP_PROX], "huawei,semtech-sx9323",
		strlen("huawei,semtech-sx9323"))) {
		diff_value = sar_calibrate_datas.semtech_cali_data.diff;
		offset_value = sar_calibrate_datas.semtech_cali_data.offset;
		calibrate_thred = sar_pdata.sar_datas.semteck_data.calibrate_thred;
	} else if (!strncmp(sensor_chip_info[CAP_PROX], "huawei,abov-a96t3x6",
		strlen("huawei,abov-a96t3x6"))) {
		diff_value = sar_calibrate_datas.abov_cali_data.diff;
		offset_value = sar_calibrate_datas.abov_cali_data.offset;
		calibrate_thred = sar_pdata.sar_datas.abov_data.calibrate_thred;
	}
	cap_prox_calibrate_data_process(calibrate_index, pkg_mcu.data,
		diff_value, offset_value,
		calibrate_thred, HW_CAP_PROX_SENSOR);
	cap_prox_calibrate_len = sizeof(sar_calibrate_datas);
	if (cap_prox_calibrate_len > sizeof(cap_prox_calibrate_data))
		cap_prox_calibrate_len = sizeof(cap_prox_calibrate_data);
	memset(cap_prox_calibrate_data, 0, cap_prox_calibrate_len);
	memcpy(cap_prox_calibrate_data,
		&sar_calibrate_datas, cap_prox_calibrate_len);
	INIT_WORK(&cap_prox_calibrate_work, cap_prox_calibrate_work_func);
	queue_work(system_power_efficient_wq, &cap_prox_calibrate_work);
	return count;
}


static DEVICE_ATTR(cap_prox_calibrate, 0664,
	attr_cap_prox_calibrate_show, attr_cap_prox_calibrate_write);

/*
 * if val is odd, then last status is sleep,
 * if is even number, then last status is wakeup
 */
static ssize_t attr_iom3_sr_test_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	unsigned long times;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	times = val;
	if (val > 0) {
		for (; val > 0; val--) {
			disable_sensors_when_suspend();
			tell_ap_status_to_mcu(ST_SLEEP);
			msleep(2);
			tell_ap_status_to_mcu(ST_WAKEUP);
			enable_sensors_when_resume();
		}
		if (times % 2) {
			tell_ap_status_to_mcu(ST_SLEEP);
			enable_sensors_when_resume();
		}
	}
	return count;
}

static DEVICE_ATTR(iom3_sr_test, 0660, NULL, attr_iom3_sr_test_store);

int fingersense_commu(unsigned int cmd, unsigned int pare,
	unsigned int responsed, bool is_subcmd)
{
	int ret = -1;
	write_info_t pkg_ap;
	read_info_t pkg_mcu;
	pkt_parameter_req_t cpkt;
	pkt_header_t *hd = (pkt_header_t *)&cpkt;

	memset(&pkg_ap, 0, sizeof(pkg_ap));
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));

	if (is_subcmd) {
		pkg_ap.tag = TAG_FINGERSENSE;
		pkg_ap.cmd = CMD_CMN_CONFIG_REQ;
		cpkt.subcmd = cmd;
		pkg_ap.wr_buf = &hd[1];
		pkg_ap.wr_len = sizeof(pare) + SUBCMD_LEN;
		memcpy(cpkt.para, &pare, sizeof(pare));
	} else {
		pkg_ap.tag = TAG_FINGERSENSE;
		pkg_ap.cmd = cmd;
		pkg_ap.wr_buf = &pare;
		pkg_ap.wr_len = sizeof(pare);
	}
	/*
	 * NO_RESP: enable/disable fingersense
	 * RESP: request fingersense data need response
	 */
	if (responsed == NO_RESP)
		ret = write_customize_cmd(&pkg_ap, NULL, true);
	else
		ret = write_customize_cmd(&pkg_ap, &pkg_mcu, true);
	if (ret) {
		hwlog_err("send finger sensor cmd %d to mcu fail,ret=%d\n",
			cmd, ret);
		return ret;
	}
	if (pkg_mcu.errno != 0)
		hwlog_err("send finger sensor cmd %d to mcu fail %d\n",
			cmd, pkg_mcu.errno);
	return ret;
}

int fingersense_enable(unsigned int enable)
{
	unsigned int cmd = 0;
	unsigned int delay = 10000;
	unsigned int ret = -1;

	/* enable-1: use main screen; enable-2: use assit screen */
	if (enable == FINGERSENSE_ENABLE_ACCEL ||
	    enable == FINGERSENSE_ENABLE_ACC1) {
		cmd = CMD_CMN_OPEN_REQ;
		ret = fingersense_commu(cmd, enable, NO_RESP, false);
		if (ret) {
			hwlog_err("%s: finger sense enable fail\n", __func__);
			return ret;
		}

		cmd = CMD_CMN_INTERVAL_REQ;
		ret = fingersense_commu(cmd, delay, NO_RESP, false);
		if (ret) {
			hwlog_err("%s: set delay fail\n", __func__);
			return ret;
		}

		if (enable == FINGERSENSE_ENABLE_ACCEL)
			cmd = SUB_CMD_TOUCH_FINGERSENSE_MAIN_SCREEN;
		else if (enable == FINGERSENSE_ENABLE_ACC1)
			cmd = SUB_CMD_TOUCH_FINGERSENSE_ASSIT_SCREEN;
		ret = fingersense_commu(cmd, cmd, NO_RESP, true);
		if (ret) {
			hwlog_err("%s: set config fail\n", __func__);
			return ret;
		}
		hwlog_info("%s: finger sense enable succsess\n", __func__);
	} else {
		cmd = CMD_CMN_CLOSE_REQ;
		ret = fingersense_commu(cmd, enable, NO_RESP, false);
		if (ret) {
			hwlog_info("%s: finger sense close fail\n", __func__);
			return ret;
		}
		hwlog_info("%s: finger sense close succsess\n", __func__);
	}

	return 0;
}

static ssize_t attr_set_fingersense_enable(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;

	if (strict_strtoul(buf, 10, &val)) {
		hwlog_err("%s: finger sense enable val %lu invalid",
			__func__, val);
		return -EINVAL;
	}

	hwlog_info("%s: finger sense enable val %ld\n", __func__, val);
	if ((val != FINGERSENSE_DISABLE) && (val != FINGERSENSE_ENABLE_ACCEL) &&
		(val != FINGERSENSE_ENABLE_ACC1)) {
		hwlog_err("%s:finger sense set enable fail, invalid val\n",
			__func__);
		return size;
	}

	if (fingersense_enabled == val) {
		hwlog_info("%s:finger sense already at seted state,fingersense_enabled:%d\n",
			__func__, fingersense_enabled);
		return size;
	}
	ret = fingersense_enable(val);
	if (ret) {
		hwlog_err("%s: finger sense enable fail: %d\n",
			__func__, ret);
		return size;
	}
	fingersense_enabled = val;

	return size;
}

static ssize_t attr_get_fingersense_enable(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_STR_SIZE, "%d\n", fingersense_enabled);
}

static DEVICE_ATTR(set_fingersense_enable, 0660,
	attr_get_fingersense_enable, attr_set_fingersense_enable);

static ssize_t attr_fingersense_data_ready(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_STR_SIZE, "%d\n", fingersense_data_ready);
}

static DEVICE_ATTR(fingersense_data_ready, 0440,
	attr_fingersense_data_ready, NULL);

static ssize_t attr_fingersense_latch_data(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int size;

	size = sizeof(fingersense_data) < MAX_STR_SIZE ?
		sizeof(fingersense_data) : MAX_STR_SIZE;

	if ((!fingersense_data_ready) || (!fingersense_enabled)) {
		hwlog_err("%s:fingersense zaxix not ready %d or not enable %d\n",
		     __func__, fingersense_data_ready, fingersense_enabled);
		return size;
	}
	memcpy(buf, (char *)fingersense_data, size);
	return size;
}

static DEVICE_ATTR(fingersense_latch_data, 0440,
	attr_fingersense_latch_data, NULL);

/* Calculate whether a is in the range of [b, c] */
int is_time_inrange(unsigned long a, unsigned long b, unsigned long c)
{
	return ((long)(a - b) >= 0) && ((long)(a - c) <= 0);
}

static ssize_t attr_fingersense_req_data(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int ret;
	unsigned int sub_cmd = SUB_CMD_ACCEL_FINGERSENSE_REQ_DATA_REQ;
	unsigned int skip = 0;
	unsigned long local_jiffies = jiffies;
	unsigned long flags = 0;

#if defined(CONFIG_HISI_VIBRATOR)
	if ((vibrator_shake == 1) ||
	    ((HALL_COVERD & (unsigned int)hall_value) && (hall_sen_type == 0))) {
		hwlog_err("coverd, vibrator shaking, not send fingersense req data cmd to mcu\n");
		return -1;
	}
#endif
	if (!fingersense_enabled) {
		hwlog_err("%s: finger sense not enable,  dont req data\n",
			__func__);
		return -1;
	}

	spin_lock_irqsave(&fsdata_lock, flags);

	/*
	 * We started transmitting the data in recent time.
	 * It's just on the way. Wait for it.
	 */
	if (fingersense_data_intrans &&
	    is_time_inrange(fingersense_data_ts,
			local_jiffies - FINGERSENSE_TRANS_TOUT, local_jiffies))
		skip = 1;

	/* The data was collected a short while ago. Just use it. */
	if (fingersense_data_ready &&
	    (is_time_inrange(fingersense_data_ts,
			local_jiffies - FINGERSENSE_FRESH_TIME, local_jiffies)))
		skip = 1;

	if (skip) {
		spin_unlock_irqrestore(&fsdata_lock, flags);
		return size;
	}

	fingersense_data_ready = false;
	fingersense_data_intrans = true; /* the data is on the way */
	fingersense_data_ts = jiffies;   /* record timestamp for the data */
	spin_unlock_irqrestore(&fsdata_lock, flags);
	ret = fingersense_commu(sub_cmd, sub_cmd, NO_RESP, true);
	if (ret) {
		spin_lock_irqsave(&fsdata_lock, flags);
		fingersense_data_intrans = false;
		spin_unlock_irqrestore(&fsdata_lock, flags);
		hwlog_err("%s: finger sense send requst data failed\n",
			__func__);
	}
	return size;
}

void preread_fingersense_data(void)
{
#if defined(CONFIG_HISI_VIBRATOR)
	if ((vibrator_shake == 1) || (HALL_COVERD & (unsigned int)hall_value))
		return;
#endif

	if (!fingersense_enabled)
		return;

	attr_fingersense_req_data(NULL, NULL, NULL, 0);
}
EXPORT_SYMBOL(preread_fingersense_data);

static DEVICE_ATTR(fingersense_req_data, 0220, NULL, attr_fingersense_req_data);

/* acc enable node */
#define SHOW_ENABLE_FUNC(NAME, TAG)\
static ssize_t show_##NAME##_enable_result(struct device *dev, \
	struct device_attribute *attr, char *buf)\
{\
	return snprintf(buf, MAX_STR_SIZE, "%d\n", sensor_status.status[TAG]);\
}

#define STORE_ENABLE_FUNC(NAME, TAG, CMD1, CMD2)\
static ssize_t attr_set_##NAME##_enable(struct device *dev, \
	struct device_attribute *attr, const char *buf, size_t size)\
{\
	unsigned long val = 0;\
	int ret = -1;\
	write_info_t	pkg_ap;\
	read_info_t pkg_mcu;\
	memset(&pkg_ap, 0, sizeof(pkg_ap));\
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));\
	if (strict_strtoul(buf, 10, &val))\
		return -EINVAL;\
	if (val == 1) {\
		pkg_ap.tag = TAG;\
		pkg_ap.cmd = CMD1;\
		pkg_ap.wr_buf = NULL;\
		pkg_ap.wr_len = 0;\
		ret = write_customize_cmd(&pkg_ap,  &pkg_mcu, true);\
		if (ret) {\
			hwlog_err("send %s enable cmd to mcu fail,ret=%d\n", \
				#NAME, ret);\
			return size;\
		} \
		if (pkg_mcu.errno != 0) \
			hwlog_err("set %s enable fail\n", #NAME);\
		else \
			hwlog_info("%s enable success\n", #NAME);\
		if ((TAG == TAG_ACCEL) && (acc_close_after_calibrate == 1)) {\
			acc_close_after_calibrate = 0;\
			hwlog_info("%s received open command during calibrate, will not close after calibrate\n", \
				#NAME);\
		} \
	} else {\
		pkg_ap.tag = TAG;\
		pkg_ap.cmd = CMD2;\
		pkg_ap.wr_buf = NULL;\
		pkg_ap.wr_len = 0;\
		ret = write_customize_cmd(&pkg_ap,  &pkg_mcu, true);\
		if (ret) {\
			hwlog_err("send %s disable cmd to mcu fail,ret=%d\n", \
				#NAME, ret);\
			return size;\
		} \
		if (pkg_mcu.errno != 0)\
			hwlog_err("set %s disable fail\n", #NAME);\
		else\
			hwlog_info("%s disable success\n", #NAME);\
	} \
	return size;\
}

#define SHOW_DELAY_FUNC(NAME, TAG) \
static ssize_t show_##NAME##_delay_result(struct device *dev, \
	struct device_attribute *attr, char *buf)\
{\
	return snprintf(buf, MAX_STR_SIZE, "%d\n", sensor_status.delay[TAG]);\
}

#define STORE_DELAY_FUNC(NAME, TAG, CMD)  \
static ssize_t attr_set_##NAME##_delay(struct device *dev, \
	struct device_attribute *attr, const char *buf, size_t size)\
{\
	unsigned long val = 0;\
	int ret = -1;\
	write_info_t	pkg_ap;\
	read_info_t pkg_mcu;\
	pkt_cmn_interval_req_t cpkt;\
	pkt_header_t *hd = (pkt_header_t *)&cpkt;\
\
	memset(&pkg_ap, 0, sizeof(pkg_ap));\
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));\
	memset(&cpkt, 0, sizeof(cpkt));\
	if (sensor_status.opened[TAG] == 0) {\
		hwlog_err("send tag %d delay must be opend first\n", TAG);\
		return -EINVAL;\
	} \
	if (strict_strtoul(buf, 10, &val))\
		return -EINVAL;\
	if (val >= 10 && val < 1000) {\
		pkg_ap.tag = TAG;\
		pkg_ap.cmd = CMD_CMN_INTERVAL_REQ;\
		cpkt.param.period = val;\
		pkg_ap.wr_buf = &hd[1];\
		pkg_ap.wr_len = sizeof(cpkt.param);\
		ret = write_customize_cmd(&pkg_ap,  &pkg_mcu, true);\
		if (ret) {\
			hwlog_err("send %s delay cmd to mcu fail,ret=%d\n", \
				#NAME, ret);\
			return size;\
		} \
		if (pkg_mcu.errno != 0)\
			hwlog_err("set %s delay fail\n", #NAME);\
		else {\
			hwlog_info("set %s delay (%ld)success\n", #NAME, val);\
		} \
	} \
	return size;\
}

static ssize_t attr_set_gsensor_gather_enable(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int ret;
	unsigned long enable = 0;
	unsigned int delay = 50;
	interval_param_t delay_param = {
		.period = delay,
		.batch_count = 1,
		.mode = AUTO_MODE,
		.reserved[0] = TYPE_STANDARD /* for step counter only */
	};

	if (strict_strtoul(buf, 10, &enable))
		return -EINVAL;

	if ((enable != 0) && (enable != 1))
		return -EINVAL;

	if (is_gsensor_gather_enable == enable) {
		hwlog_info("gsensor gather already seted to state, is_gsensor_gather_enable %d\n",
			is_gsensor_gather_enable);
		return size;
	}
	ret = inputhub_sensor_enable(TAG_CONNECTIVITY, enable);
	if (ret) {
		hwlog_err("send GSENSOR GATHER enable cmd to mcu fail,ret=%d\n",
			ret);
		return -EINVAL;
	}

	if (enable == 1) {
		ret = inputhub_sensor_setdelay(TAG_CONNECTIVITY, &delay_param);
		if (ret) {
			hwlog_err("send GSENSOR GATHER set delay cmd to mcu fail,ret=%d\n",
				ret);
			return -EINVAL;
		}
	}
	is_gsensor_gather_enable = enable;
	hwlog_info("GSENSOR GATHER set to state %lu success\n", enable);

	return size;
}

SHOW_ENABLE_FUNC(acc, TAG_ACCEL)
STORE_ENABLE_FUNC(acc, TAG_ACCEL, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(acc_enable, 0664, show_acc_enable_result, attr_set_acc_enable);
SHOW_DELAY_FUNC(acc, TAG_ACCEL)
STORE_DELAY_FUNC(acc, TAG_ACCEL, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(acc_setdelay, 0664, show_acc_delay_result, attr_set_acc_delay);

static DEVICE_ATTR(gsensor_gather_enable, 0664,
	NULL, attr_set_gsensor_gather_enable);

SHOW_ENABLE_FUNC(gyro, TAG_GYRO)
STORE_ENABLE_FUNC(gyro, TAG_GYRO, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(gyro_enable, 0664,
	show_gyro_enable_result, attr_set_gyro_enable);
SHOW_DELAY_FUNC(gyro, TAG_GYRO)
STORE_DELAY_FUNC(gyro, TAG_GYRO, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(gyro_setdelay, 0664,
	show_gyro_delay_result, attr_set_gyro_delay);

SHOW_ENABLE_FUNC(mag, TAG_MAG)
STORE_ENABLE_FUNC(mag, TAG_MAG, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(mag_enable, 0664, show_mag_enable_result, attr_set_mag_enable);
SHOW_DELAY_FUNC(mag, TAG_MAG)
STORE_DELAY_FUNC(mag, TAG_MAG, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(mag_setdelay, 0664, show_mag_delay_result, attr_set_mag_delay);

SHOW_ENABLE_FUNC(als, TAG_ALS)
STORE_ENABLE_FUNC(als, TAG_ALS, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(als_enable, 0664, show_als_enable_result, attr_set_als_enable);
SHOW_DELAY_FUNC(als, TAG_ALS)
STORE_DELAY_FUNC(als, TAG_ALS, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(als_setdelay, 0664, show_als_delay_result, attr_set_als_delay);

SHOW_ENABLE_FUNC(ps, TAG_PS)
STORE_ENABLE_FUNC(ps, TAG_PS, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(ps_enable, 0664, show_ps_enable_result, attr_set_ps_enable);
SHOW_DELAY_FUNC(ps, TAG_PS)
STORE_DELAY_FUNC(ps, TAG_PS, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(ps_setdelay, 0664, show_ps_delay_result, attr_set_ps_delay);

SHOW_ENABLE_FUNC(os, TAG_ORIENTATION)
STORE_ENABLE_FUNC(os, TAG_ORIENTATION, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(orientation_enable, 0664,
	show_os_enable_result, attr_set_os_enable);
SHOW_DELAY_FUNC(os, TAG_ORIENTATION)
STORE_DELAY_FUNC(os, TAG_ORIENTATION, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(orientation_setdelay, 0664,
	show_os_delay_result, attr_set_os_delay);

SHOW_ENABLE_FUNC(lines, TAG_LINEAR_ACCEL)
STORE_ENABLE_FUNC(lines, TAG_LINEAR_ACCEL, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(lines_enable, 0664,
	show_lines_enable_result, attr_set_lines_enable);
SHOW_DELAY_FUNC(lines, TAG_LINEAR_ACCEL)
STORE_DELAY_FUNC(lines, TAG_LINEAR_ACCEL, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(lines_setdelay, 0664,
	show_lines_delay_result, attr_set_lines_delay);

SHOW_ENABLE_FUNC(gras, TAG_GRAVITY)
STORE_ENABLE_FUNC(gras, TAG_GRAVITY, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(gras_enable, 0664,
	show_gras_enable_result, attr_set_gras_enable);
SHOW_DELAY_FUNC(gras, TAG_GRAVITY)
STORE_DELAY_FUNC(gras, TAG_GRAVITY, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(gras_setdelay, 0664,
	show_gras_delay_result, attr_set_gras_delay);

SHOW_ENABLE_FUNC(rvs, TAG_ROTATION_VECTORS)
STORE_ENABLE_FUNC(rvs, TAG_ROTATION_VECTORS, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(rvs_enable, 0664, show_rvs_enable_result, attr_set_rvs_enable);
SHOW_DELAY_FUNC(rvs, TAG_ROTATION_VECTORS)
STORE_DELAY_FUNC(rvs, TAG_ROTATION_VECTORS, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(rvs_setdelay, 0664, show_rvs_delay_result, attr_set_rvs_delay);

SHOW_ENABLE_FUNC(airpress, TAG_PRESSURE)
STORE_ENABLE_FUNC(airpress, TAG_PRESSURE, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(airpress_enable, 0664,
	show_airpress_enable_result, attr_set_airpress_enable);
SHOW_DELAY_FUNC(airpress, TAG_PRESSURE)
STORE_DELAY_FUNC(airpress, TAG_PRESSURE, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(airpress_setdelay, 0664,
	show_airpress_delay_result, attr_set_airpress_delay);

static ssize_t attr_set_pdr_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;
	int start_update_flag;
	int precise;
	int interval;
	write_info_t pkg_ap;
	read_info_t pkg_mcu;
	pdr_ioctl_t pkg_ioctl;

	memset(&pkg_ap, 0, sizeof(pkg_ap));
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));
	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;
	/* val define: xyyzzz x:0 start 1 update yy:precise zzz:interval */
	if (val == 0)
		val = 1010;
	start_update_flag = (val / 100000);
	precise = (val / 1000) % 100;
	interval = val % 1000;

	hwlog_info("val = %lu start_update_flag = %d precise = %d interval= %d\n",
		val, start_update_flag, precise, interval);
	if (precise == 0)
		precise = 1;
	if (interval == 0)
		interval = 240;

	pkg_ap.tag = TAG_PDR;
	pkg_ap.cmd = CMD_CMN_CONFIG_REQ;
	if (val >= 1000) {
		pkg_ioctl.sub_cmd = (start_update_flag == 0 ?
			SUB_CMD_FLP_PDR_START_REQ : SUB_CMD_FLP_PDR_UPDATE_REQ);
		pkg_ioctl.start_param.report_interval = interval * 1000;
		pkg_ioctl.start_param.report_precise = precise * 1000;
		pkg_ioctl.start_param.report_count = interval / precise;
		pkg_ioctl.start_param.report_times = 0;
		pkg_ap.wr_buf = &pkg_ioctl;
		pkg_ap.wr_len = sizeof(pkg_ioctl);
	} else if (val == 2) { /* 2: stop command */
		pkg_ioctl.sub_cmd = SUB_CMD_FLP_PDR_STOP_REQ;
		pkg_ioctl.stop_param = 30000;
		pkg_ap.wr_buf = &pkg_ioctl;
		pkg_ap.wr_len = sizeof(pkg_ioctl);
	}
	hwlog_info(" pkg_ioctl.sub_cmd = %d\n ", pkg_ioctl.sub_cmd);
	ret = write_customize_cmd(&pkg_ap, &pkg_mcu, true);
	if (ret) {
		hwlog_err("send pdr delay cmd to mcu fail,ret=%d\n", ret);
		return size;
	}
	if (pkg_mcu.errno != 0)
		hwlog_err("set pdr delay fail\n");
	else
		hwlog_info("set pdr delay (%ld)success\n", val);

	return size;
}


STORE_ENABLE_FUNC(pdr, TAG_PDR, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(pdr_enable, 0220, NULL, attr_set_pdr_enable);
static DEVICE_ATTR(pdr_setdelay, 0220, NULL, attr_set_pdr_delay);

SHOW_ENABLE_FUNC(handpress, TAG_HANDPRESS)
STORE_ENABLE_FUNC(handpress, TAG_HANDPRESS, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(handpress_enable, 0664,
	show_handpress_enable_result, attr_set_handpress_enable);
SHOW_DELAY_FUNC(handpress, TAG_HANDPRESS)
STORE_DELAY_FUNC(handpress, TAG_HANDPRESS, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(handpress_setdelay, 0664,
	show_handpress_delay_result, attr_set_handpress_delay);

SHOW_ENABLE_FUNC(cap_prox, TAG_CAP_PROX)
STORE_ENABLE_FUNC(cap_prox, TAG_CAP_PROX, CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(cap_prox_enable, 0664,
	show_cap_prox_enable_result, attr_set_cap_prox_enable);
SHOW_DELAY_FUNC(cap_prox, TAG_CAP_PROX)
STORE_DELAY_FUNC(cap_prox, TAG_CAP_PROX, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(cap_prox_setdelay, 0664,
	show_cap_prox_delay_result, attr_set_cap_prox_delay);

SHOW_ENABLE_FUNC(magn_bracket, TAG_MAGN_BRACKET)
STORE_ENABLE_FUNC(magn_bracket, TAG_MAGN_BRACKET,
	CMD_CMN_OPEN_REQ, CMD_CMN_CLOSE_REQ)
static DEVICE_ATTR(magn_bracket_enable, 0664,
	show_magn_bracket_enable_result, attr_set_magn_bracket_enable);
SHOW_DELAY_FUNC(magn_bracket, TAG_MAGN_BRACKET)
STORE_DELAY_FUNC(magn_bracket, TAG_MAGN_BRACKET, CMD_CMN_INTERVAL_REQ)
static DEVICE_ATTR(magn_bracket_setdelay, 0664,
	show_magn_bracket_delay_result, attr_set_magn_bracket_delay);

static ssize_t start_iom3_recovery(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	hwlog_info("%s +\n", __func__);
	iom3_need_recovery(SENSORHUB_USER_MODID, SH_FAULT_USER_DUMP);
	hwlog_info("%s -\n", __func__);
	return size;
}
static DEVICE_ATTR(iom3_recovery, 0664, NULL, start_iom3_recovery);

static ssize_t attr_set_sensor_test_mode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long val = 0;

	hwlog_info("%s +\n", __func__);
	if (strict_strtoul(buf, 10, &val)) {
		hwlog_err("In %s! val = %lu\n", __func__, val);
		return -EINVAL;
	}
	if (val == 1)
		flag_for_sensor_test = 1;
	else
		flag_for_sensor_test = 0;
	return size;
}
static DEVICE_ATTR(sensor_test, 0660, NULL, attr_set_sensor_test_mode);

/*
 * buf: motion value, 2byte,
 * motion type, 0-11
 * second status 0-4
 */
#define MOTION_DT_STUP_LENGTH (5)
#define RADIX_16 (16)
static ssize_t attr_set_dt_motion_stup(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	char dt_motion_value[MOTION_DT_STUP_LENGTH] = { 0 };
	int i;
	unsigned long source;

	source = simple_strtoul(buf, NULL, RADIX_16);
	hwlog_err("%s buf %s, source %lu, size %lu\n",
		__func__, buf, source, size);

	for (i = 0; i < MOTION_DT_STUP_LENGTH - 1; i++) {
		dt_motion_value[i] = source % ((i + 1) * RADIX_16);
		source = source / RADIX_16;
	}

	dt_motion_value[MOTION_DT_STUP_LENGTH - 1] = '\0';
	hwlog_err("%s motion %x %x %x %x\n", __func__, dt_motion_value[0],
		dt_motion_value[1], dt_motion_value[2], dt_motion_value[3]);
	inputhub_route_write(ROUTE_MOTION_PORT, dt_motion_value,
		MOTION_DT_STUP_LENGTH - 1);

	return size;
}
static DEVICE_ATTR(dt_motion_stup, 0664, NULL, attr_set_dt_motion_stup);

static ssize_t attr_set_stop_auto_data(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	stop_auto_accel = simple_strtoul(buf, NULL, 16);
	hwlog_err("%s stop_auto_accel %d\n", __func__, stop_auto_accel);
	return size;
}
static DEVICE_ATTR(dt_stop_auto_data, 0664, NULL, attr_set_stop_auto_data);

int tell_cts_test_to_mcu(int status)
{
	read_info_t pkg_mcu;
	write_info_t winfo;
	pkt_sys_statuschange_req_t pkt;

	if (status == 1) {
		inputhub_sensor_enable(TAG_AR, false);
		hwlog_info("close ar in %s\n", __func__);
	}
	if ((status == 0) || (status == 1)) {
		winfo.tag = TAG_SYS;
		winfo.cmd = CMD_SYS_CTS_RESTRICT_MODE_REQ;
		winfo.wr_len = sizeof(pkt) - sizeof(pkt.hd);
		pkt.status = status;
		winfo.wr_buf = &pkt.status;
		return write_customize_cmd(&winfo, &pkg_mcu, true);
	} else {
		hwlog_err("error status %d in %s\n", status, __func__);
		return -EINVAL;
	}
	return 0;
}

ssize_t attr_stop_auto_motion_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_STR_SIZE, "%d\n", stop_auto_motion);
}

static ssize_t attr_set_stop_auto_motion(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long val;

	val = simple_strtoul(buf, NULL, 16);
	if (val == 1) { /* cts test,disable motion */
		disable_motions_when_sysreboot();
		stop_auto_motion = 1;
		hwlog_err("%s stop_auto_motion =%d, val = %lu\n",
			__func__, stop_auto_motion, val);
		tell_cts_test_to_mcu(1);
	}
	if (val == 0) {
		stop_auto_motion = 0;
		enable_motions_when_recovery_iom3();
		hwlog_err("%s stop_auto_motion =%d, val = %lu\n",
			__func__, stop_auto_motion, val);
		tell_cts_test_to_mcu(0);
	}

	return size;
}
static DEVICE_ATTR(dt_stop_auto_motion, 0660,
	attr_stop_auto_motion_show, attr_set_stop_auto_motion);

static ssize_t attr_set_stop_als_auto_data(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	stop_auto_als = simple_strtoul(buf, NULL, 16);
	hwlog_err("%s stop_auto_als %d\n", __func__, stop_auto_als);
	return size;
}
static DEVICE_ATTR(dt_stop_als_auto_data, 0664, NULL, attr_set_stop_als_auto_data);

static ssize_t attr_set_stop_ps_auto_data(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	stop_auto_ps = simple_strtoul(buf, NULL, 16);
	hwlog_err("%s stop_auto_ps %d\n", __func__, stop_auto_ps);
	return size;
}
static DEVICE_ATTR(dt_stop_ps_auto_data, 0664, NULL, attr_set_stop_ps_auto_data);

static ssize_t attr_set_sensor_motion_stup(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int i;
	unsigned long source;
	struct sensor_data event;

	source = simple_strtoul(buf, NULL, 16);

	if (source) { /* 1: landscape */
		hwlog_err("%s landscape\n", __func__);
		event.type = TAG_ACCEL;
		event.length = 12;
		event.value[0] = 1000;
		event.value[1] = 0;
		event.value[2] = 0;
	} else { /* 0: portial */
		hwlog_err("%s portial\n", __func__);
		event.type = TAG_ACCEL;
		event.length = 12;
		event.value[0] = 0;
		event.value[1] = 1000;
		event.value[2] = 0;
	}

	for (i = 0; i < 20; i++) {
		msleep(100);
		report_sensor_event(TAG_ACCEL, event.value, event.length);
	}
	return size;
}
static DEVICE_ATTR(dt_sensor_stup, 0664, NULL, attr_set_sensor_motion_stup);

static ssize_t attr_set_sensor_stepcounter_stup(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long source;
	struct sensor_data event;

	source = simple_strtoul(buf, NULL, 10);
	event.type = TAG_STEP_COUNTER;
	event.length = 12;
	event.value[0] = source;
	event.value[1] = 0;
	event.value[2] = 0;

	report_sensor_event(TAG_STEP_COUNTER, event.value, event.length);
	return size;
}

static DEVICE_ATTR(dt_stepcounter_stup, 0664, NULL,
	attr_set_sensor_stepcounter_stup);

static ssize_t attr_set_dt_hall_sensor_stup(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct sensor_data event;
	unsigned long source;

	source = simple_strtoul(buf, NULL, 16);

	hwlog_err("%s buf %s, source %lu\n", __func__, buf, source);
	event.type = TAG_HALL;
	event.length = 4;
	event.value[0] = (int)source;

	inputhub_route_write(ROUTE_SHB_PORT, (char *)&event, 8);
	return size;
}
static DEVICE_ATTR(dt_hall_sensor_stup, 0664, NULL, attr_set_dt_hall_sensor_stup);

static ssize_t attr_set_dt_als_sensor_stup(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct sensor_data event;
	unsigned long source;

	source = simple_strtoul(buf, NULL, 16);
	hwlog_err("%s buf %s, source %lu\n", __func__, buf, source);

	event.type = TAG_ALS;
	event.length = 4;
	event.value[0] = (int)source;

	inputhub_route_write(ROUTE_SHB_PORT, (char *)&event, 8);
	return size;
}
static DEVICE_ATTR(dt_als_sensor_stup, 0664, NULL, attr_set_dt_als_sensor_stup);

static ssize_t attr_set_dt_ps_sensor_stup(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct sensor_data event;
	unsigned long source;

	source = simple_strtoul(buf, NULL, 16);
	hwlog_err("%s buf %s, source %lu\n", __func__, buf, source);

	event.type = TAG_PS;
	event.length = 4;
	event.value[0] = (int)source;

	inputhub_route_write(ROUTE_SHB_PORT, (char *)&event, 8);
	return size;
}
static DEVICE_ATTR(dt_ps_sensor_stup, 0664, NULL, attr_set_dt_ps_sensor_stup);

static ssize_t show_iom3_sr_status(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_STR_SIZE, "%s\n",
		(iom3_sr_status == ST_SLEEP) ? "ST_SLEEP" : "ST_WAKEUP");
}
static DEVICE_ATTR(iom3_sr_status, 0664, show_iom3_sr_status, NULL);

ssize_t show_cap_prox_calibrate_method(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_STR_SIZE, "%d\n", sar_pdata.calibrate_type);
}

ssize_t show_cap_prox_calibrate_orders(int tag, struct device *dev,
	struct device_attribute *attr, char *buf)
{
	switch (tag) {
	case TAG_CAP_PROX:
		return snprintf(buf, MAX_STR_SIZE, "%s\n", sar_calibrate_order);
		hwlog_info("tag %d cap_prox_calibrate_orders is %s\n",
			tag, __func__);

	case TAG_CAP_PROX1:
		return snprintf(buf, MAX_STR_SIZE, "%s\n", sar_calibrate_order);
		hwlog_info("tag %d cap_prox1_calibrate_orders is %s\n",
			tag, __func__);

	default:
		hwlog_err("tag %d get cap_prox_calibrate_orders not implement in %s\n",
			tag, __func__);
		break;
	}

	return 0;
}

ssize_t attr_get_acc_sensor_id(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	hwlog_info("%s is %s\n", __func__, acc_sensor_id);
	return snprintf(buf, MAX_STR_SIZE, "%s\n", acc_sensor_id);
}

ssize_t attr_get_gyro_sensor_id(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	hwlog_info("%s is %s\n", __func__, gyro_sensor_id);
	return snprintf(buf, MAX_STR_SIZE, "%s\n", gyro_sensor_id);
}

ssize_t attr_get_mag_sensor_id(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	hwlog_info("%s is %s\n", __func__, mag_sensor_id);
	return snprintf(buf, MAX_STR_SIZE, "%s\n", mag_sensor_id);
}

ssize_t attr_get_cap_sensor_id(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	hwlog_info("%s is %s\n", __func__, cap_sensor_id);
	return snprintf(buf, MAX_STR_SIZE, "%s\n", cap_sensor_id);
}

ssize_t show_sensor_in_board_status_sysfs(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	hwlog_info("%s is %s\n", __func__, sensor_in_board_status);
	return snprintf(buf, MAX_STR_SIZE, "%s\n", sensor_in_board_status);
}

ssize_t show_sensor_read_airpress_common(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	airpress_cali_flag = 1;
	return snprintf(buf, MAX_STR_SIZE, "%d\n", get_airpress_data);
}

static ssize_t show_sensor_read_airpress(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return show_sensor_read_airpress_common(dev, attr, buf);
}
static DEVICE_ATTR(read_airpress, 0664, show_sensor_read_airpress, NULL);

static ssize_t show_sensor_read_temperature(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_STR_SIZE, "%d\n", get_temperature_data);
}

static DEVICE_ATTR(read_temperature, 0664, show_sensor_read_temperature, NULL);

static ssize_t show_dump_sensor_status(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int tag = 0;

	hwlog_info("-------------------------------------\n");
	for (tag = TAG_SENSOR_BEGIN; tag < TAG_SENSOR_END; ++tag) {
		/* ps and step counter need always on, just skip */
		if (unlikely((tag == TAG_PS) || (tag == TAG_STEP_COUNTER) ||
				(tag == TAG_MAGN_BRACKET))) {
			continue;
		}
		hwlog_info(" %s\t %s\t %d\n", obj_tag_str[tag],
			sensor_status.opened[tag] ? "open" : "close",
			sensor_status.delay[tag]);
	}
	hwlog_info("-------------------------------------\n");
	return snprintf(buf, MAX_STR_SIZE, "please check log %d\n",
		get_temperature_data);
}

static DEVICE_ATTR(dump_sensor_status, 0664, show_dump_sensor_status, NULL);

static ssize_t show_airpress_set_calidata(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	if (strlen(sensor_chip_info[AIRPRESS]) != 0)
		return snprintf(buf, MAX_STR_SIZE, "%d\n", airpress_data.offset);
	else
		return -1;
}

static ssize_t store_airpress_set_calidata(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	long source;
	int ret;
	int i;
	int temp;
	write_info_t pkg_ap;
	read_info_t pkg_mcu;
	pkt_parameter_req_t cpkt;
	pkt_header_t *hd = (pkt_header_t *)&cpkt;

	if (strlen(sensor_chip_info[AIRPRESS]) == 0) {
		hwlog_err("AIRPRESS not exits !!\n");
		return -1;
	}
	memset(&pkg_ap, 0, sizeof(pkg_ap));
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));
	if (!airpress_cali_flag) {
		hwlog_warn("Takes effect only when the calibration data\n");
		return -1;
	}
	source = simple_strtol(buf, NULL, 10);
	if (source > MAX_AIRPRESS_OFFSET || source < MIN_AIRPRESS_OFFSET) {
		hwlog_err("Incorrect offset. source = %ld\n", source);
		return -1;
	}
	airpress_data.offset += (int)source;

	for (i = 0; i < 48; i++)
		airpress_data.airpress_extend_data[i] = i;
	/* send to mcu */
	pkg_ap.tag = TAG_PRESSURE;
	pkg_ap.cmd = CMD_CMN_CONFIG_REQ;
	cpkt.subcmd = SUB_CMD_SET_OFFSET_REQ;
	pkg_ap.wr_buf = &hd[1];
	pkg_ap.wr_len = sizeof(airpress_data.offset) + SUBCMD_LEN;
	memcpy(cpkt.para, &airpress_data.offset, sizeof(airpress_data.offset));
	hwlog_info("***%s***\n", __func__);
	ret = write_customize_cmd(&pkg_ap, &pkg_mcu, true);
	if (ret != 0) {
		hwlog_err("set airpress_sensor data failed, ret = %d\n", ret);
		return -1;
	}
	if (pkg_mcu.errno != 0)
		hwlog_err("set airpress_sensor sysfs offset fail,err=%d\n",
			pkg_mcu.errno);
	else
		hwlog_info("send airpress_sensor sysfs data :%d to mcu success\n",
			airpress_data.offset);

	if (write_calibrate_data_to_nv(AIRPRESS_CALIDATA_NV_NUM,
		sizeof(airpress_data.offset), "AIRDATA",
		(char *)&airpress_data.offset)) {
		hwlog_err("nve_direct_access write error %d\n", ret);
		return -1;
	}

	msleep(10);
	if (read_calibrate_data_from_nv(AIRPRESS_CALIDATA_NV_NUM,
		AIRPRESS_CALIDATA_NV_SIZE, "AIRDATA")) {
		hwlog_err("nve direct access read error %d\n", ret);
		return -1;
	}
	memcpy(&temp, user_info.nv_data, sizeof(temp));
	if (temp != airpress_data.offset) {
		hwlog_err("nv write fail, %d %d\n", temp, airpress_data.offset);
		return -1;
	}
	airpress_cali_flag = 0;
	return size;
}
static DEVICE_ATTR(airpress_set_calidata, 0664,
	show_airpress_set_calidata, store_airpress_set_calidata);

void airpress_touch_data_acquisition(int *air_touch_data, int len)
{
	#ifdef SENSOR_DATA_ACQUISITION
	int minThreshold_airpress[AIR_THRESHOLD_NUM] = { MIN_AIRPRESS_OFFSET };
	int maxThreshold_airpress[AIR_THRESHOLD_NUM] = { MAX_AIRPRESS_OFFSET };
	struct sensor_eng_cal_test air_test;
	int pAccTest = 0;

	if (!air_touch_data || len != AIRPRESS_CAL_NUM) {
		hwlog_err("invalid input, air_touch_data iS NULL, or len = %d\n",
			len);
		return;
	}

	memset(&air_test, 0, sizeof(air_test));
	air_test.cal_value = air_touch_data;
	air_test.value_num = AIRPRESS_CAL_NUM;
	air_test.threshold_num = AIR_THRESHOLD_NUM;
	air_test.first_item = AIRPRESS_CALI_OFFSET_MSG;
	air_test.min_threshold = minThreshold_airpress;
	air_test.max_threshold = maxThreshold_airpress;
	memcpy(air_test.name, AIRPRESS_TEST_CAL, sizeof(AIRPRESS_TEST_CAL));
	memcpy(air_test.result, AIRPRESS_RESULT, strlen(AIRPRESS_RESULT) + 1);
	for (pAccTest = 0; pAccTest < AIRPRESS_CAL_NUM; pAccTest++)
		air_test.test_name[pAccTest] = airpress_test_name[pAccTest];
	enq_notify_work_sensor(air_test);
	#endif
}

static ssize_t show_gyro_set_calidata(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	if (!gyro_cali_way)
		return 0;

	if (strlen(sensor_chip_info[GYRO]) != 0)
		return snprintf(buf, MAX_STR_SIZE, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
			set_gyro_calib_data[0], set_gyro_calib_data[1],
			set_gyro_calib_data[2], set_gyro_calib_data[3],
			set_gyro_calib_data[4], set_gyro_calib_data[5],
			set_gyro_calib_data[6], set_gyro_calib_data[7],
			set_gyro_calib_data[8], set_gyro_calib_data[9],
			set_gyro_calib_data[10], set_gyro_calib_data[11],
			set_gyro_calib_data[12], set_gyro_calib_data[13],
			set_gyro_calib_data[14]);
	else
		return -1;
}

static ssize_t store_gyro_set_calidata(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{

	const int32_t *set_gyro_cali_data = NULL;
	int i;
	int set_gyro_sensor_offset[GYRO_CALIBRATE_DATA_LENGTH];

	memset(set_gyro_sensor_offset, 0, sizeof(set_gyro_sensor_offset));
	if (!gyro_cali_way) {
		hwlog_info("%s: now is gyro self_calibreate\n", __func__);
		return size;
	}
	if (strlen(sensor_chip_info[GYRO]) == 0) {
		hwlog_err("GYRO not exits !!\n");
		return -1;
	}
	set_gyro_cali_data = (const int32_t *)buf;
	if (size == sizeof(set_gyro_calib_data)) {
		set_gyro_calib_data[0] = *set_gyro_cali_data;
		set_gyro_calib_data[1] = *(set_gyro_cali_data + 1);
		set_gyro_calib_data[2] = *(set_gyro_cali_data + 2);
		set_gyro_calib_data[3] = *(set_gyro_cali_data + 3);
		set_gyro_calib_data[4] = *(set_gyro_cali_data + 4);
		set_gyro_calib_data[5] = *(set_gyro_cali_data + 5);
		set_gyro_calib_data[6] = *(set_gyro_cali_data + 6);
		set_gyro_calib_data[7] = *(set_gyro_cali_data + 7);
		set_gyro_calib_data[8] = *(set_gyro_cali_data + 8);
		set_gyro_calib_data[9] = *(set_gyro_cali_data + 9);
		set_gyro_calib_data[10] = *(set_gyro_cali_data + 10);
		set_gyro_calib_data[11] = *(set_gyro_cali_data + 11);
		set_gyro_calib_data[12] = *(set_gyro_cali_data + 12);
		set_gyro_calib_data[13] = *(set_gyro_cali_data + 13);
		set_gyro_calib_data[14] = *(set_gyro_cali_data + 14);
	} else {
		hwlog_err("%s:size %lu is not equal to 15*4\n",  __func__, size);
		return -1;
	}
	for (i = 0; i < CALIBRATE_DATA_LENGTH; i++) {
		if (set_gyro_calib_data[i] < gyro_calib_threshold[i].low_threshold ||
		    set_gyro_calib_data[i] > gyro_calib_threshold[i].high_threshold) {
			hwlog_err("%s: gyro calibrated_data is out of range. i = %d, num = %d\n",
				__func__, i, set_gyro_calib_data[i]);
			return -1;
		}
	}
	gyro_calibrate_save(set_gyro_calib_data, sizeof(set_gyro_calib_data));
	hwlog_info("set gyro calibrate success, data=%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
			set_gyro_calib_data[0], set_gyro_calib_data[1],
			set_gyro_calib_data[2], set_gyro_calib_data[3],
			set_gyro_calib_data[4], set_gyro_calib_data[5],
			set_gyro_calib_data[6], set_gyro_calib_data[7],
			set_gyro_calib_data[8], set_gyro_calib_data[9],
			set_gyro_calib_data[10], set_gyro_calib_data[11],
			set_gyro_calib_data[12], set_gyro_calib_data[13],
			set_gyro_calib_data[14]);
	if (read_calibrate_data_from_nv(GYRO_CALIDATA_NV_NUM,
		GYRO_CALIDATA_NV_SIZE, "GYRO"))
		return -1;

	/* copy to gsensor_offset by pass */
	memcpy(set_gyro_sensor_offset, user_info.nv_data,
		sizeof(set_gyro_sensor_offset));
	hwlog_info("nve_direct_access read gyro_sensor offset: %d %d %d  sensitity:%d %d %d\n",
		set_gyro_sensor_offset[0], set_gyro_sensor_offset[1],
		set_gyro_sensor_offset[2], set_gyro_sensor_offset[3],
		set_gyro_sensor_offset[4], set_gyro_sensor_offset[5]);
	hwlog_info("nve_direct_access read gyro_sensor xis_angle: %d %d %d  %d %d %d %d %d %d\n",
		set_gyro_sensor_offset[6], set_gyro_sensor_offset[7],
		set_gyro_sensor_offset[8], set_gyro_sensor_offset[9],
		set_gyro_sensor_offset[10], set_gyro_sensor_offset[11],
		set_gyro_sensor_offset[12], set_gyro_sensor_offset[13],
		set_gyro_sensor_offset[14]);
	if (send_calibrate_data_to_mcu(TAG_GYRO, SUB_CMD_SET_OFFSET_REQ,
		set_gyro_sensor_offset, GYRO_CALIDATA_NV_SIZE, false))
		return -1;
	return size;
}

static DEVICE_ATTR(gyro_set_calidata, 0660, show_gyro_set_calidata, store_gyro_set_calidata);
static ssize_t show_acc_set_calidata(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!acc_cali_way)
		return 0;

	if (strlen(sensor_chip_info[ACC]) != 0)
		return snprintf(buf, MAX_STR_SIZE, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
			set_acc_calib_data[0], set_acc_calib_data[1],
			set_acc_calib_data[2], set_acc_calib_data[3],
			set_acc_calib_data[4], set_acc_calib_data[5],
			set_acc_calib_data[6], set_acc_calib_data[7],
			set_acc_calib_data[8], set_acc_calib_data[9],
			set_acc_calib_data[10], set_acc_calib_data[11],
			set_acc_calib_data[12], set_acc_calib_data[13],
			set_acc_calib_data[14]);
	else
		return -1;
}

static ssize_t store_acc_set_calidata(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{

	const int32_t *set_acc_cali_data = NULL;
	int set_acc_sensor_offset[ACC_CALIBRATE_DATA_LENGTH];
	int i;

	memset(set_acc_sensor_offset, 0, sizeof(set_acc_sensor_offset));
	if (!acc_cali_way) {
		hwlog_info("%s: now is acc self_calibreate\n", __func__);
		return size;
	}
	if (strlen(sensor_chip_info[ACC]) == 0) {
		hwlog_err("ACC not exits !!\n");
		return -1;
	}
	set_acc_cali_data = (const int32_t *)buf;
	if (size == sizeof(set_acc_calib_data)) {
		set_acc_calib_data[0] = *set_acc_cali_data;
		set_acc_calib_data[1] = *(set_acc_cali_data + 1);
		set_acc_calib_data[2] = *(set_acc_cali_data + 2);
		set_acc_calib_data[3] = *(set_acc_cali_data + 3);
		set_acc_calib_data[4] = *(set_acc_cali_data + 4);
		set_acc_calib_data[5] = *(set_acc_cali_data + 5);
		set_acc_calib_data[6] = *(set_acc_cali_data + 6);
		set_acc_calib_data[7] = *(set_acc_cali_data + 7);
		set_acc_calib_data[8] = *(set_acc_cali_data + 8);
		set_acc_calib_data[9] = *(set_acc_cali_data + 9);
		set_acc_calib_data[10] = *(set_acc_cali_data + 10);
		set_acc_calib_data[11] = *(set_acc_cali_data + 11);
		set_acc_calib_data[12] = *(set_acc_cali_data + 12);
		set_acc_calib_data[13] = *(set_acc_cali_data + 13);
		set_acc_calib_data[14] = *(set_acc_cali_data + 14);
	} else {
		hwlog_err("%s:size %lu is not equal to 15*4\n",
			__func__, size);
		return -1;
	}
	for (i = 0; i < CALIBRATE_DATA_LENGTH; i++) {
		if (set_acc_calib_data[i] < acc_calib_threshold[i].low_threshold ||
		    set_acc_calib_data[i] > acc_calib_threshold[i].high_threshold) {
			hwlog_err("%s: acc calibrated_data is out of range. i = %d, num = %d\n",
				__FUNCTION__, i, set_acc_calib_data[i]);
			return -1;
		}
	}
	acc_calibrate_save(set_acc_calib_data, sizeof(set_acc_calib_data));
	hwlog_info("set acc calibrate success, data=%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
			set_acc_calib_data[0], set_acc_calib_data[1],
			set_acc_calib_data[2], set_acc_calib_data[3],
			set_acc_calib_data[4], set_acc_calib_data[5],
			set_acc_calib_data[6], set_acc_calib_data[7],
			set_acc_calib_data[8], set_acc_calib_data[9],
			set_acc_calib_data[10], set_acc_calib_data[11],
			set_acc_calib_data[12], set_acc_calib_data[13],
			set_acc_calib_data[14]);
	if (read_calibrate_data_from_nv(ACC_OFFSET_NV_NUM,
		ACC_OFFSET_NV_SIZE, "gsensor"))
		return -1;

	/* copy to gsensor_offset by pass */
	memcpy(set_acc_sensor_offset, user_info.nv_data,
		sizeof(set_acc_sensor_offset));
	hwlog_info("nve_direct_access read gyro_sensor offset: %d %d %d  sensitity:%d %d %d\n",
		set_acc_sensor_offset[0], set_acc_sensor_offset[1],
		set_acc_sensor_offset[2], set_acc_sensor_offset[3],
		set_acc_sensor_offset[4], set_acc_sensor_offset[5]);
	hwlog_info("nve_direct_access read gyro_sensor xis_angle: %d %d %d  %d %d %d %d %d %d\n",
		set_acc_sensor_offset[6], set_acc_sensor_offset[7],
		set_acc_sensor_offset[8], set_acc_sensor_offset[9],
		set_acc_sensor_offset[10], set_acc_sensor_offset[11],
		set_acc_sensor_offset[12], set_acc_sensor_offset[13],
		set_acc_sensor_offset[14]);

	if (send_calibrate_data_to_mcu(TAG_ACCEL, SUB_CMD_SET_OFFSET_REQ,
		set_acc_sensor_offset, ACC_OFFSET_NV_SIZE, false))
		return -1;
	return size;
}

static DEVICE_ATTR(acc_set_calidata, 0660,
	show_acc_set_calidata, store_acc_set_calidata);

static ssize_t store_set_data_type(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int32_t set_data_type[2];
	const int32_t *set_type = NULL;
	write_info_t pkg_ap;
	read_info_t pkg_mcu;
	pkt_parameter_req_t spkt;
	pkt_header_t *shd = (pkt_header_t *)&spkt;
	int ret;

	memset(&pkg_ap, 0, sizeof(pkg_ap));
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));
	memset(&set_data_type, 0, sizeof(set_data_type));
	memset(&spkt, 0, sizeof(spkt));
	set_type = (const int32_t *)buf;
	if (size == sizeof(set_data_type)) {
		set_data_type[0] = *set_type;
		set_data_type[1] = *(set_type + 1);
	} else {
		hwlog_err("%s:size %lu is not equal to 8\n",  __func__, size);
		return -1;
	}
	if (!acc_cali_way) {
		hwlog_info("%s: now is self_calibreate\n", __func__);
		return size;
	}
	hwlog_info("%s: data type tag is %d (1.acc2.gyro),type is %d(1.raw_data2.cali_data4.nor_data)\n",
		__func__, set_data_type[0], set_data_type[1]);
	if (set_data_type[0] < 1 || set_data_type[0] > 2) {
		hwlog_err("%s:set sensor tag is fail, invalid val\n",
			__func__);
		return -1;
	}
	if (set_data_type[1] < 1 || set_data_type[1] > 4) {
		hwlog_err("%s:set data type is fail, invalid val\n",
			__func__);
		return -1;
	}
	if (set_data_type[0] == 1)
		pkg_ap.tag = TAG_ACCEL;

	if (set_data_type[0] == 2)
		pkg_ap.tag = TAG_GYRO;

	spkt.subcmd = SUB_CMD_SET_DATA_TYPE_REQ;

	pkg_ap.cmd = CMD_CMN_CONFIG_REQ;
	pkg_ap.wr_buf = &shd[1];
	pkg_ap.wr_len = sizeof(set_data_type[1]) + SUBCMD_LEN;
	memcpy(spkt.para, &set_data_type[1], sizeof(set_data_type[1]));
	ret = write_customize_cmd(&pkg_ap, &pkg_mcu, true);
	if (pkg_mcu.errno != 0)
		hwlog_err("send tag %d get diff data cmd to mcu fail,ret=%d\n",
			set_data_type[0], ret);

	return size;
}
static DEVICE_ATTR(set_data_type, 0220, NULL, store_set_data_type);

static ssize_t attr_handpress_calibrate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int result = (handpress_calibration_res == SUC) ?
		0 : handpress_calibration_res;
	return snprintf(buf, PAGE_SIZE, "%d\n", result);
}

int write_handpress_offset_to_nv(const void *offset)
{
	int ret = 0;
	int8_t offset2[24] = { 0 };

	if (!offset) {
		hwlog_err("write_handpress_offset_to_nv fail, invalid para\n");
		return -1;
	}
	memcpy(offset2, offset, HANDPRESS_CALIDATA_NV_SIZE);
	if (write_calibrate_data_to_nv(HANDPRESS_CALIDATA_NV_NUM,
		HANDPRESS_CALIDATA_NV_SIZE, "HPDATA", offset2))
		return -1;

	if (read_calibrate_data_from_nv(HANDPRESS_CALIDATA_NV_NUM,
		HANDPRESS_CALIDATA_NV_SIZE, "HPDATA"))
		return -1;
	memset(offset2, 0, sizeof(offset2));
	memcpy(offset2, user_info.nv_data, sizeof(offset2));
	hwlog_err("offsets: %d%d%d%d%d%d%d%d\n", offset2[0], offset2[1],
		offset2[2], offset2[3], offset2[4],
		offset2[5], offset2[6], offset2[7]);
	return ret;
}

static void handpress_calibrate_work_func(struct work_struct *work)
{
	int ret;

	hwlog_err("handpress calibrate work enter ++\n");
	ret = write_handpress_offset_to_nv(hp_offset);
	if (ret < 0)  {
		hwlog_err("nv write faild.\n");
		handpress_calibration_res = NV_FAIL;
	}
	hwlog_err("handpress calibrate work enter --\n");
}

static ssize_t attr_handpress_calibrate_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	read_info_t pkg_mcu;
	int data_len;
	uint16_t hand_offset[8] = { 0 };
	uint16_t hand_metal[8] = { 0 };
	uint8_t hand_idac1[8] = { 0 };
	uint8_t hand_calidata[24] = { 0 };

	handpress_calibration_res = EXEC_FAIL;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (strlen(sensor_chip_info[HANDPRESS]) == 0) {
		hwlog_err("no handpress\n");
		return count;
	}

	if (val == 1) {
		if ((hall_value & 0x01) != 0x01) {
			hwlog_err("hall value:%d\n", hall_value);
			handpress_calibration_res = POSITION_FAIL;
			return count;
		}
	}
	pkg_mcu = send_calibrate_cmd(TAG_HANDPRESS, val, &handpress_calibration_res);
	if (handpress_calibration_res == COMMU_FAIL ||
	    handpress_calibration_res == EXEC_FAIL)
		goto hp_cali_out;

	hwlog_err("data_len:%d\n", pkg_mcu.data_length);
	data_len = sizeof(hand_calidata);
	data_len = pkg_mcu.data_length > data_len ? data_len : pkg_mcu.data_length;
	memcpy(hand_calidata, pkg_mcu.data, data_len);
	memcpy(hp_offset, hand_calidata, sizeof(hp_offset));
	handpress_calibration_res = SUC;
	if (val == 1) {
		INIT_WORK(&handpress_calibrate_work, handpress_calibrate_work_func);
		queue_work(system_power_efficient_wq, &handpress_calibrate_work);
		msleep(50);
		memcpy(hand_offset, &hp_offset[8], sizeof(hand_offset));
		memcpy(hand_idac1, hp_offset, sizeof(hand_idac1));
	} else if (val == 2) {
		data_len = sizeof(hand_metal);
		memcpy(hand_metal, hand_calidata, data_len);
		hwlog_info("metal:%d %d %d %d %d %d %d %d\n",
			hand_metal[0], hand_metal[1], hand_metal[2],
			hand_metal[3], hand_metal[4], hand_metal[5],
			hand_metal[6], hand_metal[7]);
	} else {
		hwlog_err("invalid input:%d\n", (int)val);
	}
hp_cali_out:
	return count;
}
static DEVICE_ATTR(handpress_calibrate, 0664,
	attr_handpress_calibrate_show, attr_handpress_calibrate_write);

ssize_t show_get_sensors_id(int tag, struct device *dev,
	struct device_attribute *attr, char *buf)
{
	switch (tag) {
	case TAG_ACCEL:
		hwlog_info("show_get_sensors_id acc\n");
		return attr_get_acc_sensor_id(dev, attr, buf);

	case TAG_GYRO:
		return attr_get_gyro_sensor_id(dev, attr, buf);

	case TAG_MAG:
		return attr_get_mag_sensor_id(dev, attr, buf);

	case TAG_CAP_PROX:
		return attr_get_cap_sensor_id(dev, attr, buf);

	default:
		hwlog_err("tag %d get_sensors_id not implement in %s\n",
			tag, __func__);
		break;
	}

	return 0;
}

ssize_t sensors_calibrate_show(int tag, struct device *dev,
	struct device_attribute *attr, char *buf)
{
	switch (tag) {
	case TAG_ACCEL:
		/* flyhorse k: SUC-->"0", OTHERS-->"1" */
		return snprintf(buf, PAGE_SIZE, "%d\n",
				return_calibration != SUC);
	case TAG_ACC1:
		return snprintf(buf, PAGE_SIZE, "%d\n",
				acc1_return_calibration != SUC);
	case TAG_PS:
		hwlog_info("feima sensors_calibrate_show res=%d\n",
			ps_calibration_res);
		return snprintf(buf, PAGE_SIZE, "%d\n",
				ps_calibration_res != SUC);
	case TAG_ALS:
		return snprintf(buf, PAGE_SIZE, "%d\n",
				als_calibration_res != SUC);
	case TAG_GYRO:
		return snprintf(buf, PAGE_SIZE, "%d\n",
				gyro_calibration_res != SUC);
	case TAG_GYRO1:
		return snprintf(buf, PAGE_SIZE, "%d\n",
				gyro1_calibration_res != SUC);
	case TAG_PRESSURE:
		return show_airpress_set_calidata(dev, attr, buf);
	case TAG_HANDPRESS:
		return snprintf(buf, PAGE_SIZE, "%d\n",
				handpress_calibration_res != SUC);
	case TAG_CAP_PROX:
		return snprintf(buf, PAGE_SIZE, "%d\n",
				return_cap_prox_calibration != SUC);
	case TAG_CAP_PROX1:
		return snprintf(buf, PAGE_SIZE, "%d\n",
				return_cap_prox1_calibration != SUC);
	default:
		hwlog_err("tag %d calibrate not implement in %s\n",
			tag, __func__);
		break;
	}

	return 0;
}

ssize_t sensors_calibrate_store(int tag, struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	switch (tag) {
	case TAG_ACCEL:
		return attr_acc_calibrate_write(dev, attr, buf, count);

	case TAG_PS:
		return attr_ps_calibrate_write(dev, attr, buf, count);

	case TAG_ALS:
		return attr_als_calibrate_write(dev, attr, buf, count);

	case TAG_GYRO:
		return attr_gyro_calibrate_write(dev, attr, buf, count);

	case TAG_PRESSURE:
		return store_airpress_set_calidata(dev, attr, buf, count);

	case TAG_HANDPRESS:
		return attr_handpress_calibrate_write(dev, attr, buf, count);

	case TAG_CAP_PROX:
		return attr_cap_prox_calibrate_write(dev, attr, buf, count);

	case TAG_ACC1:
		return attr_acc1_calibrate_write(dev, attr, buf, count);

	case TAG_GYRO1:
		return attr_gyro1_calibrate_write(dev, attr, buf, count);

	case TAG_CAP_PROX1:
		return attr_cap_prox1_calibrate_write(dev, attr, buf, count);

	default:
		hwlog_err("tag %d calibrate not implement in %s\n",
			tag, __func__);
		break;
	}

	return count;
}

int ois_commu(int tag, unsigned int cmd, unsigned int pare,
	unsigned int responsed, bool is_subcmd)
{
	int ret = -1;
	write_info_t pkg_ap;
	read_info_t pkg_mcu;
	pkt_parameter_req_t cpkt;
	pkt_header_t *hd = (pkt_header_t *)&cpkt;

	memset(&pkg_ap, 0, sizeof(pkg_ap));
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));

	if (is_subcmd) {
		pkg_ap.tag = tag;
		pkg_ap.cmd = CMD_CMN_CONFIG_REQ;
		cpkt.subcmd = cmd;
		pkg_ap.wr_buf = &hd[1];
		pkg_ap.wr_len = sizeof(pare) + SUBCMD_LEN;
		memcpy(cpkt.para, &pare, sizeof(pare));
	} else {
		pkg_ap.tag = tag;
		pkg_ap.cmd = cmd;
		pkg_ap.wr_buf = &pare;
		pkg_ap.wr_len = sizeof(pare);
	}

	if (responsed == NO_RESP) {
		ret = write_customize_cmd(&pkg_ap, NULL, true);
		if (ret) {
			hwlog_err("send ois cmd %d to mcu fail,ret=%d\n",
				cmd, ret);
			return ret;
		}
	} else {
		ret = write_customize_cmd(&pkg_ap, &pkg_mcu, true);
		if (ret) {
			hwlog_err("send ois gyro cfg cmd failed, ret = %d\n",
				ret);
			return ret;
		}
		if (pkg_mcu.errno != 0) {
			hwlog_err("set ois gyro cfg cmd fail,err=%d\n",
				pkg_mcu.errno);
		} else {
			hwlog_info("set ois gyro cfg cmd %d success\n",
				pare);
			sensor_status.gyro_ois_status = pare;
		}
	}

	return ret;
}

static ssize_t show_ois_ctrl(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_STR_SIZE, "%d\n", sensor_status.gyro_ois_status);
}

static ssize_t store_ois_ctrl(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	long source;
	int ret = 0;
	unsigned int cmd = 0;
	unsigned int delay = 10;
	write_info_t pkg_ap;
	read_info_t pkg_mcu;

	memset(&pkg_ap, 0, sizeof(pkg_ap));
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));
	source = simple_strtol(buf, NULL, 10);

	if (source == sensor_status.gyro_ois_status) {
		hwlog_info("%s:gyro ois status unchange,source=%ld return\n",
			__func__, source);
		return size;
	}

	if (source == 1) {
		cmd = CMD_CMN_OPEN_REQ;
		ret = ois_commu(TAG_OIS, cmd, source, NO_RESP, false);
		if (ret) {
			hwlog_err("%s: ois open gyro fail\n", __func__);
			return size;
		}

		cmd = CMD_CMN_INTERVAL_REQ;
		ret = ois_commu(TAG_OIS, cmd, delay, NO_RESP, false);
		if (ret) {
			hwlog_err("%s: set delay fail\n", __func__);
			return size;
		}

		cmd = SUB_CMD_GYRO_OIS_REQ;
		ret = ois_commu(TAG_GYRO, cmd, source, RESP, true);
		if (ret) {
			hwlog_err("%s: ois enable fail\n", __func__);
			return size;
		}
		hwlog_info("%s:ois enable succsess\n", __func__);
	} else if (source == 0) {
		cmd = SUB_CMD_GYRO_OIS_REQ;
		ret = ois_commu(TAG_GYRO, cmd, source, RESP, true);
		if (ret) {
			hwlog_err("%s:ois close fail\n", __func__);
			return size;
		}
		cmd = CMD_CMN_CLOSE_REQ;
		ret = ois_commu(TAG_OIS, cmd, source, NO_RESP, false);
		if (ret) {
			hwlog_err("%s: ois close gyro fail\n", __func__);
			return size;
		}
		hwlog_info("%s:ois close succsess\n", __func__);
	} else if (source == 2) {
		cmd = SUB_CMD_GYRO_OIS_REQ;
		ret = ois_commu(TAG_GYRO, cmd, source, RESP, true);
		if (ret) {
			hwlog_err("%s: ois enable fail\n", __func__);
			return size;
		}
		hwlog_info("%s:ois reset succsess\n", __func__);
	} else if (source == 3) {
		source = 1;
		cmd = CMD_CMN_OPEN_REQ;
		ret = ois_commu(TAG_OIS, cmd, source, NO_RESP, false);
		if (ret) {
			hwlog_err("%s: ois open gyro fail\n", __func__);
			return size;
		}

		cmd = CMD_CMN_INTERVAL_REQ;
		ret = ois_commu(TAG_OIS, cmd, delay, NO_RESP, false);
		if (ret) {
			hwlog_err("%s: set delay fail\n", __func__);
			return size;
		}
		cmd = SUB_CMD_GYRO_OIS_REQ;
		ret = ois_commu(TAG_GYRO, cmd, source, NO_RESP, true);
		if (ret) {
			hwlog_err("%s: ois enable no_resp fail\n", __func__);
			return size;
		}
		hwlog_info("%s:ois enable succsess\n", __func__);
	} else {
		hwlog_info("%s:ois commend is not right\n", __func__);
	}
	return size;
}
static DEVICE_ATTR(ois_ctrl, 0664, show_ois_ctrl, store_ois_ctrl);

static ssize_t show_key_debug(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_STR_SIZE, "%d\n", sensor_status.gyro_ois_status);
}

static int send_offset_cmd(uint8_t tag, uint32_t subcmd, int value)
{
	write_info_t pkg_ap;
	read_info_t pkg_mcu;
	pkt_parameter_req_t cpkt;
	pkt_header_t *hd = (pkt_header_t *)&cpkt;
	int ret;

	memset(&pkg_ap, 0, sizeof(pkg_ap));
	memset(&pkg_mcu, 0, sizeof(pkg_mcu));

	pkg_ap.cmd = CMD_CMN_CONFIG_REQ;
	pkg_ap.tag = tag;
	cpkt.subcmd = subcmd;
	pkg_ap.wr_buf = &hd[1];
	pkg_ap.wr_len = sizeof(value) + SUBCMD_LEN;
	memcpy(cpkt.para, &value, sizeof(value));
	ret = write_customize_cmd(&pkg_ap, &pkg_mcu, true);
	if (ret < 0) {
		hwlog_err("err. write cmd\n");
		return -1;
	}

	if (pkg_mcu.errno != 0) {
		hwlog_info("mcu err\n");
		return -1;
	} else {
		return 0;
	}
}

static ssize_t store_key_debug(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int source;
	int ret = 0;
	int value;
	int offset_value;
	interval_param_t param;

	if (strlen(sensor_chip_info[KEY]) == 0) {
		hwlog_err("no key\n");
		return size;
	}

	source = simple_strtol(buf, NULL, 10);
	value = ((source >> 8) & 0xff);
	offset_value = (source & 0xff);
	hwlog_info("value:%d  offset:%d\n", value, offset_value);
	switch (value) {
	case 0:
		ret = inputhub_sensor_enable(TAG_KEY, false);
		break;
	case 1:
		ret = inputhub_sensor_enable(TAG_KEY, true);
		break;
	case 2:	/* set offset */
	case 5:
		ret = send_offset_cmd(TAG_KEY, SUB_CMD_SET_OFFSET_REQ,
			offset_value);
		break;
	case 3:
		memset(&param, 0, sizeof(param));
		param.period = 500;
		ret = inputhub_sensor_setdelay(TAG_KEY, &param);
		break;
	case 4:
		ret = send_offset_cmd(TAG_KEY, SUB_CMD_BACKLIGHT_REQ,
			offset_value);
		break;
	default:
		return -1;
	}
	if (!ret)
		return size;
	else
		return -1;
}
static DEVICE_ATTR(key_debug, 0664, show_key_debug, store_key_debug);

static ssize_t show_sar_data(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	if (!strncmp(sensor_chip_info[CAP_PROX], "huawei,semtech-sx9323",
		strlen("huawei,semtech-sx9323"))) {
		return snprintf(buf, MAX_STR_SIZE, "offset1:%d offset2:%d diff1:%d diff2:%d\n",
			sar_calibrate_datas.semtech_cali_data.offset[0],
			sar_calibrate_datas.semtech_cali_data.offset[1],
			sar_calibrate_datas.semtech_cali_data.diff[0],
			sar_calibrate_datas.semtech_cali_data.diff[1]);
	}
	if (!strncmp(sensor_chip_info[CAP_PROX], "huawei,abov-a96t3x6",
		strlen("huawei,abov-a96t3x6"))) {
		return snprintf(buf, MAX_STR_SIZE, "offset1:%d offset2:%d diff1:%d diff2:%d\n",
			sar_calibrate_datas.abov_cali_data.offset[0],
			sar_calibrate_datas.abov_cali_data.offset[1],
			sar_calibrate_datas.abov_cali_data.diff[0],
			sar_calibrate_datas.abov_cali_data.diff[1]);
	}

	return -1;
}

static DEVICE_ATTR(sar_data, 0444, show_sar_data, NULL);

static ssize_t show_hifi_supported(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_STR_SIZE, "%d\n", hifi_supported);
}
static DEVICE_ATTR(hifi_supported, 0664, show_hifi_supported, NULL);

extern struct sensor_detect_manager sensor_manager[SENSOR_MAX];
static enum detect_state sensor_detect_flag = DET_FAIL;
static ssize_t show_sensor_detect(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i;
	int detect_result = 0;
	char result[(MAX_SENSOR_NAME_LENGTH + 1) * SENSOR_MAX];

	memset(&result, ' ', (MAX_SENSOR_NAME_LENGTH + 1) * SENSOR_MAX);

	for (i = 0; i < SENSOR_MAX; i++) {
		detect_result = sensor_manager[i].detect_result;
		if (detect_result == sensor_detect_flag)
			snprintf(&buf[i * MAX_SENSOR_NAME_LENGTH],
					MAX_SENSOR_NAME_LENGTH, "%s ",
					sensor_manager[i].sensor_name_str);
	}

	return MAX_STR_SIZE;
}

static ssize_t store_sensor_detect(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int flag;

	flag = simple_strtol(buf, NULL, 10);
	sensor_detect_flag = flag ? DET_SUCC : DET_FAIL;
	hwlog_info("sensor detect value %d\n", (int)sensor_detect_flag);

	return size;
}

static DEVICE_ATTR(sensor_detect, 0660, show_sensor_detect, store_sensor_detect);

static struct attribute *sensor_attributes[] = {
	&dev_attr_acc_info.attr,
	&dev_attr_mag_info.attr,
	&dev_attr_gyro_info.attr,
	&dev_attr_ps_info.attr,
	&dev_attr_als_info.attr,
	&dev_attr_acc_read_data.attr,
	&dev_attr_mag_read_data.attr,
	&dev_attr_gyro_read_data.attr,
	&dev_attr_ps_read_data.attr,
	&dev_attr_als_read_data.attr,
	&dev_attr_gyro_selfTest.attr,
	&dev_attr_mag_selfTest.attr,
	&dev_attr_acc_selfTest.attr,
	&dev_attr_connectivity_selfTest.attr,
	&dev_attr_i2c_rw.attr,
	&dev_attr_i2c_rw16.attr,
	&dev_attr_i2c_rw_16bit_reg.attr,
	&dev_attr_acc_calibrate.attr,
	&dev_attr_acc_enable.attr,
	&dev_attr_acc_setdelay.attr,
	&dev_attr_acc_set_calidata.attr,
	&dev_attr_set_data_type.attr,
	&dev_attr_set_fingersense_enable.attr,
	&dev_attr_fingersense_req_data.attr,
	&dev_attr_fingersense_data_ready.attr,
	&dev_attr_fingersense_latch_data.attr,
	&dev_attr_gsensor_gather_enable.attr,
	&dev_attr_gyro_calibrate.attr,
	&dev_attr_gyro_enable.attr,
	&dev_attr_gyro_setdelay.attr,
	&dev_attr_gyro_set_calidata.attr,
	&dev_attr_mag_enable.attr,
	&dev_attr_mag_setdelay.attr,
	&dev_attr_als_calibrate.attr,
	&dev_attr_als_enable.attr,
	&dev_attr_als_setdelay.attr,
	&dev_attr_ps_calibrate.attr,
	&dev_attr_ps_enable.attr,
	&dev_attr_ps_setdelay.attr,
	&dev_attr_pdr_enable.attr,
	&dev_attr_pdr_setdelay.attr,
	&dev_attr_orientation_enable.attr,
	&dev_attr_orientation_setdelay.attr,
	&dev_attr_lines_enable.attr,
	&dev_attr_lines_setdelay.attr,
	&dev_attr_gras_enable.attr,
	&dev_attr_gras_setdelay.attr,
	&dev_attr_rvs_enable.attr,
	&dev_attr_rvs_setdelay.attr,
	&dev_attr_sensor_list_info.attr,
	&dev_attr_iom3_recovery.attr,
	&dev_attr_sensor_test.attr,
	&dev_attr_iom3_sr_test.attr,
	&dev_attr_dt_motion_stup.attr,
	&dev_attr_dt_sensor_stup.attr,
	&dev_attr_dt_stop_auto_data.attr,
	&dev_attr_dt_hall_sensor_stup.attr,
	&dev_attr_dt_stop_als_auto_data.attr,
	&dev_attr_dt_als_sensor_stup.attr,
	&dev_attr_dt_stop_ps_auto_data.attr,
	&dev_attr_dt_ps_sensor_stup.attr,
	&dev_attr_dt_stop_auto_motion.attr,
	&dev_attr_airpress_info.attr,
	&dev_attr_airpress_enable.attr,
	&dev_attr_airpress_setdelay.attr,
	&dev_attr_airpress_read_data.attr,
	&dev_attr_airpress_set_calidata.attr,
	&dev_attr_read_airpress.attr,
	&dev_attr_read_temperature.attr,
	&dev_attr_dt_stepcounter_stup.attr,
	&dev_attr_handpress_calibrate.attr,
	&dev_attr_handpress_selfTest.attr,
	&dev_attr_handpress_info.attr,
	&dev_attr_handpress_enable.attr,
	&dev_attr_handpress_setdelay.attr,
	&dev_attr_handpress_read_data.attr,
	&dev_attr_ois_ctrl.attr,
	&dev_attr_iom3_sr_status.attr,
	&dev_attr_dump_sensor_status.attr,
	&dev_attr_cap_prox_calibrate.attr,
	&dev_attr_cap_prox_enable.attr,
	&dev_attr_cap_prox_setdelay.attr,
	&dev_attr_key_debug.attr,
	&dev_attr_sar_data.attr,
	&dev_attr_magn_bracket_enable.attr,
	&dev_attr_magn_bracket_setdelay.attr,
	&dev_attr_hifi_supported.attr,
	&dev_attr_sensor_detect.attr,
	NULL
};

static const struct attribute_group sensor_node = {
	.attrs = sensor_attributes,
};

static struct platform_device sensor_input_info = {
	.name = "huawei_sensor",
	.id = -1,
};
static int __init sensor_input_info_init(void)
{
	int ret;

	if (is_sensorhub_disabled())
		return -1;

	hwlog_info("[%s] ++\n", __func__);
	spin_lock_init(&fsdata_lock);
	ret = platform_device_register(&sensor_input_info);
	if (ret) {
		hwlog_err("%s: platform_device_register failed, ret:%d\n",
			__func__, ret);
		goto REGISTER_ERR;
	}

	ret = sysfs_create_group(&sensor_input_info.dev.kobj, &sensor_node);
	if (ret) {
		hwlog_err("sensor_input_info_init sysfs_create_group error ret =%d\n",
			ret);
		goto SYSFS_CREATE_CGOUP_ERR;
	}
	hwlog_info("[%s] --\n", __func__);
	return 0;
SYSFS_CREATE_CGOUP_ERR:
	platform_device_unregister(&sensor_input_info);
REGISTER_ERR:
	return ret;

}

late_initcall_sync(sensor_input_info_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Sensor input info");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
