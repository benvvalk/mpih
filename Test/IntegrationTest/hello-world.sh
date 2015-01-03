#!/bin/bash

set -o pipefail

tmpdir=$(mktemp -d)

# Client commands (e.g. 'mpih send') communicate
# with the 'mpih init' daemon using this Unix socket
export MPIH_SOCKET="$tmpdir/mpih_socket"

# Start daemon that executes MPI routines
mpih init &

# Wait for daemon to start
sleep 2

my_rank=$(mpih rank)
num_ranks=$(mpih size)

dest_rank=$(( ($my_rank + 1) % $num_ranks ))

if [ $my_rank -eq 0 ]; then
	src_rank=$(($num_ranks - 1))
else
	src_rank=$(($my_rank - 1))
fi

echo 'Hello, World!' | mpih send $dest_rank

msg=$(mpih recv $src_rank)

echo "rank $my_rank received '$msg' from rank $src_rank"

# shutdown daemon
mpih finalize
