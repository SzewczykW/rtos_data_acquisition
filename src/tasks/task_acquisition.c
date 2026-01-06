/**
 * @file task_acquisition.c
 * @brief Data acquisition task implementation
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "task_acquisition.h"

#include "cmsis_os2.h"
#include "logger.h"
#include "protocol.h"
#include "task_network.h"

#include <string.h>

/** Acquisition loop delay in ms (controls sample rate) */
#define ACQUISITION_LOOP_DELAY_MS 1

static osThreadId_t acquisition_thread = NULL;
static const osThreadAttr_t acquisition_thread_attr = {
    .name = "AcquisitionTask",
    .stack_size = TASK_ACQUISITION_STACK_SIZE * sizeof(uint32_t),
    .priority = TASK_ACQUISITION_PRIORITY,
};
static volatile acquisition_state_t current_state = ACQ_STATE_IDLE;
static acquisition_stats_t stats = {0};
static adc_channel_t current_channel = TASK_ACQUISITION_DEFAULT_CHANNEL;
static uint16_t threshold_mv = TASK_ACQUISITION_DEFAULT_THRESHOLD_MV;
static uint16_t batch_size = ACQUISITION_DEFAULT_BATCH_SIZE;
static uint16_t sample_buffer[ACQUISITION_MAX_BATCH_SIZE];
static uint16_t sample_index = 0;
static bool initialized = false;
static uint8_t tx_buffer[512];

/**
 * @brief Convert millivolts to ADC value
 */
static uint16_t mv_to_adc(uint16_t mv)
{
    /* ADC is 12-bit (0-4095), VREF is 3.3V */
    return (uint16_t)((uint32_t)mv * 4095 / ADC_VREF_MV);
}

/**
 * @brief Main acquisition task
 */
static void acquisition_task(void *argument)
{
    (void)argument;

    uint16_t adc_value;
    uint16_t threshold_adc;

    LOG_INFO("Acquisition task running");

    while (1)
    {
        if (current_state != ACQ_STATE_RUNNING)
        {
            osDelay(100);
            continue;
        }

        if (!network_is_ready())
        {
            osDelay(100);
            continue;
        }

        adc_status_t status = adc_read_sync(&adc_value);
        if (status != ADC_OK)
        {
            stats.errors++;
            osDelay(ACQUISITION_LOOP_DELAY_MS);
            continue;
        }

        threshold_adc = mv_to_adc(threshold_mv);

        if (adc_value >= threshold_adc)
        {
            sample_buffer[sample_index++] = adc_value;
            stats.samples_collected++;

            if (sample_index >= batch_size)
            {
                size_t packet_len;
                protocol_status_t proto_status = protocol_build_data_packet(
                    tx_buffer, sizeof(tx_buffer), current_channel, sample_buffer,
                    sample_index, &packet_len
                );

                if (proto_status == PROTO_STATUS_OK)
                {
                    if (network_send_raw(tx_buffer, packet_len) == 0)
                    {
                        stats.packets_sent++;
                    }
                    else
                    {
                        stats.errors++;
                    }
                }
                else
                {
                    stats.errors++;
                }

                sample_index = 0;
            }
        }

        osDelay(ACQUISITION_LOOP_DELAY_MS);
    }
}

int acquisition_init(void)
{
    if (initialized)
    {
        return 0;
    }

    adc_status_t status = adc_init(current_channel);
    if (status != ADC_OK)
    {
        LOG_ERROR("ADC initialization failed: %d", status);
        return -1;
    }

    memset(&stats, 0, sizeof(stats));
    sample_index = 0;
    current_state = ACQ_STATE_IDLE;

    initialized = true;
    LOG_INFO("Acquisition module initialized");

    return 0;
}

int acquisition_task_start(void)
{
    if (!initialized)
    {
        LOG_ERROR("Acquisition not initialized");
        return -1;
    }

    if (acquisition_thread != NULL)
    {
        LOG_WARNING("Acquisition task already running");
        return 0;
    }

    acquisition_thread = osThreadNew(acquisition_task, NULL, &acquisition_thread_attr);
    if (acquisition_thread == NULL)
    {
        LOG_ERROR("Failed to create acquisition task");
        return -1;
    }

    LOG_INFO("Acquisition task started");
    return 0;
}

int acquisition_start(void)
{
    if (!initialized)
    {
        return -1;
    }

    if (current_state == ACQ_STATE_RUNNING)
    {
        return 0; /* Already running */
    }

    sample_index = 0;
    current_state = ACQ_STATE_RUNNING;
    LOG_INFO(
        "Acquisition started on channel %u, threshold %u mV", current_channel,
        threshold_mv
    );

    return 0;
}

int acquisition_stop(void)
{
    if (!initialized)
    {
        return -1;
    }

    current_state = ACQ_STATE_IDLE;
    LOG_INFO("Acquisition stopped");

    return 0;
}

bool acquisition_is_running(void)
{
    return (current_state == ACQ_STATE_RUNNING);
}

acquisition_state_t acquisition_get_state(void)
{
    return current_state;
}

int acquisition_set_threshold_mv(uint16_t mv)
{
    if (mv > ADC_VREF_MV)
    {
        return -1;
    }

    threshold_mv = mv;
    LOG_DEBUG("Threshold set to %u mV", threshold_mv);
    return 0;
}

int acquisition_set_threshold_percent(uint8_t percent)
{
    if (percent > 100)
    {
        return -1;
    }

    threshold_mv = (uint16_t)((uint32_t)percent * ADC_VREF_MV / 100);
    LOG_DEBUG("Threshold set to %u%% (%u mV)", percent, threshold_mv);
    return 0;
}

uint16_t acquisition_get_threshold_mv(void)
{
    return threshold_mv;
}

int acquisition_set_channel(adc_channel_t channel)
{
    if (channel >= ADC_CHANNEL_MAX)
    {
        return -1;
    }

    /* Need to reinitialize ADC for new channel */
    if (current_channel != channel)
    {
        adc_deinit();
        adc_status_t status = adc_init(channel);
        if (status != ADC_OK)
        {
            LOG_ERROR("Failed to switch to channel %u", channel);
            current_state = ACQ_STATE_ERROR;
            return -1;
        }
        current_channel = channel;
        LOG_INFO("ADC channel set to %u", channel);
    }

    return 0;
}

adc_channel_t acquisition_get_channel(void)
{
    return current_channel;
}

void acquisition_get_stats(acquisition_stats_t *out_stats)
{
    if (out_stats != NULL)
    {
        *out_stats = stats;
    }
}

int acquisition_set_batch_size(uint16_t size)
{
    if (size == 0 || size > ACQUISITION_MAX_BATCH_SIZE)
    {
        return -1;
    }

    batch_size = size;
    sample_index = 0; /* Reset buffer on batch size change */
    LOG_DEBUG("Batch size set to %u samples", batch_size);
    return 0;
}

uint16_t acquisition_get_batch_size(void)
{
    return batch_size;
}
