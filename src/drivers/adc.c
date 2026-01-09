/**
 * @file adc.c
 * @brief ADC Driver implementation for LPC1768
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "adc.h"

#include "LPC17xx.h"
#include "PIN_LPC17xx.h"

#include <stddef.h>

#define ADC_CLOCK_DIV 4U /**< ADC clock = PCLK / (DIV+1), 25MHz/5 = 5MHz */

/* LPC17xx Power Control (PCONP) register bits */
#define PCONP_ADC_BIT   12U
#define PCONP_IOCON_BIT 15U

/* ADC Control Register (ADCR) bits */
#define ADCR_PDN_BIT      21U
#define ADCR_START_MASK   (7U << 24) /**< START field mask (bits 24-26) */
#define ADCR_START_NOW    (1U << 24) /**< Start conversion immediately */
#define ADCR_CLKDIV_SHIFT 8U

/* ADC Interrupt Enable Register (ADINTEN) bits */
#define ADINTEN_GLOBAL_BIT 8U

/* ADC Global Data Register (ADGDR) bits */
#define ADGDR_RESULT_SHIFT 4U
#define ADGDR_RESULT_MASK  0xFFFU
#define ADGDR_DONE_BIT     31U

/** ADC pin configuration per channel */
static const struct
{
    uint8_t port;
    uint8_t pin;
    uint8_t func;
} adc_pins[ADC_CHANNEL_MAX] = {
    {0, 23, PIN_FUNC_1}, /* AD0.0 */
    {0, 24, PIN_FUNC_1}, /* AD0.1 */
    {0, 25, PIN_FUNC_1}, /* AD0.2 */
    {0, 26, PIN_FUNC_1}, /* AD0.3 */
    {1, 30, PIN_FUNC_3}, /* AD0.4 */
    {1, 31, PIN_FUNC_3}, /* AD0.5 */
    {0, 3, PIN_FUNC_2},  /* AD0.6 */
    {0, 2, PIN_FUNC_2},  /* AD0.7 */
};

static volatile uint16_t adc_last_value;
static volatile uint8_t  adc_done;
static volatile uint8_t  adc_initialized;
static adc_channel_t     adc_current_channel;

/**
 * @brief ADC Interrupt Handler
 */
void ADC_IRQHandler(void)
{
    volatile uint32_t adstat;

    /* Read ADSTAT to clear interrupt */
    adstat = LPC_ADC->ADSTAT;
    (void)adstat;

    adc_last_value = (LPC_ADC->ADGDR >> ADGDR_RESULT_SHIFT) & ADGDR_RESULT_MASK;
    adc_done       = 1U;
}

adc_status_t adc_init(adc_channel_t channel)
{
    if (channel >= ADC_CHANNEL_MAX)
    {
        return ADC_ERROR_PARAM;
    }

    if (adc_initialized)
    {
        return ADC_OK;
    }

    /* Enable power to ADC and IOCON */
    LPC_SC->PCONP |= (1U << PCONP_ADC_BIT) | (1U << PCONP_IOCON_BIT);

    PIN_Configure(
        adc_pins[channel].port, adc_pins[channel].pin, adc_pins[channel].func,
        PIN_PINMODE_TRISTATE, PIN_PINMODE_NORMAL
    );

    /* Configure ADC:
     * - Select channel
     * - Set clock divider: reg_value=4 → actual_divider=5 → 25MHz/5=5MHz (max 13MHz)
     * - Enable ADC (PDN bit)
     */
    LPC_ADC->ADCR = (1U << channel) |                      /* Select channel */
                    (ADC_CLOCK_DIV << ADCR_CLKDIV_SHIFT) | /* Clock divider */
                    (1U << ADCR_PDN_BIT);                  /* Enable ADC (PDN) */

    /* Enable global ADC interrupt (ADGINTEN) */
    LPC_ADC->ADINTEN = (1U << ADINTEN_GLOBAL_BIT);

    NVIC_EnableIRQ(ADC_IRQn);

    adc_current_channel = channel;
    adc_initialized     = 1;

    return ADC_OK;
}

adc_status_t adc_deinit(void)
{
    if (!adc_initialized)
    {
        return ADC_OK;
    }

    NVIC_DisableIRQ(ADC_IRQn);

    /* Disable global ADC interrupt */
    LPC_ADC->ADINTEN &= ~(1U << ADINTEN_GLOBAL_BIT);

    /* Power down ADC */
    LPC_ADC->ADCR &= ~(1U << ADCR_PDN_BIT);

    /* Deconfigure pin */
    PIN_Configure(
        adc_pins[adc_current_channel].port, adc_pins[adc_current_channel].pin, 0, 0, 0
    );

    /* Disable ADC power */
    LPC_SC->PCONP &= ~(1U << PCONP_ADC_BIT);

    adc_initialized = 0;

    return ADC_OK;
}

adc_status_t adc_start_conversion(void)
{
    if (!adc_initialized)
    {
        return ADC_ERROR_INIT;
    }

    adc_done = 0U;

    /* Stop any ongoing conversion and start new one */
    LPC_ADC->ADCR &= ~ADCR_START_MASK; /* Clear START bits */
    LPC_ADC->ADCR |= ADCR_START_NOW;   /* Start conversion now */

    return ADC_OK;
}

int adc_conversion_done(void)
{
    return adc_done ? 1 : 0;
}

adc_status_t adc_get_value(uint16_t *value)
{
    if (!adc_initialized)
    {
        return ADC_ERROR_INIT;
    }

    if (value == NULL)
    {
        return ADC_ERROR_PARAM;
    }

    if (!adc_done)
    {
        return ADC_ERROR_BUSY;
    }

    *value = adc_last_value;
    return ADC_OK;
}

adc_status_t adc_read_sync(uint16_t *value)
{
    adc_status_t status;

    if (value == NULL)
    {
        return ADC_ERROR_PARAM;
    }

    status = adc_start_conversion();
    if (status != ADC_OK)
    {
        return status;
    }

    while (!adc_done)
        ;

    *value = adc_last_value;
    return ADC_OK;
}
