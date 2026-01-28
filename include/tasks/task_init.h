/**
 * @file task_init.h
 * @brief System initialization task
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

/**
 * @defgroup TaskInit Task Init
 * @{
 */

#ifndef TASK_INIT_H
#define TASK_INIT_H

#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Init task configuration */
#define TASK_INIT_STACK_SIZE 2048
#define TASK_INIT_PRIORITY   osPriorityHigh

    /**
     * @brief Create init task
     * @return 0 on success, -1 on error
     */
    int init_task_start(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_INIT_H */

/** End of TaskInit group */
/** @} */
