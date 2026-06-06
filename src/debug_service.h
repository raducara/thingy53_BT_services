#ifndef DEBUG_SERVICE_H_
#define DEBUG_SERVICE_H_

#include <stddef.h>

int debug_service_init(void);
int debug_service_set_message(const char *msg);
int debug_service_notify(void);

#endif /* DEBUG_SERVICE_H_ */