# Copyright (c) Microsoft Corporation. All rights reserved.
# SPDX-License-Identifier: MIT

set(MCUX_SDK_PROJECT_NAME ${PROJECT_NAME})

file(GLOB NXPCODE_SOURCES ../nxp_code/*.c ../nxp_code/lwip/*.c)
set(PROJECT_SOURCES ${NXPCODE_SOURCES} ../main.c)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# Include sdk
include(all_devices)

include(driver_mdio-enet)

include(driver_phy-device-ksz8081)

# include modules
include(driver_igpio)

include(driver_clock)

include(driver_common)

include(device_CMSIS)

include(utility_debug_console)

include(component_lpuart_adapter)

include(component_serial_manager)

include(component_lists)

include(component_serial_manager_uart)

include(driver_lpuart)

include(device_startup)

include(driver_iomuxc)

include(utility_assert)

include(driver_xip_device)

include(driver_xip_board)

include(CMSIS_Include_core_cm7)

include(driver_mdio-common)

include(CMSIS_Include_common)

include(CMSIS_Include_dsp)

include(utilities_misc_utilities)

include(device_system)

include(driver_enet)

include(driver_trng)

include(driver_phy-common)

include(driver_dcp)

target_link_libraries(${PROJECT_NAME} PRIVATE
    FreeRTOS::Timers
    FreeRTOS::Heap::5
    FreeRTOS::ARM_CM4F
    FreeRTOS::EventGroups
    FreeRTOSPlus::Utilities::backoff_algorithm
    FreeRTOSPlus::Utilities::logging
    FreeRTOSPlus::ThirdParty::mbedtls
    az::iot_middleware::freertos
    LWIP
    SAMPLE::SOCKET::LWIP
    SAMPLE::AZUREIOT
    SAMPLE::TRANSPORT::MBEDTLS)

# add_map_file(${PROJECT_NAME} ${PROJECT_NAME}.map)

add_custom_command(TARGET ${PROJECT_NAME}
    # Run after all other rules within the target have been executed
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMENT "Generate Bin file"
    VERBATIM)
