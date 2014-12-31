#ifndef _SIZE_H_
#define _SIZE_H_

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

static const char SIZE_USAGE_MESSAGE[] =
"Usage: mpi size <socket_path>\n"
"\n"
"Description:\n"
"\n"
"   Print the number of ranks in the current MPI job.\n"
"\n"
"Options:\n"
"\n"
"   (none)\n";

static const char size_shortopts[] = "hv";

static const struct option size_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

int cmd_size(int argc, char** argv)
{
	for (int c; (c = getopt_long(argc, argv,
		size_shortopts, size_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(SIZE_USAGE_MESSAGE);
		  case 'h':
			std::cout << SIZE_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'v':
			arg >> opt::verbose;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi size: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(SIZE_USAGE_MESSAGE);
		}
	}

	// make sure a socket path is given (and nothing else)
	if (argc - optind != 1)
		die(SIZE_USAGE_MESSAGE);

	if (opt::verbose)
		std::cerr << "Connecting to 'mpih init' process..."
			<< std::endl;

	int socket = UnixSocket::connect(argv[optind]);

	if (opt::verbose)
		std::cerr << "Connected." << std::endl;

	struct event_base* base = event_base_new();
	assert(base != NULL);

	struct bufferevent* bev = bufferevent_socket_new(base,
		socket, BEV_OPT_CLOSE_ON_FREE);
	assert(bev != NULL);

	bufferevent_setcb(bev, client_read_handler, NULL,
		client_event_handler, (void*)base);
	bufferevent_setwatermark(bev, EV_READ, 0, MAX_BUFFER_SIZE);
	bufferevent_enable(bev, EV_READ|EV_WRITE);

	// send command to 'mpi init' daemon
	evbuffer_add_printf(bufferevent_get_output(bev), "SIZE\n");

	event_base_dispatch(base);
	bufferevent_free(bev);
	event_base_free(base);

	return 0;
}

#endif
