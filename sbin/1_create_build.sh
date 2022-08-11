#!/bin/bash

cd ../


export IDF_PATH=/home2/ak/opt/esp/esp-idf
export ADF_PATH=/home2/ak/opt/esp/esp-adf
export IDF_TARGET=esp32s3

export PATH=$PATH:/home2/ak/opt/esp/esp-idf/tools:/home2/ak/opt/esp/tools/python_env/idf4.4_py3.8_env/bin:/home2/ak/opt/esp/tools/tools/xtensa-esp32s3-elf/esp-2021r2-patch3-8.4.0/xtensa-esp32s3-elf/bin


rm -rf build
mkdir -p build


#cmake . -B build -DCMAKE_VERBOSE_MAKEFILE=1
cmake . -B build