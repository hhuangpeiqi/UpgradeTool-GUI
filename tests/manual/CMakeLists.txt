# Device operations remain opt-in and are never registered with CTest.
add_executable(allwinner_device EXCLUDE_FROM_ALL allwinner_device.cpp)
target_link_libraries(allwinner_device PRIVATE upgrade_allwinner upgrade_application Qt6::Core usb1)
