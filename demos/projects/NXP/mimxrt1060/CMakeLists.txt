# Copyright (c) Microsoft Corporation. All rights reserved.
# SPDX-License-Identifier: MIT

nxp_mcux_sdk_fetch()
lwip_fetch()

list(APPEND CMAKE_MODULE_PATH ${NXP_MCUX_SDK_PATH})

# set parent scope path
set(BOARD_DEMO_CONFIG_PATH ${CMAKE_CURRENT_SOURCE_DIR}/config CACHE INTERNAL "Config path")
set(BOARD_DEMO_FREERTOS_PORT_PATH ${FreeRTOS_ARM_CM4F_PATH} CACHE INTERNAL "FreeRTOS Port used ")

find_package(LWIP)

include(${CMAKE_CURRENT_SOURCE_DIR}/gcc_flags.cmake)

include_directories(${BOARD_DEMO_CONFIG_PATH})
include_directories(nxp_code)
include_directories(nxp_code/lwip)


# configure modules
set(CONFIG_USE_driver_lpuart true)
set(MCUX_DEVICE "MIMXRT1062")

# Define
add_definitions(-DFSL_FEATURE_PHYKSZ8081_USE_RMII50M_MODE=1 -DSDK_DEBUGCONSOLE=1 -DSDK_DEBUGCONSOLE_UART -DLWIP_DHCP=1 -DLWIP_DNS=1 -DUSE_RTOS=1 -DFSL_RTOS_FREE_RTOS -DLWIP_TIMEVAL_PRIVATE=0 -D__STARTUP_CLEAR_BSS -D__STARTUP_INITIALIZE_NONCACHEDATA)

add_subdirectory(sample_cmake)
add_subdirectory(sample_pnp_cmake)
