#ifndef _RANK_H_
#define _RANK_H_

#include "Options/CommonOptions.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

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

static inline void
rank_read_handler(struct bufferevent *bev, void *arg)
{
	const int MAX_LINE_SIZE = 256;

	struct event_base *base = (event_base*)arg;
	struct evbuffer* input = bufferevent_get_input(bev);

	size_t origLen = evbuffer_get_length(input);
	char* line = evbuffer_readln(input, NULL, EVBUFFER_EOL_LF);

	if (line != NULL) {
		puts(line);
		event_base_loopexit(base, NULL);
	} else if (origLen >= MAX_LINE_SIZE) {
		fprintf(stderr, "response line exceeded max length "
				"(%d bytes)\n", MAX_LINE_SIZE);
		bufferevent_free(bev);
	}

	free(line);
}

static inline void
rank_event_handler(struct bufferevent *bev, short error, void *arg)
{
	// we should never see this
	assert(!(error & BEV_EVENT_TIMEOUT));

	if (error & BEV_EVENT_EOF) {
		// connection closed
	} else if (error & BEV_EVENT_ERROR) {
		perror("libevent");
	}

	bufferevent_free(bev);
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

	// make sure a socket path is given (and nothing else)
	if (argc - optind != 1)
		die(RANK_USAGE_MESSAGE);

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

	bufferevent_setcb(bev, rank_read_handler, NULL,
		rank_event_handler, (void*)base);
	bufferevent_setwatermark(bev, EV_READ, 0, MAX_BUFFER_SIZE);
	bufferevent_enable(bev, EV_READ|EV_WRITE);

	// send command to 'mpi init' daemon
	evbuffer_add_printf(bufferevent_get_output(bev), "RANK\n");

	event_base_dispatch(base);
	bufferevent_free(bev);
	event_base_free(base);

	return 0;
}

#endif
