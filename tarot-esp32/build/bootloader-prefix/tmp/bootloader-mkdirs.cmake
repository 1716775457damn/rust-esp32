# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "F:/Espressif/frameworks/esp-idf-v5.3.1/components/bootloader/subproject"
  "E:/GitHub/rust-esp32/tarot-esp32/build/bootloader"
  "E:/GitHub/rust-esp32/tarot-esp32/build/bootloader-prefix"
  "E:/GitHub/rust-esp32/tarot-esp32/build/bootloader-prefix/tmp"
  "E:/GitHub/rust-esp32/tarot-esp32/build/bootloader-prefix/src/bootloader-stamp"
  "E:/GitHub/rust-esp32/tarot-esp32/build/bootloader-prefix/src"
  "E:/GitHub/rust-esp32/tarot-esp32/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/GitHub/rust-esp32/tarot-esp32/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/GitHub/rust-esp32/tarot-esp32/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
