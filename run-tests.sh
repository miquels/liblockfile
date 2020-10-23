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


# locking twice should retry, with -s making the retry time 1 second.
# and -B making it no more than 2 seconds (rather than 3)
dotlockfile -l -r 0 testlock.lock
dotlockfile -l -s -B testlock.lock /bin/true &
PID=$!

pre_time=$(date +%s)

# Wait 1.5 seconds to make sure at least one retry cycle passes
sleep 1.5

# unlock, then check that test lock file is eventually acquired
dotlockfile -u testlock.lock

wait

post_time=$(date +%s)
time=`expr $post_time - $pre_time`
echo $time
# If it took less than 2 seconds, that's a bug.
[ "$time" = "2" ] || { echo "lockfile should take 2 seconds to be replaced."; exit 1; }
[ ! -f testlock.lock ] || { echo "lockfile still exists after running cmd"; exit 1; }

echo "tests OK"

