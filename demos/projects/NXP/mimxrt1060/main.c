/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#include "board.h"

#include "pin_mux.h"
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/netifapi.h"
#include "lwip/opt.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/prot/dhcp.h"
#include "netif/ethernet.h"
#include "enet_ethernetif.h"
#include "fsl_phy.h"

#include "fsl_debug_console.h"

#include "clock_config.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "fsl_phyksz8081.h"
#include "fsl_enet_mdio.h"


#include "fsl_common.h"

#if defined( FSL_FEATURE_SOC_LTC_COUNT ) && ( FSL_FEATURE_SOC_LTC_COUNT > 0 )
    #include "fsl_ltc.h"
#endif
#if defined( FSL_FEATURE_SOC_CAAM_COUNT ) && ( FSL_FEATURE_SOC_CAAM_COUNT > 0 )
    #include "fsl_caam.h"
#endif
#if defined( FSL_FEATURE_SOC_CAU3_COUNT ) && ( FSL_FEATURE_SOC_CAU3_COUNT > 0 )
    #include "fsl_cau3.h"
#endif
#if defined( FSL_FEATURE_SOC_DCP_COUNT ) && ( FSL_FEATURE_SOC_DCP_COUNT > 0 )
    #include "fsl_dcp.h"
#endif
#if defined( FSL_FEATURE_SOC_TRNG_COUNT ) && ( FSL_FEATURE_SOC_TRNG_COUNT > 0 )
    #include "fsl_trng.h"
#elif defined( FSL_FEATURE_SOC_RNG_COUNT ) && ( FSL_FEATURE_SOC_RNG_COUNT > 0 )
    #include "fsl_rnga.h"
#elif defined( FSL_FEATURE_SOC_LPC_RNG_COUNT ) && ( FSL_FEATURE_SOC_LPC_RNG_COUNT > 0 )
    #include "fsl_rng.h"
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* MAC address configuration. */
#define mainConfigMAC_ADDR                 \
    {                                      \
        0x02, 0x12, 0x13, 0x10, 0x15, 0x25 \
    }

/* Address of PHY interface. */
#define mainPHY_ADDRESS    BOARD_ENET0_PHY_ADDRESS

/* MDIO operations. */
#define mainMDIO_OPS       enet_ops

/* PHY operations. */
#define mainPHY_OPS        phyksz8081_ops

/* ENET clock frequency. */
#define mainCLOCK_FREQ     CLOCK_GetFreq( kCLOCK_IpgClk )


/* ENET clock frequency. */
#define mainDHCP_TIMEOUT    ( 5000 )

#ifndef mainNETIF_INIT_FN
/*! @brief Network interface initialization function. */
    #define mainNETIF_INIT_FN    ethernetif0_init
#endif /* mainNETIF_INIT_FN */

/*******************************************************************************
 * Variables
 ******************************************************************************/
static mdio_handle_t xMdioHandle = { .ops = &mainMDIO_OPS };
static phy_handle_t xPhyHandle = { .phyAddr = mainPHY_ADDRESS, .mdioHandle = &xMdioHandle, .ops = &mainPHY_OPS };
static struct netif xNetif;
static ethernetif_config_t xEnetConfig =
{
    .phyHandle  = &xPhyHandle,
    .macAddress = mainConfigMAC_ADDR,
};
static uint64_t ulGlobalEntryTime = 1639093301;

/*
 * Prototypes for the demos that can be started from this project.
 */
extern void vStartDemoTask( void );

static void prvNetworkUp( void );

/*******************************************************************************
 * Code
 ******************************************************************************/

static void prvInitializeHeap( void )
{
    static uint8_t ucHeap1[ configTOTAL_HEAP_SIZE ];

    HeapRegion_t xHeapRegions[] =
    {
        { ( unsigned char * ) ucHeap1, sizeof( ucHeap1 ) },
        { NULL,                        0                 }
    };

    vPortDefineHeapRegions( xHeapRegions );
}
/*-----------------------------------------------------------*/

void BOARD_InitModuleClock( void )
{
    const clock_enet_pll_config_t xConfig = { .enableClkOutput = true, .enableClkOutput25M = false, .loopDivider = 1 };

    CLOCK_InitEnetPll( &xConfig );
}
/*-----------------------------------------------------------*/

void delay( void )
{
    volatile uint32_t i = 0;

    for( i = 0; i < 1000000; ++i )
    {
        __asm( "NOP" ); /* delay */
    }
}
/*-----------------------------------------------------------*/

void vLoggingPrintf( const char * pcFormat,
                     ... )
{
    va_list vargs;

    va_start( vargs, pcFormat );
    vprintf( pcFormat, vargs );
    va_end( vargs );
}
/*-----------------------------------------------------------*/

void CRYPTO_InitHardware( void )
{
    #if defined( FSL_FEATURE_SOC_LTC_COUNT ) && ( FSL_FEATURE_SOC_LTC_COUNT > 0 )

        /* Initialize LTC driver.
         * This enables clocking and resets the module to a known state. */
        LTC_Init( LTC0 );
    #endif
    #if defined( FSL_FEATURE_SOC_CAAM_COUNT ) && ( FSL_FEATURE_SOC_CAAM_COUNT > 0 ) && defined( CRYPTO_USE_DRIVER_CAAM )
        /* Initialize CAAM driver. */
        caam_config_t caamConfig;

        CAAM_GetDefaultConfig( &caamConfig );
        caamConfig.jobRingInterface[ 0 ] = &s_jrif0;
        caamConfig.jobRingInterface[ 1 ] = &s_jrif1;
        CAAM_Init( CAAM, &caamConfig );
    #endif
    #if defined( FSL_FEATURE_SOC_CAU3_COUNT ) && ( FSL_FEATURE_SOC_CAU3_COUNT > 0 )
        /* Initialize CAU3 */
        CAU3_Init( CAU3 );
    #endif
    #if defined( FSL_FEATURE_SOC_DCP_COUNT ) && ( FSL_FEATURE_SOC_DCP_COUNT > 0 )
        /* Initialize DCP */
        dcp_config_t dcpConfig;

        DCP_GetDefaultConfig( &dcpConfig );
        DCP_Init( DCP, &dcpConfig );
    #endif
    { /* Init RNG module.*/
        #if defined( FSL_FEATURE_SOC_TRNG_COUNT ) && ( FSL_FEATURE_SOC_TRNG_COUNT > 0 )
            #if defined( TRNG )
        #define TRNG0    TRNG
            #endif
            trng_config_t trngConfig;

            TRNG_GetDefaultConfig( &trngConfig );
            /* Set sample mode of the TRNG ring oscillator to Von Neumann, for better random data.*/
            trngConfig.sampleMode = kTRNG_SampleModeVonNeumann;
            /* Initialize TRNG */
            TRNG_Init( TRNG0, &trngConfig );
        #elif defined( FSL_FEATURE_SOC_RNG_COUNT ) && ( FSL_FEATURE_SOC_RNG_COUNT > 0 )
            RNGA_Init( RNG );
            RNGA_Seed( RNG, SIM->UIDL );
        #endif /* if defined( FSL_FEATURE_SOC_TRNG_COUNT ) && ( FSL_FEATURE_SOC_TRNG_COUNT > 0 ) */
    }
}
/*-----------------------------------------------------------*/

int main( void )
{
    gpio_pin_config_t gpio_config = { kGPIO_DigitalOutput, 0, kGPIO_NoIntmode };

    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    BOARD_InitModuleClock();

    IOMUXC_EnableMode( IOMUXC_GPR, kIOMUXC_GPR_ENET1TxClkOutputDir, true );

    /* Data cache must be temporarily disabled to be able to use sdram */
    SCB_DisableDCache();

    GPIO_PinInit( GPIO1, 9, &gpio_config );
    GPIO_PinInit( GPIO1, 10, &gpio_config );
    /* pull up the ENET_INT before RESET. */
    GPIO_WritePinOutput( GPIO1, 10, 1 );
    GPIO_WritePinOutput( GPIO1, 9, 0 );
    delay();
    GPIO_WritePinOutput( GPIO1, 9, 1 );
    prvInitializeHeap();
    CRYPTO_InitHardware();

    xMdioHandle.resource.csrClock_Hz = mainCLOCK_FREQ;

    vTaskStartScheduler();

    for( ; ; )
    {
    }
}
/*-----------------------------------------------------------*/

/* Psuedo random number generator.  Just used by demos so does not need to be
 * secure.  Do not use the standard C library rand() function as it can cause
 * unexpected behaviour, such as calls to malloc(). */
int uxRand( void )
{
    static UBaseType_t uxlNextRand; /*_RB_ Not seeded. */
    const uint32_t ulMultiplier = 0x015a4e35UL, ulIncrement = 1UL;

    /* Utility function to generate a pseudo random number. */

    uxlNextRand = ( ulMultiplier * uxlNextRand ) + ulIncrement;

    return( ( int ) ( uxlNextRand >> 16UL ) & 0x7fffUL );
}
/*-----------------------------------------------------------*/

void vApplicationDaemonTaskStartupHook( void )
{
    prvNetworkUp();

    /* Demos that use the network are created after the network is
     * up. */
    configPRINTF( ( "---------STARTING DEMO---------\r\n" ) );
    vStartDemoTask();
}
/*-----------------------------------------------------------*/

/**
 * @brief Loop forever if stack overflow is detected.
 *
 * If configCHECK_FOR_STACK_OVERFLOW is set to 1,
 * this hook provides a location for applications to
 * define a response to a stack overflow.
 *
 * Use this hook to help identify that a stack overflow
 * has occurred.
 *
 */
void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                    char * pcTaskName )
{
    portDISABLE_INTERRUPTS();

    /* Loop forever */
    for( ; ; )
    {
    }
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
 * used by the Idle task. */
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer,
                                    StackType_t ** ppxIdleTaskStackBuffer,
                                    uint32_t * pulIdleTaskStackSize )
{
/* If the buffers to be provided to the Idle task are declared inside this
 * function then they must be declared static - otherwise they will be allocated on
 * the stack and so not exists after this function exits. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle
     * task's state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetTimerTaskMemory() to provide the memory that is
 * used by the RTOS daemon/time task. */
void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer,
                                     StackType_t ** ppxTimerTaskStackBuffer,
                                     uint32_t * pulTimerTaskStackSize )
{
/* If the buffers to be provided to the Timer task are declared inside this
 * function then they must be declared static - otherwise they will be allocated on
 * the stack and so not exists after this function exits. */
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle
     * task's state will be stored. */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* Pass out the array that will be used as the Timer task's stack. */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
/*-----------------------------------------------------------*/

/**
 * @brief Warn user if pvPortMalloc fails.
 *
 * Called if a call to pvPortMalloc() fails because there is insufficient
 * free memory available in the FreeRTOS heap.  pvPortMalloc() is called
 * internally by FreeRTOS API functions that create tasks, queues, software
 * timers, and semaphores.  The size of the FreeRTOS heap is set by the
 * configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h.
 *
 */
void vApplicationMallocFailedHook()
{
    taskDISABLE_INTERRUPTS();

    for( ; ; )
    {
    }
}
/*-----------------------------------------------------------*/

int mbedtls_platform_entropy_poll( void * data,
                                   unsigned char * output,
                                   size_t len,
                                   size_t * olen )
{
    status_t result = kStatus_Success;

    #if defined( FSL_FEATURE_SOC_TRNG_COUNT ) && ( FSL_FEATURE_SOC_TRNG_COUNT > 0 )
        #ifndef TRNG0
        #define TRNG0    TRNG
        #endif
        result = TRNG_GetRandomData( TRNG0, output, len );
    #elif defined( FSL_FEATURE_SOC_RNG_COUNT ) && ( FSL_FEATURE_SOC_RNG_COUNT > 0 )
        result = RNGA_GetRandomData( RNG, ( void * ) output, len );
    #elif defined( FSL_FEATURE_SOC_CAAM_COUNT ) && ( FSL_FEATURE_SOC_CAAM_COUNT > 0 ) && defined( CRYPTO_USE_DRIVER_CAAM )
        result = CAAM_RNG_GetRandomData( CAAM_INSTANCE, &s_caamHandle, kCAAM_RngStateHandle0, output, len, kCAAM_RngDataAny,
                                         NULL );
    #elif defined( FSL_FEATURE_SOC_LPC_RNG_COUNT ) && ( FSL_FEATURE_SOC_LPC_RNG_COUNT > 0 )
        uint32_t rn;
        size_t length;
        int i;

        length = len;

        while( length > 0 )
        {
            rn = RNG_GetRandomData();

            if( length >= sizeof( uint32_t ) )
            {
                memcpy( output, &rn, sizeof( uint32_t ) );
                length -= sizeof( uint32_t );
                output += sizeof( uint32_t );
            }
            else
            {
                memcpy( output, &rn, length );
                output += length;
                len = 0U;
            }

            /* Discard next 32 random words for better entropy */
            for( i = 0; i < 32; i++ )
            {
                RNG_GetRandomData();
            }
        }
        result = kStatus_Success;
    #endif /* if defined( FSL_FEATURE_SOC_TRNG_COUNT ) && ( FSL_FEATURE_SOC_TRNG_COUNT > 0 ) */

    if( result == kStatus_Success )
    {
        *olen = len;
        return 0;
    }
    else
    {
        return result;
    }
}
/*-----------------------------------------------------------*/

static void prvNetworkUp( void )
{
    struct dhcp * pxDHCP;
    TickType_t xTimeoutTick;
    const ip_addr_t * pxIP;

    tcpip_init( NULL, NULL );

    netifapi_netif_add( &xNetif, NULL, NULL, NULL, &xEnetConfig, mainNETIF_INIT_FN, tcpip_input );
    netifapi_netif_set_default( &xNetif );
    netifapi_netif_set_up( &xNetif );

    configPRINTF( ( "Getting IP address from DHCP ...\r\n" ) );
    netifapi_dhcp_start( &xNetif );
    pxDHCP = netif_dhcp_data( &xNetif );

    xTimeoutTick = xTaskGetTickCount() + mainDHCP_TIMEOUT * configTICK_RATE_HZ;

    while( pxDHCP->state != DHCP_STATE_BOUND &&
           xTimeoutTick > xTaskGetTickCount() )
    {
        vTaskDelay( 100 );
    }

    if( pxDHCP->state != DHCP_STATE_BOUND )
    {
        configPRINTF( ( "DHCP failed \r\n" ) );
        configASSERT( false );
    }

    configPRINTF( ( "\r\n IPv4 Address : %u.%u.%u.%u\r\n", ( ( u8_t * ) &xNetif.ip_addr.addr )[ 0 ],
                    ( ( u8_t * ) &xNetif.ip_addr.addr )[ 1 ], ( ( u8_t * ) &xNetif.ip_addr.addr )[ 2 ], ( ( u8_t * ) &xNetif.ip_addr.addr )[ 3 ] ) );
    configPRINTF( ( "\r\n Gateway : %u.%u.%u.%u\r\n", ( ( u8_t * ) &xNetif.gw.addr )[ 0 ],
                    ( ( u8_t * ) &xNetif.gw.addr )[ 1 ], ( ( u8_t * ) &xNetif.gw.addr )[ 2 ], ( ( u8_t * ) &xNetif.gw.addr )[ 3 ] ) );

    if( ( pxIP = dns_getserver( 0 ) ) )
    {
        configPRINTF( ( "\r\n DNS : %u.%u.%u.%u\r\n", ( ( u8_t * ) &pxIP->addr )[ 0 ],
                        ( ( u8_t * ) &pxIP->addr )[ 1 ], ( ( u8_t * ) &pxIP->addr )[ 2 ], ( ( u8_t * ) &pxIP->addr )[ 3 ] ) );
    }

    configPRINTF( ( "\r\n" ) );
}
/*-----------------------------------------------------------*/

void * pvPortCalloc( size_t xNum,
                     size_t xSize )
{
    void * pvReturn;

    pvReturn = pvPortMalloc( xNum * xSize );

    if( pvReturn != NULL )
    {
        memset( pvReturn, 0x00, xNum * xSize );
    }

    return pvReturn;
}
/*-----------------------------------------------------------*/

uint64_t ullGetUnixTime( void )
{
    TickType_t xTickCount = 0;
    uint64_t ulTime = 0UL;

    /* Get the current tick count. */
    xTickCount = xTaskGetTickCount();

    /* Convert the ticks to milliseconds. */
    ulTime = ( uint64_t ) xTickCount / configTICK_RATE_HZ;

    /* Reduce ulGlobalEntryTimeMs from obtained time so as to always return the
     * elapsed time in the application. */
    ulTime = ( uint64_t ) ( ulTime + ulGlobalEntryTime );

    return ulTime;
}
/*-----------------------------------------------------------*/

/* Psuedo random number generator.  Just used by demos so does not need to be
 * secure.  Do not use the standard C library rand() function as it can cause
 * unexpected behaviour, such as calls to malloc(). */
int iMainRand32( void )
{
    static UBaseType_t uxlNextRand; /*_RB_ Not seeded. */
    const uint32_t ulMultiplier = 0x015a4e35UL, ulIncrement = 1UL;

    /* Utility function to generate a pseudo random number. */

    uxlNextRand = ( ulMultiplier * uxlNextRand ) + ulIncrement;

    return( ( int ) ( uxlNextRand >> 16UL ) & 0x7fffUL );
}
/*-----------------------------------------------------------*/

#ifdef __GNUC__
int _read(int file, char *ptr, int len)
#elif __ICCARM__
size_t __read(int file, unsigned char *ptr, size_t len)
#else
#error unknown compiler
#endif
{
	int DataIdx;
	for (DataIdx = 0; DataIdx < len; DataIdx++)
	{
		*ptr++ = DbgConsole_Getchar();
	}
	return len;
}
/*-----------------------------------------------------------*/

#ifdef __GNUC__
int _write(int file, char *ptr, int len)
#elif __ICCARM__
size_t __write(int file, unsigned char *ptr, size_t len)
#else
#error unknown compiler
#endif
{
	int DataIdx;
	for (DataIdx = 0; DataIdx < len; DataIdx++)
	{
		DbgConsole_Putchar(*ptr++);
	}
	return len;
}
/*-----------------------------------------------------------*/
