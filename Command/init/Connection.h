#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include "Command/init/log.h"
#include "Command/init/MPIChannel.h"
#include <mpi.h>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

// forward declarations
class Connection;
static inline void update_mpi_status(
	evutil_socket_t socket, short event, void* arg);
static inline void do_next_mpi_send(Connection& connection);
static inline void close_connection(Connection& connection);
static inline void mpi_send_chunk(Connection& connection);
static inline void mpi_recv_chunk(Connection& connection);
static inline void mpi_recv_chunk_size(Connection& connection);
static inline bool mpi_ops_pending();

enum ConnectionState {
	READING_HEADER=0,
	WAITING_FOR_MPI_CHANNEL,
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
	/**
	  * The "MPI channel" used by a MPI SEND/RECV stream.
	  * Each MPI channel consists of a transfer direction
	  * (SEND/RECV), a peer MPI rank, and an MPI message tag.
	  */
	MPIChannel channel;
	/** true when we are holding an MPI channel */
	bool holding_mpi_channel;

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
		next_event(NULL),
		holding_mpi_channel(false)
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
		/*
		 * If we are currently using an MPI channel,
		 * release for use by other mpih clients.
		 */
		if (holding_mpi_channel) {
			MPIChannelManager& manager =
				MPIChannelManager::getInstance();
			manager.releaseChannel(connection_id,
				channel);
			holding_mpi_channel = false;
		}
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
			case WAITING_FOR_MPI_CHANNEL:
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
		case WAITING_FOR_MPI_CHANNEL:
			s = "WAITING_FOR_MPI_CHANNEL"; break;
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

	struct evbuffer* getOutputBuffer()
	{
		assert(bev != NULL);
		struct evbuffer* output = bufferevent_get_output(bev);
		assert(output != NULL);
		return output;
	}

	size_t bytesQueued()
	{
		return evbuffer_get_length(getOutputBuffer());
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
	 * Callback to update state when we are waiting
	 * for an MPI channel in order to do a SEND/RECV.
	 */
	void update_mpi_channel_state()
	{
		MPIChannelManager& manager = MPIChannelManager::getInstance();
		ChannelRequestResult result = manager.requestChannel(
			connection_id, channel);
		if (result == QUEUED) {
			schedule_event(update_mpi_status, 1000);
			return;
		}
		assert(result == GRANTED);
		holding_mpi_channel = true;
		if (channel.m_xferDir == SEND) {
			state = MPI_READY_TO_SEND;
			if (bytesReady() > 0)
				mpi_send_chunk(*this);
		} else {
			assert(channel.m_xferDir == RECV);
			state = MPI_READY_TO_RECV_CHUNK_SIZE;
			mpi_recv_chunk_size(*this);
		}
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
				log_f(connection_id, "waiting for pending MPI "
						"transfers to complete");
			schedule_event(update_mpi_status, 1000);
			return;
		}

		if (opt::verbose >= 2)
			log_f(connection_id, "pending MPI transfers complete. "
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
			log_f(connection_id, "%s: size of chunk #%lu to rank %d (%lu bytes)",
				completed ? "send completed" : "waiting on send",
				chunk_index, rank, chunk_size);
		}
		if (completed) {
			MPI_Test(&chunk_request_id, &completed, &status);
			if (opt::verbose >= 3) {
				log_f(connection_id, "%s: chunk #%lu to rank %d (%lu bytes)",
					completed ? "send completed" : "waiting on send",
					chunk_index, rank, chunk_size);
			}
		}
		if (completed) {
			bytes_transferred += chunk_size;
			if (opt::verbose >= 2)
				log_f(connection_id, "sent %lu bytes to rank %d so far",
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

	/**
	 * Callback to update MPI state when sending EOF
	 * to remote rank. EOF is signaled by a chunk size
	 * of zero.
	 */
	void update_mpi_send_eof_state()
	{
		assert(state == MPI_SENDING_EOF);

		MPI_Status status;
		int completed;
		MPI_Test(&chunk_size_request_id, &completed, &status);

		if (opt::verbose >= 3) {
			log_f(connection_id, "%s: EOF to rank %d",
				completed ? "send completed" : "waiting on send",
				rank);
			if (completed)
				log_f(connection_id, "sent %lu bytes to rank %d so far",
					bytes_transferred, rank);
		}
		if (completed) {
			log_f(connection_id, "closing connection from mpi handler");
			close_connection(*this);
		} else {
			schedule_event(update_mpi_status, 1000);
		}
	}

	/**
	 * Callback to update state when receiving size of
     * next data chunk.
	 */
	void update_mpi_recv_chunk_size_state()
	{
		assert(state == MPI_RECVING_CHUNK_SIZE);

		MPI_Status status;
		int completed;
		MPI_Test(&chunk_size_request_id, &completed, &status);

		if (opt::verbose >= 3) {
			log_f(connection_id, "%s: size of chunk #%lu from rank %d",
					completed ? "recv completed" : "waiting on recv",
					chunk_index, rank);
		}

		if (completed) {
			chunk_index++;
			int count;
			MPI_Get_count(&status, MPI_INT, &count);
			assert(count == 1);
			if (chunk_size == 0) {
				if (opt::verbose >= 3)
					log_f(connection_id, "received EOF from rank %d", rank);
				state = FLUSHING_SOCKET;
				if (bytesQueued() == 0)
					close_connection(*this);
			}
			else {
				if (opt::verbose >= 3) {
					log_f(connection_id, "size of chunk #%lu: %lu bytes",
						chunk_index, chunk_size);
				}
				state = MPI_READY_TO_RECV_CHUNK;
				mpi_recv_chunk(*this);
			}
		}

		if (!completed)
			schedule_event(update_mpi_status, 1000);
	}

	/** Callback to update state when receiving data chunk */

	void update_mpi_recv_chunk_state()
	{
		assert(state == MPI_RECVING_CHUNK);

		MPI_Status status;
		int completed;
		MPI_Test(&chunk_request_id, &completed, &status);

		if (opt::verbose >= 3) {
			log_f(connection_id, "%s: chunk #%lu from rank %d (%lu bytes)",
				completed ? "recv completed" : "waiting on recv",
				chunk_index, rank, chunk_size);
		}
		if (completed) {
			int count;
			MPI_Get_count(&status, MPI_BYTE, &count);
			assert(count == chunk_size);
			bytes_transferred += chunk_size;
			if (opt::verbose >= 3) {
				log_f(connection_id, "received %lu bytes from rank %d so far",
					bytes_transferred, rank);
			}
			// copy recv'd data from MPI buffer to Unix socket
			assert(chunk_size > 0);
			evbuffer_add(getOutputBuffer(), chunk_buffer,
				chunk_size);
			// clear MPI buffer and other state
			clear_mpi_state();
			// post receive for size of next chunk
			state = MPI_READY_TO_RECV_CHUNK_SIZE;
			mpi_recv_chunk_size(*this);
		}
		if (!completed)
			schedule_event(update_mpi_status, 1000);
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
		log_f(connection.id(), "closing connection");

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
