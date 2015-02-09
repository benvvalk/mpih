#!/bin/bash
set -eu

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
		stderr "Usage: $(basename $0) <num_lines>"
		stderr "Example: $(basename $0) 100"
	fi
	exit 1
fi

n=$1; shift

#------------------------------------------------------------
# test
#------------------------------------------------------------

stderr "log for rank $MPIH_RANK: $MPIH_LOG"

data_file=data.txt
recv1_file=recv1.txt
recv2_file=recv2.txt
seq 1 $n > data.txt

if [ $MPIH_RANK -eq 0 ]; then
	mpih send 1 $data_file
	mpih send 1 $data_file
else
	expected_size=$(du -b $data_file | cut -f1)
	expected_md5sum=$(md5sum $data_file | cut -d' ' -f1)
	mpih recv 0 > $recv1_file
	recv1_size=$(du -b $recv1_file | cut -f1)
	recv1_md5sum=$(md5sum $recv1_file | cut -d' ' -f1)
	mpih recv 0 > $recv2_file
	recv2_size=$(du -b $recv2_file | cut -f1)
	recv2_md5sum=$(md5sum $recv2_file | cut -d' ' -f1)
	if [ "$recv1_size" -ne "$expected_size" ]; then
		echo "FAILED: data size for recv #1 does not match expected size"
		stderr "correct size (bytes): $expected_size"
		stderr "recv #1 size (bytes): $recv1_size"
		exit 1
	elif [ "$recv1_md5sum" != "$expected_md5sum" ]; then
		stderr "FAILED: md5sum for recv #1 does not match md5sum"
		stderr "correct md5sum: $expected_md5sum"
		stderr "recv #1 md5sum: $recv1_md5sum"
		exit 1
	else
		stderr "PASSED!"
	fi
fi
