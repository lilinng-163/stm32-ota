cmake_minimum_required(VERSION 3.22)

add_library(OTA OBJECT)

set(src Src/crc32.c Src/ota_client.c)

target_sources(OTA PRIVATE ${src})
target_include_directories(OTA PUBLIC Inc)
target_link_libraries(OTA PRIVATE stm32cubemx)