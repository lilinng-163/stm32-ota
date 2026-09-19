cmake_minimum_required(VERSION 3.22)

add_library(OTA OBJECT)

set(src Src/jump2app.c Src/check.c Src/crc32.c)

target_sources(OTA PRIVATE ${src})
target_include_directories(OTA PUBLIC Inc)
target_link_libraries(OTA PRIVATE stm32cubemx)