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

# locking again should fail
! dotlockfile -l -r 0 testlock.lock

# lock should be gone when command completes
wait
[ ! -f testlock.lock ] || { echo "lockfile still exists after unlock"; exit 1; }

echo "tests OK"

