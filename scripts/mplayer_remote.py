#!/usr/bin/python
#
# (c) 2007, Bernhard Walle <bernhard.walle@gmx.de>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
# 02110-1301, USA.
#

import socket
from optparse import *

def parse_commandline():
    usage = 'Usage: mplayer_remote.py [-o host] [-p port] '+ \
        '-s | -P | -S | -O'
    parser = OptionParser(usage)

    # host/port
    parser.add_option("-o", "--host", dest="host", metavar="HOST",
                      default="localhost",
                      help="Use HOST instead of localhost")
    parser.add_option("-p", "--port", dest="port", metavar="PORT",
                      default="12454",
                      help="Use PORT instead of 12454")
    
    # actions
    parser.add_option("-s", "--show", dest="show", action="store_true",
            help="Shows a list of channels")
    parser.add_option("-P", "--playpause", dest="playpause", action="store_true",
            help="Toggle between play and pause")
    parser.add_option("-S", "--playstream", dest="playstream", metavar="NUMBER",
            help="Plays stream NUMBER (see the -s option to get the number)")
    parser.add_option("-O", "--stop", dest="stop", action="store_true",
            help="Stops playback")

    return parser.parse_args()

def main():
    (options, args) = parse_commandline()
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((options.host, int(options.port)))

    try:

        if options.show:
            s.send('mplayer streams\n')
            response = s.recv(4096)
            quit = False
            i = 0
            while not quit:
                for station in response.splitlines():
                    if str(station) == str('__END__'):
                        quit = True
                        break
                    print "%d: %s" % (i, station)
                    i += 1
                
                if not quit:
                    response = s.recv(4096)

        if options.playpause:
            s.send('mplayer pauseplay\n')

        if options.stop:
            s.send('mplayer stop\n')

        if options.playstream:
            s.send('mplayer play ' + options.playstream + '\n')

    except KeyboardInterrupt:
        print 'Aborted by user invocation'
    finally:
        s.close()

if __name__ == "__main__":
    main()



# vim: set sw=4 ts=4 et:
