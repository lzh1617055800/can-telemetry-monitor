cmake_minimum_required(VERSION 3.20)

project(CAN_Telemetry_Monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)#引入测试单元

option(ENABLE_SANITIZERS "Build with ASan and UBSan" OFF)
option(ENABLE_THREAD_SANITIZER "Build with TSan" OFF)
option(BUILD_QML_CLIENT "Build the optional Qt6/QML CAN monitor" OFF)

if(ENABLE_SANITIZERS AND ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "ENABLE_SANITIZERS and ENABLE_THREAD_SANITIZER cannot both be on")
endif()

function(configure_sanitizers target_name)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        return()
    endif()

    if(ENABLE_SANITIZERS)
        target_compile_options(${target_name} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target_name} PRIVATE
            -fsanitize=address,undefined
        )
    elseif(ENABLE_THREAD_SANITIZER)
        target_compile_options(${target_name} PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        target_link_options(${target_name} PRIVATE
            -fsanitize=thread
        )
    endif()
endfunction()

function(register_project_test target_name)
    if(ENABLE_THREAD_SANITIZER AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_program(SETARCH_EXECUTABLE setarch REQUIRED)

        add_test(
            NAME ${target_name}
            COMMAND ${SETARCH_EXECUTABLE}
                    ${CMAKE_SYSTEM_PROCESSOR}
                    -R
                    $<TARGET_FILE:${target_name}>
        )
    else()
        add_test(
            NAME ${target_name}
            COMMAND $<TARGET_FILE:${target_name}>
        )
    endif()
endfunction()

set(HTTP_SERVER_SOURCES
    src/main.cpp
    src/Server.cpp
    src/EventLoop.cpp
    src/Connection.cpp
    src/HttpRequest.cpp
    src/HttpResponse.cpp
    src/Timer.cpp
    src/ThreadPool.cpp
    src/Logger.cpp
    src/FrameStore.cpp
    src/CanReceiver.cpp
    src/CanSender.cpp
    src/CanBusStatistics.cpp
    src/CanRuntime.cpp
)

add_executable(http_server ${HTTP_SERVER_SOURCES})

target_include_directories(http_server PRIVATE ${PROJECT_SOURCE_DIR}/include)

set_target_properties(http_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(UNIX)
    target_link_libraries(http_server PRIVATE pthread)
endif()
configure_sanitizers(http_server)

if(BUILD_QML_CLIENT)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)

    find_package(Qt6 6.2 REQUIRED COMPONENTS Gui Qml Quick)

    qt_add_executable(can_qml_monitor
        qml_client/main.cpp
    )

    set_source_files_properties(
        qml_client/Main.qml
        PROPERTIES
            QT_RESOURCE_ALIAS Main.qml
    )

    qt_add_qml_module(can_qml_monitor
        URI CanMonitor
        VERSION 1.0
        RESOURCE_PREFIX /qt/qml
        QML_FILES
            qml_client/Main.qml
    )

    target_link_libraries(can_qml_monitor
        PRIVATE
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
    )

    set_target_properties(can_qml_monitor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(can_loopback_demo
        tests/can_loopback_demo.cpp
        src/FrameStore.cpp
        src/CanReceiver.cpp
        src/CanSender.cpp
        src/CanBusStatistics.cpp
    )

    target_include_directories(can_loopback_demo
        PRIVATE ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(can_loopback_demo PRIVATE pthread)

    configure_sanitizers(can_loopback_demo)
endif()

if(BUILD_TESTING)
    add_executable(http_request_test
        tests/http_request_test.cpp
        src/HttpRequest.cpp
    )
    target_include_directories(http_request_test
        PRIVATE ${PROJECT_SOURCE_DIR}/include
    )
    configure_sanitizers(http_request_test)
    register_project_test(http_request_test)
    add_executable(frame_store_test
        tests/frame_store_test.cpp
        src/FrameStore.cpp
    )
    target_include_directories(frame_store_test
        PRIVATE ${PROJECT_SOURCE_DIR}/include
    )
    configure_sanitizers(frame_store_test)
    register_project_test(frame_store_test)

    add_executable(can_statistics_test
        tests/can_statistics_test.cpp
        src/CanBusStatistics.cpp
    )
    target_include_directories(can_statistics_test
        PRIVATE ${PROJECT_SOURCE_DIR}/include
    )
    configure_sanitizers(can_statistics_test)
    register_project_test(can_statistics_test)
endif()
