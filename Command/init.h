#ifndef _INIT_H_
#define _INIT_H_

#include "Options/CommonOptions.h"
#include "IO/IOUtil.h"
#include <getopt.h>
#include <iostream>
#include <sstream>

static const char INIT_USAGE_MESSAGE[] =
"Usage: mpi init [--fifo <fifo_file>]\n"
"\n"
"Description:\n"
"\n"
"   Initialize the current MPI rank and start a daemon\n"
"   that will listen for commands (e.g. 'mpi send').\n"
"   If the --fifo option is used the daemon will read\n"
"   commands from <fifo_file>; otherwise the daemon will\n"
"   read commands from STDIN.\n"
"\n"
"Options:\n"
"\n"
"   --fifo FILE   read and execute FIFO commands from FILE\n";

static const char init_shortopts[] = "f:hv";

static const struct option init_longopts[] = {
	{ "fifo",     required_argument, NULL, 'f' },
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

int cmd_init(int argc, char** argv)
{
	for (int c; (c = getopt_long(argc, argv,
		init_shortopts, init_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(INIT_USAGE_MESSAGE);
		  case 'f':
			arg >> opt::fifoPath;
			break;
		  case 'h':
			std::cout << INIT_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'v':
			arg >> opt::verbose;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi init: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(INIT_USAGE_MESSAGE);
		}
	}

	return 0;
}

#endif
