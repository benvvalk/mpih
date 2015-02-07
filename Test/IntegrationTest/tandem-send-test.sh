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
		stderr "Usage: $(basename $0) <size>"
		stderr "Example: $(basename $0) 1M"
	fi
	exit 1
fi

size=$1; shift

#------------------------------------------------------------
# test
#------------------------------------------------------------

data_file=random.bin
dd if=/dev/urandom count=1 bs=$size >$data_file 2>/dev/null

if [ $MPIH_RANK -eq 0 ]; then
	mpih send 1 $data_file
	mpih send 1 $data_file
else
	expected_size=$(du -b $data_file | cut -f1)
	expected_md5sum=$(md5sum $data_file | cut -d' ' -f1)
	mpih recv 0 > recv1.bin
	recv1_size=$(du -b recv1.bin | cut -f1)
	recv1_md5sum=$(md5sum recv1.bin | cut -d' ' -f1)
	mpih recv 0 > recv2.bin
	recv2_size=$(du -b recv2.bin | cut -f1)
	recv2_md5sum=$(md5sum recv2.bin | cut -d' ' -f1)
	if [ "$recv1_size" -ne "$expected_size" ]; then
		echo "FAILED: data size for recv #1 does not match expected size"
		stderr "correct size (bytes): $expected_size"
		stderr "recv #1 size (bytes): $recv1_size"
		exit 1
	elif [ "$recv1_md5sum" != "$expected_md5sum" ]; then
		stderr "FAILED: md5sum for recv #1 does not match expected size"
		stderr "correct md5sum: $expected_md5sum"
		stderr "recv #1 md5sum: $recv1_md5sum"
		exit 1
	else
		stderr "PASSED!"
	fi
fi
