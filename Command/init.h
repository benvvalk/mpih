#ifndef _INIT_H_
#define _INIT_H_

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "Options/CommonOptions.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include <mpi.h>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <cassert>
#include <algorithm>

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

enum ConnectionMode {
	READING_COMMAND=0,
	RECEIVING_DATA,
	SENDING_DATA,
	CLOSED
};

#define MPI_BUFFER_SIZE 65536

struct ConnectionState {
	ConnectionMode mode;
	size_t mpi_buffer_len;
	char* mpi_buffer;
};

#define MAX_READ_SIZE 16384
#define MAX_HEADER_SIZE 256

static inline void
close_connection(struct bufferevent* bev,
	ConnectionState* state)
{
	if (state != NULL) {
		if (state->mpi_buffer != NULL)
			free(state->mpi_buffer);
		free(state);
	}
	bufferevent_free(bev);
}

static inline void
process_header(const char* line, struct bufferevent *bev)
{
	printf("Received command: '%s'\n", line);

	if (strcmp(line, "RANK") == 0) {
		assert(bev != NULL);
		struct evbuffer* output = bufferevent_get_output(bev);
		assert(output != NULL);
		evbuffer_add_printf(output, "%d\n", mpi::rank);
	} else {
		fprintf(stderr, "error: unrecognized command '%s'\n",
			line);
	}
}

static inline void
init_read_handler(struct bufferevent *bev, void *arg)
{
	ConnectionState* state = (ConnectionState*)arg;
	assert(state != NULL);

	struct evbuffer *input, *output;
	input = bufferevent_get_input(bev);
	output = bufferevent_get_output(bev);

	if (state->mode == READING_COMMAND) {
		size_t n;
		size_t origLen = evbuffer_get_length(input);
		char* line = evbuffer_readln(input, &n, EVBUFFER_EOL_LF);
		if (line != NULL) {
			process_header(line, bev);
		} else if (origLen >= MAX_HEADER_SIZE) {
			fprintf(stderr, "header line exceeded max length "
				"(%d bytes)\n", MAX_HEADER_SIZE);
			close_connection(bev, state);
		}
		free(line);
	}
}

static inline void
init_event_handler(struct bufferevent *bev, short error, void *arg)
{
	ConnectionState* state = (ConnectionState*)arg;

	// we should never see this
	assert(!(error & BEV_EVENT_TIMEOUT));

	if (error & BEV_EVENT_EOF) {
		// connection closed
	} else if (error & BEV_EVENT_ERROR) {
		perror("libevent");
	}

	close_connection(bev, state);
}

static inline void
init_accept_handler(evutil_socket_t listener, short event, void *arg)
{
	struct event_base *base = (event_base*)arg;

	evutil_socket_t fd = UnixSocket::accept(listener, false);

	printf("Connected to client.\n");

	ConnectionState* state = (ConnectionState *)
		malloc(sizeof(ConnectionState));
	state->mode = READING_COMMAND;
	state->mpi_buffer = NULL;
	state->mpi_buffer_len = 0;

	struct bufferevent* bev = bufferevent_socket_new(base,
		fd, BEV_OPT_CLOSE_ON_FREE);

	// set callbacks for buffer input/output
	bufferevent_setcb(bev, init_read_handler, NULL,
		init_event_handler, state);
	// set low/high watermarks for invoking callbacks
	bufferevent_setwatermark(bev, EV_READ, 0, MAX_READ_SIZE);
	// enable callbacks
	bufferevent_enable(bev, EV_READ|EV_WRITE);
}

static inline void serverLoop(const char* socketPath)
{
	evutil_socket_t listener = UnixSocket::listen(socketPath, false);

	struct event_base* base = event_base_new();
	assert(base != NULL);

	struct event* listener_event = event_new(base, listener,
		EV_READ|EV_PERSIST, init_accept_handler, (void*)base);

	int result = event_add(listener_event, NULL);
	assert(result == 0);

	printf("Waiting for connection...\n");

	event_base_dispatch(base);
	event_free(listener_event);
	event_base_free(base);
}

static inline int cmd_init(int argc, char** argv)
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

	// turn off buffering on stdout/stderr
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi::numProc);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi::rank);

	serverLoop(argv[optind]);

	MPI_Finalize();

	return 0;
}

#endif
