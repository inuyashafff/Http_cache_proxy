#include "http.hpp"
#include <cassert>
#include <string>
#include <iostream>
#include <sstream>

void url_test(const std::string& url_str,
	HttpUrl::Protocol protocol,
	const std::string& host, const std::string& port,
	const std::string& path)
{
	HttpUrl url(url_str);
	std::cout << url_str << " -> " << url << std::endl;
	assert(url.protocol == protocol);
	assert(url.host == host);
	assert(url.port == port);
	assert(url.path == path);
}

void url_fail(const std::string& url_str)
{
	try {
		HttpUrl url(url_str);
		std::cout << url_str << " is illegal!" << std::endl;
		assert(0);
	} catch (const ParseError& e) {
		// OK
	}
}

void url_tests()
{
	url_test("http://www.google.com/", HttpUrl::HTTP, "www.google.com", "80", "/");
	url_test("https://www.google.com/", HttpUrl::HTTPS, "www.google.com", "443", "/");
	url_test("https://www.google.com/", HttpUrl::HTTPS, "www.google.com", "443", "/");
	url_test("http://localhost:8000/", HttpUrl::HTTP, "localhost", "8000", "/");
	url_test("http://vcm-2935.vm.duke.edu:8000/event/1/add_person",
		HttpUrl::HTTP, "vcm-2935.vm.duke.edu", "8000", "/event/1/add_person");
}

void http_tests()
{
	static const char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n1234567890";
	HttpMessage msg;
	HttpParser parser(msg);

	std::stringstream ss(http_response);
	parser.parse(ss);
	assert(msg.startLine[0] == "HTTP/1.1");
	assert(msg.startLine[1] == "200");
	assert(msg.startLine[2] == "OK");
	assert(msg.headerLines.size() == 1);
	const auto& first = msg.headerLines.front();
	assert(first.key == "Content-Length");
	assert(first.value == "10");
	assert(msg.body == "1234567890");
}

void cache_tests()
{
	auto resp_time = std::chrono::system_clock::now();
	auto req_time = resp_time - std::chrono::seconds(2);
	HttpMessage msg = {
		{ "HTTP/1.1", "200", "OK" },
		{
			{ "Date", "Wed, 28 Feb 2018 20:51:55 GMT" },
			{ "Cache-Control", "no-cache, no-store, s-maxage=86400, max-age=100"},
		},
	};
	HttpParser parser(msg);
	ResponseCacheInfo ci;
	parser.parseResponseCacheInfo(ci, req_time, resp_time);
	assert(ci.no_cache == true);
	assert(ci.no_store == true);
	assert(ci.private_ == false);
	std::cout << ci << std::endl;
}

int main(int argc, char *argv[])
{
	setenv("TZ", "UTC", 1);

	url_tests();
	http_tests();
	cache_tests();
}
