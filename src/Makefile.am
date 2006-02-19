
MAINTAINERCLEANFILES    = Makefile.in
bin_PROGRAMS            = lcd-stuff-mail lcd-stuff-rss

lcd_stuff_mail_SOURCES       =                               \
    main.c                                              \
    main.h                                              \
    mail.c						\
    mail.h						\
    maillib.c						\
    maillib.h						\
    constants.h						\
    global.h						\
    lcdlib.c						\
    lcdlib.h						\
    helpfunctions.h					\
    helpfunctions.c

lcd_stuff_rss_SOURCES       =                               \
    main.c                                              \
    main.h                                              \
    constants.h						\
    global.h						\
    lcdlib.c						\
    lcdlib.h						\
    rss.h						\
    rss.c						\
    helpfunctions.h					\
    helpfunctions.c


lcd_stuff_rss_LDADD = ../shared/libLCDstuff.a
lcd_stuff_mail_LDADD = ../shared/libLCDstuff.a

# vim: set sw=8 ts=8 noet:
