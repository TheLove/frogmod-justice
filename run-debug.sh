#!/bin/bash

ulimit -c unlimited

while true; do
echo 'Starting ./frogserv'
./frogserv
mv core core-`date +"%F_%H-%M-%S"`
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
