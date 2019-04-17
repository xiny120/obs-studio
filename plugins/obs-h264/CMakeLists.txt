project(obs-h264)

find_package(Libh264 REQUIRED)
include_directories(${LIBH264_INCLUDE_DIRS})
add_definitions(${LIBH264_DEFINITIONS})

set(obs-h264_SOURCES
	obs-h264.c
	obs-h264-plugin-main.c)

add_library(obs-h264 MODULE
	${obs-h264_SOURCES})
target_link_libraries(obs-h264
	libobs
	${LIBH264_LIBRARIES})

install_obs_plugin_with_data(obs-h264 data)
