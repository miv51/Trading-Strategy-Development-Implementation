
//custom websocket utilities needed specifically for this bot

/*
this RFC6455 implementation consists only of what I need to handle messages from the alpaca data streams
More specifically:
	It does not explicitly handle all opcode types
	It does not fully account for protocol violations or unexpected behavior
	It does not handle additional extensions such as compression
*/

#ifndef WS_UTILS_H
#define WS_UTILS_H

#define WS_UTILS_BUFFER_SIZE 4096

#define IS_LITTLE_ENDIAN 1 //set to 0 if system is big endian

#if defined IS_LITTLE_ENDIAN
#if IS_LITTLE_ENDIAN

#define TO_NETWORK_BYTE_ORDER_16(x) (((x & 0xFF) << 8) | ((x >> 8) & 0xFF)) //network byte order is big endian so convert little endian to big endian
#define TO_NETWORK_BYTE_ORDER_32(x) (((x << 24) & 0xFF000000) | ((x << 8) & 0x00FF0000) | TO_NETWORK_BYTE_ORDER_16((x >> 16)))
#define TO_NETWORK_BYTE_ORDER_64(x) (((x << 56) & 0xFF00000000000000) | ((x << 40) & 0x00FF000000000000) | ((x << 24) & 0x0000FF0000000000) | ((x << 8) & 0x000000FF00000000) | TO_NETWORK_BYTE_ORDER_32((x >> 32)))


#else

#define TO_NETWORK_BYTE_ORDER_16(x) x //network byte order is big endian so convert big endian to big endian
#define TO_NETWORK_BYTE_ORDER_32(x) x
#define TO_NETWORK_BYTE_ORDER_64(x) x

#endif

#define TO_SYSTEM_BYTE_ORDER_16(x) TO_NETWORK_BYTE_ORDER_16(x) //convert big endian to system byte order (most likely little endian)
#define TO_SYSTEM_BYTE_ORDER_32(x) TO_NETWORK_BYTE_ORDER_32(x)
#define TO_SYSTEM_BYTE_ORDER_64(x) TO_NETWORK_BYTE_ORDER_64(x)

#endif

#include <ctime>
#include <string>
#include <stdexcept>
#include <random>

#include "exceptUtils.h"
#include "socketUtils.h"
#include "httpUtils.h"

constexpr uint8_t WS_SMALL_MESSAGE_MASK_BYTE = 1 << 7;
constexpr char WS_MESSAGE_MASK_CHAR = char(1 << 7 | 0x7e);
constexpr char WS_LARGE_MESSAGE_MASK_CHAR = char(1 << 7 | 0x7f);

//construct the frame header of the message
constexpr char constructBaseFrame(const uint8_t, const uint8_t, const uint8_t, const uint8_t, const uint8_t);
constexpr char constructBaseFrame(const uint8_t, const uint8_t);

//evaluate the expected frame headers and the closing frame at runtime since it will be the same in all messages for this application
const char WS_TEXT_FRAME = constructBaseFrame(1, 0, 0, 0, 0x1);
const char WS_BINARY_FRAME = constructBaseFrame(1, 0, 0, 0, 0x2);
const char WS_PING_FRAME = constructBaseFrame(1, 0, 0, 0, 0x9); //if we receive this frame, respond immediately
const char WS_PONG_FRAME = constructBaseFrame(1, 0, 0, 0, 0xa); //the response frame for ping frames
const char WS_CLOSE_FRAME = constructBaseFrame(1, 0, 0, 0, 0x8); //closing frame is always the same

constexpr const char* WS_CLOSE_MESSAGE = "\x88\x00";

/*
a class designed for handling websocket message sending and receiving
reuses the same char buffers to reduce memory allocations and deallocations

somewhat supports nonblocking I/O - if recv is called and there is a message to be received ...
... then the full message will be received before recv returns, otherwise recv returns immediately
*/

class websocket : public SSLSocket
{
public:
	websocket(const SSLContextWrapper&, const std::string, const bool, const bool, const time_t);
	~websocket();

	/*
	opens the connection
	for alpaca data streams, I won't receive any updates on anything until I subscribe to some kind of update.
	that means I don't have to worry about handling a chunk of a message that isn't part of the initial header
	that I could get with an http request.
	*/

	void open(const dictionary&, const char*, http::httpResponse&);
	int close();

	/*
	both send and recv assume the system is little endian and the network is big endian
	*/

	int send(const std::string&, const char);
	bool recv(std::string&); //returns true if a message was received - only need to check for non-blocking I/O

private:
	bool signal_on_control; //recv returns whatever this flag is set to when a ping frame is received
	bool opened;

	char frame_header;
	char mask_and_length;

	char message_buffer[WS_UTILS_BUFFER_SIZE];

	unsigned char mask_key[4]; //store the key for masking outgoing messages

	time_t sec_since_epoch;

	size_t message_length;
	size_t bytes_recv; //number of bytes received by the last read

	time_t timeout;
};

//generate the websocket key
std::string generateRandomBase64String(size_t);

#endif
