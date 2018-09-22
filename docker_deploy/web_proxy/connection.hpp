#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "http.hpp"
#include "server.hpp"
#include <boost/asio.hpp>
#include <memory>
#include <chrono>

/** The Connection class represents an HTTP connection.
 * It has an internal state so that it will parse the start line,
 * the header lines and the body.
 * It makes use of boost::asio asynchronous I/O.  The I/O functions
 * return immediately, and the handler will be called after
 * the operation is finished.
 */
class Connection : public std::enable_shared_from_this<Connection> {
protected:
	Server& server; /**< Reference to the server. */
	boost::asio::streambuf buffer; /**< Buffer for sending/receiving. */
	HttpParser parser; /**< HTTP Parser. */
	/** Unique ID of the request.
	 * Note: Several requests can be made within a single
	 * connection.  Therefore, this variable is mutable.
	 */
	size_t id;

	//CITE: the idea of this method is from
	//https://stackoverflow.com/a/47789633
	template <typename Derived>
	std::shared_ptr<Derived> shared_from(Derived *that)
	{
		return std::static_pointer_cast<Derived>(
			that->shared_from_this());
	}

	virtual bool handle_error(const boost::system::error_code&);
	/** Action when a bad header is encountered.
	 *
	 * Generally, a 400 or 502 should be sent,
	 * but _always_ to the client side.  As the behavior
	 * differs in derived classes, this method is pure virtual.
	 */
	virtual void bad_header(const char *) = 0;

	bool check_protocol(const std::string&);
	void on_read_header(const boost::system::error_code&);
	void read_body();
	void read_sized_body(size_t,
		std::function<void(const boost::system::error_code&, size_t)>);
	void read_chunk_header();
	void wait_header();
	void on_read_body(const boost::system::error_code&);
	void on_read_chunk_header(const boost::system::error_code&);
	virtual void on_read_done() = 0;
	virtual void on_write_done(const boost::system::error_code&, size_t) = 0;
	void replace_header(const std::string&, const std::string&);
	void replace_header(const std::string&, const CacheInfo::time_point&);

public:
	HttpMessage message; /**< Parsed message. */
	/** Socket for incoming connection. */
	boost::asio::ip::tcp::socket socket;

	Connection(Server& server);
	Connection(const Connection&) = delete;
	virtual void write_message(const HttpMessage&);
};

class RemoteConnection;

class ClientConnection : public Connection {
protected:
	bool tunneling = false;

	void bad_header(const char *) override;
	void on_read_done() override;
	void on_write_done(const boost::system::error_code&, size_t);
	void wait_header();
public:
	ClientConnection(Server&);
	void on_accept(const boost::system::error_code&);
	void check_cached(std::shared_ptr<Server::CacheItem>&,
		const RequestCacheInfo&);
	void write_message(const HttpMessage&) override;
};

class RemoteConnection : public Connection {
protected:
	std::shared_ptr<ClientConnection> client;
	std::string request_url, request_host;
	std::chrono::system_clock::time_point request_time, response_time;

	bool handle_error(const boost::system::error_code&) override;
	void bad_header(const char *) override;
	void on_resolve(const boost::system::error_code&,
			boost::asio::ip::tcp::resolver::iterator);
	void on_connect(const boost::system::error_code&);
	void on_read_done() override;
	void on_write_done(const boost::system::error_code&, size_t);
	bool is_cacheable(const std::string&, const std::string&,
			ResponseCacheInfo&);
	bool store_cache(const std::string&, const ResponseCacheInfo&);

public:
	RemoteConnection(Server&, size_t, std::shared_ptr<ClientConnection>);
	void start_resolve();
	void write_message(const HttpMessage&) override;
};

#endif /* CONNECTION_HPP */
