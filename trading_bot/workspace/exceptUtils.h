
//a portable base exception class
//needed for non-windows platforms since, on those, std::exception doesn't take a character array as a constructor argument

#ifndef EXCEPT_UTILS_H
#define EXCEPT_UTILS_H

#include <stdexcept>
#include <string>

namespace exceptions
{
	class exception : public std::exception
	{
	public:
		exception() : message("Unknown Exception.") {}
		exception(const char* Message) : message(Message) {}
		exception(const std::string Message) : message(Message) {}

		virtual const char* what() const noexcept override { return message.c_str(); }

	private:
		std::string message;
	};
}

#endif
