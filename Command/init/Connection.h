#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include "Command/init/log.h"
#include <mpi.h>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdarg>

// forward declarations
class Connection;
static inline void log_f(Connection& connection, const char* fmt, ...);
static inline void update_mpi_status(
	evutil_socket_t socket, short event, void* arg);
static inline void do_next_mpi_send(Connection& connection);

static inline bool mpi_ops_pending();

enum ConnectionState {
	READING_HEADER=0,
	MPI_READY_TO_RECV_CHUNK_SIZE,
	MPI_RECVING_CHUNK_SIZE,
	MPI_READY_TO_RECV_CHUNK,
	MPI_RECVING_CHUNK,
	MPI_READY_TO_SEND,
	MPI_SENDING_CHUNK,
	MPI_SENDING_EOF,
	MPI_FINALIZE,
	FLUSHING_SOCKET,
	DONE,
	CLOSED
};

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
	int chunk_size;
	/** chunk number we are currently sending/recving */
	size_t chunk_index;
	/** buffer for non-blocking MPI send/recv */
	char* chunk_buffer;
	/** bytes successfully transferred for current send/recv */
	size_t bytes_transferred;
	/** ID for checking state of asynchronous send/recv */
	MPI_Request chunk_size_request_id;
	/** ID for checking state of asynchronous send/recv */
	MPI_Request chunk_request_id;
	/** indicates Unix socket has been closed on remote end. */
	bool eof;
	/** timeout event (libevent) */
	struct event* next_event;

	Connection() :
		connection_id(next_connection_id),
		state(READING_HEADER),
		rank(0),
		socket(-1),
		bev(NULL),
		chunk_size(0),
		chunk_index(0),
		chunk_buffer(NULL),
		bytes_transferred(0),
		eof(false),
		next_event(NULL)
	{
		next_connection_id = (next_connection_id + 1) % SIZE_MAX;
		memset(&chunk_size_request_id, 0, sizeof(MPI_Request));
		memset(&chunk_request_id, 0, sizeof(MPI_Request));
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

	void clear_mpi_state()
	{
		if (chunk_buffer != NULL)
			free(chunk_buffer);
		chunk_buffer = NULL;
		chunk_size = 0;
		memset(&chunk_size_request_id, 0, sizeof(MPI_Request));
		memset(&chunk_request_id, 0, sizeof(MPI_Request));
	}

	void clear()
	{
		clear_mpi_state();
		state = READING_HEADER;
		rank = 0;
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
		state = CLOSED;
	}

	void printState()
	{
		printf("connection state:\n");
		printf("\tstate: %d\n", state);
		printf("\trank: %d\n", rank);
		printf("\tchunk_size: %d\n", chunk_size);
	}

	size_t id()
	{
		return connection_id;
	}

	bool mpi_ops_pending()
	{
		switch(state)
		{
			case MPI_READY_TO_RECV_CHUNK_SIZE:
			case MPI_READY_TO_RECV_CHUNK:
			case MPI_READY_TO_SEND:
			case MPI_RECVING_CHUNK_SIZE:
			case MPI_RECVING_CHUNK:
			case MPI_SENDING_CHUNK:
			case MPI_SENDING_EOF:
				return true;
			default:
				return false;
		}
	}

	std::string getState()
	{
		std::string s;
		switch (state) {
		case READING_HEADER:
			s = "READING_HEADER"; break;
		case MPI_READY_TO_RECV_CHUNK_SIZE:
			s = "MPI_READY_TO_RECV_CHUNK_SIZE"; break;
		case MPI_RECVING_CHUNK_SIZE:
			s = "MPI_RECVING_CHUNK_SIZE"; break;
		case MPI_READY_TO_RECV_CHUNK:
			s = "MPI_READY_TO_RECV_CHUNK"; break;
		case MPI_RECVING_CHUNK:
			s = "MPI_RECVING_CHUNK"; break;
		case MPI_READY_TO_SEND:
			s = "MPI_READY_TO_SEND"; break;
		case MPI_SENDING_CHUNK:
			s = "MPI_SENDING_CHUNK"; break;
		case MPI_SENDING_EOF:
			s = "MPI_SENDING_EOF"; break;
		case MPI_FINALIZE:
			s = "MPI_FINALIZE"; break;
		case FLUSHING_SOCKET:
			s = "FLUSHING_SOCKET"; break;
		case DONE:
			s = "DONE"; break;
		case CLOSED:
			s = "CLOSED"; break;
		}
		return s;
	}

	struct event_base* getBase()
	{
		assert(bev != NULL);
		struct event_base* base = bufferevent_get_base(bev);
		assert(base != NULL);
		return base;
	}

	size_t bytesReady()
	{
		assert(bev != NULL);
		struct evbuffer* input = bufferevent_get_input(bev);
		assert(input != NULL);
		return evbuffer_get_length(input);
	}

	void schedule_event(event_callback_fn callback,
		size_t microseconds)
	{
		if (next_event != NULL)
			event_free(next_event);

		assert(bev != NULL);
		assert(callback != NULL);
		struct event_base* base = bufferevent_get_base(bev);
		assert(base != NULL);
		struct timeval time;
		time.tv_sec = 0;
		time.tv_usec = microseconds;
		next_event = event_new(base, -1, 0, callback, this);
		event_add(next_event, &time);
	}

	/**
	 * Callback to update state of 'mpih finalize' command.
	 * If all pending MPI transfers have completed then
	 * shutdown the 'mpih init' daemon; if not, setup a
	 * a callback to check again later.
	 */
	void update_mpi_finalize_state()
	{
		assert(state == MPI_FINALIZE);

		if (::mpi_ops_pending()) {
			if (opt::verbose >= 3)
				log_f(*this, "waiting for pending MPI "
						"transfers to complete");
			schedule_event(update_mpi_status, 1000);
			return;
		}

		if (opt::verbose >= 2)
			log_f(*this, "pending MPI transfers complete. "
					"Shutting down!");

		event_base_loopexit(getBase(), NULL);
	}

	/**
	 * Callback to update state of 'mpih send' command.
	 */
	void update_mpi_send_state()
	{
		assert(state == MPI_SENDING_CHUNK);

		MPI_Status status;
		int completed;
		MPI_Test(&chunk_size_request_id, &completed, &status);

		if (opt::verbose >= 3) {
			log_f(*this, "%s: size of chunk #%lu to rank %d (%lu bytes)",
				completed ? "send completed" : "waiting on send",
				chunk_index, rank, chunk_size);
		}
		if (completed) {
			MPI_Test(&chunk_request_id, &completed, &status);
			if (opt::verbose >= 3) {
				log_f(*this, "%s: chunk #%lu to rank %d (%lu bytes)",
					completed ? "send completed" : "waiting on send",
					chunk_index, rank, chunk_size);
			}
		}
		if (completed) {
			bytes_transferred += chunk_size;
			if (opt::verbose >= 2)
				log_f(*this, "sent %lu bytes to rank %d so far",
					bytes_transferred, rank);
			clear_mpi_state();
			state = MPI_READY_TO_SEND;
			chunk_index++;
			if (eof || bytesReady() > 0)
				do_next_mpi_send(*this);
		} else {
			schedule_event(update_mpi_status, 1000);
		}
	}

private:

	/** next available connection id */
	static size_t next_connection_id;

	/** unique identifier for this connection */
	size_t connection_id;

};
size_t Connection::next_connection_id = 0;

typedef std::vector<Connection*> ConnectionList;
static ConnectionList g_connections;

static inline void
close_connection(Connection& connection)
{
	if (opt::verbose)
		log_f(connection, "closing connection");

	connection.close();

	ConnectionList::iterator it = std::find(
		g_connections.begin(), g_connections.end(),
		&connection);

	assert(it != g_connections.end());
	delete *it;
	g_connections.erase(it);
}

static inline void
close_all_connections()
{
	ConnectionList::iterator it = g_connections.begin();
	for (; it != g_connections.end(); ++it) {
		(*it)->close();
		delete *it;
	}
	g_connections.clear();
}

static inline bool mpi_ops_pending()
{
	ConnectionList::iterator it = g_connections.begin();
	for (; it != g_connections.end(); ++it) {
		assert(*it != NULL);
		if ((*it)->mpi_ops_pending())
			return true;
	}
	return false;
}

#endif
