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

enum ConnectionState {
	READING_COMMAND=0,
	MPI_READY_TO_RECV_MSG_SIZE,
	MPI_READY_TO_RECV_MSG,
	MPI_READY_TO_SEND,
	MPI_RECVING_MSG_SIZE,
	MPI_RECVING_MSG,
	MPI_SENDING_CHUNK,
	MPI_SENDING_EOF,
	CLOSED
};

#define MPI_BUFFER_SIZE 65536

struct Connection {

	/** connection state (e.g. sending data) */
	ConnectionState state;
	/** remote rank for sending/receiving data */
	int rank;
	/** socket (connects to local client) */
	evutil_socket_t socket;
	/** socket buffer (managed by libevent) */
	struct bufferevent* bev;
	/** length of MPI send/recv buffer */
	uint64_t chunk_size;
	/** buffer for non-blocking MPI send/recv */
	char* chunk_buffer;
	/** ID for checking state of asynchronous send/recv */
	MPI_Request chunk_size_request_id;
	/** ID for checking state of asynchronous send/recv */
	MPI_Request chunk_request_id;
	/** indicates Unix socket has been closed on remote end. */
	bool eof;

	Connection() :
		connection_id(next_connection_id),
		state(READING_COMMAND),
		rank(0),
		socket(-1),
		bev(NULL),
		chunk_size(0),
		chunk_buffer(NULL),
		chunk_size_request_id(0),
		chunk_request_id(0),
		eof(false)
	{
		next_connection_id = (next_connection_id + 1) % SIZE_MAX;
	}

	~Connection()
	{
		clear();
	}

	bool operator==(const Connection& connection)
	{
		return connection_id ==
			connection.connection_id;
	}

	void clear_mpi_buffer()
	{
		if (chunk_buffer != NULL)
			free(chunk_buffer);
		chunk_buffer = NULL;
		chunk_size = 0;
	}

	void clear()
	{
		clear_mpi_buffer();
		state = READING_COMMAND;
		rank = 0;
		chunk_size_request_id = 0;
		chunk_request_id = 0;
		eof = false;
	}

	void close()
	{
		if (socket != -1)
			evutil_closesocket(socket);
		socket = -1;
		if (bev != NULL)
			bufferevent_free(bev);
		bev = NULL;
		eof = true;
	}

private:

	/** next available connection id */
	static size_t next_connection_id;

	/** unique identifier for this connection */
	size_t connection_id;

};

size_t Connection::next_connection_id = 0;

typedef std::vector<Connection> ConnectionList;
ConnectionList g_connections;

#define MAX_READ_SIZE 16384
#define MAX_HEADER_SIZE 256
#define MPI_DEFAULT_TAG 0

static inline void
close_connection(Connection& connection)
{
	connection.close();

	ConnectionList::iterator it = std::find(
		g_connections.begin(), g_connections.end(),
		connection);

	assert(it != g_connections.end());

	g_connections.erase(it);
}

static inline bool
mpi_calls_pending(ConnectionState state)
{
	switch(state)
	{
	case MPI_READY_TO_RECV_MSG_SIZE:
	case MPI_READY_TO_RECV_MSG:
	case MPI_READY_TO_SEND:
	case MPI_RECVING_MSG_SIZE:
	case MPI_RECVING_MSG:
	case MPI_SENDING_CHUNK:
	case MPI_SENDING_EOF:
		return true;
	case READING_COMMAND:
	case CLOSED:
		return false;
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

// forward declarations
static inline void
process_next_header(Connection& connection);
static inline void post_mpi_recv_msg(Connection& connection);
static inline void post_mpi_send(Connection& connection);
static inline void mpi_send_eof(Connection& connection);
static inline void do_next_mpi_send(Connection& connection);

static inline void update_mpi_status(
	evutil_socket_t socket, short event, void* arg)
{
	const unsigned TIMER_SEC = 1;

	assert(arg != NULL);
	Connection& connection = *(Connection*)arg;

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);

	size_t bytes_ready = evbuffer_get_length(input);

	int completed = 0;
	if (connection.state == MPI_SENDING_CHUNK) {
		MPI_Test(&connection.chunk_size_request_id, &completed, MPI_STATUS_IGNORE);
		if (opt::verbose >= 3) {
			printf("chunk size %lu to rank %d: %s\n",
				connection.chunk_size, connection.rank,
				completed ? "sent successfully" : "in flight");
		}
		if (completed) {
			MPI_Test(&connection.chunk_request_id, &completed, MPI_STATUS_IGNORE);
			if (opt::verbose >= 3) {
				printf("chunk to rank %d (%lu bytes): %s\n",
					connection.rank, connection.chunk_size,
					completed ? "sent successfully" : "in flight");
			}
		}
		if (completed) {
			connection.clear_mpi_buffer();
			connection.state = MPI_READY_TO_SEND;
			if (bytes_ready > 0)
				do_next_mpi_send(connection);
			return;
		}
	} else if (connection.state == MPI_SENDING_EOF) {
		MPI_Test(&connection.chunk_size_request_id, &completed, MPI_STATUS_IGNORE);
		if (opt::verbose >= 3) {
			printf("EOF to rank %d: %s\n", connection.rank,
				completed ? "sent successfully" : "in flight");
		}
		if (completed) {
			close_connection(connection);
			return;
		}
	} else if (connection.state == MPI_RECVING_MSG_SIZE) {
		MPI_Test(&connection.chunk_size_request_id, &completed, MPI_STATUS_IGNORE);
		if (opt::verbose >= 3) {
			printf("chunk size from rank %d: %s\n", connection.rank,
				completed ? "received successfully" : "in flight");
		}
		if (completed) {
			if (connection.chunk_size > 0)
				post_mpi_recv_msg(connection);
			else
				process_next_header(connection);
			return;
		}
	} else if (connection.state == MPI_RECVING_MSG) {
		MPI_Test(&connection.chunk_request_id, &completed, MPI_STATUS_IGNORE);
		if (opt::verbose >= 3) {
			printf("chunk from rank %d (%lu bytes): %s\n",
				connection.rank, connection.chunk_size,
				completed ? "received successfully" : "in flight");
		}
		if (completed) {
			process_next_header(connection);
			return;
		}
	}

	// still waiting for current send/recv to complete;
	// check again in TIMER_SEC
	create_timer_event(base, update_mpi_status,
			(void*)&connection, TIMER_SEC);
}

static inline void mpi_send_eof(Connection& connection)
{
	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	assert(connection.state == MPI_READY_TO_SEND);

	connection.state = MPI_SENDING_EOF;
	connection.chunk_size = 0;

	if (opt::verbose >= 2)
		printf("sending EOF to rank %d\n", connection.rank);

	// send chunk size of zero to indicate EOF
	MPI_Isend(&connection.chunk_size, 1, MPI_UINT64_T,
		connection.rank, 0, MPI_COMM_WORLD,
		&connection.chunk_size_request_id);

	// check if MPI_Isend has completed
	update_mpi_status(socket, 0, (void*)&connection);
}

static inline void post_mpi_send(Connection& connection)
{
	assert(connection.state == MPI_READY_TO_SEND);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);

	uint64_t chunk_size = evbuffer_get_length(input);
	assert(chunk_size > 0);

	connection.state = MPI_SENDING_CHUNK;
	connection.chunk_size = chunk_size;
	connection.chunk_buffer = (char*)malloc(connection.chunk_size);
	assert(connection.chunk_buffer != NULL);

	// move data chunk from libevent buffer to MPI buffer
	int bytesRemoved = evbuffer_remove(input,
		(void*)connection.chunk_buffer, connection.chunk_size);
	assert(bytesRemoved == connection.chunk_size);

	if (opt::verbose >= 2)
		printf("sending message size %lu to rank %d\n",
			connection.chunk_size, connection.rank);

	// send chunk size in advance of data chunk
	MPI_Isend(&connection.chunk_size, 1, MPI_UINT64_T,
		connection.rank, 0, MPI_COMM_WORLD,
		&connection.chunk_size_request_id);

	if (opt::verbose >= 2)
		printf("sending message to rank %d (%lu bytes)\n",
				connection.rank, connection.chunk_size);

	// send message body
	MPI_Isend(connection.chunk_buffer, connection.chunk_size,
		MPI_BYTE, connection.rank, MPI_DEFAULT_TAG,
		MPI_COMM_WORLD, &connection.chunk_request_id);

	// check if MPI_Isend's have completed
	update_mpi_status(socket, 0, (void*)&connection);
}

static inline void do_next_mpi_send(Connection& connection)
{
	assert(connection.state == MPI_READY_TO_SEND);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	struct event_base* base = bufferevent_get_base(bev);

	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);

	uint64_t bytes_ready = evbuffer_get_length(input);

	if (connection.eof && bytes_ready == 0)
		mpi_send_eof(connection);
	else {
		assert(bytes_ready > 0);
		post_mpi_send(connection);
	}
}

static inline void post_mpi_recv_size(Connection& connection,
	int rank)
{
	assert(connection.state == READING_COMMAND);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	connection.state = MPI_RECVING_MSG_SIZE;
	connection.rank = rank;

	if (opt::verbose >= 2)
		printf("receiving message size from rank %d\n",
			connection.rank);

	// send message size in advance of message body
	MPI_Irecv(&connection.chunk_size, 1, MPI_UINT64_T,
		connection.rank, 0, MPI_COMM_WORLD,
		&connection.chunk_size_request_id);

	update_mpi_status(socket, 0, (void*)&connection);
}

static inline void post_mpi_recv_msg(Connection& connection)
{
	assert(connection.state == MPI_RECVING_MSG_SIZE);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	connection.state = MPI_RECVING_MSG;
	connection.chunk_buffer = (char*)malloc(connection.chunk_size);

	// send message body (a length of zero indicates EOF)
	if (connection.chunk_size > 0) {

		assert(connection.chunk_buffer != NULL);

		if (opt::verbose >= 2)
			printf("receiving message from rank %d (%lu bytes)\n",
				connection.rank, connection.chunk_size);

		MPI_Isend(connection.chunk_buffer, connection.chunk_size,
				MPI_BYTE, connection.rank, MPI_DEFAULT_TAG,
				MPI_COMM_WORLD, &connection.chunk_request_id);
		update_mpi_status(socket, 0, (void*)&connection);
	}
}

static inline char* read_header(Connection& connection)
{
	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);

	size_t len = evbuffer_get_length(input);
	char* line = evbuffer_readln(input, NULL, EVBUFFER_EOL_LF);

	if (line == NULL && len > MAX_HEADER_SIZE) {
		fprintf(stderr, "header line exceeded max length "
				"(%d bytes)\n", MAX_HEADER_SIZE);
		close_connection(connection);
	}

	return line;
}

static inline void
process_next_header(Connection& connection)
{
	connection.clear();
	connection.state = READING_COMMAND;

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	char* header = read_header(connection);

	// haven't fully received header line yet
	if (header == NULL)
		return;

	if (opt::verbose >= 2)
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
		ss >> rank;
		if (ss.fail() || !ss.eof()) {
			fprintf(stderr, "error: malformed SEND header, "
				"expected 'SEND <RANK>'\n");
			return;
		}

		connection.clear();
		connection.rank = rank;
		connection.state = MPI_READY_TO_SEND;

		struct evbuffer* input = bufferevent_get_input(bev);
		assert(input != NULL);

		if (evbuffer_get_length(input) > 0)
			post_mpi_send(connection);

	} else if (command == "RECV") {

		int rank;
		ss >> rank;
		if (ss.fail() || !ss.eof()) {
			fprintf(stderr, "error: malformed RECV header, "
				"expected 'RECV <RANK>'\n");
			return;
		}
		post_mpi_recv_size(connection, rank);

	} else {
		fprintf(stderr, "error: unrecognized header command '%s'\n",
			command.c_str());
	}

}

static inline void
init_read_handler(struct bufferevent *bev, void *arg)
{
	assert(arg != NULL);
	Connection& connection = *(Connection*)arg;

	if (connection.state == READING_COMMAND)
		process_next_header(connection);
	else if (connection.state == MPI_READY_TO_SEND)
		do_next_mpi_send(connection);
}

static inline void
init_event_handler(struct bufferevent *bev, short error, void *arg)
{
	// we are not using any timeouts
	assert(!(error & BEV_EVENT_TIMEOUT));

	assert(arg != NULL);
	Connection& connection = *(Connection*)arg;

	if (error & BEV_EVENT_EOF) {
		// client has closed socket
		connection.eof = true;
		// we may still have pending MPI sends
		if (connection.state == MPI_READY_TO_SEND) {
			do_next_mpi_send(connection);
			return;
		}
	} else if (error & BEV_EVENT_ERROR) {
		perror("libevent");
	}

	if (!mpi_calls_pending(connection.state))
		close_connection(connection);
}

static inline void
init_accept_handler(evutil_socket_t listener, short event, void *arg)
{
	// main state object for libevent
	struct event_base *base = (event_base*)arg;

	// connect to client (or die)
	evutil_socket_t fd = UnixSocket::accept(listener, false);
	if (opt::verbose)
		printf("Connected to client.\n");


	// create buffer and associate with new connection
	struct bufferevent* bev = bufferevent_socket_new(base, fd, 0);
	assert(bev != NULL);

	// track state of connection in global map
	g_connections.push_back(Connection());
	Connection& connection = g_connections.back();
	connection.bev = bev;
	connection.socket = fd;

	// set callbacks for buffer input/output
	bufferevent_setcb(bev, init_read_handler, NULL,
		init_event_handler, &connection);
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
