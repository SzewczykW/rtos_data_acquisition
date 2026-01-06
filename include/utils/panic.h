/**
 * @file panic.h
 * @brief System panic and error handlers for FreeRTOS
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Print panic message using raw UART registers
     * @param msg Null-terminated panic message
     * @param info Additional info (e.g., task name), can be NULL
     * @note This function does NOT return - enters infinite loop
     */
    void panic(const char *msg, const char *info) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* PANIC_H */
