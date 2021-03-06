#ifndef _INIT_MPI_H_
#define _INIT_MPI_H_

#include "Command/init/Connection.h"
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <cassert>

#define MPI_DEFAULT_TAG 0

namespace mpi {
	int rank;
	int numProc;
}

// forward declaration
static inline void update_mpi_status(
	evutil_socket_t socket, short event, void* arg);

static inline void mpi_send_chunk_size(Connection& connection)
{
	assert(connection.state == MPI_READY_TO_SEND_CHUNK_SIZE);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);

	uint64_t chunk_size = evbuffer_get_length(input);
	if (connection.eof && chunk_size == 0) {
		if (opt::verbose) {
			log_f(connection.id(), "send to rank %d complete "
				"(%lu bytes)", connection.rank,
				connection.bytes_transferred);
			log_f(connection.id(), "sending EOF to rank %d",
				connection.rank);
		}
		connection.state = MPI_SENDING_EOF;
	} else {
		assert(chunk_size > 0);
		connection.state = MPI_SENDING_CHUNK_SIZE;
	}
	connection.chunk_size = chunk_size;

	if (opt::verbose >= 2)
		log_f(connection.id(), "sending size of chunk #%lu (%d bytes) to rank %d",
			connection.chunk_index, connection.chunk_size, connection.rank);

	// send chunk size in advance of data chunk
	MPI_Isend((void*)&connection.chunk_size, 1, MPI_INT,
		connection.rank, MPI_DEFAULT_TAG, MPI_COMM_WORLD,
		&connection.chunk_size_request_id);

	// check if MPI_Isend has completed
	update_mpi_status(socket, 0, (void*)&connection);
}

static inline void mpi_send_chunk(Connection& connection)
{
	assert(connection.state == MPI_READY_TO_SEND_CHUNK);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);

	// sanity check (we've already sent the chunk size to the receiver)
	assert(connection.chunk_size <= evbuffer_get_length(input));

	connection.state = MPI_SENDING_CHUNK;
	connection.chunk_buffer = (char*)malloc(connection.chunk_size);
	assert(connection.chunk_buffer != NULL);

	// move data chunk from libevent buffer to MPI buffer
	int bytesRemoved = evbuffer_remove(input,
		(void*)connection.chunk_buffer, connection.chunk_size);
	assert(bytesRemoved == connection.chunk_size);

	if (opt::verbose >= 2)
		log_f(connection.id(), "sending chunk #%lu to rank %d (%d bytes)",
			connection.chunk_index, connection.rank, connection.chunk_size);

	// send message body
	MPI_Isend((void*)connection.chunk_buffer, connection.chunk_size,
		MPI_BYTE, connection.rank, MPI_DEFAULT_TAG,
		MPI_COMM_WORLD, &connection.chunk_request_id);

	// check if MPI_Isend's have completed
	update_mpi_status(socket, 0, (void*)&connection);
}

static inline void mpi_recv_chunk_size(Connection& connection)
{
	assert(connection.state == MPI_READY_TO_RECV_CHUNK_SIZE);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	connection.state = MPI_RECVING_CHUNK_SIZE;

	if (opt::verbose >= 2)
		log_f(connection.id(), "receiving size for chunk #%lu from rank %d",
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
		log_f(connection.id(), "receiving chunk #%lu from rank %d (%d bytes)",
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

	if (opt::verbose >= 3)
		log_f(connection.id(), "entering update_mpi_status with state %s",
			connection.getState().c_str());

	if (connection.state == WAITING_FOR_MPI_CHANNEL) {
		connection.update_mpi_channel_state();
		return;
	} else if (connection.state == MPI_FINALIZE) {
		connection.update_mpi_finalize_state();
		return;
	} else if (connection.state == MPI_SENDING_CHUNK_SIZE) {
		connection.update_mpi_send_chunk_size_state();
		return;
	} else if (connection.state == MPI_SENDING_CHUNK) {
		connection.update_mpi_send_chunk_state();
		return;
	} else if (connection.state == MPI_SENDING_EOF) {
		connection.update_mpi_send_eof_state();
		return;
	} else if (connection.state == MPI_RECVING_CHUNK_SIZE) {
		connection.update_mpi_recv_chunk_size_state();
		return;
	} else if (connection.state == MPI_RECVING_CHUNK) {
		connection.update_mpi_recv_chunk_state();
		return;
	} else {
		log_f(connection.id(), "illegal MPI state (%d) in timer event handler!",
			connection.state);
		exit(EXIT_FAILURE);
	}
}

#endif
