#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <mpi.h>
#include <vector>
#include <algorithm>
#include <cassert>

enum ConnectionState {
	READING_HEADER=0,
	MPI_READY_TO_RECV_CHUNK_SIZE,
	MPI_RECVING_CHUNK_SIZE,
	MPI_READY_TO_RECV_CHUNK,
	MPI_RECVING_CHUNK,
	MPI_READY_TO_SEND,
	MPI_SENDING_CHUNK,
	MPI_SENDING_EOF,
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
		state(READING_HEADER),
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
		state = READING_HEADER;
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
			case READING_HEADER:
			case CLOSED:
				return false;
		}
	}

private:

	/** next available connection id */
	static size_t next_connection_id;

	/** unique identifier for this connection */
	size_t connection_id;

};

size_t Connection::next_connection_id = 0;

typedef std::vector<Connection> ConnectionList;
static ConnectionList g_connections;

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

#endif
