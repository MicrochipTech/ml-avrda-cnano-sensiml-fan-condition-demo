/*******************************************************************************
  Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This file contains the "main" function for a project.

  Description:
    This file contains the "main" function for a project.  The
    "main" function calls the "SYS_Initialize" function to initialize the state
    machines of all modules in the system
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stddef.h>                     // Defines NULL
#include <stdbool.h>                    // Defines true
#include <stdint.h>
#include <stdlib.h>                     // Defines EXIT_FAILURE
#include <stdio.h>
#include "ringbuffer.h"
#include "sensor.h"
#include "app_config.h"
#include "kb.h"
#include "sml_output.h"
#include "sml_recognition_run.h"
// *****************************************************************************
// *****************************************************************************
// Section: Platform specific includes
// *****************************************************************************
// *****************************************************************************
#include "mcc_generated_files/mcc.h"

// *****************************************************************************
// *****************************************************************************
// Section: Global variables
// *****************************************************************************
// *****************************************************************************

/* Must be large enough to hold the connect/disconnect strings from SensiML DCL */
#define UART_RXBUF_LEN  128
static uint8_t _uartRxBuffer_data[UART_RXBUF_LEN];
static ringbuffer_t uartRxBuffer;

static volatile uint32_t tickcounter = 0;
static volatile uint16_t tickrate = 0;

static struct sensor_device_t sensor;
static snsr_data_t _snsr_buffer_data[SNSR_BUF_LEN][SNSR_NUM_AXES];
static ringbuffer_t snsr_buffer;
static volatile bool snsr_buffer_overrun = false;

// *****************************************************************************
// *****************************************************************************
// Section: Platform specific stub definitions
// *****************************************************************************
// *****************************************************************************
ISR(USART1_RXC_vect) {
    ringbuffer_size_t rdcnt;
    uint8_t *ptr = ringbuffer_get_write_buffer(&uartRxBuffer, &rdcnt);
    if (UART_IsRxReady() && rdcnt) {
        *ptr = UART_RX_DATA;
        ringbuffer_advance_write_index(&uartRxBuffer, 1);
    }
}

size_t __attribute__(( unused )) UART_Write(uint8_t *ptr, const size_t nbytes) {
    size_t bytecnt;
    for (bytecnt=0; bytecnt < nbytes; bytecnt++) {
        USART1_Write(*(ptr + bytecnt));
    }
    return bytecnt;
}

// *****************************************************************************
// *****************************************************************************
// Section: Generic stub definitions
// *****************************************************************************
// *****************************************************************************
static void Null_Handler() {
    // Do nothing
}

size_t __attribute__(( unused )) UART_Read(uint8_t *ptr, const size_t nbytes) {
    return ringbuffer_read(&uartRxBuffer, ptr, nbytes);
}

static void Ticker_Callback() {
    static uint32_t mstick = 0;

    ++tickcounter;
    if (tickrate == 0 || mstick > tickrate) {
        mstick = 0;
    }
    else if (++mstick == tickrate) {
        LED_STATUS_Toggle();
        mstick = 0;
    }
}

uint64_t read_timer_ms(void) {
    return tickcounter;
}

uint64_t read_timer_us(void) {
    return tickcounter * 1000U + (uint32_t) TC_TimerGet_us();
}

void sleep_ms(uint32_t ms) {
    uint32_t t0 = read_timer_ms();
    while ((read_timer_ms() - t0) < ms) { };
}

void sleep_us(uint32_t us) {
    uint32_t t0 = read_timer_us();
    while ((read_timer_us() - t0) < us) { };
}

// For handling read of the sensor data
static void SNSR_ISR_HANDLER() {
    /* Check if any errors we've flagged have been acknowledged */
    if ((sensor.status != SNSR_STATUS_OK) || snsr_buffer_overrun)
        return;
    
    ringbuffer_size_t wrcnt;
    snsr_data_t *ptr = ringbuffer_get_write_buffer(&snsr_buffer, &wrcnt);
    
    if (wrcnt == 0)
        snsr_buffer_overrun = true;
    else if ((sensor.status = sensor_read(&sensor, ptr)) == SNSR_STATUS_OK)
        ringbuffer_advance_write_index(&snsr_buffer, 1);
}

// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************

int main ( void )
{
    int8_t app_failed = 0;

    /* Initialize all modules */
    SYS_Initialize ( NULL );

    /* Register and start the millisecond interrupt ticker */
    TC_TimerCallbackRegister(Ticker_Callback);
    TC_TimerStart();

    printf("\n");

    /* Application init routine */
    app_failed = 1;
    while (1)
    {
        /* Initialize the sensor data buffer */
        if (ringbuffer_init(&snsr_buffer, _snsr_buffer_data, sizeof(_snsr_buffer_data) / sizeof(_snsr_buffer_data[0]), sizeof(_snsr_buffer_data[0])))
            break;
    
        /* Initialize the UART RX buffer */
        if (ringbuffer_init(&uartRxBuffer, _uartRxBuffer_data, sizeof(_uartRxBuffer_data) / sizeof(_uartRxBuffer_data[0]), sizeof(_uartRxBuffer_data[0])))
            break;

        /* Enable the RX interrupt */
        UART_RXC_Enable();

        /* Init and configure sensor */
        if (sensor_init(&sensor) != SNSR_STATUS_OK) {
            printf("ERROR: sensor init result = %d\n", sensor.status);
            break;
        }

        if (sensor_set_config(&sensor) != SNSR_STATUS_OK) {
            printf("ERROR: sensor configuration result = %d\n", sensor.status);
            break;
        }

        printf("sensor type is %s\n", SNSR_NAME);
        printf("sensor sample rate set at %dHz\n", SNSR_SAMPLE_RATE);
#if SNSR_USE_ACCEL
        printf("accelerometer enabled with range set at +/-%dGs\n", SNSR_ACCEL_RANGE);
#else
        printf("accelerometer disabled\n");
#endif
#if SNSR_USE_GYRO
        printf("gyrometer enabled with range set at %dDPS\n", SNSR_GYRO_RANGE);
#else
        printf("gyrometer disabled\n");
#endif
 
        /* Initialize SensiML Knowledge Pack */
        kb_model_init();
        sml_output_init(NULL);
        
        /* Display the model knowledge pack UUID */
        const uint8_t *ptr = kb_get_model_uuid_ptr(0);
        printf("Running SensiML knowledge pack uuid ");
        printf("%02x", *ptr++); 
        for (int i=1; i < 15; i++) {
            if ((i%4) == 0)
                printf("-");
            printf("%02x", *ptr++); 
        }
        printf("%02x", *ptr++); 
        printf("\n");        

        /* Activate External Interrupt Controller for sensor capture */
        MIKRO_INT_CallbackRegister(SNSR_ISR_HANDLER);

        /* STATE CHANGE - Application successfully initialized */
        tickrate = 0;
        LED_ALL_Off();
        LED_STATUS_On();

        /* STATE CHANGE - Application is running inference model */
        tickrate = TICK_RATE_SLOW;

        app_failed = 0;
        break;
    }
    
    // Use a majority voting scheme for prediction post processing
#define NUM_CLASSES 7U
#define NUM_VOTES   3U    
#define MAJORITY_VOTES ((NUM_VOTES + 1) / 2U)
    int clsid = 1;
    int votehist[NUM_VOTES] = {1};
    int votecounts[NUM_CLASSES] = {0};
    while (!app_failed)
    {
        /* Maintain state machines of all system modules. */
        SYS_Tasks ( );

        if (sensor.status != SNSR_STATUS_OK) {
            printf("ERROR: Got a bad sensor status: %d\n", sensor.status);
            break;
        }
        else if (snsr_buffer_overrun == true) {
            printf("\n\n\nOverrun!\n\n\n");

            /* STATE CHANGE - buffer overflow */
            tickrate = 0;
            LED_ALL_Off();
            LED_STATUS_On(); LED_RED_On();
            sleep_ms(5000U);
            LED_ALL_Off();

            // Clear OVERFLOW
            MIKRO_INT_CallbackRegister(Null_Handler);
            ringbuffer_reset(&snsr_buffer);
            snsr_buffer_overrun = false;
            MIKRO_INT_CallbackRegister(SNSR_ISR_HANDLER);

            /* STATE CHANGE - Application is running inference model */
            tickrate = TICK_RATE_SLOW;
            continue;
        }
        else {
            ringbuffer_size_t rdcnt;
            snsr_dataframe_t const *ptr = (snsr_dataframe_t const *) ringbuffer_get_read_buffer(&snsr_buffer, &rdcnt);
            while (rdcnt--) {
                int ret = sml_recognition_run((snsr_data_t *) ptr++, SNSR_NUM_AXES);
                ringbuffer_advance_read_index(&snsr_buffer, 1);
                
                if (ret >= 0) {                    
                    /* Update the voting counts */
                    votecounts[votehist[0]]--;
                    for (int i=1; i < NUM_VOTES; i++)
                        votehist[i-1] = votehist[i];
                    votehist[NUM_VOTES-1] = ret;
                    votecounts[ret]++;
                    
                    /* If there's a new state that is consistently classified as the same class, update the class ID */
                    if (ret != clsid) {
                        /* Get the class with the most votes */
                        int maxval = -1, maxcls = -1;
                        for (int i=0; i < NUM_CLASSES; i++) {
                            if (votecounts[i] > maxval) {
                                maxval = votecounts[i];
                                maxcls = i;
                            }
                        }

                        /* Only touch the LEDs if we decided on a new class */
                        if (maxval >= MAJORITY_VOTES && maxcls != clsid) {
                            clsid = maxcls;

                            tickrate = 0;
                            LED_ALL_Off();
                            if (clsid == 2) {
                                tickrate = 100;
                            }
                            else if (clsid == 6) {
                                tickrate = 50;
                            }
                            else if (clsid == 0) {
                                tickrate = 50;
                            }
                            else if (clsid == 3) {
                                tickrate = 1000u;
                            }
                            else if (clsid == 4) {
                                tickrate = 600u;
                            }
                            else if (clsid == 5) {
                                tickrate = 300u;
                            }                            
                            else if (clsid == 1) {
                                LED_STATUS_On();
                            }
                            else {
                                tickrate = TICK_RATE_SLOW;
                            }
                        }
                    }                 
                }
            }
        }
    }

    tickrate = 0;
    LED_ALL_Off();
    LED_RED_On();

    /* Loop forever on error */
    while (1) {};

    return ( EXIT_FAILURE );
}


/*******************************************************************************
 End of File
*/
