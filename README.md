# Description

```
Usage: mpih [--socket <path>] [--help] <command> [<args>]

Description:

   'mpih' stands for 'MPI harness'. It provides
   a command-line interface for streaming data between
   machines using MPI, a widely used messaging API for
   implementing cluster-based software.

   While MPI applications are usually written in programming
   languages such as C or python, mpih allows
   users to implement MPI applications using shell scripts.

The available commands are:

   finalize  shutdown current MPI rank (stops daemon)
   help      show usage for specific commands
   init      initialize current MPI rank (starts daemon)
   rank      print rank of current MPI process
   recv      stream data from another MPI rank
   send      stream data to another MPI rank
   size      print number of ranks in current MPI job

See 'mpih help <command>' for help on specific commands.
```

# Synopsis

hello-world.sh:
```
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
```

Run MPI job:
```
mpirun -np 10 hello-world.sh
```

Output:
```
rank 1 received: 'Hello, from rank 0!'
rank 3 received: 'Hello, from rank 2!'
rank 2 received: 'Hello, from rank 1!'
rank 4 received: 'Hello, from rank 3!'
rank 5 received: 'Hello, from rank 4!'
rank 6 received: 'Hello, from rank 5!'
rank 7 received: 'Hello, from rank 6!'
rank 9 received: 'Hello, from rank 8!'
rank 8 received: 'Hello, from rank 7!'
rank 0 received: 'Hello, from rank 9!'
```

# Dependencies

'mpih' requires:

  * an MPI library (e.g. OpenMPI, MPICH)
  * libevent

# Installing

Compile and install to your ``$HOME/bin`` (requires git and CMake):

```
$ git clone git@bitbucket.org:benvvalk/mpih.git
$ mkdir mpih/build
$ cd mpih/build
$ cmake -DCMAKE_INSTALL_PREFIX:PATH=$HOME ..
$ make
$ make install
```

# Author

Ben Vandervalk
