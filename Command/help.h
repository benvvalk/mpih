#ifndef _HELP_H_
#define _HELP_H_

#include "config.h"
#include <getopt.h>
#include <iostream>

// forward declaration to break cyclic dependency
int invoke_cmd(const char* cmd, int argc, char** argv);

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM_NAME " [--socket <path>] [--help] <command> [<args>]\n"
"\n"
"Description:\n"
"\n"
"   '" PROGRAM_NAME "' stands for 'MPI harness'. It provides\n"
"   a command-line interface for writing MPI-based shell\n"
"   scripts.\n"
"\n"
"The available commands are:\n"
"\n"
"   finalize  shutdown MPI this MPI rank (stops daemon)\n"
"   help      show usage for specific commands\n"
"   init      initialize this MPI rank (starts a daemon)\n"
"   rank      print rank of current MPI process\n"
"   recv      stream data from another MPI rank\n"
"   send      stream data to another MPI rank\n"
"   size      print number of ranks in current MPI job\n"
"\n"
"See '" PROGRAM_NAME " help <command>' for help on specific commands.\n";

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
