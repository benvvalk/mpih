#ifndef _INIT_H_
#define _INIT_H_

#include "Options/CommonOptions.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include <mpi.h>
#include <getopt.h>
#include <iostream>
#include <sstream>

static const char INIT_USAGE_MESSAGE[] =
"Usage: mpi init <socket_path>\n"
"\n"
"Description:\n"
"\n"
"   Initialize the current MPI rank and start a daemon\n"
"   that will listen for commands issued from the\n"
"   shell (e.g. 'mpi send').\n"
"\n"
"   Communication between clients (mpi commands) and\n"
"   the daemon occurs over a Unix domain socket located at\n"
"   <socket_path>.  <socket_path> does not need to\n"
"   exist prior to running 'mpi init'. If <socket_path>\n"
"   does exist the daemon will delete the file and recreate\n"
"   it.\n"
"\n"
"Options:\n"
"\n"
"   (none)\n";

namespace mpi {
	int rank;
	int numProc;
}

static const char init_shortopts[] = "hv";

static const struct option init_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

void commandHandler(const char* cmd, int socket_fd, char* dataHead,
	size_t dataHeadLen)
{
	const size_t BUFFER_SIZE = 16384;
	char buffer[BUFFER_SIZE];
	int n = 1;
	while (n > 0)
		n = recv(socket_fd, &buffer, BUFFER_SIZE, 0);
	if (n < 0)
		perror("recv");
}

int serverLoop(const char* socketPath)
{
	int s = UnixSocket::listen(socketPath);

	for(;;) {

		const size_t BUFFER_SIZE = 1024;
		char buffer[BUFFER_SIZE+1];
		char* bufferPos = &buffer[0];
		const char* bufferEnd = &buffer[0] + BUFFER_SIZE;

		int done, n;
		std::cout << "Waiting for a connection...\n";
		int s2;
		struct sockaddr_un remote;
		socklen_t t = sizeof(remote);
		if ((s2 = accept(s, (struct sockaddr *)&remote, &t)) == -1) {
			perror("accept");
			exit(1);
		}

		std::cout << "Connected.\n";
		done = 0;
		memset(&buffer[0], 0, BUFFER_SIZE+1);
		do {

			n = recv(s2, bufferPos, bufferEnd - bufferPos, 0);
			bufferPos += n;

			char* data = &buffer[0];
			if (n > 0) {
				char* cmd = strsep(&data, "\n");
				if (data != NULL) {
					std::cout << "Received command: " << cmd
						<< std::endl;
					commandHandler(cmd, s2, data, bufferPos - data);
					done = 1;
				} else if (bufferPos >= bufferEnd) {
					std::cerr << "Client request exceeded max length for "
						<< "header line: " << buffer << std::endl;
					done = 1;
				}
			} else {
				if (n < 0)
					perror("recv");
				done = 1;
			}

		} while (!done);

		close(s2);
	}

	return 0;
}

int cmd_init(int argc, char** argv)
{
	for (int c; (c = getopt_long(argc, argv,
		init_shortopts, init_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(INIT_USAGE_MESSAGE);
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

	// make sure a socket path is given (and nothing else)
	if (argc - optind != 1)
		die(INIT_USAGE_MESSAGE);

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi::numProc);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi::rank);

	serverLoop(argv[optind]);

	MPI_Finalize();

	return 0;
}

#endif
