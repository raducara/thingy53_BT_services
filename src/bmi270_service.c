#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#define BMI270_REG_CHIP_ID         0x00

extern int bmi270_reg_read(const struct device *dev, uint8_t reg,
                            uint8_t *data, uint16_t length);

#include "bmi270_service.h"

static bool bmi270_ready;
static bool bmi270_notify_enabled;
static const struct device *bmi270 = DEVICE_DT_GET(DT_NODELABEL(bmi270));

#define BT_UUID_BMI270_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0040)

#define BT_UUID_BMI270_CHAR_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0041)

static struct bt_uuid_128 bmi270_service_uuid =
	BT_UUID_INIT_128(BT_UUID_BMI270_SERVICE_VAL);

static struct bt_uuid_128 bmi270_char_uuid =
	BT_UUID_INIT_128(BT_UUID_BMI270_CHAR_VAL);

static void bmi270_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bmi270_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("BMI270 notifications %s\n",
	       bmi270_notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(bmi270_svc,
	BT_GATT_PRIMARY_SERVICE(&bmi270_service_uuid),
	BT_GATT_CHARACTERISTIC(&bmi270_char_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(bmi270_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static int bmi270_send_notification(const struct sensor_value *accel_x,
				   const struct sensor_value *accel_y,
				   const struct sensor_value *accel_z,
				   const struct sensor_value *gyro_x,
				   const struct sensor_value *gyro_y,
				   const struct sensor_value *gyro_z)
{
	char msg[128];

	if (!bmi270_notify_enabled) {
		return 0;
	}

	snprintk(msg, sizeof(msg),
		 "AX=%d.%06d AY=%d.%06d AZ=%d.%06d GX=%d.%06d GY=%d.%06d GZ=%d.%06d",
		 accel_x->val1, accel_x->val2,
		 accel_y->val1, accel_y->val2,
		 accel_z->val1, accel_z->val2,
		 gyro_x->val1, gyro_x->val2,
		 gyro_y->val1, gyro_y->val2,
		 gyro_z->val1, gyro_z->val2);

	return bt_gatt_notify(NULL, &bmi270_svc.attrs[2], msg, strlen(msg));
}

int bmi270_service_init(void)
{
	if (!DT_NODE_EXISTS(DT_NODELABEL(bmi270))) {
		printk("BMI270 DT node not found\n");
		bmi270_ready = false;
		return -ENODEV;
	}

	if (bmi270 == NULL) {
		printk("BMI270 device pointer is NULL\n");
		bmi270_ready = false;
		return -ENODEV;
	}

	if (!device_is_ready(bmi270)) {
		printk("BMI270 device not ready: %p\n", bmi270);
		bmi270_ready = false;
		return -ENODEV;
	}

	printk("BMI270 sensor ready: %s\n", bmi270->name);
	bmi270_ready = true;

	return 0;
}

int bmi270_get_chip_id(uint8_t *chip_id)
{
	int err;

	if (chip_id == NULL) {
		return -EINVAL;
	}

	if (!bmi270_ready) {
		return -ENODEV;
	}

	err = bmi270_reg_read(bmi270, BMI270_REG_CHIP_ID, chip_id, 1);
	if (err) {
		printk("BMI270 chip id read failed (err %d)\n", err);
		return err;
	}

	printk("BMI270 chip id: 0x%02x\n", *chip_id);
	return 0;
}

int bmi270_execute(void)
{
	int err;
	struct sensor_value accel_x;
	struct sensor_value accel_y;
	struct sensor_value accel_z;
	struct sensor_value gyro_x;
	struct sensor_value gyro_y;
	struct sensor_value gyro_z;

	if (!bmi270_ready) {
		return -ENODEV;
	}

	err = sensor_sample_fetch(bmi270);
	if (err) {
		printk("BMI270 sensor_sample_fetch failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bmi270, SENSOR_CHAN_ACCEL_X, &accel_x);
	if (err) {
		printk("BMI270 accel X read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bmi270, SENSOR_CHAN_ACCEL_Y, &accel_y);
	if (err) {
		printk("BMI270 accel Y read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bmi270, SENSOR_CHAN_ACCEL_Z, &accel_z);
	if (err) {
		printk("BMI270 accel Z read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bmi270, SENSOR_CHAN_GYRO_X, &gyro_x);
	if (err) {
		printk("BMI270 gyro X read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bmi270, SENSOR_CHAN_GYRO_Y, &gyro_y);
	if (err) {
		printk("BMI270 gyro Y read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bmi270, SENSOR_CHAN_GYRO_Z, &gyro_z);
	if (err) {
		printk("BMI270 gyro Z read failed (err %d)\n", err);
		return err;
	}

	err = bmi270_send_notification(&accel_x, &accel_y, &accel_z,
				       &gyro_x, &gyro_y, &gyro_z);
	if (err && err != -ENOTCONN) {
		printk("BMI270 notify failed (err %d)\n", err);
		return err;
	}

	printk("AX:%d.%06d AY:%d.%06d AZ:%d.%06d GX:%d.%06d GY:%d.%06d GZ:%d.%06d\n",
	       accel_x.val1, accel_x.val2,
	       accel_y.val1, accel_y.val2,
	       accel_z.val1, accel_z.val2,
	       gyro_x.val1, gyro_x.val2,
	       gyro_y.val1, gyro_y.val2,
	       gyro_z.val1, gyro_z.val2);

	return 0;
}
