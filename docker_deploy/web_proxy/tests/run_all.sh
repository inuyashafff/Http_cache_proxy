#!/bin/bash

# This script runs all test cases using netcat (nc).

# Test instructions:
# Start a fresh proxy server which listens on port 12345.
# Ensure that port 23456 is not in use.
# Then, run this script, you should see the output of netcat
# (i.e., the received request and response).

SERVER_PORT=12345
CLIENT_PORT=23456

nc -z localhost $SERVER_PORT
if [ $? -ne 0 ]; then
	echo "You don't have your proxy server running!"
	exit
fi
nc localhost $CLIENT_PORT > /dev/null # remove any pending data

testid=0

testcase()
{
	testid=`expr $testid + 1`
	echo "----------Test $testid-----------"
	if [ "$2" != "" ]; then
		nc -l -p $CLIENT_PORT < "$2" &
	fi
	sleep .2
	nc localhost $SERVER_PORT < "$1" &
	wait
}

result()
{
	echo "------Test $testid finishes------"
	echo "You should see $1"
	echo
}

testcase request1.txt response1.txt
result "a normal GET request/response."

testcase request1.txt response2.txt
result "the response has a Content-Length field. \
so the body ends before EOF."

testcase request1.txt response3.txt
result "the response uses chunked encoding. \
The proxy is able to determine the end of the body \
according to the encoding."

testcase request1.txt response4.txt
result "the response is 404. \
The proxy should say it is not cachable."

testcase request2.txt response1.txt
result "the request is POST and has a body, \
whose length is determined by Content-Length \
in request's header."

testcase request1.txt crap.txt
result "the response is illegal. \
The proxy should respond an HTTP 502."

testcase crap.txt
result "the request is illegal. \
The proxy should respond an HTTP 400."

testcase request1.txt response5.txt
result "the response is cached."

testcase request1.txt #response5.txt
# need not run server because the reponse is cached
result "the stored response is used. \
(This is the same request as the previous one.)"

testcase request3.txt response5.txt
result "the stored response is not used \
because the request has no-cache in its \
Cache-Control."

testcase request4.txt response6.txt
result "the response is not stored \
because no-store is present in its \
Cache-Control."

testcase request4.txt response6.txt
result "the server is contacted again
because the previous response was not stored.
(This is the same request as the previous one.)"

