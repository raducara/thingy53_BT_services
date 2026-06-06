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

#include "bme68x_service.h"

static bool bme_ready;
static bool bme_notify_enabled;
static const struct device *bme = DEVICE_DT_GET_ONE(bosch_bme680);

#define BT_UUID_BME_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0000)

#define BT_UUID_BME_CHAR_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0001)

static struct bt_uuid_128 bme_service_uuid =
	BT_UUID_INIT_128(BT_UUID_BME_SERVICE_VAL);

static struct bt_uuid_128 bme_char_uuid =
	BT_UUID_INIT_128(BT_UUID_BME_CHAR_VAL);

static void bme_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bme_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("BME notifications %s\n", bme_notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(bme_svc,
	BT_GATT_PRIMARY_SERVICE(&bme_service_uuid),
	BT_GATT_CHARACTERISTIC(&bme_char_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(bme_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static int bme_send_notification(const struct sensor_value *temp,
				 const struct sensor_value *press,
				 const struct sensor_value *humidity,
				 const struct sensor_value *gas_res)
{
	char msg[96];

	if (!bme_notify_enabled) {
		return 0;
	}

	snprintk(msg, sizeof(msg),
		 "T=%d.%03d P=%d.%05d H=%d.%06d G=%d.%06d",
		 temp->val1, temp->val2,
		 press->val1, press->val2,
		 humidity->val1, humidity->val2,
		 gas_res->val1, gas_res->val2);

	return bt_gatt_notify(NULL, &bme_svc.attrs[2], msg, strlen(msg));
}

int bme68x_service_init(void)
{
	if (!device_is_ready(bme)) {
		printk("BME sensor device not ready\n");
		bme_ready = false;
		return -ENODEV;
	}

	printk("BME sensor ready: %s\n", bme->name);
	bme_ready = true;

	return 0;
}

int bme68x_execute(void)
{
	int err;
	struct sensor_value temp;
	struct sensor_value press;
	struct sensor_value humidity;
	struct sensor_value gas_res;

	if (!bme_ready) {
		return -ENODEV;
	}

	err = sensor_sample_fetch(bme);
	if (err) {
		printk("sensor_sample_fetch failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bme, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	if (err) {
		printk("temp read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bme, SENSOR_CHAN_PRESS, &press);
	if (err) {
		printk("press read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bme, SENSOR_CHAN_HUMIDITY, &humidity);
	if (err) {
		printk("humidity read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(bme, SENSOR_CHAN_GAS_RES, &gas_res);
	if (err) {
		printk("gas resistance read failed (err %d)\n", err);
		return err;
	}

	err = bme_send_notification(&temp, &press, &humidity, &gas_res);
	if (err && err != -ENOTCONN) {
		printk("BME notify failed (err %d)\n", err);
		return err;
	}

	printk("T: %d.%06d | P: %d.%06d | H: %d.%06d | G: %d.%06d\n",
	       temp.val1, temp.val2,
	       press.val1, press.val2,
	       humidity.val1, humidity.val2,
	       gas_res.val1, gas_res.val2);

	return 0;
}