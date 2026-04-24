cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_webrtc_chat)

set(spiffs_img_dir "${PROJECT_DIR}/spiffs")
spiffs_create_partition_image(spiffs ${spiffs_img_dir} FLASH_IN_PROJECT)