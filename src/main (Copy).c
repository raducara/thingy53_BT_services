/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#if defined(CONFIG_SENSOR)
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#endif
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <soc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/lbs.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#include "bmm150_service.h"

#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)


#define RUN_STATUS_LED          DK_LED1
#define CON_STATUS_LED          DK_LED2
#define RUN_LED_BLINK_INTERVAL  1000

#define USER_LED                DK_LED3

#define USER_BUTTON             DK_BTN1_MSK

#if defined(CONFIG_SENSOR)

/*BME68x sensor */
static bool bme_notify_enabled;

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
#endif

/*BMI270 accel sensor*/

#if defined(CONFIG_SENSOR)
static bool accel_notify_enabled;

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
		 "AX=%d.%03d AY=%d.%03d AZ=%d.%03d",
		 x->val1, x->val2,
		 y->val1, y->val2,
		 z->val1, z->val2);

	return bt_gatt_notify(NULL, &accel_svc.attrs[2], msg, strlen(msg));
}
#endif


static bool app_button_state;
static struct k_work adv_work;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LBS_VAL),
};

static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

static void advertising_start(void)
{
	k_work_submit(&adv_work);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
		return;
	}

	printk("Connected\n");

	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));

	dk_set_led_off(CON_STATUS_LED);
}

static void recycled_cb(void)
{
	printk("Connection object available from previous conn. Disconnect is complete!\n");
	advertising_start();
}

#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d %s\n", addr, level, err,
		       bt_security_err_to_str(err));
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.recycled         = recycled_cb,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_LBS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d %s\n", addr, reason,
	       bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif

static void app_led_cb(bool led_state)
{
	dk_set_led(USER_LED, led_state);
}

static bool app_button_cb(void)
{
	return app_button_state;
}

static struct bt_lbs_cb lbs_callbacks = {
	.led_cb    = app_led_cb,
	.button_cb = app_button_cb,
};

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & USER_BUTTON) {
		uint32_t user_button_state = button_state & USER_BUTTON;

		bt_lbs_send_button_state(user_button_state);
		app_button_state = user_button_state ? true : false;
	}
}

static int init_button(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Cannot init buttons (err: %d)\n", err);
	}

	return err;
}

int main(void)
{
	int blink_status = 0;
	int err;

#if defined(CONFIG_SENSOR)
    bool sensor_ready = false;
    bool accel_ready = false;
    const struct device *bme = DEVICE_DT_GET_ONE(bosch_bme680);
	const struct device *accel = DEVICE_DT_GET(DT_ALIAS(accel0));

    struct sensor_value temp;
    struct sensor_value press;
    struct sensor_value humidity;
    struct sensor_value gas_res;
    struct sensor_value accel_x;
    struct sensor_value accel_y;
    struct sensor_value accel_z;
#endif

	printk("Starting Bluetooth Peripheral LBS sample\n");

#if defined(CONFIG_SENSOR)
	printk("BME68x support enabled on %s\n", CONFIG_BOARD);

	if (!device_is_ready(bme)) {
		printk("BME sensor device not ready\n");
	} else {
		printk("BME sensor ready: %s\n", bme->name);
		sensor_ready = true;
	}

	if (!device_is_ready(accel)) {
		printk("Accelerometer device not ready\n");
	} else {
		printk("Accelerometer ready: %s\n", accel->name);
		accel_ready = true;
	}

#endif

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return 0;
	}

	err = init_button();
	if (err) {
		printk("Button init failed (err %d)\n", err);
		return 0;
	}

	if (IS_ENABLED(CONFIG_BT_LBS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			printk("Failed to register authorization callbacks.\n");
			return 0;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			printk("Failed to register authorization info callbacks.\n");
			return 0;
		}
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_lbs_init(&lbs_callbacks);
	if (err) {
		printk("Failed to init LBS (err:%d)\n", err);
		return 0;
	}

	err = bmm150_service_init();
	if (err) {
		printk("Failed to init BMM150 service (err %d)\n", err);
		return 0;
	}

	k_work_init(&adv_work, adv_work_handler);
	advertising_start();

	for (;;) {

/*read env BM68x data from sensor and send BT notif*/
#if defined(CONFIG_SENSOR)
		if (sensor_ready) {
            err = sensor_sample_fetch(bme);
            if (err) {
                printk("sensor_sample_fetch failed (err %d)\n", err);
            } else {
                err = sensor_channel_get(bme, SENSOR_CHAN_AMBIENT_TEMP, &temp);
                if (err) {
                    printk("temp read failed (err %d)\n", err);
                }

                if (!err) {
                    err = sensor_channel_get(bme, SENSOR_CHAN_PRESS, &press);
                    if (err) {
                        printk("press read failed (err %d)\n", err);
                    }
                }

                if (!err) {
                    err = sensor_channel_get(bme, SENSOR_CHAN_HUMIDITY, &humidity);
                    if (err) {
                        printk("humidity read failed (err %d)\n", err);
                    }
                }

                if (!err) {
                    err = sensor_channel_get(bme, SENSOR_CHAN_GAS_RES, &gas_res);
                    if (err) {
                        printk("gas resistance read failed (err %d)\n", err);
                    }
                }

                if (!err) {
                    err = bme_send_notification(&temp, &press, &humidity, &gas_res);
                    if (err && err != -ENOTCONN) {
                        printk("BME notify failed (err %d)\n", err);
                    }
                }

                printk("T: %d.%06d | P: %d.%06d | H: %d.%06d | G: %d.%06d\n",
                       temp.val1, temp.val2,
                       press.val1, press.val2,
                       humidity.val1, humidity.val2,
                       gas_res.val1, gas_res.val2);
            }
        }
#endif

/*read accelerometer data from sensor and send BT notif*/
#if defined(CONFIG_SENSOR)
        if (accel_ready) {
            err = sensor_sample_fetch(accel);
            if (err) {
                printk("accel sensor_sample_fetch failed (err %d)\n", err);
            } else {
                err = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_X, &accel_x);
                if (err) {
                    printk("accel X read failed (err %d)\n", err);
                }

                if (!err) {
                    err = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Y, &accel_y);
                    if (err) {
                        printk("accel Y read failed (err %d)\n", err);
                    }
                }

                if (!err) {
                    err = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Z, &accel_z);
                    if (err) {
                        printk("accel Z read failed (err %d)\n", err);
                    }
                }

                if (!err) {
                    err = accel_send_notification(&accel_x, &accel_y, &accel_z);
                    if (err && err != -ENOTCONN) {
                        printk("Accel notify failed (err %d)\n", err);
                    }
                }

                printk("AX: %d.%06d | AY: %d.%06d | AZ: %d.%06d\n",
                       accel_x.val1, accel_x.val2,
                       accel_y.val1, accel_y.val2,
                       accel_z.val1, accel_z.val2);
            }
        }
#endif

		err = bmm150_execute();
		if (err && err != -ENODEV && err != -ENOTCONN) {
			printk("bmm150_execute failed (err %d)\n", err);
		}

		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}
