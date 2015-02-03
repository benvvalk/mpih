# Warning!

This project is pretty new. It seems to be working but it probably needs further testing.

# Description

```
$ mpih help
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
   run       set up environment and run a user script
   send      stream data to another MPI rank
   size      print number of ranks in current MPI job

See 'mpih help <command>' for help on specific commands.
```
# Synopsis

```bash
$ cat hello-world.sh
#!/bin/bash
set -eu -o pipefail

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
```

```bash
$ mpirun -np 10 mpih run hello-world.sh
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
$ git clone git@github.com:benvvalk/mpih.git
$ mkdir mpih/build
$ cd mpih/build
$ cmake -DCMAKE_INSTALL_PREFIX:PATH=$HOME ..
$ make
$ make install
```

# See also

  * [MPI-Bash](http://www.ccs3.lanl.gov/~pakin/software/mpibash-4.3.html)
  * [mpififo](https://bitbucket.org/nathanweeks/mpififo)

# Author

Ben Vandervalk
