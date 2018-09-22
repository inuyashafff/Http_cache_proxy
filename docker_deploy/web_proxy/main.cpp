#include "server.hpp"

#include <iostream>
#include <exception>
#include <unistd.h>
#include <errno.h>

#define LOG_FILENAME "/var/log/erss/proxy.log"
#define CACHE_ENTRIES 4096
#define PROXY_PORT 12345

int main()
{
	setenv("TZ", "UTC", 1);
	Server::log_type log;
	log.open(LOG_FILENAME);
	if (!log) {
		throw std::ios_base::failure("cannot open log");
	}
	// drop permission
	if (setgid(getgid()) == -1 || setuid(getuid()) == -1) {
		// fail
		return 1;
	}
	Server server(PROXY_PORT, CACHE_ENTRIES, log);
	server.run();
	return 0;
}
