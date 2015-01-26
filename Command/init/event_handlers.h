#ifndef _EVENT_HANDLERS_H_
#define _EVENT_HANDLERS_H_

#include "Command/init/log.h"
#include "Command/init/mpi.h"
#include "IO/SocketUtil.h"
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <sstream>

#define MAX_HEADER_SIZE 256
#define MAX_BUFFER_SIZE 16384

/**
 * Becomes true if a client has issued
 * 'mpih finalize'. If true, the daemon will wait
 * for all existing data transfers will be completed
 * and then call MPI_Finalize. If a client tries to
 * initiate any new transfers (e.g. 'mpih send'),
 * an error will be raised.
 */
static bool g_finalize_pending = false;

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

static inline char* read_header(Connection& connection)
{
	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);

	size_t len = evbuffer_get_length(input);
	char* line = evbuffer_readln(input, NULL, EVBUFFER_EOL_LF);

	if (line == NULL && len > MAX_HEADER_SIZE) {
		log_f(connection, "header line exceeded max length "
				"(%d bytes)", MAX_HEADER_SIZE);
		close_connection(connection);
	}

	return line;
}

static inline void
process_next_header(Connection& connection)
{
	connection.clear();
	connection.state = READING_HEADER;

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	char* header = read_header(connection);

	// haven't fully received header line yet
	if (header == NULL)
		return;

	if (g_finalize_pending) {
		log_f(connection, "error, a client has attempted to issue commands "
			"after 'mpih finalize' has been called!: '%s'", header);
		exit(EXIT_FAILURE);
	}

	if (opt::verbose >= 2)
		log_f(connection, "received header line '%s'", header);

	std::stringstream ss(header);
	free(header);
	std::string command;
	ss >> command;

	// empty or all-whitespace header line
	if (command.empty())
		return;

	struct evbuffer* output = bufferevent_get_output(bev);
	assert(output != NULL);

	if (command == "RANK") {

		evbuffer_add_printf(output, "%d\n", mpi::rank);

	} else if (command == "SIZE") {
		
		evbuffer_add_printf(output, "%d\n", mpi::numProc);		
		
	} else if (command == "SEND") {

		int rank;
		ss >> rank;
		if (ss.fail() || !ss.eof()) {
			log_f(connection, "error: malformed SEND header, "
				"expected 'SEND <RANK>'");
			return;
		}

		connection.clear();
		connection.rank = rank;
		connection.state = MPI_READY_TO_SEND;

		struct evbuffer* input = bufferevent_get_input(bev);
		assert(input != NULL);

		if (evbuffer_get_length(input) > 0)
			mpi_send_chunk(connection);

	} else if (command == "RECV") {

		int rank;
		ss >> rank;
		if (ss.fail() || !ss.eof()) {
			log_f(connection, "error: malformed RECV header, "
				"expected 'RECV <RANK>'");
			return;
		}

		connection.clear();
		connection.rank = rank;
		connection.state = MPI_READY_TO_RECV_CHUNK_SIZE;

		mpi_recv_chunk_size(connection);

	} else if (command == "FINALIZE") {

		if (opt::verbose)
			log_f(connection, "preparing to shut down daemon...");

		g_finalize_pending = true;
		connection.state = MPI_FINALIZE;

		evutil_socket_t socket = bufferevent_getfd(bev);

		update_mpi_status(socket, 0, &connection);

	} else {
		log_f(connection, "error: unrecognized header command '%s'",
			command.c_str());
	}
}

static inline void
init_write_handler(struct bufferevent *unused, void *arg)
{
	assert(arg != NULL);
	Connection& connection = *(Connection*)arg;

	// for consistency, always use bev from connection
	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct evbuffer* output = bufferevent_get_output(bev);
	assert(output != NULL);

	if (connection.state == FLUSHING_SOCKET) {
		assert(evbuffer_get_length(output) == 0);
		close_connection(connection);
	}
}

static inline void
init_read_handler(struct bufferevent *bev, void *arg)
{
	assert(arg != NULL);
	Connection& connection = *(Connection*)arg;

	if (connection.state == READING_HEADER)
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
		if (opt::verbose >= 2)
			log_f(connection, "read EOF from client");
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

	if (!connection.mpi_ops_pending()) {
		log_f(connection, "closing connection from event handler");
		close_connection(connection);
	}
}

static inline void
init_accept_handler(evutil_socket_t listener, short event, void *arg)
{
	// main state object for libevent
	struct event_base *base = (event_base*)arg;

	// connect to client (or die)
	evutil_socket_t fd = UnixSocket::accept(listener, false);

	// create buffer and associate with new connection
	struct bufferevent* bev = bufferevent_socket_new(base, fd, 0);
	assert(bev != NULL);

	// track state of connection in global map
	Connection* connection = new Connection();
	g_connections.push_back(connection);
	connection->bev = bev;
	connection->socket = fd;

	if (opt::verbose)
		log_f(*connection, "opened connection to client");

	// set callbacks for buffer input/output
	bufferevent_setcb(bev, init_read_handler,
		init_write_handler, init_event_handler,
		connection);
	// set low/high watermarks for invoking callbacks
	bufferevent_setwatermark(bev, EV_READ, 0, MAX_BUFFER_SIZE);
	// enable callbacks
	bufferevent_enable(bev, EV_READ|EV_WRITE);
}

#endif
