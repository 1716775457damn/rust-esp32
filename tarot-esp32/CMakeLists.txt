cmake_minimum_required(VERSION 3.16)

# The following five lines of boilerplate have to be in every project

# Include esp-idf project cmake
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define project
project(tarot-esp32)

# Spiffs partition gen (Manual triggered via idf.py or scripts)
# Generate SPIFFS image from spiffs_data directory
spiffs_create_partition_image(spiffs spiffs_data FLASH_IN_PROJECT)
