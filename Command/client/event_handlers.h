#ifndef _CLIENT_EVENT_HANDLERS_H_
#define _CLIENT_EVENT_HANDLERS_H_

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <cassert>

static inline void
client_read_handler(struct bufferevent *bev, void *arg)
{
	const int MAX_LINE_SIZE = 256;

	struct event_base *base = (event_base*)arg;
	struct evbuffer* input = bufferevent_get_input(bev);

	size_t origLen = evbuffer_get_length(input);
	char* line = evbuffer_readln(input, NULL, EVBUFFER_EOL_LF);

	if (line != NULL) {
		puts(line);
		event_base_loopexit(base, NULL);
	} else if (origLen >= MAX_LINE_SIZE) {
		fprintf(stderr, "response line exceeded max length "
				"(%d bytes)\n", MAX_LINE_SIZE);
		bufferevent_free(bev);
	}

	free(line);
}

static inline void
client_event_handler(struct bufferevent *bev, short error, void *arg)
{
	// we should never see this
	assert(!(error & BEV_EVENT_TIMEOUT));

	if (error & BEV_EVENT_EOF) {
		// connection closed
	} else if (error & BEV_EVENT_ERROR) {
		perror("libevent");
	}

	bufferevent_free(bev);
}

#endif
