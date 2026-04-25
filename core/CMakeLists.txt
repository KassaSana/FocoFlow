cmake_minimum_required(VERSION 3.16)

project(NeuralFocusCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(NEUROFOCUS_BUILD_TESTS "Build test executables" ON)
option(NEUROFOCUS_ENABLE_ZMQ "Build ZeroMQ publisher" ON)

set(CORE_SOURCES
    event_processor.cpp
    windows_hooks.cpp
    mmap_logger.cpp
)

add_library(neurofocus_core STATIC ${CORE_SOURCES})
target_include_directories(neurofocus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(neurofocus_core PRIVATE user32 gdi32 psapi)
endif()

if(NEUROFOCUS_ENABLE_ZMQ)
    find_path(ZMQ_INCLUDE_DIR zmq.h)
    find_library(ZMQ_LIBRARY NAMES zmq libzmq)

    if(ZMQ_INCLUDE_DIR AND ZMQ_LIBRARY)
        add_library(neurofocus_zmq STATIC zmq_publisher.cpp)
        target_include_directories(neurofocus_zmq
            PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
            PRIVATE ${ZMQ_INCLUDE_DIR}
        )
        target_link_libraries(neurofocus_zmq PRIVATE ${ZMQ_LIBRARY})
        target_compile_definitions(neurofocus_zmq PUBLIC NEUROFOCUS_HAVE_ZMQ=1)
    else()
        message(WARNING "ZeroMQ not found; disabling NEUROFOCUS_ENABLE_ZMQ.")
        set(NEUROFOCUS_ENABLE_ZMQ OFF)
    endif()
endif()

if(NEUROFOCUS_BUILD_TESTS)
    add_executable(hooks_test hooks_test.cpp)
    target_link_libraries(hooks_test PRIVATE neurofocus_core)

    add_executable(processor_test processor_test.cpp)
    target_link_libraries(processor_test PRIVATE neurofocus_core)

    add_executable(mmap_logger_test mmap_logger_test.cpp)
    target_link_libraries(mmap_logger_test PRIVATE neurofocus_core)

    add_executable(context_demo experimental/context_demo.cpp)
    target_include_directories(context_demo PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/experimental
    )
    if(WIN32)
        target_link_libraries(context_demo PRIVATE user32 gdi32)
    endif()

    if(NEUROFOCUS_ENABLE_ZMQ)
        add_executable(zmq_publisher_test zmq_publisher_test.cpp)
        target_link_libraries(zmq_publisher_test PRIVATE neurofocus_zmq)
    endif()
endif()

add_executable(neurofocus_engine neurofocus_engine.cpp)
target_link_libraries(neurofocus_engine PRIVATE neurofocus_core)
if(NEUROFOCUS_ENABLE_ZMQ)
    target_link_libraries(neurofocus_engine PRIVATE neurofocus_zmq)
endif()
