
#include "httpUtils.h"

using namespace http;

void http::httpResponse::clear()
{
	fields.clear();
	status_message.clear();
	message.clear();

	status_code = 0;
}

void http::constructRequest(const dictionary& parameters, const dictionary& headers, const std::string& host, const std::string& path,
	const std::string& type, std::string& request)
{
	request = type + " " + path;

	//add parameters
	if (!parameters.empty())
	{
		request.append("?"); //request += "?";

		for (const auto& pair : parameters) request.append(pair.first + "=" + pair.second + "&"); //request += pair.first + "=" + pair.second + "&";

		request.pop_back(); //remove the trailing & or ? character
	}

	request.append(" HTTP/1.1\r\n"); //request += " HTTP/1.1\r\n";

	//add headers
	if (!headers.empty()) for (const auto& pair : headers) request.append(pair.first + ": " + pair.second + "\r\n"); //request += pair.first + ": " + pair.second + "\r\n";

	request.append("Host: " + host + "\r\n\r\n"); //request += "Host: " + host + "\r\n\r\n";
}

void http::parseResponseHeader(SSLSocket& ssl_socket, time_t timeout, httpResponse& response)
{
	char buffer[HTTP_UTILS_BUFFER_SIZE];
	int bytes;

	time_t sec_since_epoch = time(nullptr);
	size_t index = 0;

	std::string field;
	std::string segment = "Status Code Info: ";

	while (time(nullptr) - sec_since_epoch < timeout)
	{
		bytes = ssl_socket.read(buffer, HTTP_UTILS_BUFFER_SIZE);
		
		if (bytes > 0)
		{
			segment += std::string(&buffer[0], bytes); //read the memory addresses from buffer[0] to buffer[bytes]
			index = segment.find("\r\n\r\n");

			if (index != std::string::npos)
			{
				response.message = segment.substr(index + 4);
				segment = segment.substr(0, index);

				break;
			}

			sec_since_epoch = time(nullptr);
		}
	}

	if (time(nullptr) - sec_since_epoch >= timeout) throw exceptions::exception("Http request timed out.");

	index = segment.find("\r\n");

	while (index != std::string::npos)
	{
		field = segment.substr(0, index);
		segment = segment.substr(index + 2);
		index = field.find(": ");

		response.fields[field.substr(0, index)] = field.substr(index + 2);

		index = segment.find("\r\n");
	}

	segment = response.fields["Status Code Info"].substr(9);
	index = segment.find(" ");

	response.status_code = std::stoi(segment.substr(0, index));
	response.status_message = segment.substr(index + 1);

	response.fields.erase("Status Code Info");
}

void http::get(const SSLContextWrapper& ssl_context_wrapper, httpResponse& response, const dictionary& parameters, const dictionary& headers,\
	const std::string& host, const std::string& path, const time_t timeout)
{
	http::httpClient client(ssl_context_wrapper, host, true, timeout);

	client.reConnect();
	client.get(response, parameters, headers, path);
}

void http::post(const SSLContextWrapper& ssl_context_wrapper, httpResponse& response, const dictionary& parameters, const dictionary& headers, \
	const std::string& host, const std::string& path, const std::string& body, const time_t timeout)
{
	http::httpClient client(ssl_context_wrapper, host, true, timeout);

	client.reConnect();
	client.post(response, parameters, headers, path, body);
}

void http::patch(const SSLContextWrapper& ssl_context_wrapper, httpResponse& response, const dictionary& parameters, const dictionary& headers, \
	const std::string& host, const std::string& path, const std::string& body, const time_t timeout)
{
	http::httpClient client(ssl_context_wrapper, host, true, timeout);

	client.reConnect();
	client.patch(response, parameters, headers, path, body);
}

void http::del(const SSLContextWrapper& ssl_context_wrapper, httpResponse& response, const dictionary& parameters, const dictionary& headers, \
	const std::string& host, const std::string& path, const time_t timeout)
{
	http::httpClient client(ssl_context_wrapper, host, true, timeout);

	client.reConnect();
	client.del(response, parameters, headers, path);
}

http::httpClient::httpClient(const httpClient& other_client)
	: ssl_socket(other_client.ssl_socket), host(other_client.host), timeout(other_client.timeout), current_status(status::RECEIVED_RESPONSE)
{
	bytes = 0;

	sec_since_epoch = 0;
	index = 0;

	max_message_length = 0;
}

http::httpClient::httpClient(const SSLContextWrapper& ssl_context_wrapper, const std::string Host, const bool blocking, const time_t Timeout)
	: ssl_socket(ssl_context_wrapper, Host, blocking), host(Host), timeout(Timeout), current_status(status::RECEIVED_RESPONSE)
{
	bytes = 0;

	sec_since_epoch = 0;
	index = 0;

	max_message_length = 0;
}

http::httpClient::~httpClient() //this is a cheap way of (mostly) ensuring that keep-alive connections will be closed on the server side
{
	if (ssl_socket.get_struct())
	{
		request.clear();

		dictionary parameters;
		dictionary headers;

		headers["Connection"] = "close";

		http::httpResponse response;

		try { get(response, parameters, headers, "/"); }
		catch (const SSLNoReturn&) {}
		catch (const std::exception&) {} //if the SSL socket is not initialized
		catch (...) {}
	}
}

httpClient& httpClient::operator=(const httpClient& other_client)
{
	throw std::runtime_error("httpClient type doesn't support re-assignment.");
}

void http::httpClient::reConnect()
{
	ssl_socket.reInit();

	current_status = status::RECEIVED_RESPONSE;
}

void http::httpClient::get(httpResponse& response, const dictionary& parameters, const dictionary& headers, const std::string& path)
{
	get(parameters, headers, path);

	do recvResponse(response);
	while (current_status != status::RECEIVED_RESPONSE && current_status != status::TIMED_OUT);
}

void http::httpClient::get(const dictionary& parameters, const dictionary& headers, const std::string& path)
{
	if (current_status != status::RECEIVED_RESPONSE && current_status != status::TIMED_OUT) throw std::runtime_error("Cannot make another http request while receiving a response.");

	request.clear();

	constructRequest(parameters, headers, host, path, "GET", request);

	current_status = status::SEND_REQUEST;
}

void http::httpClient::post(httpResponse& response, const dictionary& parameters, const dictionary& headers, const std::string& path, const std::string& body)
{
	post(parameters, headers, path, body);

	do recvResponse(response);
	while (current_status != status::RECEIVED_RESPONSE && current_status != status::TIMED_OUT);
}

void http::httpClient::post(const dictionary& parameters, const dictionary& headers, const std::string& path, const std::string& body)
{
	if (current_status != status::RECEIVED_RESPONSE && current_status != status::TIMED_OUT) throw std::runtime_error("Cannot make another http request while receiving a response.");

	request.clear();

	constructRequest(parameters, headers, host, path, "POST", request);

	request += body;
	current_status = status::SEND_REQUEST;
}

void http::httpClient::patch(httpResponse& response, const dictionary& parameters, const dictionary& headers, const std::string& path, const std::string& body)
{
	patch(parameters, headers, path, body);

	do recvResponse(response);
	while (current_status != status::RECEIVED_RESPONSE && current_status != status::TIMED_OUT);
}

void http::httpClient::patch(const dictionary& parameters, const dictionary& headers, const std::string& path, const std::string& body)
{
	if (current_status != status::RECEIVED_RESPONSE && current_status != status::TIMED_OUT) throw std::runtime_error("Cannot make another http request while receiving a response.");

	request.clear();

	constructRequest(parameters, headers, host, path, "PATCH", request);

	request += body;
	current_status = status::SEND_REQUEST;
}

void http::httpClient::del(httpResponse& response, const dictionary& parameters, const dictionary& headers, const std::string& path)
{
	del(parameters, headers, path);

	do recvResponse(response);
	while (current_status != status::RECEIVED_RESPONSE && current_status != status::TIMED_OUT);
}

void http::httpClient::del(const dictionary& parameters, const dictionary& headers, const std::string& path)
{
	if (current_status != status::RECEIVED_RESPONSE && current_status != status::TIMED_OUT) throw std::runtime_error("Cannot make another http request while receiving a response.");

	request.clear();

	constructRequest(parameters, headers, host, path, "DELETE", request);

	current_status = status::SEND_REQUEST;
}

status http::httpClient::recvResponse(httpResponse& response)
{
	switch (current_status)
	{
		case status::SEND_REQUEST:
		{
			bytes = ssl_socket.write(request);

			if (bytes >= request.size()) current_status = status::RECEIVE_HEADER;
			else current_status = status::SENDING_REQUEST;

			break;
		}
		case status::SENDING_REQUEST:
		{
			bytes += ssl_socket.write(request.substr(bytes));

			if (bytes >= request.size()) current_status = status::RECEIVE_HEADER;

			break;
		}
		case status::RECEIVE_HEADER:
		{
			response.clear();

			sec_since_epoch = time(nullptr);
			index = 0;

			segment = "Status Code Info: ";

			current_status = status::RECEIVING_HEADER;

			break;
		}
		case status::RECEIVING_HEADER:
		{
			if (time(nullptr) - sec_since_epoch >= timeout) current_status = status::TIMED_OUT;

			bytes = ssl_socket.read(buffer, HTTP_UTILS_BUFFER_SIZE);

			if (bytes)
			{
				segment.append(&buffer[0], bytes); //segment += std::string(&buffer[0], bytes);
				index = segment.find("\r\n\r\n");

				if (index != std::string::npos)
				{
					response.message = segment.substr(index + 4);
					segment = segment.substr(0, index);
					index = segment.find("\r\n");

					while (index != std::string::npos)
					{
						field = segment.substr(0, index);
						segment = segment.substr(index + 2);
						index = field.find(": ");

						response.fields[field.substr(0, index)] = field.substr(index + 2);

						index = segment.find("\r\n");
					}

					segment = response.fields["Status Code Info"].substr(9);
					index = segment.find(" ");

					response.status_code = std::stoi(segment.substr(0, index));
					response.status_message = segment.substr(index + 1);

					response.fields.erase("Status Code Info");

					if (response.fields.find("Transfer-Encoding") != response.fields.end()) current_status = status::RECEIVE_CHUNKED_BODY;
					else if (response.fields.find("Content-Length") != response.fields.end()) current_status = status::RECEIVE_BODY;
					else if (response.status_code == 204) current_status = status::RECEIVED_RESPONSE;

					/*
					Every response from a well - behaved http server MUST include either a "Transfer-Encoding" or "Content-Length" header ...
					... unless the status code from the response is 204 - which indicates a successful delete request.
					*/

					else throw exceptions::exception("Http response body is missing required header.");
				}

				sec_since_epoch = time(nullptr);
			}

			break;
		}
		case status::RECEIVE_CHUNKED_BODY:
		{
			sec_since_epoch = time(nullptr);

			last_segment = "";
			segment = response.message;

			response.message.clear(); //write the final response here

			current_status = status::RECEIVING_CHUNK_SIZE;

			break;
		}
		case status::RECEIVING_CHUNK_SIZE: //receive chunk sizes
		{
			if (time(nullptr) - sec_since_epoch >= timeout) current_status = status::TIMED_OUT;

			full_segment = last_segment + segment;
			index = full_segment.find("\r\n");

			if (index != std::string::npos)
			{
				if (full_segment.substr(0, index) == "0") current_status = status::RECEIVED_RESPONSE; //a chunk size of 0 indicates the end of the message
				else
				{
					//do something with the chunk sizes here if needed
					segment = full_segment.substr(index + 2);
					last_segment.clear();

					current_status = status::RECEIVING_CHUNK;
				}
			}
			else
			{
				bytes = ssl_socket.read(buffer, HTTP_UTILS_BUFFER_SIZE);

				if (bytes)
				{
					//can log chunk sizes if needed - std::string chunk_size = last_segment;

					last_segment = segment;
					segment.assign(&buffer[0], bytes); //segment = std::string(&buffer[0], bytes);
					sec_since_epoch = time(nullptr);
				}
			}

			break;
		}
		case status::RECEIVING_CHUNK: //receive chunks
		{
			if (time(nullptr) - sec_since_epoch >= timeout) current_status = status::TIMED_OUT;

			full_segment = last_segment + segment;
			index = full_segment.find("\r\n");

			if (index != std::string::npos)
			{
				response.message.append(full_segment.substr(0, index)); //response.message += full_segment.substr(0, index);
				segment = full_segment.substr(index + 2);
				last_segment.clear();

				current_status = status::RECEIVING_CHUNK_SIZE;
			}
			else
			{
				bytes = ssl_socket.read(buffer, HTTP_UTILS_BUFFER_SIZE);

				if (bytes)
				{
					response.message.append(last_segment); //response.message += last_segment;

					last_segment = segment;
					segment.assign(&buffer[0], bytes); //segment = std::string(&buffer[0], bytes);
					sec_since_epoch = time(nullptr);
				}
			}

			break;
		}
		case status::RECEIVE_BODY:
		{
			max_message_length = std::stoll(response.fields["Content-Length"]);
			sec_since_epoch = time(nullptr);

			if (response.message.size() >= max_message_length) current_status = status::RECEIVED_RESPONSE;
			else current_status = status::RECEIVING_BODY;

			break;
		}
		case status::RECEIVING_BODY:
		{
			if (time(nullptr) - sec_since_epoch >= timeout) current_status = status::TIMED_OUT;

			bytes = ssl_socket.read(buffer, HTTP_UTILS_BUFFER_SIZE);

			if (bytes)
			{
				response.message.append(&buffer[0], bytes); //response.message += std::string(&buffer[0], bytes);

				if (response.message.size() >= max_message_length) current_status = status::RECEIVED_RESPONSE;

				sec_since_epoch = time(nullptr);
			}

			break;
		}
		case status::RECEIVED_RESPONSE: break;
		case status::TIMED_OUT: break;
		default: throw std::runtime_error("Unknown http get status.");
	}

	return current_status;
}