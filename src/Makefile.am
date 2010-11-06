
INCLUDES                = -I$(top_builddir) -I$(top_srcdir)
MAINTAINERCLEANFILES    = Makefile.in
bin_PROGRAMS            = lcd-stuff 

lcd_stuff_SOURCES       =                               \
    keyfile.h						\
    keyfile.c						\
    util.c						\
    util.h						\
    servicethread.c					\
    servicethread.h					\
    main.c                                              \
    main.h                                              \
    mplayer.c						\
    mplayer.h						\
    constants.h						\
    global.h

if LCDSTUFF_RSS
lcd_stuff_SOURCES += rss.c rss.h
endif

if LCDSTUFF_WEATHER
lcd_stuff_SOURCES += weather.h weather.c weatherlib.c weatherlib.h
endif

if LCDSTUFF_MAIL
lcd_stuff_SOURCES += mail.c mail.h maillib.c maillib.h
endif

if LCDSTUFF_MPD
lcd_stuff_SOURCES += mpd.c mpd.h
endif

lcd_stuff_LDADD = ../shared/libLCDstuff.a

# vim: set sw=8 ts=8 noet:
