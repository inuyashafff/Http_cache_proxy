#include "http.hpp"
#include "server.hpp"
#include "connection.hpp"

#include <functional>
#include <iostream>
#include <chrono>
#include <thread>
#include <exception>

#include <unistd.h>
#include <signal.h>

//CITE: some ideas borrowed from boost::asio http server example:
//http://www.boost.org/doc/libs/1_58_0/doc/html/boost_asio/example/cpp11/http/server/server.cpp
//specifically:
//  - basic class hierachy
//  - how to use asynchronous I/O
//  - how to handle signals.

namespace asio = boost::asio;
using asio::ip::tcp;

/** Construct a Server object.
 */
Server::Server(uint16_t port, size_t cache_size, log_type& log)
	: acceptor(io_service, tcp::endpoint(tcp::v4(), port))
	, signal_set(io_service)
	, id_counter(0)
	, resolver(io_service)
	, cache(cache_size)
	, log(log)
{
	using namespace std::placeholders;
	signal_set.add(SIGINT);
	signal_set.add(SIGTERM);
	signal_set.async_wait(std::bind(&Server::finish, this, _1, _2));
}

/** Run the server.
 * The server will spawn several work threads,
 * and start accepting connections.
 */
void Server::run()
{
	acceptor.listen();
	start_accept();

	log << "(no-id): NOTE server started" << std::endl;
	std::vector<std::thread> pool(4);
	for (auto& thrd : pool) {
		thrd = std::thread(std::bind(&Server::work_thread, this));
	}
	for (auto& thrd : pool) {
		thrd.join();
	}
}

/** Gracefully stop the server.
 */
void Server::finish(const boost::system::error_code&, int)
{
	acceptor.close();
	io_service.stop();
	log_guard l(log);
	log << "(no-id): NOTE server exited" << std::endl;
};

/** The procedure being run in a work thread.
 */
void Server::work_thread()
{
	while (!io_service.stopped()) {
		try {
			io_service.run();
		} catch (const std::exception& e) {
			log_guard l(log);
			log << "(no-id): ERROR " << e.what() << std::endl;
		}
	}
}

/** Create a new ClientConnection object
 * and start an asynchronous accept.
 * When a connection is accepted, the asynchronous handler
 * calls Server::start_accept() again.
 */
void Server::start_accept()
{
	auto conn = std::make_shared<ClientConnection>(*this);
	acceptor.async_accept(conn->socket,
		[=](const boost::system::error_code& ec)
		{
			conn->on_accept(ec);
			start_accept();
		});
}

/** Generate a new id.
 */
size_t Server::new_id()
{
	return id_counter.fetch_add(1) + 1;
	/* it's the same as
	 *     return ++id_counter;
	 * only that I promised not to write ++expr in another expression.
	 */
}
