#!/bin/bash
set -eu -o pipefail

if [ $# -ne 1 ]; then
	echo "Usage: mpirun -np <num_ranks> $(basename $0) <file_size>" >&2
	echo "Example: mpirun -np 2 $(basename $0) 1M" >&2
	exit 1
fi

file_size=$1; shift

# client commands (e.g. 'mpih send') communicate
# with the 'mpih init' daemon using this Unix socket
tmpdir=$(mktemp -d)
export MPIH_SOCKET="$tmpdir/mpih_socket"

# start daemon that executes MPI routines
mpih init -vvv --log $tmpdir/init.log
# wait for daemon to start up
sleep 2

my_rank=$(mpih rank)
num_ranks=$(mpih size)
max_rank=$(($num_ranks - 1))

data_file=random.bin

if [ $my_rank -eq 0 ]; then
	head -c $file_size /dev/urandom > $data_file
	for i in $(seq 0 $max_rank); do
		mpih send $i random.bin &
	done
fi

my_md5sum=$(mpih recv 0 | md5sum | cut -d' ' -f1)
correct_md5sum=$(md5sum $data_file | cut -d' ' -f1)

if [ "$my_md5sum" == "$correct_md5sum" ]; then
	echo "success: data recv'ed by rank $my_rank is correct!"
else
	echo "error: data recv'ed by rank $my_rank differs from data sent!"
fi

# shutdown daemon
mpih finalize
