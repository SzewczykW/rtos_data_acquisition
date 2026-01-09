/**
 * @file task_acquisition.h
 * @brief Data acquisition task for ADC sampling and network transmission
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#ifndef TASK_ACQUISITION_H
#define TASK_ACQUISITION_H

#include "adc.h"
#include "cmsis_os2.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**< Stack size for the acquisition task */
#define TASK_ACQUISITION_STACK_SIZE 1024
/**< Priority for the acquisition task */
#define TASK_ACQUISITION_PRIORITY osPriorityBelowNormal
/**< Default ADC channel for acquisition */
#define TASK_ACQUISITION_DEFAULT_CHANNEL ADC_CHANNEL_0
/**< Default threshold in millivolts */
#define TASK_ACQUISITION_DEFAULT_THRESHOLD_MV 1650
/**< ADC reference voltage in millivolts */
#define ADC_VREF_MV 3300
/**< Default batch size */
#define ACQUISITION_DEFAULT_BATCH_SIZE 100
/**< Maximum batch size (samples per packet) */
#define ACQUISITION_MAX_BATCH_SIZE 500

    /**
     * @brief Acquisition task state
     */
    typedef enum
    {
        ACQ_STATE_IDLE = 0, /**< Not acquiring */
        ACQ_STATE_RUNNING,  /**< Acquiring data */
        ACQ_STATE_ERROR     /**< Error state */
    } acquisition_state_t;

    /**
     * @brief Acquisition statistics
     */
    typedef struct
    {
        uint32_t samples_collected; /**< Total samples collected */
        uint32_t packets_sent;      /**< Total packets sent */
        uint32_t errors;            /**< Error count */
    } acquisition_stats_t;

    /**
     * @brief Initialize acquisition module
     * @return 0 on success, negative on error
     */
    int acquisition_init(void);

    /**
     * @brief Create and start acquisition task
     * @return 0 on success, negative on error
     */
    int acquisition_task_start(void);

    /**
     * @brief Start data acquisition
     * @return 0 on success, negative on error
     */
    int acquisition_start(void);

    /**
     * @brief Stop data acquisition
     * @return 0 on success, negative on error
     */
    int acquisition_stop(void);

    /**
     * @brief Check if acquisition is running
     * @return true if acquiring
     */
    bool acquisition_is_running(void);

    /**
     * @brief Get current acquisition state
     * @return Current state
     */
    acquisition_state_t acquisition_get_state(void);

    /**
     * @brief Set ADC threshold (trigger level)
     * @param threshold_mv Threshold in millivolts
     * @return 0 on success, negative on error
     */
    int acquisition_set_threshold_mv(uint16_t threshold_mv);

    /**
     * @brief Set ADC threshold as percentage of range
     * @param percent Threshold as percentage (0-100)
     * @return 0 on success, negative on error
     */
    int acquisition_set_threshold_percent(uint8_t percent);

    /**
     * @brief Get current threshold in millivolts
     * @return Threshold in millivolts
     */
    uint16_t acquisition_get_threshold_mv(void);

    /**
     * @brief Set ADC channel
     * @param channel ADC channel
     * @return 0 on success, negative on error
     */
    int acquisition_set_channel(adc_channel_t channel);

    /**
     * @brief Get current ADC channel
     * @return Current channel
     */
    adc_channel_t acquisition_get_channel(void);

    /**
     * @brief Get acquisition statistics
     * @param stats Pointer to store statistics
     */
    void acquisition_get_stats(acquisition_stats_t *stats);

    /**
     * @brief Set batch size (samples per packet)
     * @param batch_size Number of samples per packet (1 to ACQUISITION_MAX_BATCH_SIZE)
     * @return 0 on success, negative on error
     */
    int acquisition_set_batch_size(uint16_t batch_size);

    /**
     * @brief Get current batch size
     * @return Current batch size
     */
    uint16_t acquisition_get_batch_size(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_ACQUISITION_H */
