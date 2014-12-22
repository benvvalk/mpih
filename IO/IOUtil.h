#ifndef _IO_UTIL_H_
#define _IO_UTIL_H_

static inline void die(const char* msg) {
	std::cerr << msg;
	exit(EXIT_FAILURE);
}

#endif
