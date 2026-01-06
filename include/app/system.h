/**
 * @file system.h
 * @brief System initialization and FreeRTOS hooks
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Initialize system before starting FreeRTOS scheduler
     * @return 0 on success, -1 on error
     */
    int system_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_H */
