# config to select component, the format is CONFIG_USE_${component}
# Please refer to cmake files below to get available components:
#  ${SdkRootDirPath}/devices/MIMX8ML8/all_lib_device.cmake

set(CONFIG_COMPILER gcc)
set(CONFIG_TOOLCHAIN armgcc)
set(CONFIG_USE_COMPONENT_CONFIGURATION false)
set(CONFIG_USE_driver_clock true)
set(CONFIG_USE_driver_common true)
set(CONFIG_USE_driver_rdc true)
set(CONFIG_USE_driver_audiomix true)
set(CONFIG_USE_driver_mu true)
set(CONFIG_USE_device_MIMX8ML8_CMSIS true)
set(CONFIG_USE_utility_debug_console true)
set(CONFIG_USE_component_iuart_adapter true)
set(CONFIG_USE_component_serial_manager_uart true)
set(CONFIG_USE_component_serial_manager true)
set(CONFIG_USE_driver_iuart true)
set(CONFIG_USE_component_lists true)
set(CONFIG_USE_device_MIMX8ML8_startup true)
set(CONFIG_USE_utility_assert true)
set(CONFIG_USE_utilities_misc_utilities true)
set(CONFIG_USE_CMSIS_Include_core_cm true)
set(CONFIG_USE_utility_str true)
set(CONFIG_USE_device_MIMX8ML8_system true)

# FreeRTOS + RPMsg-Lite (same stack as rpmsg_lite_pingpong_rtos_linux)
set(CONFIG_USE_middleware_freertos-kernel true)
set(CONFIG_USE_middleware_freertos-kernel_template true)
set(CONFIG_USE_middleware_freertos-kernel_heap_4 true)
set(CONFIG_USE_middleware_freertos-kernel_extension true)
set(CONFIG_USE_middleware_multicore_rpmsg_lite true)
set(CONFIG_USE_middleware_multicore_rpmsg_lite_freertos true)
set(CONFIG_USE_middleware_multicore_rpmsg_lite_imx8mp_m7_freertos true)

set(CONFIG_CORE cm7f)
set(CONFIG_DEVICE MIMX8ML8)
set(CONFIG_BOARD evkmimx8mp)
set(CONFIG_KIT evkmimx8mp)
set(CONFIG_DEVICE_ID MIMX8ML8xxxLZ)
set(CONFIG_FPU SP_FPU)
set(CONFIG_DSP NO_DSP)

# Step 02: set to 1 to route AES-GCM/HMAC through caam_crypto (needs M7_CAAM_HW for silicon).
# Default 0 keeps Step 01 software crypto behavior.
if(NOT DEFINED M7_USE_CAAM)
    set(M7_USE_CAAM 0)
endif()
add_compile_definitions(M7_USE_CAAM=${M7_USE_CAAM})
# Hardware bring-up: i.MX8MP JR0 driver (not RT fsl_caam CAAM_Type).
if(NOT DEFINED M7_CAAM_HW)
    set(M7_CAAM_HW 0)
endif()
add_compile_definitions(M7_CAAM_HW=${M7_CAAM_HW})

