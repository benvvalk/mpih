#ifndef _RANK_H_
#define _RANK_H_

#include "Options/CommonOptions.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include <getopt.h>
#include <iostream>
#include <sstream>

static const char RANK_USAGE_MESSAGE[] =
"Usage: mpi rank <socket_path>\n"
"\n"
"Description:\n"
"\n"
"   Print the rank of the current MPI process.\n"
"\n"
"Options:\n"
"\n"
"   (none)\n";

static const char rank_shortopts[] = "hv";

static const struct option rank_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

int cmd_rank(int argc, char** argv)
{
	for (int c; (c = getopt_long(argc, argv,
		rank_shortopts, rank_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(RANK_USAGE_MESSAGE);
		  case 'h':
			std::cout << RANK_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'v':
			arg >> opt::verbose;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi rank: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(RANK_USAGE_MESSAGE);
		}
	}

	// make sure a socket path is given (and nothing else)
	if (argc - optind != 1)
		die(RANK_USAGE_MESSAGE);

	std::cout << "Trying to connect..." << std::endl;
	int socket = UnixSocket::connect(argv[optind]);

	// send "RANK" command
	const char* cmd = "RANK\n";
	if (send(socket, cmd, strlen(cmd), 0) == -1) {
		perror("send");
		exit(EXIT_FAILURE);
	}

	close(socket);
	return 0;
}

#endif
