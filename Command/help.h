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
"   a command-line interface for streaming data between\n"
"   machines using MPI, a widely used messaging API for\n"
"   implementing cluster-based software.\n"
"\n"
"   While MPI applications are usually written in programming\n"
"   languages such as C or python, " PROGRAM_NAME " allows\n"
"   users to implement MPI applications using shell scripts.\n"
"\n"
"The available commands are:\n"
"\n"
"   finalize  shutdown current MPI rank (stops daemon)\n"
"   help      show usage for specific commands\n"
"   init      initialize current MPI rank (starts daemon)\n"
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
