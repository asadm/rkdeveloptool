cmake_minimum_required(VERSION 3.15)

set(PROJECT_NAME "rkDevelopTool_Mac")
project(${PROJECT_NAME} VERSION 1.3)

file(GLOB LOCAL_CPP_FILES *.cpp)

set(CMAKE_C_STANDARD 99)
if(EMSCRIPTEN)
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 11)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Configure a header file to pass some of the CMake settings to the source
configure_file(
    "${PROJECT_SOURCE_DIR}/config.h.in"
    "${PROJECT_BINARY_DIR}/config.h"
)

# Build libusb from the vendored submodule.
set(libusb_os_sources
    libusb/libusb/os/events_posix.c
    libusb/libusb/os/threads_posix.c
    libusb/libusb/os/darwin_usb.c
)
set(libusb_os_libs "-framework IOKit" "-framework CoreFoundation" "-framework Security")

if(EMSCRIPTEN)
    list(REMOVE_ITEM libusb_os_sources libusb/libusb/os/darwin_usb.c)
    list(APPEND libusb_os_sources
        libusb/libusb/os/emscripten_webusb.cpp
        libusb/libusb/os/emscripten_time.c
    )
    set(libusb_os_libs)
endif()

add_library(libusb_static STATIC
    libusb/libusb/core.c
    libusb/libusb/descriptor.c
    libusb/libusb/hotplug.c
    libusb/libusb/io.c
    libusb/libusb/strerror.c
    libusb/libusb/sync.c
    ${libusb_os_sources}
)

if(EMSCRIPTEN)
    target_compile_options(libusb_static PRIVATE -pthread)
endif()

if(EMSCRIPTEN)
    set(libusb_config_dir ${CMAKE_CURRENT_SOURCE_DIR}/libusb/emscripten)
else()
    set(libusb_config_dir ${CMAKE_CURRENT_SOURCE_DIR}/libusb/Xcode)
endif()

target_include_directories(libusb_static
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libusb/libusb
    PRIVATE
        ${libusb_config_dir}
        ${CMAKE_CURRENT_SOURCE_DIR}/libusb/libusb/os
)

target_link_libraries(libusb_static
    PUBLIC
        ${libusb_os_libs}
)

find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} ${LOCAL_CPP_FILES})

# Emscripten link options for the final binary.
if(EMSCRIPTEN)
    set(EM_COMPILE_PTHREAD_FLAGS "-pthread")
    set(EM_LINK_FLAGS
        "-pthread"
        "-sUSE_PTHREADS=1"
        "-sASYNCIFY"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createModule"
        "-sEXPORT_ES6=1"
        "-sENVIRONMENT=web"
        "-sFORCE_FILESYSTEM=1"
        "-sEXPORTED_RUNTIME_METHODS=['FS','PATH']"
        "--bind"
    )
    target_link_options(${PROJECT_NAME} PRIVATE ${EM_LINK_FLAGS})
    target_compile_options(${PROJECT_NAME} PRIVATE ${EM_COMPILE_PTHREAD_FLAGS})
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_BINARY_DIR}
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/libusb/libusb
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        libusb_static
        Threads::Threads
)
