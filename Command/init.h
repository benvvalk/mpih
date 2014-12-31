#ifndef _INIT_H_
#define _INIT_H_

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "Options/CommonOptions.h"
#include "Command/init/Connection.h"
#include "Command/init/mpi.h"
#include "Command/init/event_handlers.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include "Env/env.h"
#include <mpi.h>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <cassert>
#include <algorithm>

static const char INIT_USAGE_MESSAGE[] =
"Usage: mpi [--socket <path>] init &\n"
"\n"
"Description:\n"
"\n"
"   Start a daemon that will listen for MPI commands\n"
"   on the Unix socket file at <path>. <path> must be\n"
"   specified using either the MPIH_SOCKET environment\n"
"   variable or the --socket option, with --socket\n"
"   taking precedence. <path> does not need to\n"
"   exist prior to running 'mpi init'. If <path> does\n"
"   exist, the file will be deleted and recreated\n"
"   by the daemon.\n"
"\n"
"   The normal way to issue commands to the daemon is\n"
"   to run other 'mpih' commands (e.g. 'mpih send') with\n"
"   the same --socket option (or MPIH_SOCKET value).\n"
"\n"
"Options:\n"
"\n"
"   -s,--socket PATH   communicate over Unix socket\n"
"                      at PATH\n";

static const char init_shortopts[] = "hv";

static const struct option init_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

static inline void server_loop(const char* socketPath)
{
	// create Unix domain socket that listens for connections
	evutil_socket_t listener = UnixSocket::listen(socketPath, false);

	// main state object for libevent
	struct event_base* base = event_base_new();
	assert(base != NULL);

	// register handler for new connections
	struct event* listener_event = event_new(base, listener,
		EV_READ|EV_PERSIST, init_accept_handler, (void*)base);
	assert(listener_event != NULL);

	int result = event_add(listener_event, NULL);
	assert(result == 0);

	if (opt::verbose)
		printf("Listening for connections...\n");

	// start libevent loop
	event_base_dispatch(base);

	// cleanup
	event_free(listener_event);
	event_base_free(base);
}

static inline int cmd_init(int argc, char** argv)
{
	// parse command line options
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
			opt::verbose++;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi init: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(INIT_USAGE_MESSAGE);
		}
	}

	// use line buffering on stdout/stderr
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	// initialize MPI
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi::numProc);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi::rank);

	// start connection handling loop on Unix socket
	server_loop(opt::socketPath.c_str());

	// shutdown MPI
	MPI_Finalize();

	return 0;
}

#endif
