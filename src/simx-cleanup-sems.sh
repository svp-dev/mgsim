#! /bin/sh
journal=${1:-/tmp/simx-sem-journal-$UID}

while true; do
    sleep 1;
    (while read semid pid ; do
	if ! kill -0 $pid >/dev/null 2>&1; then
	    ipcrm -s $semid >/dev/null 2>&1
	fi
    done) <"$journal"
done
