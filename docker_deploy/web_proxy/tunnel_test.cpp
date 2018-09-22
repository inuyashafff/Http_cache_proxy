#include "tunnel.hpp"

namespace asio = boost::asio;
using asio::ip::tcp;

int main()
{
	asio::io_service io;
	tcp::socket a(io), b(io);

	a.connect(tcp::endpoint(tcp::v4(), 1234));
	b.connect(tcp::endpoint(tcp::v4(), 5678));
	auto tun = std::make_shared<Tunnel>(std::move(a), std::move(b));
	tun->start();
	io.run();
}
