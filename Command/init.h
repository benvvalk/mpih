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
	MPI_READY_TO_RECV_MSG_SIZE,
	MPI_READY_TO_RECV_MSG,
	MPI_READY_TO_SEND_MSG,
	MPI_RECVING_MSG_SIZE,
	MPI_RECVING_MSG,
	MPI_SENDING_MSG,
	CLOSED
};

#define MPI_BUFFER_SIZE 65536

struct ConnectionState {

	/** connection state (e.g. sending data) */
	ConnectionMode mode;
	/** remote rank for sending/receiving data */
	int rank;
	/** length of MPI send/recv buffer */
	uint64_t mpi_buffer_len;
	/** buffer for non-blocking MPI send/recv */
	char* mpi_buffer;
	/** ID for checking state of asynchronous send/recv */
	MPI_Request size_msg_id;
	/** ID for checking state of asynchronous send/recv */
	MPI_Request body_msg_id;

	ConnectionState() :
		mode(READING_COMMAND),
		rank(0),
		mpi_buffer_len(0),
		mpi_buffer(NULL),
		size_msg_id(0),
		body_msg_id(0)
	{ }

	~ConnectionState()
	{
		clear();
	}

	void clear()
	{
		if (mpi_buffer != NULL)
			free(mpi_buffer);

		mode = READING_COMMAND;
		rank = 0;
		mpi_buffer_len = 0;
		mpi_buffer = NULL;
		size_msg_id = 0;
		body_msg_id = 0;
	}
};

typedef std::map<evutil_socket_t, ConnectionState> ConnectionStateMap;
ConnectionStateMap g_connectionStates;

#define MAX_READ_SIZE 16384
#define MAX_HEADER_SIZE 256
#define MPI_DEFAULT_TAG 0

static inline ConnectionState&
get_connection_state(struct bufferevent* bev)
{
	assert(bev != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);
	ConnectionStateMap::iterator it =
		g_connectionStates.find(socket);
	assert(it != g_connectionStates.end());

	return it->second;
}

static inline void
close_connection(struct bufferevent* bev)
{
	assert(bev != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);
	size_t numRemoved = g_connectionStates.erase(socket);
	assert(numRemoved == 1);

	bufferevent_free(bev);
}

static inline void mpi_wait_all()
{
	for (ConnectionStateMap::iterator it = g_connectionStates.begin();
		it != g_connectionStates.end(); ++it) {
		ConnectionState& state = it->second;
		switch(state.mode) {
		MPI_RECVING_MSG_SIZE:
			MPI_Wait(&state.size_msg_id, MPI_STATUS_IGNORE);
			break;
		MPI_RECVING_MSG:
			MPI_Wait(&state.body_msg_id, MPI_STATUS_IGNORE);
			break;
		MPI_SENDING_MSG:
			MPI_Wait(&state.size_msg_id, MPI_STATUS_IGNORE);
			MPI_Wait(&state.body_msg_id, MPI_STATUS_IGNORE);
			break;
		default:
			break;
		}
	}
}

static inline void create_timer_event(struct event_base* base,
	void (*callback_func)(evutil_socket_t, short, void*),
	void* callback_arg, unsigned seconds)
{
	assert(base != NULL);
	assert(callback_func != NULL);
	struct timeval time;
	time.tv_sec = seconds;
	time.tv_usec = 0;
	struct event* ev = event_new(base, -1, 0,
			callback_func, callback_arg);
	event_add(ev, &time);
}

// forward declaration
static inline void
process_next_header(struct bufferevent *bev);
// forward declaration
static inline void post_mpi_recv_msg(struct bufferevent* bev);

static inline void update_connection_status(
	evutil_socket_t socket, short event, void* arg)
{
	const unsigned TIMER_SEC = 1;

	struct bufferevent* bev = (bufferevent*)arg;
	assert(bev != NULL);
	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	ConnectionStateMap::iterator it =
		g_connectionStates.find(socket);
	assert(it != g_connectionStates.end());
	ConnectionState& state = it->second;

	int completed = 0;
	if (state.mode == MPI_SENDING_MSG) {
		MPI_Test(&state.size_msg_id, &completed, MPI_STATUS_IGNORE);
		if (completed && state.mpi_buffer_len > 0)
			MPI_Test(&state.body_msg_id, &completed, MPI_STATUS_IGNORE);
		if (completed) {
			process_next_header(bev);
			return;
		}
	} else if (state.mode == MPI_RECVING_MSG_SIZE) {
		MPI_Test(&state.size_msg_id, &completed, MPI_STATUS_IGNORE);
		if (completed) {
			if (state.mpi_buffer_len > 0)
				post_mpi_recv_msg(bev);
			else
				process_next_header(bev);
			return;
		}
	} else if (state.mode == MPI_RECVING_MSG) {
		MPI_Test(&state.body_msg_id, &completed, MPI_STATUS_IGNORE);
		if (completed) {
			process_next_header(bev);
			return;
		}
	}

	// still waiting for current send/recv to complete;
	// check again in TIMER_SEC
	create_timer_event(base, update_connection_status,
			(void*)bev, TIMER_SEC);
}

static inline void post_mpi_send(struct bufferevent* bev,
	int rank, size_t size)
{
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	ConnectionState& state = get_connection_state(bev);
	assert(state.mode == READING_COMMAND);

	state.clear();
	state.mode = MPI_SENDING_MSG;
	state.rank = rank;
	state.mpi_buffer_len = size;
	state.mpi_buffer = (char*)malloc(size);

	// send message size in advance of message body
	MPI_Isend(&state.mpi_buffer_len, 1, MPI_UINT64_T,
		state.rank, 0, MPI_COMM_WORLD,
		&state.size_msg_id);

	// send message body (a length of zero indicates EOF)
	if (state.mpi_buffer_len > 0) {
		assert(state.mpi_buffer != NULL);
		MPI_Isend(state.mpi_buffer, state.mpi_buffer_len,
				MPI_BYTE, state.rank, MPI_DEFAULT_TAG,
				MPI_COMM_WORLD, &state.body_msg_id);
	}

	update_connection_status(socket, 0, (void*)bev);
}

static inline void post_mpi_recv_size(struct bufferevent* bev,
	int rank)
{
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	ConnectionState& state = get_connection_state(bev);
	assert(state.mode == READING_COMMAND);

	state.clear();
	state.mode = MPI_RECVING_MSG_SIZE;
	state.rank = rank;

	// send message size in advance of message body
	MPI_Irecv(&state.mpi_buffer_len, 1, MPI_UINT64_T,
		state.rank, 0, MPI_COMM_WORLD,
		&state.size_msg_id);

	update_connection_status(socket, 0, (void*)bev);
}

static inline void post_mpi_recv_msg(struct bufferevent* bev)
{
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	ConnectionState& state = get_connection_state(bev);
	assert(state.mode == MPI_RECVING_MSG_SIZE);

	state.mode = MPI_RECVING_MSG;
	state.mpi_buffer = (char*)malloc(state.mpi_buffer_len);

	// send message body (a length of zero indicates EOF)
	if (state.mpi_buffer_len > 0) {
		assert(state.mpi_buffer != NULL);
		MPI_Isend(state.mpi_buffer, state.mpi_buffer_len,
				MPI_BYTE, state.rank, MPI_DEFAULT_TAG,
				MPI_COMM_WORLD, &state.body_msg_id);
		update_connection_status(socket, 0, (void*)bev);
	}
}

static inline char* read_header(struct bufferevent* bev)
{
	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);
	size_t len = evbuffer_get_length(input);
	char* line = evbuffer_readln(input, NULL, EVBUFFER_EOL_LF);
	if (line == NULL && len > MAX_HEADER_SIZE) {
		fprintf(stderr, "header line exceeded max length "
				"(%d bytes)\n", MAX_HEADER_SIZE);
		close_connection(bev);
	}
	return line;
}

static inline void
process_next_header(struct bufferevent *bev)
{
	assert(bev != NULL);
	ConnectionState& state = get_connection_state(bev);

	state.clear();
	state.mode = READING_COMMAND;

	char* header = read_header(bev);

	// haven't fully received header line yet
	if (header == NULL)
		return;

	printf("Received header line: '%s'\n", header);

	std::stringstream ss(header);
	free(header);
	std::string command;
	ss >> command;

	// empty or all-whitespace header line
	if (command.empty())
		return;

	if (command == "RANK") {

		assert(bev != NULL);
		struct evbuffer* output = bufferevent_get_output(bev);
		assert(output != NULL);
		evbuffer_add_printf(output, "%d\n", mpi::rank);

	} else if (command == "SEND") {

		int rank;
		size_t size;
		ss >> rank;
		ss >> size;
		if (ss.fail() || !ss.eof()) {
			fprintf(stderr, "malformed SEND header, "
				"expected 'SEND <RANK> <BYTES>'\n");
			return;
		}
		post_mpi_send(bev, rank, size);

	} else if (command == "RECV") {

		int rank;
		ss >> rank;
		if (ss.fail() || !ss.eof()) {
			fprintf(stderr, "malformed RECV header, "
				"expected 'RECV <RANK>'\n");
			return;
		}
		post_mpi_recv_size(bev, rank);

	} else {
		fprintf(stderr, "error: unrecognized header '%s'\n",
			command.c_str());
	}

}

static inline void
init_read_handler(struct bufferevent *bev, void *arg)
{
	assert(bev != NULL);
	ConnectionState& state = get_connection_state(bev);
	if (state.mode == READING_COMMAND)
		process_next_header(bev);
}

static inline void
init_event_handler(struct bufferevent *bev, short error, void *arg)
{
	// we should never see this
	assert(!(error & BEV_EVENT_TIMEOUT));

	if (error & BEV_EVENT_EOF) {
		// connection closed
	} else if (error & BEV_EVENT_ERROR) {
		perror("libevent");
	}

	close_connection(bev);
}

static inline void
init_accept_handler(evutil_socket_t listener, short event, void *arg)
{
	// main state object for libevent
	struct event_base *base = (event_base*)arg;

	// connect to client (or die)
	evutil_socket_t fd = UnixSocket::accept(listener, false);
	printf("Connected to client.\n");

	// track state of connection in global map
	std::pair<ConnectionStateMap::iterator, bool>
		inserted = g_connectionStates.insert(
		std::make_pair(fd, ConnectionState()));
	assert(inserted.second);

	// create buffer and associate with new connection
	struct bufferevent* bev = bufferevent_socket_new(base,
		fd, BEV_OPT_CLOSE_ON_FREE);

	// set callbacks for buffer input/output
	bufferevent_setcb(bev, init_read_handler, NULL,
		init_event_handler, NULL);
	// set low/high watermarks for invoking callbacks
	bufferevent_setwatermark(bev, EV_READ, 0, MAX_READ_SIZE);
	// enable callbacks
	bufferevent_enable(bev, EV_READ|EV_WRITE);
}

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

	printf("Waiting for connection...\n");

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

	// use line buffering on stdout/stderr
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	// initialize MPI
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi::numProc);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi::rank);

	// start connection handling loop on Unix socket
	server_loop(argv[optind]);

	// shutdown MPI
	MPI_Finalize();

	return 0;
}

#endif
