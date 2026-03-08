#ifndef ASSISTANTD_SHUTDOWN_H
#define ASSISTANTD_SHUTDOWN_H

#include "assistantd/status.h"

assistantd_status_t assistantd_shutdown_install_handlers(void);
void assistantd_shutdown_request(void);
int assistantd_shutdown_requested(void);

#endif  // ASSISTANTD_SHUTDOWN_H
