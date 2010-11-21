# - Try to find libetpan
# Once done this will define
#
#  LIBETPAN_FOUND - system has libetpan
#  LIBETPAN_INCLUDE_DIRS - the libetpan include directory
#  LIBETPAN_LIBRARIES - Link these to use libetpan
#  LIBETPAN_DEFINITIONS - Compiler switches required for using libetpan
#
#  Copyright (c) 2010 Bernhard Walle <bernhard@bwalle.de>
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#


if (LIBETPAN_LIBRARIES AND LIBETPAN_INCLUDE_DIRS)
  # in cache already
  set(LIBETPAN_FOUND TRUE)
else (LIBETPAN_LIBRARIES AND LIBETPAN_INCLUDE_DIRS)
  find_path(LIBETPAN_INCLUDE_DIR
    NAMES
      libetpan/libetpan.h
    PATHS
      /usr/include
      /usr/local/include
      /opt/local/include
      /sw/include
    PATH_SUFFIXES
      libetpan
  )

  find_library(ETPAN_LIBRARY
    NAMES
      etpan
    PATHS
      /usr/lib
      /usr/local/lib
      /opt/local/lib
      /sw/lib
  )
  mark_as_advanced(ETPAN_LIBRARY)

  if (ETPAN_LIBRARY)
    set(ETPAN_FOUND TRUE)
  endif (ETPAN_LIBRARY)

  set(LIBETPAN_INCLUDE_DIRS
    ${LIBETPAN_INCLUDE_DIR}
  )

  if (ETPAN_FOUND)
    set(LIBETPAN_LIBRARIES
      ${LIBETPAN_LIBRARIES}
      ${ETPAN_LIBRARY}
    )
  endif (ETPAN_FOUND)

  if (LIBETPAN_INCLUDE_DIRS AND LIBETPAN_LIBRARIES)
     set(LIBETPAN_FOUND TRUE)
  endif (LIBETPAN_INCLUDE_DIRS AND LIBETPAN_LIBRARIES)

  if (LIBETPAN_FOUND)
    if (NOT libetpan_FIND_QUIETLY)
      message(STATUS "Found libetpan: ${LIBETPAN_LIBRARIES}")
    endif (NOT libetpan_FIND_QUIETLY)
  else (LIBETPAN_FOUND)
    if (libetpan_FIND_REQUIRED)
      message(FATAL_ERROR "Could not find libetpan")
    endif (libetpan_FIND_REQUIRED)
  endif (LIBETPAN_FOUND)

  # show the LIBETPAN_INCLUDE_DIRS and LIBETPAN_LIBRARIES variables only in the advanced view
  mark_as_advanced(LIBETPAN_INCLUDE_DIRS LIBETPAN_INCLUDE_DIR LIBETPAN_LIBRARIES)

endif (LIBETPAN_LIBRARIES AND LIBETPAN_INCLUDE_DIRS)

