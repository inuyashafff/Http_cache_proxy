#include "http.hpp"
#include <cassert>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iterator>
#include <strings.h> // for POSIX string functions
#include <cctype>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>

/** Construct an HttpUrl object from a string.
 * 
 * @param url The URL string to be parsed.
 * @see operator<<(std::ostream& os, const HttpUrl& url)
 * for formatted output.
 */
HttpUrl::HttpUrl(const std::string& url)
{
	size_t pos, last;

	if (strncasecmp(url.c_str(), "http://", 7) == 0) {
		protocol = HTTP;
		last = 7;
	} else if (strncasecmp(url.c_str(), "https://", 8) == 0) {
		protocol = HTTPS;
		last = 8;
	} else {
		protocol = NONE;
		last = 0;
	}
	pos = url.find_first_of(":/", last);
	if (pos != std::string::npos) {
		host = url.substr(last, pos - last);
		if (url[pos] == ':') {
			size_t pos1 = url.find('/', pos + 1);
			if (pos1 != std::string::npos) {
				port = url.substr(pos + 1, pos1 - pos - 1);
				path = url.substr(pos1);
			} else {
				port = url.substr(pos + 1);
				path = "/";
			}
		} else {
			port = (protocol == HTTP) ? "80" : "443";
			path = url.substr(pos);
		}
	} else {
		host = url.substr(last);
		port = (protocol == HTTP) ? "80" : "443";
		path = "/";
	}
}

/** Write an HttpUrl object to the output stream.
 *
 * @param os The output stream.
 * @param url The HttpUrl object to be output.
 * @returns the output stream.
 */
std::ostream& operator<<(std::ostream& os, const HttpUrl& url)
{
	static const char *protocols[] = {"NONE", "HTTP", "HTTPS"};
	return os
		<< "HttpUrl{protocol = " << protocols[url.protocol]
		<< ", host = \"" << url.host
		<< "\", port = " << url.port
		<< ", path = \"" << url.path
		<< "\"}";
}


/** Constructor.
 */
HttpParser::HttpParser(HttpMessage& message)
	: message(message)
{
}

/** The HTTP time format.
 *
 * It is used for parsing and formatting header fields
 * holding a date and time.
 *
 */
#define HTTP_TIME_FORMAT "%a, %d %b %Y %H:%M:%S GMT"

/** Parse a date string into a CacheInfo::time_point value.
 *
 * It uses HTTP_TIME_FORMAT as the format string.
 *
 * @param datestr The date string.
 * @returns The time_point value.
 */
CacheInfo::time_point
HttpParser::parse_date(const std::string& datestr)
{
	std::tm tmb = { 0 };
	std::istringstream ss(datestr);

	ss >> std::get_time(&tmb, HTTP_TIME_FORMAT);
	if (!ss) {
		throw ParseError("invalid date format");
	}
	return std::chrono::system_clock::from_time_t(mktime(&tmb));
}

/** Write a CacheInfo::time_point to an output stream.
 *
 * It formats the time according to HTTP_TIME_FORMAT.
 *
 * @param os The output stream.
 * @param tp The CacheInfo::time_point value.
 * @returns the output stream.
 */
std::ostream& operator<<(std::ostream& os, CacheInfo::time_point tp)
{
	std::tm tmb;
	std::time_t time;

	time = std::chrono::system_clock::to_time_t(tp);
	gmtime_r(&time, &tmb);
	return os << std::put_time(&tmb, HTTP_TIME_FORMAT);
}

/** Write a CacheInfo::duration to an output stream.
 *
 * It writes the number of seconds followed by the letter 's'.
 *
 * @param os The output stream.
 * @param dur The CacheInfo::duration value.
 * @returns the output stream.
 */
std::ostream& operator<<(std::ostream& os, CacheInfo::duration dur)
{
	using namespace std::chrono;
	return os << duration_cast<seconds>(dur).count() << 's';
}

/** Read content from input stream and parse as an HTTP message.
 *
 * It will stop when the parser is in ACCEPT state or a read error
 * (e.g., end of file) occurs.
 *
 * @param is The input stream.
 */
void HttpParser::parse(std::istream& is)
{
	while (is && state != ACCEPT) {
		parseStep(is);
	}
}

/** Read some content and incrementally parse an HTTP message.
 */
void HttpParser::parseStep(std::istream& is)
{
	switch (state) {
	case START:
		putStartLine(is);
		break;
	case HEADER:
		putHeader(is);
		break;
	case BODY:
		putContent(is);
		break;
	case ACCEPT:
		assert(0 && "Cannot put more stuff");
		break;
	}
}

void HttpParser::putStartLine(std::istream& is)
{
	std::string line;
	if (!std::getline(is, line)) {
		return;
	}
	size_t pos, last = 0;
	for (int i = 0; i < 2; i++) {
		pos = line.find_first_of(' ', last);
		if (pos == std::string::npos) {
			throw ParseError("Invalid start line (need 3 fields)");
		}
		message.startLine[i] = line.substr(last, pos - last);
		last = pos + 1;
	}
	// do not check for more spaces, because the
	// third field may contain a space (e.g., "Not Found")
	pos = line.find_first_of('\r', last);
	if (pos == std::string::npos) {
		message.startLine[2] = line.substr(last);
	} else {
		message.startLine[2] = line.substr(last, pos - last);
	}
	state = HEADER;
}

void HttpParser::putHeader(std::istream& is)
{
	std::string line;
	if (!std::getline(is, line)) {
		return;
	}
	if (line.empty() || line == "\r") {
		state = hasBody() ? BODY : ACCEPT;
		return;
	}
	size_t pos = line.find_first_of(": ");
	if (pos == std::string::npos) {
		throw ParseError("Invalid header line (no colon)");
	}
	if (line[pos] == ' ') { // RFC 7230  3.2.4
		throw ParseError("Invalid header line (space before colon)");
	}
	size_t pos1 = line.find_first_not_of(" \t", pos + 1);
	size_t pos2 = line.find_last_not_of(" \t\r") + 1;
	if (pos1 == std::string::npos) { // nothing after colon?
		pos1 = pos2;
	}
	message.headerLines.push_back({
		line.substr(0, pos),
		line.substr(pos1, pos2 - pos1),
	});
	auto& header = message.headerLines.back();
	canonicalize(header.key);

	if (header.key ==  "Content-Length") {
		std::istringstream ss(header.value);
		ss >> contentLength;
		if (ss) {
			format = LENGTH;
			message.body.reserve(contentLength);
		}
	} else if (header.key == "Transfer-Encoding") {
		if (header.value.find("chunked") != std::string::npos) {
			format = CHUNKED;
		}
	}
}

void HttpParser::canonicalize(std::string& s)
{
	enum { FIRST, OTHER } state = FIRST;

	for (char& ch : s) {
		if (std::isalpha(ch)) {
			ch = (state == FIRST)
				? std::toupper(ch)
				: std::tolower(ch);
			state = OTHER;
		} else {
			state = FIRST;
		}
	}
}

void HttpParser::putContent(std::istream& is)
{
	std::istreambuf_iterator<char> it(is), end;

	switch (format) {
	case PLAIN:
		if (it != end) {
			message.body.append(it, end);
		} else {
			state = ACCEPT;
		}
		break;
	case LENGTH:
		appendBody(it, contentLength);
		state = ACCEPT;
		break;
	case CHUNKED:
		std::string line;
		if (contentLength == 0) { // chunk header
			std::getline(is, line);
			message.body.append(line + "\n");
			if (line.empty() || line == "\r") {
				state = ACCEPT;
			} else {
				std::istringstream ss(line);
				ss >> std::hex >> contentLength;
				if (contentLength) {
					contentLength += 2; // including "\r\n"
				}
			}
		} else {
			appendBody(it, contentLength);
			contentLength = 0;
		}
	}
}

bool HttpParser::hasBody() const
{
	const auto& s = message.startLine[0];
	if (s == "HTTP/1.0" || s == "HTTP/1.1") { // response
		const auto& status = message.startLine[1];
		if (status.size() != 3) {
			throw ParseError("Invalid status code");
		}
		return !(status[0] == '1' || status == "204" || status == "304");
	} else {
		if ((format == LENGTH && contentLength > 0) || format == CHUNKED) {
			return true;
		}
		return false;
	}
}

void HttpParser::appendBody(std::istreambuf_iterator<char>& it, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		message.body.push_back(*it);
		++it;
	}
}

void HttpParser::reset()
{
	for (auto& s : message.startLine) {
		s.clear();
	}
	message.headerLines.clear();
	message.body.clear();
	state = START;
}

std::ostream& operator<<(std::ostream& os, const HttpMessage& msg)
{

	os << msg.startLine << "\r\n";
	for (const auto& h : msg.headerLines) {
		os << h.key << ": " << h.value << "\r\n";
	}
	os << "\r\n" << msg.body;
	return os;
}

std::ostream& operator<<(std::ostream& os, const HttpMessage::StartLine& startLine){
	return os << startLine[0] << ' ' << startLine[1] << ' ' << startLine[2];
} 

CacheInfo::duration HttpParser::parseDeltaSeconds(
	const std::string& s)
{
	uint32_t value;

	if (!boost::conversion::try_lexical_convert(s, value)) {
		/** @bug Numbers > 2^32 should be treated
		 * as infinity instead of error.
		 */
		throw ParseError("invalid delta-second format");
	}
	return std::chrono::seconds(value);
}

/** Parses a ResponseCacheInfo from the message the parser
 * is associated with.
 *
 * @param[out] ci The ResponseCacheInfo object to store the result.
 * @param request_time The time of the request.
 * @param response_time The time of the response.
 * @returns true if parse succeeds.
 * @returns false if parse fails.
 */
bool HttpParser::parseResponseCacheInfo(ResponseCacheInfo& ci,
	std::chrono::system_clock::time_point request_time,
	std::chrono::system_clock::time_point response_time)
{
	boost::optional<std::chrono::system_clock::time_point> expires;
	boost::optional<std::chrono::system_clock::time_point> date_value;
	std::chrono::system_clock::duration age_value(0);
	std::string cache_control;

	for (auto& h : message.headerLines) {
		try {
			if (h.key == "Age") {
				age_value = parseDeltaSeconds(h.value);
			} else if (h.key == "Cache-Control") {
				cache_control = h.value;
			} else if (h.key == "Date") {
				date_value = parse_date(h.value);
			} else if (h.key == "Etag") {
				ci.etag = h.value;
			} else if (h.key == "Expires") {
				expires = parse_date(h.value);
			} else if (h.key == "Last-Modified") {
				ci.last_modified = parse_date(h.value);
			}
		} catch (const ParseError&) {
			/** @note Any parse failure is treated as if
			 * the corresponding field does not exist.
			 */
		}
	}
	if (!date_value) {
	/** @note In order to calculate relevant information,
	 * Date field must be present in the message header.
	 * Otherwise, parse will fail and this method returns false.
	 */
		return false;
	}
	ci.date_value = *date_value;
	ci.request_time = request_time;
	ci.response_time = response_time;
	// RFC 7234  4.2.3.
	auto apparent_age = (response_time > *date_value)
		? response_time - *date_value
		: std::chrono::seconds(0);
	auto response_delay = response_time - request_time;
	auto corrected_age_value = age_value + response_delay;
	ci.corrected_initial_age = std::max(apparent_age, corrected_age_value);
	parseCacheControl(ci, cache_control, expires);
	return true;
}

/** Parses the Cache-Control line and write relevant information
 * into the ResponseCacheInfo object.
 * @param[out] ci The ResponseCacheInfo object to store the result.
 * @param cache_control The Cache-Control line.
 * @param expires the Expires field in the header (optional).
 */
void HttpParser::parseCacheControl(
	ResponseCacheInfo& ci,
	const std::string& cache_control,
	boost::optional<std::chrono::system_clock::time_point> expires)
{
	std::istringstream ss(cache_control);
	std::string field;
	boost::optional<std::chrono::system_clock::duration> max_age, s_maxage;

	ci.no_cache = ci.no_store = ci.private_ = false;
	while (std::getline(ss >> std::ws, field, ',')) {
		try {
			if (field == "no-cache") {
				ci.no_cache = true;
			} else if (field == "no-store") {
				ci.no_store = true;
			} else if (field == "private") {
				ci.private_ = true;
			} else if (boost::starts_with(field, "max-age=")) {
				max_age = parseDeltaSeconds(
						field.substr(8));
			} else if (boost::starts_with(field, "s-maxage=")) {
				s_maxage = parseDeltaSeconds(
						field.substr(9));
			}
		} catch (const ParseError&) {
			// any parse failure is treated as if
			// the corresponding field does not exist
		}
	}
	// RFC 7234  4.2.1.
	if (s_maxage) {
		ci.freshness_lifetime = *s_maxage;
	} else if (max_age) {
		ci.freshness_lifetime = *max_age;
	} else if (expires) {
		ci.freshness_lifetime = *expires - ci.date_value;
	} else if (ci.last_modified) { // use heuristic
		auto now = std::chrono::system_clock::now();
		ci.freshness_lifetime = (now - *ci.last_modified) / 10;
		//ci.freshness_lifetime = std::chrono::seconds(30);
	}
}

/** Calculate the current age of a stored response
 * according to RFC 7234  4.2.3.
 * @returns the current age.
 */
CacheInfo::duration ResponseCacheInfo::current_age() const
{
	auto now = std::chrono::system_clock::now();
	auto resident_time = now - response_time;
	return corrected_initial_age + resident_time;
}

/** Determine if the stored response has expired.
 *
 * @returns true if expired.
 * @returns false if not expired.
 */
bool ResponseCacheInfo::expired() const
{
	return current_age() >= freshness_lifetime;
}

/** Write a ResponseCacheInfo into the output stream.
 * @param os The output stream.
 * @param ci The ResponseCacheInfo object.
 * @returns the output stream.
 */
std::ostream& operator<<(std::ostream& os, const ResponseCacheInfo& ci)
{
	return os << "ResponseCacheInfo{ date_value = \"" << ci.date_value
		<< "\", request_time = \"" << ci.request_time
		<< "\", response_time = \"" << ci.response_time
		<< "\", corrected_age_value = " << ci.corrected_initial_age
		<< ", freshness_lifetime = " << ci.freshness_lifetime
		<< ", etag = " << ci.etag
		<< ", no_cache = " << ci.no_cache
		<< ", no_store = " << ci.no_store
		<< ", private_ = " << ci.private_
		<< " }";
}

/** Parses a RequestCacheInfo from the message the parser
 * is associated with.
 * @param[out] ci The RequestCacheInfo object to store the result.
 */
void HttpParser::parseRequestCacheInfo(RequestCacheInfo& ci)
{
	ci.no_cache = false;
	for (auto& h : message.headerLines) {
		try {
			if (h.key == "Cache-Control") {
				parseCacheControl(ci, h.value);
			} else if (h.key == "If-Modified-Since") {
				ci.if_modified_since = parse_date(h.value);
			} else if (h.key == "If-None-Match") {
				ci.if_none_match = h.value;
			}
		} catch (const ParseError&) {
			/** @note Any parse failure is treated as if
			 * the corresponding field does not exist.
			 */
		}
	}
}

/** Parses the Cache-Control line and write relevant information
 * into the RequestCacheInfo object.
 * @param[out] ci The RequestCacheInfo object to store the result.
 * @param cache_control The Cache-Control line.
 */
void HttpParser::parseCacheControl(RequestCacheInfo& ci,
	const std::string& cache_control)
{
	std::istringstream ss(cache_control);
	std::string field;

	ci.no_cache = false;
	while (std::getline(ss >> std::ws, field, ',')) {
		if (field == "no-cache") {
			ci.no_cache = true;
		}
	}
}

/** Write a RequestCacheInfo into the output stream.
 * @param os The output stream.
 * @param ci The RequestCacheInfo object.
 * @returns the output stream.
 */
std::ostream& operator<<(std::ostream& os, const RequestCacheInfo& ci)
{
	os << "RequestCacheInfo{ if_modified_since = ";
	if (ci.if_modified_since) {
		os << '"' << *ci.if_modified_since << '"';
	} else {
		os << "(not set)";
	}
	return os << ", if_none_match = \"" << ci.if_none_match
		<< "\", no_cache = " << ci.no_cache
		<< " }";
}
