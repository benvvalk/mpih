#ifndef _MPIH_FINALIZE_H_
#define _MPIH_FINALIZE_H_

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

static const char FINALIZE_USAGE_MESSAGE[] =
"Usage: " PROGRAM_NAME " [--socket <path>] finalize\n"
"\n"
"Description:\n"
"\n"
"   Shut down the current MPI rank.\n"
"\n"
"   This command stops the daemon that has been\n"
"   started with the 'mpi init' command. When\n"
"   all ranks have called 'mpi finalize', the\n"
"   MPI job will shut down cleanly.\n"
"\n"
"Options:\n"
"\n"
"   -s,--socket PATH   connect to 'mpi init' daemon\n"
"                      through Unix socket at PATH\n";

static const char finalize_shortopts[] = "hv";

static const struct option finalize_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

int cmd_finalize(int argc, char** argv)
{
	for (int c; (c = getopt_long(argc, argv,
		finalize_shortopts, finalize_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(FINALIZE_USAGE_MESSAGE);
		  case 'h':
			std::cout << FINALIZE_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'v':
			arg >> opt::verbose;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi finalize: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(FINALIZE_USAGE_MESSAGE);
		}
	}

	if (opt::verbose)
		std::cerr << "Connecting to 'mpih init' process..."
			<< std::endl;

	int socket = UnixSocket::connect(opt::socketPath.c_str());

	if (opt::verbose)
		std::cerr << "Connected." << std::endl;

	struct event_base* base = event_base_new();
	assert(base != NULL);

	struct bufferevent* bev = bufferevent_socket_new(base,
		socket, BEV_OPT_CLOSE_ON_FREE);
	assert(bev != NULL);

	bufferevent_setcb(bev, NULL, NULL,
		client_event_handler, NULL);
	bufferevent_setwatermark(bev, EV_READ, 0, 0);
	bufferevent_enable(bev, EV_READ|EV_WRITE);

	// send command to 'mpi init' daemon
	evbuffer_add_printf(bufferevent_get_output(bev), "FINALIZE\n");

	event_base_dispatch(base);
	event_base_free(base);

	return 0;
}
#endif
