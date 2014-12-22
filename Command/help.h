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
"The available commands are:\n"
"\n"
"   help      show usage for specific commands\n"
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
