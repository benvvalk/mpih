#!/bin/bash
set -eu -o pipefail

#------------------------------------------------------------
# helper functions
#------------------------------------------------------------

stderr() {
	echo "$@" >&2
}

#------------------------------------------------------------
# argument checking
#------------------------------------------------------------

if [ "$MPIH_SIZE" -ne 2 ]; then
	if [ "$MPIH_RANK" -eq 0 ]; then
		stderr "error: this MPI script must be run with exactly 2 processes"
	fi
	exit 1
fi
if [ $# -lt 1 ]; then
	if [ "$MPIH_RANK" -eq 0 ]; then
		stderr "Usage: $(basename $0) <size>"
		stderr "Example: $(basename $0) 1M"
	fi
	exit 1
fi

size=$1; shift

#------------------------------------------------------------
# run test
#------------------------------------------------------------

echo "rank $MPIH_RANK log: $MPIH_LOG"

data_file=random.bin
if [ $MPIH_RANK -eq 0 ]; then
	dd if=/dev/urandom count=1 bs=$size > $data_file 2>/dev/null
	mpih send 1 random.bin
else
	my_md5sum=$(mpih recv 0 | md5sum | cut -d' ' -f1)
	correct_md5sum=$(md5sum $data_file | cut -d' ' -f1)

	if [ "$my_md5sum" == "$correct_md5sum" ]; then
		stderr "PASSED: received data identical to sent data!"
	else
		stderr "FAILED: received data differs from sent data!"
	fi
fi
