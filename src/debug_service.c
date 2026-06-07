#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "debug_service.h"

static bool debug_notify_enabled;
static char debug_value[96];

#define BT_UUID_DEBUG_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc00f0)

#define BT_UUID_DEBUG_CHAR_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abc00f1)

static struct bt_uuid_128 debug_service_uuid =
	BT_UUID_INIT_128(BT_UUID_DEBUG_SERVICE_VAL);
static struct bt_uuid_128 debug_char_uuid =
	BT_UUID_INIT_128(BT_UUID_DEBUG_CHAR_VAL);

static ssize_t read_debug(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr,
			  void *buf,
			  uint16_t len,
			  uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 value, strlen(value));
}

static void debug_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	debug_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Debug notifications %s\n",
	       debug_notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(debug_svc,
	BT_GATT_PRIMARY_SERVICE(&debug_service_uuid),
	BT_GATT_CHARACTERISTIC(&debug_char_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_debug, NULL, debug_value),
	BT_GATT_CCC(debug_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int debug_service_init(void)
{
	debug_value[0] = '\0';
	return 0;
}

int debug_service_set_message(const char *msg)
{
	if (msg == NULL) {
		return -EINVAL;
	}

	strncpy(debug_value, msg, sizeof(debug_value) - 1);
	debug_value[sizeof(debug_value) - 1] = '\0';

	return 0;
}

int debug_service_notify(void)
{
	if (!debug_notify_enabled) {
		return 0;
	}

	return bt_gatt_notify(NULL, &debug_svc.attrs[2],
			      debug_value, strlen(debug_value));
}