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
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "bmm150_service.h"

static bool mag_ready;
static bool bmm150_notify_enabled;

static const struct device *mag = DEVICE_DT_GET_ONE(bosch_bmm150);

#define BMM150_REG_CHIP_ID         0x40

union bmm150_bus;

typedef int (*bmm150_reg_read_fn)(const union bmm150_bus *bus,
                                  uint8_t start,
                                  uint8_t *buf,
                                  int size);

typedef int (*bmm150_reg_write_fn)(const union bmm150_bus *bus,
                                   uint8_t reg,
                                   uint8_t val);

struct bmm150_bus_io {
        bmm150_reg_read_fn read;
        bmm150_reg_write_fn write;
};

union bmm150_bus {
#if defined(CONFIG_I2C)
        struct i2c_dt_spec i2c;
#endif
#if defined(CONFIG_SPI)
        struct spi_dt_spec spi;
#endif
};

struct bmm150_config {
        union bmm150_bus bus;
        const struct bmm150_bus_io *bus_io;
};

#define BT_UUID_BMM150_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0020)

#define BT_UUID_BMM150_CHAR_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc0021)

static struct bt_uuid_128 bmm150_service_uuid =
	BT_UUID_INIT_128(BT_UUID_BMM150_SERVICE_VAL);

static struct bt_uuid_128 bmm150_char_uuid =
	BT_UUID_INIT_128(BT_UUID_BMM150_CHAR_VAL);

static void bmm150_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bmm150_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("BMM150 notifications %s\n",
	       bmm150_notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(bmm150_svc,
	BT_GATT_PRIMARY_SERVICE(&bmm150_service_uuid),
	BT_GATT_CHARACTERISTIC(&bmm150_char_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(bmm150_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static int bmm150_send_notification(const struct sensor_value *x,
				    const struct sensor_value *y,
				    const struct sensor_value *z)
{
	char msg[64];

	if (!bmm150_notify_enabled) {
		return 0;
	}

	snprintk(msg, sizeof(msg),
		 "MX=%d.%06d MY=%d.%06d MZ=%d.%06d",
		 x->val1, x->val2,
		 y->val1, y->val2,
		 z->val1, z->val2);

	return bt_gatt_notify(NULL, &bmm150_svc.attrs[2], msg, strlen(msg));
}

int bmm150_service_init(void)
{
	if (mag == NULL) {
		printk("BMM150 device pointer is NULL\n");
		mag_ready = false;
		return -ENODEV;
	}

	if (!device_is_ready(mag)) {
		printk("BMM150 device not ready: %p\n", mag);
		mag_ready = false;
		return -ENODEV;
	}

	printk("BMM150 ready: %s (%p)\n", mag->name, mag);
	mag_ready = true;

	return 0;
}

int bmm150_get_chip_id(uint8_t *chip_id)
{
	const struct bmm150_config *cfg;
	int err;

	if (chip_id == NULL) {
		return -EINVAL;
	}

	if (!mag_ready) {
		return -ENODEV;
	}

	cfg = mag->config;
	if (cfg == NULL || cfg->bus_io == NULL || cfg->bus_io->read == NULL) {
		return -EINVAL;
	}

	err = cfg->bus_io->read(&cfg->bus, BMM150_REG_CHIP_ID, chip_id, 1);
	if (err) {
		printk("BMM150 chip id read failed (err %d)\n", err);
		return err;
	}

	printk("BMM150 chip id: 0x%02x\n", *chip_id);
	return 0;
}

int bmm150_execute(void)
{
	int err;
	struct sensor_value magn_x;
	struct sensor_value magn_y;
	struct sensor_value magn_z;

	if (!mag_ready) {
		return -ENODEV;
	}

	err = sensor_sample_fetch(mag);
	if (err) {
		printk("mag sensor_sample_fetch failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(mag, SENSOR_CHAN_MAGN_X, &magn_x);
	if (err) {
		printk("mag X read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(mag, SENSOR_CHAN_MAGN_Y, &magn_y);
	if (err) {
		printk("mag Y read failed (err %d)\n", err);
		return err;
	}

	err = sensor_channel_get(mag, SENSOR_CHAN_MAGN_Z, &magn_z);
	if (err) {
		printk("mag Z read failed (err %d)\n", err);
		return err;
	}

	err = bmm150_send_notification(&magn_x, &magn_y, &magn_z);
	if (err && err != -ENOTCONN) {
		printk("BMM150 notify failed (err %d)\n", err);
		return err;
	}

	printk("MX: %d.%06d | MY: %d.%06d | MZ: %d.%06d\n",
	       magn_x.val1, magn_x.val2,
	       magn_y.val1, magn_y.val2,
	       magn_z.val1, magn_z.val2);

	return 0;
}