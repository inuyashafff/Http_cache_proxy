#ifndef SERVER_HPP
#define SERVER_HPP

#include <boost/asio.hpp>
#include <memory>
#include <fstream>
#include <atomic>

#include "http.hpp"
#include "cache.hpp"
#include "mutex.hpp"

/** A caching web server.
 */
class Server {
	/** The io_service object used by all
	 * boost::asio objects.
	 */
	boost::asio::io_service io_service;
	/** acceptor can accept connections.
	 */
	boost::asio::ip::tcp::acceptor acceptor;
	/** The set of signals that the server will catch.
	 * On catching any of these signals,
	 * the server will gracefully exit.
	 */
	boost::asio::signal_set signal_set;
	/** The counter for generating unique IDs.
	 * It is a size_t wrapped by std::atomic
	 * because multiple threads will be incrementing
	 * it simultaneously.
	 */
	std::atomic<size_t> id_counter;
public:
	/** Item to be stored in the cache. */
	struct CacheItem {
		HttpMessage message;
		ResponseCacheInfo info;
	};
	/** Type of the cache. */
	typedef Cache<std::string, std::shared_ptr<CacheItem> > cache_type;
	/** Type of the log.
	 * It is synchronized with a mutex because
	 * multiple threads will write to it.
	 */
	typedef mutex_synchronized<std::ofstream> log_type;
	/** Type of the lock_guard for the log. */
	typedef std::lock_guard<log_type> log_guard;

	/** resolver can translate hostname and service name
	 * into IP address and port number.
	 */
	boost::asio::ip::tcp::resolver resolver;
	cache_type cache; /**< The cache storing responses. */
	log_type& log; /**< The message log. */

	Server(uint16_t, size_t, log_type&);
	Server(const Server&) = delete;
	void run();
	/** Return a reference to io_service. */
	boost::asio::io_service& get_io_service()
	{
		return io_service;
	}
	size_t new_id();
private:
	void start_accept();
	void work_thread();
	void finish(const boost::system::error_code&, int);
};

#endif /* SERVER_HPP */
