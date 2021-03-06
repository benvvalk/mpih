#ifndef _RANK_H_
#define _RANK_H_

#include "config.h"
#include "Options/CommonOptions.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include "Command/client/event_handlers.h"
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

static const char RANK_USAGE_MESSAGE[] =
"Usage: " PROGRAM_NAME " [--socket <path>] rank\n"
"\n"
"Description:\n"
"\n"
"   Print the rank of the current MPI process.\n"
"\n"
"Options:\n"
"\n"
"   -s,--socket PATH   communicate over Unix socket\n"
"                      at PATH\n";

static const char rank_shortopts[] = "hv";

static const struct option rank_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

int query_rank()
{
	int socket = UnixSocket::connect(opt::socketPath.c_str());

	if (opt::verbose)
		std::cerr << "Connected." << std::endl;

	struct event_base* base = event_base_new();
	assert(base != NULL);

	struct bufferevent* bev = bufferevent_socket_new(base,
		socket, BEV_OPT_CLOSE_ON_FREE);
	assert(bev != NULL);

	bufferevent_setcb(bev, integer_read_handler, NULL,
		client_event_handler, (void*)&mpi::rank);
	bufferevent_setwatermark(bev, EV_READ, 0, MAX_BUFFER_SIZE);
	bufferevent_enable(bev, EV_READ|EV_WRITE);

	// send command to 'mpi init' daemon
	evbuffer_add_printf(bufferevent_get_output(bev), "RANK\n");

	event_base_dispatch(base);
	bufferevent_free(bev);
	event_base_free(base);

	return mpi::rank;
}

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

	if (opt::verbose)
		std::cerr << "Connecting to 'mpih init' process..."
			<< std::endl;

	int rank = query_rank();
	printf("%d\n", rank);

	return 0;
}

#endif
