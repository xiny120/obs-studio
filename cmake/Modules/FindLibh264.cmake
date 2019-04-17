# Once done these will be defined:
#
#  LIBH264_FOUND
#  LIBH264_INCLUDE_DIRS
#  LIBH264_LIBRARIES
#
# For use in OBS: 
#
#  H264_INCLUDE_DIR

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(_H264 QUIET h264)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_lib_suffix 64)
else()
	set(_lib_suffix 32)
endif()

find_path(H264_INCLUDE_DIR
	NAMES codec_app_def.h
	HINTS
		ENV h264Path${_lib_suffix}
		ENV h264Path
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${h264Path${_lib_suffix}}
		${h264Path}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_H264_INCLUDE_DIRS}
	PATHS
		/usr/include /usr/local/include /opt/local/include /sw/include
	PATH_SUFFIXES
		include)

		

find_library(H264_LIB
	NAMES ${_H264_LIBRARIES} welsenc 
	${_H264_LIBRARIES}welsecore
	${_H264_LIBRARIES}WelsVP
	HINTS
		ENV h264Path${_lib_suffix}
		ENV h264Path
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${h264Path${_lib_suffix}}
		${h264Path}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_H264_LIBRARY_DIRS}
	PATHS
		/usr/lib /usr/local/lib /opt/local/lib /sw/lib
	PATH_SUFFIXES
		lib${_lib_suffix} lib
		libs${_lib_suffix} libs
		bin${_lib_suffix} bin
		../lib${_lib_suffix} ../lib
		../libs${_lib_suffix} ../libs
		../bin${_lib_suffix} ../bin)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libh264 DEFAULT_MSG H264_LIB H264_INCLUDE_DIR)
mark_as_advanced(H264_INCLUDE_DIR H264_LIB)


if(LIBH264_FOUND)
	set(LIBH264_INCLUDE_DIRS ${H264_INCLUDE_DIR})
	set(LIBH264_LIBRARIES ${H264_LIB})
endif()
