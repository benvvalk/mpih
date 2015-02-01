#ifndef _INIT_MPI_H_
#define _INIT_MPI_H_

#include "Command/init/Connection.h"
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <cassert>

#define MPI_DEFAULT_TAG 0

static const unsigned TIMER_MICROSEC = 1000;

namespace mpi {
	int rank;
	int numProc;
}

// forward declarations
static inline void create_timer_event(struct event_base* base,
	void (*callback_func)(evutil_socket_t, short, void*),
	void* callback_arg, unsigned seconds);
static inline void update_mpi_status(
	evutil_socket_t socket, short event, void* arg);

static inline void mpi_send_eof(Connection& connection)
{
	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	assert(connection.state == MPI_READY_TO_SEND);

	connection.state = MPI_SENDING_EOF;
	connection.chunk_size = 0;

	if (opt::verbose >= 2)
		log_f(connection, "sending EOF to rank %d", connection.rank);

	// send chunk size of zero to indicate EOF
	MPI_Isend((void*)&connection.chunk_size, 1, MPI_INT,
		connection.rank, MPI_DEFAULT_TAG, MPI_COMM_WORLD,
		&connection.chunk_size_request_id);

	// check if MPI_Isend has completed
	update_mpi_status(socket, 0, (void*)&connection);
}

static inline void mpi_send_chunk(Connection& connection)
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
		log_f(connection, "sending size of chunk #%lu (%d bytes) to rank %d",
			connection.chunk_index, connection.chunk_size, connection.rank);

	// send chunk size in advance of data chunk
	MPI_Isend((void*)&connection.chunk_size, 1, MPI_INT,
		connection.rank, MPI_DEFAULT_TAG, MPI_COMM_WORLD,
		&connection.chunk_size_request_id);

	if (opt::verbose >= 2)
		log_f(connection, "sending chunk #%lu to rank %d (%d bytes)",
			connection.chunk_index, connection.rank, connection.chunk_size);

	// send message body
	MPI_Isend((void*)connection.chunk_buffer, connection.chunk_size,
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
		mpi_send_chunk(connection);
	}
}

static inline void mpi_recv_chunk_size(Connection& connection)
{
	assert(connection.state == MPI_READY_TO_RECV_CHUNK_SIZE);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	connection.state = MPI_RECVING_CHUNK_SIZE;

	if (opt::verbose >= 2)
		log_f(connection, "receiving size for chunk #%lu from rank %d",
			connection.chunk_index, connection.rank);

	// send message size in advance of message body
	MPI_Irecv((void*)&connection.chunk_size, 1, MPI_INT,
		connection.rank, MPI_DEFAULT_TAG, MPI_COMM_WORLD,
		&connection.chunk_size_request_id);

	update_mpi_status(socket, 0, (void*)&connection);
}

static inline void mpi_recv_chunk(Connection& connection)
{
	assert(connection.state == MPI_READY_TO_RECV_CHUNK);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	connection.state = MPI_RECVING_CHUNK;
	connection.chunk_buffer = (char*)malloc(connection.chunk_size);
	assert(connection.chunk_size > 0);
	assert(connection.chunk_buffer != NULL);

	if (opt::verbose >= 2)
		log_f(connection, "receiving chunk #%lu from rank %d (%d bytes)",
			connection.chunk_index, connection.rank, connection.chunk_size);

	MPI_Irecv((void*)connection.chunk_buffer, connection.chunk_size,
			MPI_BYTE, connection.rank, MPI_DEFAULT_TAG,
			MPI_COMM_WORLD, &connection.chunk_request_id);

	update_mpi_status(socket, 0, (void*)&connection);
}

static inline void update_mpi_status(
	evutil_socket_t socket, short event, void* arg)
{
	assert(arg != NULL);
	Connection& connection = *(Connection*)arg;

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);

	size_t bytes_ready = evbuffer_get_length(input);

	struct evbuffer* output = bufferevent_get_output(bev);
	assert(output != NULL);

	MPI_Status status;
	int count;
	int completed = 0;

	log_f(connection, "entering update_mpi_status with state %s",
		connection.getState().c_str());

	if (connection.state == MPI_FINALIZE) {
		connection.update_mpi_finalize_state();
		return;
	} else if (connection.state == MPI_SENDING_CHUNK) {
		connection.update_mpi_send_state();
		return;
	} else if (connection.state == MPI_SENDING_EOF) {
		connection.update_mpi_send_eof_state();
		return;
	} else if (connection.state == MPI_RECVING_CHUNK_SIZE) {
		MPI_Test(&connection.chunk_size_request_id, &completed, &status);
		if (opt::verbose >= 3) {
			log_f(connection, "%s: size of chunk #%lu from rank %d",
					completed ? "recv completed" : "waiting on recv",
					connection.chunk_index, connection.rank);
			if (completed) {
				connection.chunk_index++;
				MPI_Get_count(&status, MPI_INT, &count);
				assert(count == 1);
				log_f(connection, "size of chunk #%lu: %lu bytes",
					connection.chunk_index, connection.chunk_size);
			}
		}
		if (completed) {
			if (connection.chunk_size == 0) {
				if (opt::verbose >= 3)
					log_f(connection, "received EOF from rank %d",
						connection.rank);
				connection.state = FLUSHING_SOCKET;
				if (evbuffer_get_length(output) == 0)
					close_connection(connection);
			}
			else {
				connection.state = MPI_READY_TO_RECV_CHUNK;
				mpi_recv_chunk(connection);
			}
			return;
		}
	} else if (connection.state == MPI_RECVING_CHUNK) {
		MPI_Test(&connection.chunk_request_id, &completed, &status);
		if (opt::verbose >= 3) {
			log_f(connection, "%s: chunk #%lu from rank %d (%lu bytes)",
				completed ? "recv completed" : "waiting on recv",
				connection.chunk_index, connection.rank,
				connection.chunk_size);
			if (completed) {
				MPI_Get_count(&status, MPI_BYTE, &count);
				assert(count == connection.chunk_size);
				connection.bytes_transferred += connection.chunk_size;
				log_f(connection, "received %lu bytes from rank %d so far",
					connection.bytes_transferred, connection.rank);
			}
		}
		if (completed) {
			// copy recv'd data from MPI buffer to Unix socket
			assert(connection.chunk_size > 0);
			evbuffer_add(output, connection.chunk_buffer,
				connection.chunk_size);

			// clear MPI buffer and other state
			connection.clear_mpi_state();
			// post receive for size of next chunk
			connection.state = MPI_READY_TO_RECV_CHUNK_SIZE;
			mpi_recv_chunk_size(connection);

			return;
		}
	} else {
		log_f(connection, "illegal MPI state (%d) in timer event handler!",
			connection.state);
		exit(EXIT_FAILURE);
	}

	// still waiting for current send/recv to complete;
	// check again in TIMER_MICROSEC

	log_f(connection, "scheduling update_mpi_status call in "
		"%lu microseconds", TIMER_MICROSEC);
	connection.schedule_event(update_mpi_status, TIMER_MICROSEC);
}

#endif
