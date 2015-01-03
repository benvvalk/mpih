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
