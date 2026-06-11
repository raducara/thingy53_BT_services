#ifndef BMI270_SERVICE_H_
#define BMI270_SERVICE_H_

#include <zephyr/types.h>

int bmi270_service_init(void);
int bmi270_execute(void);
int bmi270_get_chip_id(uint8_t *chip_id);

#endif /* BMI270_SERVICE_H_ */
