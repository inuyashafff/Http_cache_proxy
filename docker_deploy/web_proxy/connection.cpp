#include "connection.hpp"
#include "tunnel.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <ctime>
#include <chrono>
#include <iomanip>

using namespace std::placeholders; // _1, _2, etc.
namespace asio = boost::asio;
using asio::ip::tcp;

/** Stock HTTP 200 Response.
 */
static HttpMessage HTTP200 = {
	{ "HTTP/1.1", "200", "OK" }
};
/** Stock HTTP 400 Response.
 */
static HttpMessage HTTP400 = {
	{ "HTTP/1.1", "400", "Invalid Request" },
	{ { "Content-Length", "0" } },
};
/** Stock HTTP 502 Response.
 */
static HttpMessage HTTP502 = {
	{ "HTTP/1.1", "502", "Bad Gateway" },
	{ { "Content-Length", "0" } },
};

/** A convenience class for printing std::chrono::system_clock::time_point.
 *
 * @see operator <<(std::ostream&, const put_time&).
 */
struct put_time {
	std::tm tmb; /**< std::tm buffer. */
	/** The format string.
	 *
	 * Use the syntax for std::put_time.
	 *
	 * @note This is usually a static string.
	 * Be sure the string is still alive when the actual output
	 * takes place.
	 */
	const char *fmt;

	put_time(std::chrono::system_clock::time_point, const char * = "%c");
};

/** Constructor.
 *
 * Translate a std::chrono::system_clock::time_point value into
 * a std::tm value, and store in the object.
 *
 * @param tp A std::chrono::system_clock::time_point value.
 * @param fmt The format string.
 * @see fmt
 */
put_time::put_time(std::chrono::system_clock::time_point tp, const char *fmt)
	: fmt(fmt)
{
	std::time_t time = std::chrono::system_clock::to_time_t(tp);
	::gmtime_r(&time, &tmb);
}

/** Write formatted date and time to a std::ostream.
 *
 * std::put_time is used for std::tm to string conversion.
 *
 */
std::ostream& operator <<(std::ostream& os, const put_time& t)
{
	return os << std::put_time(&t.tmb, t.fmt);
}


/** Constructor.
 * @ref id is initialized to 0.
 * @note 0 is not considered a valid ID, and will be write
 * as `(no-id)` in the log.
 * @param server Reference to the server.
 */
Connection::Connection(Server& server)
	: server(server)
	, parser(message)
	, id(0)
	, socket(server.get_io_service())
{
}

/** Error handler.
 *
 * When an error occurred in an asynchronous I/O,
 * the handler will be passed a error_code which evaluate to true.
 * To simplify error handling in each of the asynchronous handlers,
 * they all call this method.
 * If this method returns true, the asynchronous handlers will
 * return, and subsequent I/O operations will not take place.
 * Eventually, the Connection object itself is destructed.
 *
 * @param err The error code.
 * @returns true when there is an error.
 * @returns false when there is no error.
 */
bool Connection::handle_error(const boost::system::error_code& err)
{
	if (err) {
		Server::log_guard l(server.log);
		if (id == 0) {
			server.log << "(no-id)";
		} else {
			server.log << id;
		}
		if (err == asio::error::eof) {
			server.log << ": NOTE connection closed";
		} else {
			server.log << ": ERROR " << err.message();
		}
		server.log << std::endl;
		return true;
	}
	return false;
}

/** Check if the request/response contains a correct protocol.
 *
 * Currently we only acknowledge HTTP 1.0 and 1.1.
 *
 * @param protocol the protocol string.
 * @returns true if the protocol is valid.
 * @returns false if the protocol is valid.
 */
bool Connection::check_protocol(const std::string& protocol)
{
	if (protocol != "HTTP/1.0" && protocol != "HTTP/1.1") {
		bad_header("unsupported protocol");
		return false;
	}
	return true;
}

/** Handler when a start line/header line is read.
 *
 * The line read is sent to the parser.
 * After that, depending on the parser's state,
 * another asynchronous read for a header line or body will
 * be made, or, if the message has no body, @ref on_read_done
 * will be called.
 *
 * @param err Error code.
 */
void Connection::on_read_header(const boost::system::error_code& err)
{
	if (handle_error(err)) {
		return;
	}
	try {
		std::istream is(&buffer);
		parser.parseStep(is);
	} catch (const ParseError& e) {
		bad_header(e.what());
		return;
	}
	switch (parser.status()) {
	case HttpParser::BODY:
		read_body();
		break;
	case HttpParser::ACCEPT:
		on_read_done();
		break;
	case HttpParser::START:
	case HttpParser::HEADER:
		asio::async_read_until(socket, buffer, "\r\n",
			std::bind(&Connection::on_read_header, shared_from(this), _1));
		break;
	}
}

/** Initiate an asynchronous read for HTTP body.
 * @see on_read_body.
 */
void Connection::read_body()
{
	auto handler = std::bind(&Connection::on_read_body,
			shared_from(this), _1);
	switch (parser.format) {
	case HttpParser::PLAIN:
		asio::async_read(socket, buffer,
			asio::transfer_all(), handler);
		break;
	case HttpParser::LENGTH:
		read_sized_body(parser.contentLength, handler);
		break;
	case HttpParser::CHUNKED:
		read_chunk_header();
		break;
	}
}

/** Initiate an asynchronous read for a "sized body".
 *
 * The number of bytes to transfer is known.
 * These bytes will be appended to the message body.
 *
 * @param size Bytes to read.
 * @param handler Asynchronous handler.
 * @see read_body.
 */
void Connection::read_sized_body(size_t size,
	std::function<void(const boost::system::error_code&, size_t)> handler)
{
	size_t to_transfer = size - std::min(size, buffer.size());
	asio::async_read(socket, buffer,
		asio::transfer_exactly(to_transfer), handler);
}

/** Initiate an asynchronous read for a chunk header.
 *
 * This is used for the "chunked" encoding.
 *
 * @see on_read_chunk_header.
 */
void Connection::read_chunk_header()
{
	asio::async_read_until(socket, buffer, "\r\n",
		std::bind(&Connection::on_read_chunk_header,
			shared_from(this), _1));
}

/** Handler for reading a chunk header.
 *
 * The chunk header will be sent to the parser.
 * Then, depending on the parser's state,
 * it will call one of the following:
 *   - @ref on_read_done (when the message is complete);
 *   - @ref read_sized_body (when there are certain bytes
 *     to be read);
 *   - @ref read_chunk_header (when another chunk header follows).
 *
 * @param err Error code.
 */
void Connection::on_read_chunk_header(const boost::system::error_code& err)
{
	if (handle_error(err)) {
		return;
	}
	std::istream is(&buffer);
	parser.parseStep(is);
	if (parser.status() == HttpParser::ACCEPT) {
		on_read_done();
	} else if (parser.contentLength > 0) {
		read_sized_body(parser.contentLength,
			std::bind(&Connection::on_read_body,
				shared_from(this), _1));
	} else {
		read_chunk_header();
	}
}

/** Handler for reading the message body.
 * @param err Error code.
 */
void Connection::on_read_body(const boost::system::error_code& err)
{
	if (handle_error(err) && err != asio::error::eof) {
		return;
	}
	std::istream is(&buffer);
	parser.parseStep(is);
	if (parser.status() == HttpParser::ACCEPT || err == asio::error::eof) {
		on_read_done();
	} else {
		if (parser.format == HttpParser::CHUNKED) {
			read_chunk_header();
		} else {
			asio::async_read(socket, buffer,
				asio::transfer_at_least(1),
				std::bind(&Connection::on_read_body,
					shared_from(this), _1));
		}
	}
}

/** Write an HTTP message to @ref socket.
 *
 * It will first fill @ref buffer with the message,
 * then initiate an asynchronous write.
 *
 * @note It's OK to change message after this function
 * returns because the byte string to be sent is stored
 * in the buffer.
 * However, it's not safe to call this function
 * again before the asynchronous write completes
 * because the buffer will be overwritten.
 *
 * @param message The HTTP message to be sent.
 */
void Connection::write_message(const HttpMessage& message)
{
	std::ostream os(&buffer);
	os << message;
	asio::async_write(socket, buffer,
		std::bind(&Connection::on_write_done,
			shared_from(this), _1, _2));
}

/** Wait for a new HTTP message;
 *
 * Wait for a line of input.  The handler is @ref on_read_header.
 *
 * @note The parser state is reset, and message is cleared.
 */
void Connection::wait_header()
{
	parser.reset();
	asio::async_read_until(socket, buffer, "\r\n",
		std::bind(&ClientConnection::on_read_header,
			shared_from(this), _1));
}

/** Replace a header field in the message.
 *
 * @param key Key of the header field.
 * @param value New value for the field.
 */
void Connection::replace_header(const std::string& key, const std::string& value)
{
	for (auto& h : message.headerLines) {
		if (h.key == key) {
			h.value = value;
			return;
		}
	}
	message.headerLines.push_back({key, value});
}

/** Replace a header field in the message.
 *
 * This is for replacing a date field.
 *
 * @param key Key of the header field.
 * @param tp New value (a time point) for the field.
 */
void Connection::replace_header(const std::string& key,
	const CacheInfo::time_point& tp)
{
	std::stringstream ss;
	ss << tp;
	replace_header(key, ss.str());
}


/** Constructor.
 *
 * @param server Reference to the server.
 */
ClientConnection::ClientConnection(Server& server)
	: Connection(server)
{
}

/** Handler for a bad header line.
 *
 * An HTTP 400 will be sent to the client (this side).
 * A log entry will also be written.
 *
 * @param what What makes the header bad.
 */
void ClientConnection::bad_header(const char *what)
{
	{
		Server::log_guard l(server.log);
		server.log << id << ": ERROR " << what << std::endl;
	}
	write_message(HTTP400);
}

/** Handler for accepting an connection.
 * This method simply calls @ref wait_header.
 */
void ClientConnection::on_accept(const boost::system::error_code& err)
{
	if (handle_error(err)) {
		return;
	}
	wait_header();
}

/** Wait for a new HTTP message.
 * On top of Connection::wait_header, a new ID is assigned
 * to the current connection because we are going to receive
 * a new request.
 */
void ClientConnection::wait_header()
{
	id = server.new_id();
	Connection::wait_header();
}

/** Handler when the entire message is read.
 *
 * If the request method is GET, the cache will be examined,
 * and a stored response will be sent back without contacting
 * the origin server, if appropriate.
 * Otherwise, a RemoteConnection (with the same ID) is created.
 * It will connect to the origin server and fetch the content.
 */
void ClientConnection::on_read_done()
{
	if (!check_protocol(message.startLine[2])) {
		return;
	}
	{
		auto now = std::chrono::system_clock::now();
		Server::log_guard l(server.log);
		server.log << id
			<< ": \"" << message.startLine
			<< "\" from " << socket.remote_endpoint().address()
			<< " @ " << put_time(now) << std::endl;
	}
	tunneling = message.startLine[0] == "CONNECT";
	RequestCacheInfo ci;
	parser.parseRequestCacheInfo(ci);
	std::shared_ptr<Server::CacheItem> cached;
	if (message.startLine[0] == "GET") {
		const std::string& url = message.startLine[1];
		Server::cache_type::Accessor acc(server.cache, url);
		auto result = acc.get();
		if (result.first != url || !result.second) {
			Server::log_guard l(server.log);
			server.log << id << ": not in cache" << std::endl;
		} else {
			cached = result.second;
		}
	}
	if (cached) {
		check_cached(cached, ci);
	} else {
		// don't forward browser's caching-related fields
		// so that I will not be receive a 304 while not caching anything
		HttpMessage::HeaderLines::iterator it, next;
		for (it = message.headerLines.begin();
				it != message.headerLines.end(); it = next) {
			next = it;
			++next;
			if (it->key == "If-Modified-Since"
					|| it->key == "If-None-Match") {
				message.headerLines.erase(it);
			}
		}
	}
	if (cached) {
		write_message(cached->message);
	} else {
		auto remote = std::make_shared<RemoteConnection>(
				server, id, shared_from(this));
		remote->start_resolve();
	}
}


/** Check if the cached response needs revalidation.
 * @param cached Smart pointer to Server::CacheItem
 * (the entry to be examined).
 * @param ci Reference to RequestCacheInfo
 * (extracted from @ref message).
 */
void ClientConnection::check_cached(
	std::shared_ptr<Server::CacheItem>& cached,
	const RequestCacheInfo& ci)
{
	ResponseCacheInfo& ri = cached->info;
	if (ci.no_cache || ri.no_cache) {
		Server::log_guard l(server.log);
		server.log << id << ": in cache, requires validation"
			<< std::endl;
		cached.reset();
	} else {
		using std::chrono::system_clock;
		auto current_age = cached->info.current_age();
		auto lifetime = cached->info.freshness_lifetime;
		if (current_age >= lifetime) { // expired
			auto now = system_clock::now();
			auto expire_time = now - current_age + lifetime;
			Server::log_guard l(server.log);
			server.log << id << ": in cache, but expired at "
				<< put_time(expire_time) << std::endl;
			cached.reset();
		}
	}
	if (!cached) {
		// need to send request to origin
		// modify the header field with values in the cache
		if (ri.last_modified) {
			replace_header("If-Modified-Since", *ri.last_modified);
		}
		if (!ri.etag.empty()) {
			replace_header("If-None-Match", ri.etag);
		}
	} else {
		Server::log_guard l(server.log);
		server.log << id << ": in cache, valid" << std::endl;
	}
}

void ClientConnection::write_message(const HttpMessage& message)
{
	{
		Server::log_guard l(server.log);
		server.log << id << ": Responding \""
			<< message.startLine << "\"" << std::endl;
	}
	Connection::write_message(message);
}

void ClientConnection::on_write_done(
	const boost::system::error_code& err,
	size_t bytes_written)
{
	if (handle_error(err)) {
		return;
	}
	if (!tunneling) {
		wait_header();
	}
}

RemoteConnection::RemoteConnection(
	Server& server, size_t id,
	std::shared_ptr<ClientConnection> client)
	: Connection(server)
	, client(client)
{
	this->id = id;
}

bool RemoteConnection::handle_error(const boost::system::error_code& err)
{
	if (err) {
		Connection::handle_error(err);
		if (err != asio::error::eof) {
			client->write_message(HTTP502);
		}
		return true;
	}
	return false;
}

void RemoteConnection::bad_header(const char *what)
{
	{
		Server::log_guard l(server.log);
		server.log << id << ": ERROR " << what << std::endl;
	}
	client->write_message(HTTP502);
}

void RemoteConnection::start_resolve()
{
	auto& s = client->message.startLine[1];
	request_url = s;
	HttpUrl url(s);
	s = url.path; // modifies message
	request_host = url.host;
	server.resolver.async_resolve({url.host, url.port},
		std::bind(&RemoteConnection::on_resolve, shared_from(this), _1, _2));
}

void RemoteConnection::on_resolve(
	const boost::system::error_code& err,
	tcp::resolver::iterator it)
{
	if (handle_error(err)) {
		return;
	}
	asio::async_connect(socket, it,
		std::bind(&RemoteConnection::on_connect,
			shared_from(this), _1));
}

void RemoteConnection::on_connect(const boost::system::error_code& err)
{
	if (handle_error(err)) {
		return;
	}
	if (client->message.startLine[0] == "CONNECT") {
		client->write_message(HTTP200);
		auto tun = std::make_shared<Tunnel>(
			server,
			id,
			std::move(client->socket),
			std::move(socket));
		tun->start();
	} else {
		request_time = std::chrono::system_clock::now();
		write_message(client->message);
	}
}

/** We do not put a message whose body size is larger than
 * MAX_CACHEABLE_BODYSIZE in the cache
 */
#define MAX_CACHEABLE_BODYSIZE (2*1024*1024)

bool RemoteConnection::is_cacheable(
	const std::string& request_method,
	const std::string& status,
	ResponseCacheInfo& ci)
{
	if (request_method != "GET") {
		Server::log_guard l(server.log);
		server.log << id << ": not cachable because request method is "
			<< request_method << std::endl;
		return false;
	}
	if (status != "200" && status != "304") {
		Server::log_guard l(server.log);
		server.log << id << ": not cachable because status code is "
			<< status << std::endl;
		return false;
	}
	if (message.body.size() > MAX_CACHEABLE_BODYSIZE) {
		Server::log_guard l(server.log);
		server.log << id << ": not cachable because body size is "
			"larger than " << MAX_CACHEABLE_BODYSIZE << std::endl;
		return false;
	}
	if (!parser.parseResponseCacheInfo(ci, request_time, response_time)) {
		Server::log_guard l(server.log);
		server.log << id << ": not cachable because "
			"the response does not have a Date field."
			<< std::endl;
		return false;
	}
	if (ci.no_store || ci.private_) {
		Server::log_guard l(server.log);
		server.log << id << ": not cachable because "
			"no-store and/or private is set"
			" in Cache-Control" << std::endl;
		return false;
	}
	return true;
}

bool RemoteConnection::store_cache(const std::string& status,
	const ResponseCacheInfo& ci)
{
	Server::cache_type::Accessor acc(server.cache, request_url);
	auto result = acc.get();
	if (status == "200") {
		// overwrite the cache
		auto ptr = std::make_shared<Server::CacheItem>(
			Server::CacheItem{std::move(message), std::move(ci) });
		acc.set(ptr);
	} else if (result.first == request_url) {
		auto ptr = result.second;
		// replace header lines and ResponseCacheInfo
		// only if the URL matches.
		ptr->message.headerLines = std::move(message.headerLines);
		ptr->info = ci;
	} else {
		// there is a hashing conflict and the stored response
		// is not for this URL.
		Server::log_guard l(server.log);
		server.log << id << ": not cachable because "
			"the response is 304 and previous cache "
			"does not exist";
		return false;
	}
	return true;
}

void RemoteConnection::on_read_done()
{
	if (!check_protocol(message.startLine[0])) {
		return;
	}
	if (parser.status() < HttpParser::BODY) {
		// I kind of accept truncated body
		Server::log_guard l(server.log);
		server.log << id << ": ERROR incomplete response." << std::endl;
		client->write_message(HTTP502);
		return;
	}
	response_time = std::chrono::system_clock::now();
	{
		Server::log_guard l(server.log);
		server.log << id << ": Received \"" << message.startLine
			<< "\" from " << request_host << std::endl;
	}
	const auto request_method = client->message.startLine[0];
	const auto& status = message.startLine[1];
	client->write_message(message);
	ResponseCacheInfo ci;
	bool cached = is_cacheable(request_method, status, ci)
		&& store_cache(status, ci);
	if (cached) {
		Server::log_guard l(server.log);
		server.log << id << ": cached, ";
		// I do not bother with must-revalidate here
		// because I'll never send a stale response
		if (ci.no_cache) {
			server.log << "but requires re-validation";
		} else {
			auto expire = response_time + ci.freshness_lifetime;
			server.log << "expires at " << put_time(expire);
		}
		server.log << std::endl;
	}
}

void RemoteConnection::write_message(const HttpMessage& message)
{
	{
		Server::log_guard l(server.log);
		server.log << id << ": Requesting \"" << message.startLine
			<< "\" from " << request_host << std::endl;
	}
	Connection::write_message(message);
}

void RemoteConnection::on_write_done(
	const boost::system::error_code& err,
	size_t bytes_written)
{
	if (handle_error(err)) {
		return;
	}
	wait_header();
}
