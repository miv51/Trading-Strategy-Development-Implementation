
#include "socketUtils.h"

#ifdef _WIN32

WSAWrapper::WSAWrapper()
{
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) throw std::runtime_error("WSA Initialization failed.");
}

WSAWrapper::~WSAWrapper()
{
	WSACleanup();
}

#endif

inline void closeSocket(socketFD ssl_socket)
{
#ifdef _WIN32

	closesocket(ssl_socket);

#else

	close(ssl_socket);

#endif
}

void socketCleanup(SSL*& ssl_struct, socketFD ssl_socket)
{
	if (ssl_struct)
	{
		int shutdown_status, shutdown_attempts;

		for (shutdown_attempts = SOCKET_UTILS_MAX_SHUTDOWN_ATTEMPTS; shutdown_attempts > 0; shutdown_attempts--)
		{
			shutdown_status = SSL_shutdown(ssl_struct); //should be called twice - once for sending and once for receiving.

			if (shutdown_status == 1) break; //complete shutdown
			if (shutdown_status == 0) continue; //incomplete shutdown
			if (shutdown_status == -1) //error
			{
				shutdown_status = SSL_get_error(ssl_struct, shutdown_status);

				if (shutdown_status == SSL_ERROR_ZERO_RETURN) break; //this is considered a successful shutdown

				/*
				unsigned long ssl_error = ERR_get_error();

				std::cout << "SDS : " << shutdown_status << std::endl; //shut down status
				std::cout << "SEC : " << ssl_error << std::endl; //ssl error code
				std::cout << "SEM : " << ERR_error_string(ssl_error, nullptr) << std::endl; //ssl error message

				if (ssl_socket) closeSocket(ssl_socket);
				*/

				//throw std::runtime_error("SSL error occurred during socket cleanup.");
			}
			
			if (ssl_socket) closeSocket(ssl_socket);

			//throw std::runtime_error("Received an unknown shutdown status.");
		}

		if (shutdown_attempts <= 0)
		{
			if (ssl_socket) closeSocket(ssl_socket);

			//throw std::runtime_error("SSL Connection was not shutdown within the maximum allowed attempts.");
		}

		SSL_free(ssl_struct);

		ssl_struct = nullptr; //avoid unintended use of this now discarded object
	}

	if (ssl_socket) closeSocket(ssl_socket);
}

SSLContextWrapper::SSLContextWrapper()
{
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	ssl_context = SSL_CTX_new(TLS_client_method()); //SSLv23_client_method()

	if (!ssl_context)
	{
		ERR_free_strings();
		EVP_cleanup();

		CRYPTO_cleanup_all_ex_data();

		throw std::runtime_error("SSL context creation failed.");
	}
}

SSLContextWrapper::~SSLContextWrapper()
{
	if (ssl_context) SSL_CTX_free(ssl_context);

	ssl_context = nullptr;

	ERR_free_strings();
	EVP_cleanup();

	CRYPTO_cleanup_all_ex_data();
}

SSL_CTX* SSLContextWrapper::get_context() const noexcept
{
	return ssl_context;
}

void socketInit(SSLSocket& ssl_socket)
{
	addrinfo hints{};
	addrinfo* result = nullptr;

	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	// Resolve the hostname
	if (getaddrinfo(ssl_socket.host.c_str(), "443", &hints, &result))
	{
		freeaddrinfo(result);

		throw exceptions::exception("Failed to resolve hostname.");
	}

	bool connected = false;

	for (addrinfo* next_addr = result; next_addr != nullptr; next_addr = next_addr->ai_next)
	{
		ssl_socket.ssl_socket = socket(next_addr->ai_family, next_addr->ai_socktype, next_addr->ai_protocol);

		if (ssl_socket.ssl_socket == INVALID_SOCKET) continue; //socket creation failed
		if (connect(ssl_socket.ssl_socket, next_addr->ai_addr, next_addr->ai_addrlen) == -1) //could not connect to ssl_socket.host
		{
			closeSocket(ssl_socket.ssl_socket);

			continue;
		}

		connected = true; //we were able to connect to a host

		break;
	}

	freeaddrinfo(result);

	if (!connected) throw exceptions::exception("Either : socket creation failed, or could not connect to " + ssl_socket.host + '.');

	ssl_socket.ssl_struct = SSL_new(ssl_socket.ssl_context_wrapper.get_context());

	if (!ssl_socket.ssl_struct)
	{
		closeSocket(ssl_socket.ssl_socket);

		throw std::runtime_error("SSL structure creation failed.");
	}

	SSL_set_fd(ssl_socket.ssl_struct, ssl_socket.ssl_socket);

	//Server Name Indication (SNI) is needed for alpaca since alpaca has multiple domain names
	if (SSL_set_tlsext_host_name(ssl_socket.ssl_struct, ssl_socket.host.c_str()) != 1)
	{
		socketCleanup(ssl_socket.ssl_struct, ssl_socket.ssl_socket);

		throw exceptions::exception("SNI failed for " + ssl_socket.host);
	}

	if (SSL_connect(ssl_socket.ssl_struct) != 1)
	{
		socketCleanup(ssl_socket.ssl_struct, ssl_socket.ssl_socket);

		throw exceptions::exception("SSL handshake failed for " + ssl_socket.host);
	}

	//set the socket to non-blocking
	if (!ssl_socket.blocking)
	{

#ifdef _WIN32

		u_long non_blocking = 1;

		if (ioctlsocket(ssl_socket.ssl_socket, FIONBIO, &non_blocking) == SOCKET_ERROR)
		{
			socketCleanup(ssl_socket.ssl_struct, ssl_socket.ssl_socket);

			throw std::runtime_error("Could not set socket mode to non-blocking.");
		}
#else
		int socket_flags = fcntl(ssl_socket.ssl_socket, F_GETFL, 0);

		fcntl(ssl_socket.ssl_socket, F_SETFL, socket_flags | O_NONBLOCK);
#endif
		SSL_set_mode(ssl_socket.ssl_struct, SSL_MODE_AUTO_RETRY);
	}
}

SSLSocket::SSLSocket(const SSLSocket& other_socket)
	: ssl_context_wrapper(const_cast<SSLContextWrapper&>(other_socket.ssl_context_wrapper)), host(other_socket.host), blocking(other_socket.blocking), ssl_struct(nullptr) {}

SSLSocket::SSLSocket(const SSLContextWrapper& SSL_context_wrapper, const std::string Host, const bool Blocking) //assumes port = 443
	: ssl_context_wrapper(const_cast<SSLContextWrapper&>(SSL_context_wrapper)), host(Host), blocking(Blocking), ssl_struct(nullptr) {}

SSLSocket::~SSLSocket()
{
	socketCleanup(ssl_struct, ssl_socket);
}

SSLSocket& SSLSocket::operator=(const SSLSocket& other_socket)
{
	throw std::runtime_error("SSLSocket type doesn't support re-assignment.");
}

void SSLSocket::reInit()
{
	socketCleanup(ssl_struct, ssl_socket);
	socketInit(*this);
}

SSL* SSLSocket::get_struct() const noexcept
{
	return ssl_struct;
}

int SSLSocket::read(void* buffer, const int buffer_size)
{
	bytes_read = SSL_read(ssl_struct, buffer, buffer_size);

	if (bytes_read > 0) return bytes_read;

	error_read = SSL_get_error(ssl_struct, bytes_read);

	if (error_read == SSL_ERROR_ZERO_RETURN) throw SSLNoReturn();
	if (error_read != SSL_ERROR_WANT_READ)
	{
		unsigned long ssl_error = ERR_get_error();

		throw exceptions::exception(std::string("SSL error occured when reading - ssl error no. ") + std::to_string(error_read) + \
			std::string(" with error no. ") + std::to_string(ssl_error));
	}

	return 0;
}

int SSLSocket::write(const std::string& message)
{
	bytes_write = SSL_write(ssl_struct, message.c_str(), message.size());

	if (bytes_write > 0) return bytes_write;

	error_write = SSL_get_error(ssl_struct, bytes_write);

	//keep calling SSLSocket::write while receiving SSL_ERROR_WANT_WRITE

	if (error_write == SSL_ERROR_ZERO_RETURN) throw SSLNoReturn();
	if (error_write != SSL_ERROR_WANT_WRITE)
	{
		unsigned long ssl_error = ERR_get_error();

		throw exceptions::exception(std::string("SSL error occured when writing - ssl error no. ") + std::to_string(error_write) + \
			std::string(" with error no. ") + std::to_string(ssl_error));
	}

	return 0;
}
