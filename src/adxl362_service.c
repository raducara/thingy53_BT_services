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

#include "adxl362_service.h"

static bool accel_ready;
static bool accel_notify_enabled;
static const struct device *accel = DEVICE_DT_GET(DT_ALIAS(accel0));

#define BT_UUID_ACCEL_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0010)

#define BT_UUID_ACCEL_CHAR_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0011)

static struct bt_uuid_128 accel_service_uuid =
	BT_UUID_INIT_128(BT_UUID_ACCEL_SERVICE_VAL);

static struct bt_uuid_128 accel_char_uuid =
	BT_UUID_INIT_128(BT_UUID_ACCEL_CHAR_VAL);

static void accel_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	accel_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Accel notifications %s\n", accel_notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(accel_svc,
	BT_GATT_PRIMARY_SERVICE(&accel_service_uuid),
	BT_GATT_CHARACTERISTIC(&accel_char_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(accel_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static int accel_send_notification(const struct sensor_value *x,
				   const struct sensor_value *y,
				   const struct sensor_value *z)
{
	char msg[64];

	if (!accel_notify_enabled) {
		return 0;
	}

	snprintk(msg, sizeof(msg),
		 "AX=%d.%06d AY=%d.%06d AZ=%d.%06d",
		 x->val1, x->val2,
		 y->val1, y->val2,
		 z->val1, z->val2);

	return bt_gatt_notify(NULL, &accel_svc.attrs[2], msg, strlen(msg));
}

int adxl362_service_init(void)
{
	if (!device_is_ready(accel)) {
		printk("Accelerometer device not ready\n");
		accel_ready = false;
		return -ENODEV;
	}

	printk("Accelerometer ready: %s\n", accel->name);
	accel_ready = true;

	return 0;
}

int adxl362_execute(void)
{
	int err;
	struct sensor_value accel_x;
	struct sensor_value accel_y;
	struct sensor_value accel_z;

	if (!accel_ready) {
		return -ENODEV;
	}

	err = sensor_sample_fetch(accel);
	if (err) {
		printk("accel sensor_sample_fetch failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_X, &accel_x);
	if (err) {
		printk("accel X read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Y, &accel_y);
	if (err) {
		printk("accel Y read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Z, &accel_z);
	if (err) {
		printk("accel Z read failed (err %d)\n", err);
		return err;
	}

	err = accel_send_notification(&accel_x, &accel_y, &accel_z);
	if (err && err != -ENOTCONN) {
		printk("Accel notify failed (err %d)\n", err);
		return err;
	}

	printk("AX: %d.%06d | AY: %d.%06d | AZ: %d.%06d\n",
	       accel_x.val1, accel_x.val2,
	       accel_y.val1, accel_y.val2,
	       accel_z.val1, accel_z.val2);

	return 0;
}