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

set(SRC
    keyfile.c
    main.c
    mplayer.c
    screen.c
    servicethread.c
    util.c

)

if (BUILD_MAIL)
    set(SRC ${SRC} mail.c maillib.c)
endif (BUILD_MAIL)

if (BUILD_RSS)
    set(SRC ${SRC} rss.c)
endif (BUILD_RSS)

if (BUILD_WEATHER)
    set(SRC ${SRC} weather.c weatherlib.c)
endif (BUILD_WEATHER)

if (BUILD_MPD)
    set(SRC ${SRC} mpd.c)
endif (BUILD_MPD)

add_executable(lcd-stuff ${SRC})
target_link_libraries(lcd-stuff LCDstuff ${EXTRA_LIBS})

install(
    TARGETS         lcd-stuff
    DESTINATION     bin
)
