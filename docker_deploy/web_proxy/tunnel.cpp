#include "tunnel.hpp"

using namespace std::placeholders;
namespace asio = boost::asio;
using asio::ip::tcp;

Tunnel::Tunnel(Server& server, size_t id,
	tcp::socket client, tcp::socket remote)
	: server(server)
	, socket{std::move(client), std::move(remote)}
	, id(id)
{
}

void Tunnel::start()
{
	activate(0);
	activate(1);
	running = true;
	Server::log_guard l(server.log);
	server.log << id << ": NOTE Tunnel established" << std::endl;
}

void Tunnel::stop()
{
	if (running) {
		running = false;
		socket[0].close();
		socket[1].close();
		Server::log_guard l(server.log);
		server.log << id
			<< ": Tunnel closed" << std::endl;
	}
}

void Tunnel::activate(int i)
{
	asio::async_read(socket[i], buffer[i],
		asio::transfer_at_least(1),
		std::bind(&Tunnel::on_read, shared_from_this(), i, _1, _2));
}

void Tunnel::on_read(int i, const boost::system::error_code& err, size_t n)
{
	if (err) {
		stop();
		return;
	}
	asio::async_write(socket[!i], buffer[i],
		std::bind(&Tunnel::on_write, shared_from_this(), !i, _1, _2));
}

void Tunnel::on_write(int i, const boost::system::error_code& err, size_t n)
{
	if (err) {
		stop();
		return;
	}
	activate(!i);
}
