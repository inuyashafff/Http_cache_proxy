#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>
#include <array>
#include <list>
#include <iostream>
#include <utility>
#include <exception>
#include <chrono>
#include <boost/optional.hpp>

class ParseError : public std::exception {
	const char *what_;
public:
	ParseError(const char *what_) : what_(what_) { }
	const char *what() const noexcept { return what_; }
};

struct HttpUrl {
	/** Protocol represents the protocol specified in the URL.
	 */
	enum Protocol {
		NONE, /**< No or unrecognized protocol */
		HTTP, /**< HTTP */
		HTTPS /**< HTTPS */
	};

	Protocol protocol;
	std::string host, port, path;

	HttpUrl() = default;
	HttpUrl(const HttpUrl&) = default;
	HttpUrl(const std::string&);
};

struct HttpMessage {
	struct Header {
		std::string key, value;
	};
	typedef std::array<std::string, 3> StartLine;
	typedef std::list<Header> HeaderLines;
	StartLine startLine;
	HeaderLines headerLines;
	std::string body;
};

// for debugging purpose
std::ostream& operator<<(std::ostream&, const HttpUrl&);
std::ostream& operator<<(std::ostream&, const HttpMessage&);
std::ostream& operator<<(std::ostream&, const HttpMessage::StartLine&);

/** A CacheInfo object stores all relevant information
 * for caching.
 *
 * This class does not contain any fields and functions
 * as the base class of ResponseCacheInfo and 
 * RequestCacheInfo.
 *
 * It has two typedef's representing time points and
 * durations.  Formatted output are defined on these types.
 */
struct CacheInfo {
	/** Type representing a time point.
	 * @see operator<<(std::ostream&, CacheInfo::time_point). 
	 * */
	typedef std::chrono::system_clock::time_point time_point;
	/** Type representing a duration.
	 * @see operator<<(std::ostream&, CacheInfo::duration).
	 * */
	typedef std::chrono::system_clock::duration duration;
};

std::ostream& operator<<(std::ostream&, CacheInfo::time_point);
std::ostream& operator<<(std::ostream&, CacheInfo::duration);

/** Relevant information for caching in a response.
 */
struct ResponseCacheInfo : CacheInfo {
	time_point date_value; /**< Date field in HTTP header. */
	time_point request_time; /**< Time when the request is made. */
	time_point response_time; /**< Time when the response is received. */
	/** Last-Modified field in HTTP header.
	 * @note This field may not exist; therefore, it is
	 * a boost::optional value.
	 */
	boost::optional<time_point> last_modified;
	duration corrected_initial_age; /**< RFC 7234 Sec 4.2.3 */
	duration freshness_lifetime; /**< RFC 7234 Sec 4.2.1 */
	std::string etag; /**< RFC 7232 Sec 2.3 */
	/** `no-cache` flag in Cache-Control.
	 * @note When this flag is set, the cache must not
	 * send the stored response without validation
	 * (RFC 7234 Sec 4).
	 */
	bool no_cache;
	/** `no-store` flag in Cache-Control.
	 * @note When this flag is set, the cache must not
	 * store the response (RFC 7234 Sec 5.2.1.5).
	 */
	bool no_store;
	/** `private` flag in Cache-Control. */
	bool private_;

	duration current_age() const;
	bool expired() const;
};

std::ostream& operator<<(std::ostream&, const ResponseCacheInfo&);

/** Relevant information for caching policy in a request.
 */
struct RequestCacheInfo : CacheInfo {
	/** The If-Modified-Since header field (optional).  */
	boost::optional<time_point> if_modified_since;
	/** The If-None-Match header field.  */
	std::string if_none_match;
	/** The `no-cache` field in Cache-Control.  */
	bool no_cache;
};

std::ostream& operator<<(std::ostream&, const RequestCacheInfo&);

/** The HttpParser class is used for parsing an HTTP message
 * from a input stream.
 *
 * An HttpParser object is associated to a HttpMessage object,
 * where the parsed message is stored.
 *
 * After parsing the message, parseRequestCacheInfo or
 * parseResponseCacheInfo can be called to extract specific
 * information about caching.
 */
class HttpParser {
public:
	enum State {
		START, HEADER, BODY, ACCEPT
	};
	enum BodyFormat {
		PLAIN, LENGTH, CHUNKED
	};

	HttpParser(HttpMessage&);

	BodyFormat format = PLAIN;
	size_t contentLength = 0;

	/* put a line into the parser (when parsing the first line or header); or
	 * put some bytes into the parser (when parsing the content)
	 */
	void parse(std::istream& is);
	void parseStep(std::istream& is);

	State status() const
	{
		return state;
	}

	void reset();

	bool parseResponseCacheInfo(ResponseCacheInfo&,
		std::chrono::system_clock::time_point request_time,
		std::chrono::system_clock::time_point response_time);
	void parseRequestCacheInfo(RequestCacheInfo&);
private:
	HttpMessage& message;
	State state = START;

	void putStartLine(std::istream&);
	void putHeader(std::istream&);
	void putContent(std::istream&);
	void appendBody(std::istreambuf_iterator<char>&, size_t);
	bool hasBody() const;
	static void canonicalize(std::string&);
	static CacheInfo::time_point parse_date(
			const std::string&);
	static CacheInfo::duration parseDeltaSeconds(
			const std::string&);
	void parseCacheControl(ResponseCacheInfo& ci, const std::string&,
		boost::optional<std::chrono::system_clock::time_point>);
	void parseCacheControl(RequestCacheInfo& ci, const std::string&);
};
#endif /* HTTP_HPP */
