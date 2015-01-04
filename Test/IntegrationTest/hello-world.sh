#!/bin/bash

set -eu -o pipefail

# client commands (e.g. 'mpih send') communicate
# with the 'mpih init' daemon using this Unix socket
tmpdir=$(mktemp -d)
export MPIH_SOCKET="$tmpdir/mpih_socket"

# start daemon that executes MPI routines
mpih init
# wait for daemon to start up
sleep 2

my_rank=$(mpih rank)
num_ranks=$(mpih size)

dest_rank=$((($my_rank + 1) % $num_ranks))
src_rank=$(($my_rank - 1))
if [ $src_rank -lt 0 ]; then
	src_rank=$(($num_ranks - 1))
fi

# send a message to a neighbour
echo "Hello, from rank $my_rank!" | mpih send $dest_rank

# receive a message from a neighbour
msg=$(mpih recv $src_rank)
echo "rank $my_rank received: '$msg'"

# shutdown daemon
mpih finalize
