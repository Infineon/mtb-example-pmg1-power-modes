/******************************************************************************
* File Name: main.c
*
* Description: This example demonstrates how to transition PMG1 MCU among the
*              following low power modes: Sleep and Deep Sleep.
*
* Related Document: See README.md
*
*******************************************************************************
* Copyright 2022-2023, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
 * Include header files
 ******************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include "cycfg_pins.h"
#include "stdio.h"
#include <inttypes.h>

/******************************************************************************
 * Macros
 *****************************************************************************/
#define LED_ON                  (0U)
#define LED_OFF                 (1U)
#define SWITCH_INTR_PRIORITY    (3U)

#define SLEEP_SWITCH_PRESS      (1U)
#define DEEP_SLEEP_SWITCH_PRESS (3U)
#define BLINK_TIME_MS           (200U)

/* Debug print macro to enable UART print */
#define DEBUG_PRINT             (0U)

/* CY ASSERT failure */
#define CY_ASSERT_FAILED        (0U)

/*******************************************************************************
 * Global variables
 ******************************************************************************/
volatile int16_t SwitchPressCount = 0;

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/
void switch_isr();
void led_blink(uint32_t blink_time, uint32_t num_toggles);

/* Sleep Callback function */
cy_en_syspm_status_t sleep_callback(cy_stc_syspm_callback_params_t  *callbackParams,
                                    cy_en_syspm_callback_mode_t mode);
/* Deep Sleep Callback function */
cy_en_syspm_status_t deep_sleep_callback(cy_stc_syspm_callback_params_t *callbackParams,
                                         cy_en_syspm_callback_mode_t mode);

/* SysPm callback params */
cy_stc_syspm_callback_params_t callbackParams = {
        /*.base       =*/ NULL,
        /*.context    =*/ NULL
};

/* Callback declaration for Sleep mode */
cy_stc_syspm_callback_t sleep_cb    =  {sleep_callback,              /* Callback function */
                                       CY_SYSPM_SLEEP,               /* Callback type */
                                       0,                            /* Skip mode */
                                       &callbackParams,              /* Callback params */
                                       NULL, NULL};                  /* For internal usage */

/* Callback declaration for Deep Sleep mode */
cy_stc_syspm_callback_t deep_sleep_cb = {deep_sleep_callback,        /* Callback function */
                                       CY_SYSPM_DEEPSLEEP,           /* Callback type */
                                       0,                            /* Skip mode */
                                       &callbackParams,              /* Callback params */
                                       NULL, NULL};                  /* For internal usage */

/* Initialize the switch interrupt */
cy_stc_sysint_t switch_intr_config =
{
    CYBSP_USER_BTN_IRQ,     /* Source of interrupt signal */
    SWITCH_INTR_PRIORITY    /* Interrupt priority */
};

#if DEBUG_PRINT
cy_stc_scb_uart_context_t CYBSP_UART_context;

/* Variable used for tracking the print status */
volatile bool ENTER_LOOP = true;

/*******************************************************************************
* Function Name: check_status
********************************************************************************
* Summary:
*  Prints the error message.
*
* Parameters:
*  error_msg - message to print if any error encountered.
*  status - status obtained after evaluation.
*
* Return:
*  void
*
*******************************************************************************/
void check_status(char *message, cy_rslt_t status)
{
    char error_msg[50];

    sprintf(error_msg, "Error Code: 0x%08" PRIX32 "\n", status);

    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n=====================================================\r\n");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\nFAIL: ");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, message);
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, error_msg);
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n=====================================================\r\n");
}
#endif

/*******************************************************************************
 * Function Name: main
 *******************************************************************************
 *
 * Summary:
 *  System entrance point. This function configures and initializes the GPIO
 *  interrupt, UART Component and Register callback functions.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  int
 *
 ******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    cy_en_sysint_status_t intr_result;
    bool cb_result = true;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enable global interrupts */
    __enable_irq();

#if DEBUG_PRINT
    /* Configure and enable the UART peripheral */
    Cy_SCB_UART_Init(CYBSP_UART_HW, &CYBSP_UART_config, &CYBSP_UART_context);
    Cy_SCB_UART_Enable(CYBSP_UART_HW);

    /* Sequence to clear screen */
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\x1b[2J\x1b[;H");

    /* Print "Power modes" */
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "****************** ");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "PMG1 MCU: Power modes");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "****************** \r\n\n");
#endif

    /* Initialize and enable GPIO interrupt */
    intr_result = Cy_SysInt_Init(&switch_intr_config, switch_isr);
    if(intr_result != CY_SYSINT_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API Cy_SysInt_Init failed with error code", intr_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enables interrupt in the NVIC interrupt controller */
    NVIC_EnableIRQ(switch_intr_config.intrSrc);

    /* Register Sleep callback */
    cb_result = Cy_SysPm_RegisterCallback(&sleep_cb);
    if (cb_result != true)
    {
#if DEBUG_PRINT
        check_status("API Cy_SysPm_RegisterCallback failed with error code", CY_RSLT_TYPE_ERROR);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Register Deep Sleep callback */
    cb_result = Cy_SysPm_RegisterCallback(&deep_sleep_cb);
    if (cb_result != true)
    {
#if DEBUG_PRINT
        check_status("API Cy_SysPm_RegisterCallback failed with error code", CY_RSLT_TYPE_ERROR);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    for (;;)
    {
        /* Turn on User LED */
        Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_NUM, LED_ON);

        /* Sleep mode */
        if(SwitchPressCount == SLEEP_SWITCH_PRESS)
        {
#if DEBUG_PRINT
            /* Send a string over serial terminal */
            Cy_SCB_UART_PutString(CYBSP_UART_HW, "Enter Sleep mode\r\n");
#endif
            /* Go to Sleep */
            Cy_SysPm_CpuEnterSleep();
       }
       /* Deep sleep mode */
       else if (SwitchPressCount == DEEP_SLEEP_SWITCH_PRESS)
       {
#if DEBUG_PRINT
           /* Send a string over serial terminal */
           Cy_SCB_UART_PutString(CYBSP_UART_HW, "Enter Deep Sleep mode\r\n");
#endif
           /* Go to Deep Sleep */
           Cy_SysPm_CpuEnterDeepSleep();

           /* Making switch press count to 0U */
           SwitchPressCount = 0;
        }

#if DEBUG_PRINT
        if (ENTER_LOOP)
        {
            Cy_SCB_UART_PutString(CYBSP_UART_HW, "Entered for loop\r\n");
            ENTER_LOOP = false;
        }
#endif
    }
}

/*******************************************************************************
 * Function Name: switch_isr
 *******************************************************************************
 *
 * Summary:
 *  This function is executed when GPIO interrupt is triggered.
 *  It Clears the triggered pin interrupt
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void switch_isr(void)
{
    /* Counts the switch press */
    SwitchPressCount++;

    /* Clears the triggered pin interrupt */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_NUM);
}

/*******************************************************************************
 * Function Name: callback_function
 *******************************************************************************
 *
 * Callback function implementation. It turns the LED off before going to
 * sleep power mode / deep sleep power mode.
 * After waking up, it sets the LED to blink for two times for sleep mode and
 * it sets the LED to blink for three times for deep sleep mode.
 *
 * Parameters:
 *  mode: Sleep mode (1) / Deep sleep mode (2)
 *  led_blink_count: Number of times User LED blinks
 *
 * Return:
 *  void
 *
 ******************************************************************************/
cy_en_syspm_status_t callback_function(uint8_t mode, uint8_t led_blink_count)
{
    cy_en_syspm_status_t ret_val = CY_SYSPM_FAIL;

    switch (mode)
    {
        case CY_SYSPM_CHECK_READY:
            ret_val = CY_SYSPM_SUCCESS;
            break;

        case CY_SYSPM_CHECK_FAIL:
#if DEBUG_PRINT
            /* Send a string over serial terminal */
            Cy_SCB_UART_PutString(CYBSP_UART_HW, "Device failed to enter Deep Sleep mode\r\n");
#endif
            ret_val = CY_SYSPM_FAIL;
            break;

        case CY_SYSPM_BEFORE_TRANSITION:
            /* Blink the LED for two times before entering into Sleep mode */
            led_blink(BLINK_TIME_MS, led_blink_count);

            ret_val = CY_SYSPM_SUCCESS;
            break;

        case CY_SYSPM_AFTER_TRANSITION:
#if DEBUG_PRINT
            /* Send a string over serial terminal */
            Cy_SCB_UART_PutString(CYBSP_UART_HW, "Enters Active mode\r\n");
#endif
            ret_val = CY_SYSPM_SUCCESS;
            break;

        default:
            /* Don't do anything in the other modes */
            ret_val = CY_SYSPM_SUCCESS;
            break;
    }
    return ret_val;
}

/*******************************************************************************
 * Function Name: sleep_callback
 *******************************************************************************
 *
 * Sleep callback implementation. It turns the LED off before going to
 * sleep power mode. After waking up, it sets the LED to blink for two times.
 *
 * Parameters:
 *  callbackParams: The pointer to the callback parameters structure
 *  cy_stc_syspm_callback_params_t.
 *  mode: Callback mode, see cy_en_syspm_callback_mode_t
 *
 * Return:
 *  Entered status, see cy_en_syspm_status_t.
 *
 ******************************************************************************/
cy_en_syspm_status_t sleep_callback(
        cy_stc_syspm_callback_params_t *callbackParams, cy_en_syspm_callback_mode_t mode)
{
    cy_en_syspm_status_t retVal;

    retVal = callback_function(mode, 2);

    return retVal;
}

/*******************************************************************************
 * Function Name: deep_sleep_callback
 *******************************************************************************
 *
 * Summary:
 *  Deep Sleep callback implementation. It turns the LED off before going to deep
 *  sleep power mode. After waking up, it sets the LED to blink for three times.
 *
 * Parameters:
 *  callbackParams: The pointer to the callback parameters structure cy_stc_syspm_callback_params_t.
 *  mode: Callback mode, see cy_en_syspm_callback_mode_t
 *
 * Return:
 *  Entered status, see cy_en_syspm_status_t.
 *
 ******************************************************************************/
cy_en_syspm_status_t deep_sleep_callback(
        cy_stc_syspm_callback_params_t *callbackParams, cy_en_syspm_callback_mode_t mode)
{
    cy_en_syspm_status_t retVal;

    retVal = callback_function(mode, 3);

    return retVal;
}

/*******************************************************************************
 * Function Name: led_blink
 *******************************************************************************
 * Summary:
 * Blinks the LED for num_toggles times, with a period of blink_time.
 *
 * Parameters:
 * blink_time:  Time in ms for on and off times.
 * num_toggles: Describes how many times to toggle the LED on and off.
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void led_blink(uint32_t blink_time, uint32_t num_toggles)
{
    /* Variable used to set LED blink time */
    uint8_t count = 0U;

    /* Toggle the LED the desired number of times in this loop */
    for (count = 0; count < num_toggles; count++)
    {
        Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_NUM, LED_OFF);
        Cy_SysLib_Delay(blink_time);
        Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_NUM, LED_ON);
        Cy_SysLib_Delay(blink_time);
    }

    /* Turn off the User LED */
    Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_NUM, LED_OFF);
}

/* [] END OF FILE */

