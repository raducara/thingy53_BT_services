#ifndef BMM150_SERVICE_H_
#define BMM150_SERVICE_H_

#include <zephyr/types.h>

int bmm150_service_init(void);
int bmm150_execute(void);
int bmm150_get_chip_id(uint8_t *chip_id);

#endif /* BMM150_SERVICE_H_ */