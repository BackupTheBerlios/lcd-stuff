# (c) 2010, Bernhard Walle <bernhard@bwalle.de>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#

project(lcd-stuff C)
cmake_minimum_required(VERSION 2.6)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

include(FindPkgConfig)

option(BUILD_MAIL       "Build the mail screen (requires libetpan)"     ON)
option(BUILD_RSS        "Build the RSS screen (requires libmrss)"       ON)
option(BUILD_MPD        "Build the MPD screen (requires libmpd)"        ON)
option(BUILD_WEATHER    "Build the weather screen (requires libnxml)"   ON)

#
# Change the include path of the compiler so that it finds config.h
# and we can write <shared/....h>.
#

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

#
# glib
#

pkg_search_module(GLIB REQUIRED glib-2.0)
if (NOT GLIB_FOUND)
    message(FATAL_ERROR "glib-2.0 library not found.")
endif (NOT GLIB_FOUND)

include_directories(${GLIB_INCLUDE_DIRS})
set(EXTRA_LIBS ${EXTRA_LIBS} ${GLIB_LIBRARIES})


#
# gthread
#

pkg_search_module(GTHREAD REQUIRED gthread-2.0)
if (NOT GTHREAD_FOUND)
    message(FATAL_ERROR "gthread-2.0 library not found.")
endif (NOT GTHREAD_FOUND)

include_directories(${GTHREAD_INCLUDE_DIRS})
set(EXTRA_LIBS ${EXTRA_LIBS} ${GTHREAD_LIBRARIES})


if (BUILD_WEATHER OR BUILD_RSS)
    #
    # libnxml
    #

    pkg_search_module(NXML REQUIRED nxml)
    if (NOT NXML_FOUND)
        message(FATAL_ERROR "nxml library not found.")
    endif (NOT NXML_FOUND)

    include_directories(${NXML_INCLUDE_DIRS})
    set(EXTRA_LIBS ${EXTRA_LIBS} ${NXML_LIBRARIES})
    set(HAVE_LCDSTUFF_WEATHER 1)
else (BUILD_WEATHER OR BUILD_RSS)
    set(HAVE_LCDSTUFF_WEATHER 0)
endif (BUILD_WEATHER OR BUILD_RSS)


if (BUILD_RSS)
    #
    # libmrss
    #

    pkg_search_module(MRSS REQUIRED mrss)
    if (NOT MRSS_FOUND)
        message(FATAL_ERROR "mrss library not found.")
    endif (NOT MRSS_FOUND)

    include_directories(${MRSS_INCLUDE_DIRS})
    set(EXTRA_LIBS ${EXTRA_LIBS} ${MRSS_LIBRARIES})
    set(HAVE_LCDSTUFF_RSS 1)
else (BUILD_RSS)
    set(HAVE_LCDSTUFF_RSS 0)
endif (BUILD_RSS)


if (BUILD_MPD)
    #
    # libmpd
    #

    pkg_search_module(MPD REQUIRED libmpd)
    if (NOT MPD_FOUND)
        message(FATAL_ERROR "libmpd library not found.")
    endif (NOT MPD_FOUND)

    include_directories(${MPD_INCLUDE_DIRS})
    set(EXTRA_LIBS ${EXTRA_LIBS} ${MPD_LIBRARIES})
    set(HAVE_LCDSTUFF_MPD 1)
else (BUILD_MPD)
    set(HAVE_LCDSTUFF_MPD 0)
endif (BUILD_MPD)


if (BUILD_MAIL)
    #
    # libetpan
    #

    include (Findlibetpan)
    if (NOT LIBETPAN_FOUND)
        message(FATAL_ERROR "libetpan not found.")
    endif (NOT LIBETPAN_FOUND)

    include_directories(${LIBETPAN_INCLUDE_DIRS})
    set(EXTRA_LIBS ${EXTRA_LIBS} ${LIBETPAN_LIBRARIES})
    set(HAVE_LCDSTUFF_MAIL 1)
else (BUILD_MAIL)
    set(HAVE_LCDSTUFF_MAIL 0)
endif (BUILD_MAIL)

#
# generate config.h
#
configure_file(
    "${PROJECT_SOURCE_DIR}/config.h.in"
    "${PROJECT_BINARY_DIR}/config.h"
)

message(STATUS "Building email              : ${BUILD_MAIL}")
message(STATUS "Building RSS                : ${BUILD_RSS}")
message(STATUS "Building MPD                : ${BUILD_MPD}")
message(STATUS "Building weather            : ${BUILD_WEATHER}")

add_subdirectory(shared)
add_subdirectory(src)

install(
    FILES           lcd-stuff.conf
    DESTINATION     /etc
)

# vim: set sw=4 ts=4 et:
