
#include "jsonUtils.h"

void JSONParser::parseJSON(dictionary& pairs, const std::string& json)
{
	const char* c = json.c_str();
	const char* start = c;

	unsigned short int level = 0;

	while (true)
	{
		while (*(c) != '"') { IS_NULL_CHAR(*c); c++; } //find the beginning of the next key in the json object

		IS_NULL_CHAR(*c);

		start = ++c;

		while (*c != '"') { IS_NULL_CHAR(*c); c++; } //find the end of the current key

		IS_NULL_CHAR(*c);

		key.assign(start, c - start); //much more efficient than key += c;

		//we know c is '"' (before the increment) based on the logic statement above
		while (*(++c) != ':') { IS_NULL_CHAR(*c); } //find the separator between the key and value pair
		while (*(++c) == ' ') { IS_NULL_CHAR(*c); } //ignore any spaces between the value and separator

		IS_NULL_CHAR(*c);

		if (*c == '"') //value is a string
		{
			start = ++c;

			IS_NULL_CHAR(*c);

			while (*c != '"') { IS_NULL_CHAR(*c); c++; }
		}
		else if (*c == '[') //value is a sub array
		{
			start = ++c;
			level = 1; //keep track of nested arrays

			IS_NULL_CHAR(*c);

			while (true)
			{
				while (*c != '"') //while we are not reading a substring
				{
					if (*c == '[') level++;
					else if (*c == ']')
					{
						level--;

						if (level == 0) break;
					}

					IS_NULL_CHAR(*(++c));
				}

				if (level == 0) break;

				while (*(++c) != '"') { IS_NULL_CHAR(*c); } //ignore nested arrays while reading a substring

				c++;
			}
		}
		else if (*c == '{') //value is a nested json object
		{
			start = ++c;
			level = 1; //keep track of nested arrays

			IS_NULL_CHAR(*c);

			while (true)
			{
				while (*c != '"') //while we are not reading a substring
				{
					if (*c == '{') level++;
					else if (*c == '}')
					{
						level--;

						if (level == 0) break;
					}

					IS_NULL_CHAR(*(++c));
				}

				if (level == 0) break;

				while (*(++c) != '"') { IS_NULL_CHAR(*c); } //ignore nested arrays while reading a substring

				c++;
			}
		}
		else //value is a number, boolean, or null
		{
			start = c;

			while (*c != ',' && *c != '}' && *c != ' ' && *c != '\0') c++; //key : value pairs should be separated by commas

			if (start == c) throw std::runtime_error("JSON Key is missing a value.");
		}

		value.assign(start, c - start); //much more efficient than value += c;

		while (*c != ',' && *c != '\0') c++; //key : value pairs should be separated by commas

		pairs[key] = value;

		if (*c == '\0') return;
	}
}