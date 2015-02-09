#!/bin/bash
set -eu

#------------------------------------------------------------
# argument checking
#------------------------------------------------------------

if [ "$MPIH_RANK" -eq 0 ]; then
	if [ "$MPIH_SIZE" -ne 2 ]; then
		echo "error: this MPI script must be run with exactly 2 processes" >&2
		exit 1
	fi
	if [ $# -lt 1 ]; then
		echo "Usage: $(basename $0) <size>" >&2
		echo "Example: $(basename $0) 1M" >&2
		exit 1
	fi
fi

size=$1; shift

#------------------------------------------------------------
# helper functions
#------------------------------------------------------------

test_data() {
	dd if=/dev/zero count=1 bs=$size 2>/dev/null
}

throughput() {
	cpipe -vt 2>&1 >/dev/null | \
		awk '{running_avg=$6} END {print running_avg}'
}

if [ $MPIH_RANK -eq 0 ]; then
	test_data | mpih send 1
else
	unix_throughput=$(test_data | throughput)
	echo "unix pipeline throughput: $unix_throughput"
	mpih_throughput=$(mpih recv 0 | throughput)
	echo "mpih throughput: $mpih_throughput"
fi
