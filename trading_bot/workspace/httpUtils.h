
//A module that I made for specific http requests
//I only added the functionality that I needed for this bot

#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

#define HTTP_UTILS_BUFFER_SIZE 4096

#include "exceptUtils.h"

#include <ctime>
#include <string>
#include <stdexcept>

#include "socketUtils.h"

#ifndef TYPEDEF_DICTIONARY
#define TYPEDEF_DICTIONARY

#include <unordered_map>

typedef std::unordered_map<std::string, std::string> dictionary;

#endif

namespace http
{
	enum status //status codes for asynchronous http requests
	{
		SEND_REQUEST,         //ready to send the request
		SENDING_REQUEST,      //in the process of sending the request
		RECEIVE_HEADER,       //ready to receive the header
		RECEIVING_HEADER,     //in the process of receiving the header
		RECEIVE_BODY,         //ready to receive the body
		RECEIVING_BODY,	      //in the process of receiving the body
		RECEIVE_CHUNKED_BODY, //ready to receive a chunked body
		RECEIVING_CHUNK_SIZE, //in the process of receiving the size of a chunk
		RECEIVING_CHUNK,      //in the process of receiving a chunk of a body
		RECEIVED_RESPONSE,    //the whole header and body have been received
		TIMED_OUT             //no data was received within the timeout limit
	};

	struct httpResponse
	{
		int status_code = 0;

		std::string status_message;
		std::string message;

		dictionary fields;

		void clear();
	};

	void constructRequest(const dictionary&, const dictionary&, const std::string&, const std::string&, const std::string&, std::string&); //construct the http request
	void parseResponseHeader(SSLSocket&, time_t, httpResponse&); //parse the response header from a request

	/*
	make sure that ...
		1) the response object is empty before making any http request and
		2) the "Connection" header is set to "close" for individual http requests
			If you want to initialte a keep-alive connection, use the httpClient
	if reusing the response object call httpResponse.clear before performing another request
	*/

	void get(const SSLContextWrapper&, httpResponse&, const dictionary&, const dictionary&, const std::string&, const std::string&, const time_t);
	void post(const SSLContextWrapper&, httpResponse&, const dictionary&, const dictionary&, const std::string&, const std::string&, const std::string&, const time_t);
	void patch(const SSLContextWrapper&, httpResponse&, const dictionary&, const dictionary&, const std::string&, const std::string&, const std::string&, const time_t);
	void del(const SSLContextWrapper&, httpResponse&, const dictionary&, const dictionary&, const std::string&, const std::string&, const time_t); //delete request

	class httpClient
	{
	public:
		httpClient(const httpClient&);
		httpClient(const SSLContextWrapper&, const std::string, const bool, const time_t);
		~httpClient();

		httpClient& operator=(const httpClient&);

		void reConnect();

		void get(httpResponse&, const dictionary&, const dictionary&, const std::string&); //for individual get requests
		void get(const dictionary&, const dictionary&, const std::string&); //for asynchronous get requests - prepares the request to be sent

		void post(httpResponse&, const dictionary&, const dictionary&, const std::string&, const std::string&); //for individual post requests
		void post(const dictionary&, const dictionary&, const std::string&, const std::string&); //for asynchronous post requests - prepares the request to be sent

		void patch(httpResponse&, const dictionary&, const dictionary&, const std::string&, const std::string&); //for individual patch requests
		void patch(const dictionary&, const dictionary&, const std::string&, const std::string&); //for asynchronous patch requests - prepares the request to be sent

		void del(httpResponse&, const dictionary&, const dictionary&, const std::string&); //for individual delete requests
		void del(const dictionary&, const dictionary&, const std::string&); //for asynchronous delete requests - prepares the request to be sent

		status recvResponse(httpResponse&); //receive data for a asynchronous http request - use after request is prepared

	private:
		SSLSocket ssl_socket;

		std::string request;
		std::string host;

		time_t timeout;

		char buffer[HTTP_UTILS_BUFFER_SIZE];
		int bytes;

		time_t sec_since_epoch;
		size_t index;

		std::string field;
		std::string segment;

		size_t max_message_length;

		std::string full_segment;
		std::string last_segment;

		status current_status;
	};
}
#endif
