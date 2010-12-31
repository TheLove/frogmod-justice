#!/bin/bash

ulimit -c unlimited

while true; do
echo 'Starting ./frogserv'
valgrind -v --leak-check=full --show-reachable=yes ./frogserv 2>&1 | tee frogserv-valgrind-`date +"%F_%H-%M-%S"`.log
if [ -f core ]; then
	mv core core-`date +"%F_%H-%M-%S"`
fi
if [ ! -z $1 ]; then
/usr/sbin/sendmail -t "$1" << EOF
From: frogserv@mappinghell.net
Subject: Server crash

Server crashed.

.


EOF

echo 'Crash email sent'
fi
sleep 1
done
