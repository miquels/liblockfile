#! /bin/sh
#
#	Some simple tests to check if locking/unlocking works correctly.
#

set -e
PATH=.:$PATH

rm -f testlock.lock

# lock should succeed
dotlockfile -l -r 1 testlock.lock

# locking again should fail
! dotlockfile -l -r 0 testlock.lock

# unlocking should succeed
dotlockfile -u testlock.lock

[ ! -f testlock.lock ] || { echo "lockfile still exists after unlock"; exit 1; }

# same but with a command
dotlockfile -l -r 1 testlock.lock sleep 2 &
PID=$!
sleep 1

# locking again should fail
if dotlockfile -l -r 0 testlock.lock
then
	if kill -0 $PID 2>/dev/null
	then
		echo "double lock while running command"
		exit 1
	fi
fi

# lock should be gone when command completes
wait
[ ! -f testlock.lock ] || { echo "lockfile still exists after running cmd"; exit 1; }

# test locking with given sleep interval
dotlockfile -l -r 0 testlock.lock
dotlockfile -l -i 4 testlock.lock /bin/true &

time_start=$(date '+%s')

sleep 4.5
dotlockfile -u testlock.lock

wait

time_end=$(date '+%s')
time_elapsed=`expr $time_end - $time_start`

# Time should equal 8 seconds
[ "$time_elapsed" = '8' ] || { echo "lockfile should take 8 seconds to be replaced. [$time_elapsed]"; exit 1; }
[ ! -f testlock.lock ] || { echo "lockfile still exists after running cmd"; exit 1; }

echo "tests OK"

