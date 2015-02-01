#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include "Command/init/log.h"
#include <mpi.h>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdarg>

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
	/** buffer for non-blocking MPI send/recv */
	char* chunk_buffer;
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
		chunk_buffer(NULL),
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

private:

	/** next available connection id */
	static size_t next_connection_id;

	/** unique identifier for this connection */
	size_t connection_id;

};

// forward declaration
static inline void log_f(Connection& connection, const char* fmt, ...);

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
