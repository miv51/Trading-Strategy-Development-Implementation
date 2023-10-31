
//A module that I made for managing ssl resources and sockets needed for this bot

#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include "exceptUtils.h"

#include <string>
#include <stdexcept>
#include <openssl/ssl.h>
#include <openssl/err.h>

#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

#define SOCKET_UTILS_MAX_SHUTDOWN_ATTEMPTS 2

#ifdef _WIN32
#include <winsock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib") // Automatically link a library - only works in VS

using socketFD = SOCKET; //type definition for the file descriptor type on windows

#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> //for the close() function
#include <netdb.h>
#include <fcntl.h>

using socketFD = int; //type definition for the file descriptor type on non-windows
const int INVALID_SOCKET = -1; //already defined on windows

#endif

/*
Instructions to configure openssl in VS 2022:
1) get openssl directly from this page - https://slproweb.com/products/Win32OpenSSL.html
2) Open the 'Project' tab at the top left corner
	Open 'project name' Properties
3) Under 'Configuration Properties' navigate to 'VC++ Directories'
	In the 'Include Directories' field, add C:\\Program Files\\OpenSSL-Win64\\include
4) Under 'Configuration Properties' navigate to 'VC++ Directories'
	In the 'Library Directories' field, add C:\\Program Files\\OpenSSL-Win64\\lib
5) Under 'Linker' > 'Input' > 'Additional Dependencies, add 'libssl.lib' and 'libcrypto.lib'
6) Copy 'libssl-3-x64.dll' and 'libcrypto-3-x64.dll' to the same directory as the executable for this bot.
*/

class SSLNoReturn : public std::exception {}; //throw upon receiving SSL_ERROR_ZERO_RETURN

#ifdef _WIN32

class WSAWrapper //when using sockets on Windows this needs to be used
{
public:
	WSAWrapper();
	~WSAWrapper();

private:
	WSADATA wsaData;
};

#endif

void socketCleanup(SSL*&, socketFD);

class SSLContextWrapper //you need to make sure that this object outlives all sockets in your program
{
public:
	SSLContextWrapper();
	~SSLContextWrapper();

	SSL_CTX* get_context() const noexcept;

private:
	SSL_CTX* ssl_context;
};

class SSLSocket
{
public:
	SSLSocket(const SSLSocket&);
	SSLSocket(const SSLContextWrapper&, const std::string, bool);
	~SSLSocket();

	SSLSocket& operator=(const SSLSocket&); //I have this to make sure that I am only using the copy constructor

	void reInit(); //initialize or reinitialize the socket

	SSL* get_struct() const noexcept;

	int read(void* buffer, const int buffer_size); //could be virtual
	int write(const std::string& message); //could be virtual

protected:
	SSL* ssl_struct;
	socketFD ssl_socket;

	std::string host;

private:
	friend void socketInit(SSLSocket&); //this shouldn't be accessable outside the class

	int bytes_read;
	int bytes_write;

	int error_read; //ssl error code on read
	int error_write; //ssl error code on write

	bool blocking; //true if the socket is a blocking socket
	
	SSLContextWrapper& ssl_context_wrapper;
};

#endif
