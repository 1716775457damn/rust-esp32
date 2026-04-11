cmake_minimum_required(VERSION 3.16)

# 这个文件是 ESP-IDF 的入口构建脚本
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(tarot-esp32)

# Generate SPIFFS image from spiffs_data directory
spiffs_create_partition_image(spiffs spiffs_data FLASH_IN_PROJECT)
