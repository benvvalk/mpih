#include "Command/commands.h"
#include "Options/CommonOptions.h"
#include "Env/env.h"
#include <string>
#include <getopt.h>
#include <stdlib.h>

using namespace std;

static const char main_shortopts[] = "+hs:v";

static const struct option main_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "socket",   required_argument, NULL, 's' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

int main(int argc, char** argv)
{
	if (getenv(MPIH_SOCKET))
		opt::socketPath = getenv(MPIH_SOCKET);

	for (int c; (c = getopt_long(argc, argv,
		main_shortopts, main_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(USAGE_MESSAGE);
		  case 'h':
			arg >> opt::help; break;
		  case 's':
			arg >> opt::socketPath; break;
		  case 'v':
			opt::verbose++; break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpih: invalid option: `-"
				<< (char)c << optarg << "'\n";
			return EXIT_FAILURE;
		}
	}

	string command;
	if (argc - optind > 0)
		command = argv[optind++];

	if (opt::socketPath.empty() &&
		command != "version" &&
		command != "--version" &&
		command != "help" &&
		!opt::help)
	{
		std::cerr << "error: no socket path specified\n";
		die(USAGE_MESSAGE);
	}

	return invoke_cmd(command.c_str(), argc, argv);
}
