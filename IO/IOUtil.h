#ifndef _IO_UTIL_H_
#define _IO_UTIL_H_

#include <cstring>
#include <fstream>
#include <iostream>

/** Print an error message and exit if stream is not good. */
static inline void assert_good(const std::ios& stream,
		const std::string& path)
{
	if (!stream.good()) {
		std::cerr << "error: `" << path << "': "
			<< strerror(errno) << std::endl;
		exit(EXIT_FAILURE);
	}
}

static inline void die(const char* msg) {
	std::cerr << msg;
	exit(EXIT_FAILURE);
}

static inline std::istream* open_istream(const std::string& path)
{
	std::istream* p;
	if (path == "-")
		return &std::cin;
	p = new std::ifstream(path.c_str());
	assert_good(*p, path);
	return p;
}

static inline std::ostream* open_ostream(const std::string& path)
{
	std::ostream* p;
	if (path == "-")
		return &std::cout;
	p = new std::ofstream(path.c_str());
	assert_good(*p, path);
	return p;
}

static inline void close_istream(std::istream* in, const std::string& path)
{
	if (path == "-")
		return;
	std::ifstream* ifs = static_cast<std::ifstream*>(in);
	ifs->close();
	delete ifs;
}

static inline void close_ostream(std::ostream* out, const std::string& path)
{
	if (path == "-")
		return;
	std::ofstream* ofs = static_cast<std::ofstream*>(out);
	ofs->close();
	delete ofs;
}
#endif
