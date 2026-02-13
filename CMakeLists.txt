cmake_minimum_required(VERSION 3.16.0)
list(APPEND EXTRA_COMPONENT_DIRS 
    "components"
    "components/esp-who/components"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(face_app)
