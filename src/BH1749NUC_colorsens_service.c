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

#include "BH1749NUC_colorsens_service.h"

static bool color_ready;
static bool color_notify_enabled;
static const struct device *color = DEVICE_DT_GET(DT_NODELABEL(bh1749));

#define BT_UUID_COLOR_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0030)

#define BT_UUID_COLOR_CHAR_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0031)

static struct bt_uuid_128 color_service_uuid =
	BT_UUID_INIT_128(BT_UUID_COLOR_SERVICE_VAL);

static struct bt_uuid_128 color_char_uuid =
	BT_UUID_INIT_128(BT_UUID_COLOR_CHAR_VAL);

static void color_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	color_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Color sensor notifications %s\n",
	       color_notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(color_svc,
	BT_GATT_PRIMARY_SERVICE(&color_service_uuid),
	BT_GATT_CHARACTERISTIC(&color_char_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(color_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static int color_send_notification(const struct sensor_value *red,
				   const struct sensor_value *green,
				   const struct sensor_value *blue,
				   const struct sensor_value *ir)
{
	char msg[96];

	if (!color_notify_enabled) {
		return 0;
	}

	snprintk(msg, sizeof(msg),
		 "R=%d.%06d G=%d.%06d B=%d.%06d IR=%d.%06d",
		 red->val1, red->val2,
		 green->val1, green->val2,
		 blue->val1, blue->val2,
		 ir->val1, ir->val2);

	return bt_gatt_notify(NULL, &color_svc.attrs[2], msg, strlen(msg));
}

int bh1749_service_init(void)
{
	if (!DT_NODE_EXISTS(DT_NODELABEL(bh1749))) {
		printk("BH1749 DT node not found\n");
		color_ready = false;
		return -ENODEV;
	}

	if (color == NULL) {
		printk("BH1749 device pointer is NULL\n");
		color_ready = false;
		return -ENODEV;
	}

	if (!device_is_ready(color)) {
		printk("BH1749 color sensor device not ready: %p\n", color);
		color_ready = false;
		return -ENODEV;
	}

	printk("BH1749 color sensor ready: %s (%p)\n", color->name, color);
	color_ready = true;

	return 0;
}

int bh1749_execute(void)
{
	int err;
	struct sensor_value red;
	struct sensor_value green;
	struct sensor_value blue;
	struct sensor_value ir;

	if (!color_ready) {
		return -ENODEV;
	}

	err = sensor_sample_fetch(color);
	if (err) {
		printk("color sensor_sample_fetch failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(color, SENSOR_CHAN_RED, &red);
	if (err) {
		printk("red channel read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(color, SENSOR_CHAN_GREEN, &green);
	if (err) {
		printk("green channel read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(color, SENSOR_CHAN_BLUE, &blue);
	if (err) {
		printk("blue channel read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(color, SENSOR_CHAN_IR, &ir);
	if (err) {
		printk("IR channel read failed (err %d)\n", err);
		return err;
	}

	err = color_send_notification(&red, &green, &blue, &ir);
	if (err && err != -ENOTCONN) {
		printk("Color sensor notify failed (err %d)\n", err);
		return err;
	}

	printk("R: %d.%06d | G: %d.%06d | B: %d.%06d | IR: %d.%06d\n",
	       red.val1, red.val2,
	       green.val1, green.val2,
	       blue.val1, blue.val2,
	       ir.val1, ir.val2);

	return 0;
}
