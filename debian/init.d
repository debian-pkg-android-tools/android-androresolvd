#!/bin/sh
### BEGIN INIT INFO
# Provides:          android-androresolvd
# Required-Start:    $remote_fs $network $local_fs
# Required-Stop:     $remote_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Updates /etc/resolv.conf from Android system props
### END INIT INFO

# Author: Sven-Ola Tuecke <sven-ola@gmx.de>

set -e

PATH=/sbin:/usr/sbin:/bin:/usr/bin
DAEMON=/usr/sbin/androresolvd

test -x $DAEMON || exit 0

. /lib/lsb/init-functions

case "$1" in
	start)
		$DAEMON
		# No error if damon fails to start
		exit 0
	;;
	stop)
		PID=$(pidof $DAEMON)
		case $PID in "");;*)
			kill -INT $PID
		;;esac
	;;
	restart|force-reload)
		$0 stop
		$0 start
	;;
	*)
		echo "Usage: $0 {start|stop|restart}" >&2
		exit 3
	;;
esac
