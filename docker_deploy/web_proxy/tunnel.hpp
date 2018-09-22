#ifndef TUNNEL_HPP
#define TUNNEL_HPP

#include "server.hpp"

#include <boost/asio.hpp>
#include <memory>
#include <atomic>

class Tunnel : public std::enable_shared_from_this<Tunnel> {
	Server& server;
	boost::asio::streambuf buffer[2];
	boost::asio::ip::tcp::socket socket[2];
	size_t id;
	std::atomic<bool> running;

	void activate(int);
	void on_read(int, const boost::system::error_code&, size_t);
	void on_write(int, const boost::system::error_code&, size_t);
public:
	Tunnel(Server& server, size_t id,
		boost::asio::ip::tcp::socket client,
		boost::asio::ip::tcp::socket remote);
	void start();
	void stop();
};

#endif /* TUNNEL_HPP */
