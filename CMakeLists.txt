cmake_minimum_required(VERSION 3.20)
project(ModbusGateway LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

set(MODBUS_PKGCONFIG_DIR "$ENV{HOME}/.local/libmodbus-3.2.0/lib/pkgconfig"
    CACHE PATH "Directory containing libmodbus.pc")
set(ENV{PKG_CONFIG_PATH} "${MODBUS_PKGCONFIG_DIR}:$ENV{PKG_CONFIG_PATH}")
find_package(PkgConfig REQUIRED)
pkg_check_modules(MODBUS REQUIRED IMPORTED_TARGET libmodbus>=3.2.0)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

function(add_modbus_program name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE PkgConfig::MODBUS)
endfunction()

foreach(name IN ITEMS echo_server echo_client gateway_worker_demo)
    add_executable(${name} examples/${name}.c)
endforeach()
target_link_libraries(gateway_worker_demo PRIVATE Threads::Threads)

foreach(name IN ITEMS check_modbus gateway_queue_demo gateway_frontend_demo)
    add_modbus_program(${name} examples/${name}.c)
endforeach()
foreach(name IN ITEMS modbus_server modbus_client modbus_gateway
                      modbus_rtu_server modbus_rtu_client gateway_shared_backend_demo)
    add_modbus_program(${name} ${name}.c)
endforeach()
foreach(name IN ITEMS modbus_gateway gateway_queue_demo gateway_frontend_demo gateway_shared_backend_demo)
    target_link_libraries(${name} PRIVATE Threads::Threads)
endforeach()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_modbus_program(gateway_three_clients gateway_shared_backend_demo.c)
    target_link_libraries(gateway_three_clients PRIVATE Threads::Threads)
    target_compile_definitions(gateway_three_clients PRIVATE GATEWAY_DEMO_CLIENT_COUNT=3)
    add_modbus_program(gateway_four_clients gateway_shared_backend_demo.c)
    target_link_libraries(gateway_four_clients PRIVATE Threads::Threads)
    target_compile_definitions(gateway_four_clients PRIVATE GATEWAY_DEMO_CLIENT_COUNT=4)

    add_test(NAME libmodbus_version COMMAND check_modbus)
    foreach(suite IN ITEMS disconnect capacity shutdown system)
        add_test(NAME gateway_${suite}
                 COMMAND "${Python3_EXECUTABLE}" -B
                         "${CMAKE_CURRENT_SOURCE_DIR}/tests/gateway_${suite}_demo.py")
        set_tests_properties(gateway_${suite} PROPERTIES
            RESOURCE_LOCK gateway_ports
            TIMEOUT 30
            ENVIRONMENT "GATEWAY_BINARY=$<TARGET_FILE:gateway_shared_backend_demo>;GATEWAY_THREE_CLIENTS=$<TARGET_FILE:gateway_three_clients>;GATEWAY_FOUR_CLIENTS=$<TARGET_FILE:gateway_four_clients>;RTU_SERVER_BINARY=$<TARGET_FILE:modbus_rtu_server>")
    endforeach()
endif()
