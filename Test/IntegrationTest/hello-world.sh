#!/bin/bash
set -eu -o pipefail

echo "rank $MPIH_RANK log: $MPIH_LOG"

# send 'hello' messages in a ring
dest_rank=$((($MPIH_RANK + 1) % $MPIH_SIZE))
src_rank=$(($MPIH_RANK - 1))
if [ $src_rank -lt 0 ]; then
	src_rank=$(($MPIH_SIZE - 1))
fi

# send a message to a neighbour
echo "Hello, from rank $MPIH_RANK!" | mpih send $dest_rank

# receive a message from a neighbour
msg=$(mpih recv $src_rank)
echo "rank $MPIH_RANK received: '$msg'"
