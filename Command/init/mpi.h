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

// forward declarations

static inline void create_timer_event(struct event_base* base,
	void (*callback_func)(evutil_socket_t, short, void*),
	void* callback_arg, unsigned seconds);
static inline void process_next_header(Connection& connection);
static inline void post_mpi_recv_msg(Connection& connection);
static inline void mpi_send_chunk(Connection& connection);
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
	} else if (connection.state == MPI_RECVING_CHUNK_SIZE) {
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
	} else if (connection.state == MPI_RECVING_CHUNK) {
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
		mpi_send_chunk(connection);
	}
}

static inline void post_mpi_recv_size(Connection& connection,
	int rank)
{
	assert(connection.state == READING_HEADER);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	connection.state = MPI_RECVING_CHUNK_SIZE;
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
	assert(connection.state == MPI_RECVING_CHUNK);

	struct bufferevent* bev = connection.bev;
	assert(bev != NULL);

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	evutil_socket_t socket = bufferevent_getfd(bev);

	connection.state = MPI_RECVING_CHUNK;
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

#endif
