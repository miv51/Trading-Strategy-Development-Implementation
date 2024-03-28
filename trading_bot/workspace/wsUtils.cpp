
#include "wsUtils.h"

constexpr char constructBaseFrame(const uint8_t fin, const uint8_t rsv1, const uint8_t rsv2, const uint8_t rsv3, const uint8_t opcode)
{
	return static_cast<char>(fin << 7 | rsv1 << 6 | rsv2 << 5 | rsv3 << 4 | opcode);
}

constexpr char constructBaseFrame(const uint8_t fin, const uint8_t opcode)
{
	return static_cast<char>(fin << 7 | opcode);
}

websocket::websocket(const SSLContextWrapper& ssl_context_wrapper, const std::string Host, const bool blocking, const bool Signal_on_control, const time_t Timeout)
	: SSLSocket(ssl_context_wrapper, Host, blocking), opened(false), signal_on_control(Signal_on_control), timeout(Timeout)
{
	frame_header = 0;
	mask_and_length = 0;

	sec_since_epoch = 0;

	message_length = 0;
	bytes_recv = 0;
}

websocket::~websocket()
{
	//I have this here in case the application shuts down unexpectedly
	//but it should not be used as the primary way of closing a websocket

	if (opened)
	{
		try { close(); }
		catch (const SSLNoReturn&) {}
		catch (const std::exception&) {} //if the SSL socket is not initialized
		catch (...) {}
	}
}

int websocket::send(const std::string& message, const char header)
{
	std::string new_message(1, header);

	//int mask = 1 //all outgoing messages will be masked

	size_t length = message.size();
	size_t delivered = 0;

	if (length < 0x7e) new_message += char(WS_SMALL_MESSAGE_MASK_BYTE | length); //mask << 7 = 128
	else if (length < 0x10000)
	{
		new_message += WS_MESSAGE_MASK_CHAR;

		uint16_t length_16 = TO_NETWORK_BYTE_ORDER_16(static_cast<uint16_t>(length));
		new_message += std::string(reinterpret_cast<const char*>(&length_16), 2);
	}
	else
	{
		new_message += WS_LARGE_MESSAGE_MASK_CHAR;

		uint64_t length_64 = TO_NETWORK_BYTE_ORDER_64(static_cast<uint64_t>(length));
		new_message += std::string(reinterpret_cast<const char*>(&length_64), 8);
	}

	std::random_device random_gen;
	std::generate_n(&mask_key[0], 4, [&random_gen] { return random_gen() % 256; });

	new_message += std::string(reinterpret_cast<const char*>(mask_key), 4);

	for (size_t i = 0; i < length; ++i) new_message += static_cast<char>(message[i] ^ mask_key[i % 4]);

	length = new_message.size();
	sec_since_epoch = time(nullptr);

	while (delivered < length)
	{
		delivered += write(new_message.substr(delivered, length - delivered));

		if (time(nullptr) - sec_since_epoch >= timeout) throw exceptions::exception("Timed out while sending a websocket message.");
	}

	return length;
}

bool websocket::recv(std::string& message)
{
	message.clear();

	//NOTE - For fragmented messages FIN = 1 only on the last frame and opcode = 0x0 on all but the first frame.
	//NOTE - Clients and servers MUST support receiving both fragmented and unfragmented messages (which I currently do not).
	//NOTE - messages split into multiple frames can only be interrupted by control frames (ping, pong, and close)

	bytes_recv = read(&frame_header, 1);

	if (bytes_recv == 0) return false;
	else if (frame_header != WS_TEXT_FRAME && frame_header != WS_BINARY_FRAME && frame_header != WS_PING_FRAME)
	{
		throw std::runtime_error(("Incoming websocket message has an unexpected frame header: <" + std::string(&frame_header, 1) + ">").c_str());
	}

	sec_since_epoch = time(nullptr);

	do
	{
		bytes_recv = read(&mask_and_length, 1);

		if (time(nullptr) - sec_since_epoch >= timeout) throw exceptions::exception("Timed out while reading a websocket message0.");
	}
	while (bytes_recv == 0);

	if (bytes_recv == -1) throw std::runtime_error("SSL error occured.");

	//bool has_mask = mask_and_length >> 7 & 1;

	if (mask_and_length >> 7 & 1) throw std::runtime_error("Incoming websocket messages should not be masked for this specific application.");

	message_length = mask_and_length & 0x7f & 0x7f;
	sec_since_epoch = time(nullptr);
	bytes_recv = 0; //use bytes_recv to count the total number of bytes received for the message length

	if (message_length == 0x7e)
	{
		do
		{
			bytes_recv += read(&message_buffer[bytes_recv], 2ULL - bytes_recv); //assumes WS_UTILS_BUFFER_SIZE >= 2

			if (time(nullptr) - sec_since_epoch >= timeout) throw exceptions::exception("Timed out while reading a websocket message1.");
		}
		while (bytes_recv < 2ULL);

		message_length = reinterpret_cast<uint16_t&>(message_buffer);
		message_length = TO_SYSTEM_BYTE_ORDER_16(message_length);
	}
	else if (message_length == 0x7f)
	{
		do
		{
			bytes_recv += read(&message_buffer[bytes_recv], 8ULL - bytes_recv); //assumes WS_UTILS_BUFFER_SIZE >= 8

			if (time(nullptr) - sec_since_epoch >= timeout) throw exceptions::exception("Timed out while reading a websocket message2.");
		}
		while (bytes_recv < 8ULL);

		message_length = reinterpret_cast<uint64_t&>(message_buffer);
		message_length = TO_SYSTEM_BYTE_ORDER_64(message_length);
	}
	else if (message_length > 0x7f) throw std::runtime_error("Received an unknown format for the payload length.");

	sec_since_epoch = time(nullptr);

	while (message_length > WS_UTILS_BUFFER_SIZE)
	{
		if (time(nullptr) - sec_since_epoch >= timeout) throw exceptions::exception("Timed out while reading a websocket message3.");

		bytes_recv = read(message_buffer, WS_UTILS_BUFFER_SIZE);

		if (bytes_recv)
		{
			message += std::string(&message_buffer[0], bytes_recv);
			message_length -= bytes_recv;

			sec_since_epoch = time(nullptr);
		}
	}

	while (message_length > 0)
	{
		if (time(nullptr) - sec_since_epoch >= timeout) throw exceptions::exception("Timed out while reading a websocket message4.");

		bytes_recv = read(message_buffer, message_length);

		if (bytes_recv)
		{
			message += std::string(&message_buffer[0], bytes_recv);
			message_length -= bytes_recv;

			sec_since_epoch = time(nullptr);
		}
	}

	if (frame_header == WS_PING_FRAME)
	{
		if (!send(message, WS_PONG_FRAME)) throw std::runtime_error("Failed to send pong message.");

		message.clear();

		return signal_on_control;
	}

	return true;
}

void websocket::open(const dictionary& headers, const char* path, http::httpResponse& response)
{
	std::string request;

	http::constructRequest(dictionary(), headers, host, path, "GET", request);

	SSL_write(ssl_struct, request.c_str(), request.size());

	http::parseResponseHeader(*this, timeout, response);

	if (response.status_code == 101) opened = true;
}

int websocket::close()
{
	if (write(WS_CLOSE_MESSAGE))
	{
		opened = false;

		return 1; //close message sent successfully
	}

	return 0;
}

std::string generateRandomBase64String(size_t length)
{
	std::random_device rd;
	std::default_random_engine rng(rd());
	std::uniform_int_distribution<unsigned int> distribution(0, 255);

	std::string randomBytes;

	randomBytes.reserve(length);

	for (size_t i = 0; i < length; ++i) randomBytes += static_cast<char>(distribution(rng));

	// Encode the random bytes as base64
	constexpr char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string base64String;

	size_t i = 0;

	while (i + 2 < randomBytes.size())
	{
		uint32_t triple = (randomBytes[i] << 0x10) + (randomBytes[i + 1] << 0x08) + randomBytes[i + 2];

		base64String += base64_chars[(triple >> 18) & 0x3F];
		base64String += base64_chars[(triple >> 12) & 0x3F];
		base64String += base64_chars[(triple >> 6) & 0x3F];
		base64String += base64_chars[triple & 0x3F];

		i += 3;
	}

	// Handle padding
	if (i + 1 == randomBytes.size())
	{
		uint32_t doublet = (randomBytes[i] << 0x10) + (randomBytes[i + 1] << 0x08);

		base64String += base64_chars[(doublet >> 18) & 0x3F];
		base64String += base64_chars[(doublet >> 12) & 0x3F];
		base64String += base64_chars[(doublet >> 6) & 0x3F];
		base64String += '=';
	}
	else if (i < randomBytes.size())
	{
		uint32_t single = randomBytes[i] << 0x10;

		base64String += base64_chars[(single >> 18) & 0x3F];
		base64String += base64_chars[(single >> 12) & 0x3F];
		base64String += "==";
	}

	return base64String;
}
