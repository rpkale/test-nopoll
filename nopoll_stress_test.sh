#!/bin/sh

IPADDRESS=$1
SERVER_PORT=$2
DURATION=$3
SPAWNCOUNT=$4
for count in $(seq 1 ${SPAWNCOUNT}) ; do
    ./test_nopoll client ${count} ${IPADDRESS} ${SERVER_PORT} ${DURATION} &
done

echo "Waiting for processes to finish..."
wait
