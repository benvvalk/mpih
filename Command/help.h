#ifndef _HELP_H_
#define _HELP_H_

#include <getopt.h>
#include <iostream>

// forward declaration to break cyclic dependency
int invoke_cmd(const char* cmd, int argc, char** argv);

#define PROGRAM "mpi"

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [--help] <command> [<args>]\n"
"\n"
"Description:\n"
"\n"
"   '" PROGRAM "' is a command-line interface for writing\n"
"   MPI-based shell scripts.\n"
"\n"
"Synopsis:\n"
"\n"
"   #!/bin/bash\n"
"\n"
"   MPI_FIFO=$(mkfifo cmd_fifo)\n"
"   mpi init $MPI_FIFO &\n"
"   rank=$(mpi rank)\n"
"   if [ $rank -eq 0 ]; then\n"
"      echo 'Hello from Rank 0!' | mpi send --rank 1\n"
"      # prints 'Hello from Rank 1!'\n"
"      mpi recv --rank 1 | cat\n"
"   else\n"
"      echo 'Hello from rank 1!' | mpi send --rank 0\n"
"      # prints 'Hello from Rank 0!'\n"
"      mpi recv --rank 0 | cat\n"
"   fi\n"
"   mpi finalize\n"
"\n"
"The available commands are:\n"
"\n"
"   help      show usage for specific commands\n"
"   init      initialize this MPI rank (starts a daemon)\n"
"   finalize  shutdown MPI this MPI rank (stops daemon)\n"
"   numRanks  get number of ranks in current MPI job\n"
"   rank      get rank of this MPI process\n"
"   recv      receive data from another MPI rank\n"
"   send      send data to another MPI rank\n"
"\n"
"See '" PROGRAM " help <command>' for help on specific commands.\n";

int cmd_help(int argc, char** argv)
{
	// 'mpi help'

	if (optind >= argc) {
		std::cout << USAGE_MESSAGE;
		return EXIT_SUCCESS;
	}

	// 'mpi help <cmd>'

	char help_opt[] = "--help";
	char* cmd = argv[optind];
	char* argv_help[] = { cmd, help_opt };
	optind=1;
	return invoke_cmd(cmd, 2, argv_help);
}

#endif
