
//custom json utilities needed specifically for this bot

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <string>
#include <stdexcept>

#ifndef TYPEDEF_DICTIONARY
#define TYPEDEF_DICTIONARY

#include <unordered_map>

typedef std::unordered_map<std::string, std::string> dictionary;

#endif

#define IS_NULL_CHAR(chr) if (chr == '\0') throw std::runtime_error("Invalid JSON Format.");

/*
a class for parsing one layer of a json object
does not check for valid data types, it just parses based on token values
*/

class JSONParser
{
private:
	std::string key;
	std::string value;

public:
	JSONParser() {}
	~JSONParser() {}

	void parseJSON(dictionary&, const std::string&);
};

/*
a class for parsing json arrays
container - structure for storing or updating with json data
updateObject - the object we are updating with the information from the container
containerUpdateFunc - update information in container
updateFunc - use the data in the container to do something to updateObject

Logic flow ...
1) container is updated/modified/filled as the current json object is being parsed
2) updateObject is updated/modified with data in the container after the current json object is fully parsed
3) go back to step 1
*/

template <typename container, typename updateObject, void (*containerUpdateFunc)(container&, const std::string&, const std::string&), void (*updateFunc)(const container&, updateObject&)>
class JSONArrayParser
{
private:
	container information; //contains information about the most recently parsed json object

	std::string key;
	std::string value;

public:
	JSONArrayParser() {}
	~JSONArrayParser() {}

	//return a read-only reference to the information container
	inline const container& get_info() const noexcept { return information; }

	void parseJSONArray(const std::string& json, updateObject& update_object)
	{
		const char* c = json.c_str();
		const char* start = c;

		unsigned short int level = 0;

		while (true)
		{
			while (*c != '{' && *c != '\0') c++; //find the beginning of the next json object in the array

			if (*c == '\0') return;

			while (true)
			{
				//we know c is '{' (before the increment) based on the logic statements above
				while (*(++c) != '"') { IS_NULL_CHAR(*c); } //find the beginning of the next key in the json object

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

						while (*(++c) != '"') IS_NULL_CHAR(*c); //ignore nested arrays while reading a substring

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

						while (*(++c) != '"') IS_NULL_CHAR(*c); //ignore nested arrays while reading a substring

						c++;
					}
				}
				else //value is a number, boolean, or null
				{
					start = c;

					while (*c != ',' && *c != '}' && *c != ' ') { IS_NULL_CHAR(*c); c++; } //key : value pairs should be separated by commas

					if (start == c) throw std::runtime_error("JSON Key is missing a value.");
				}

				value.assign(start, c - start); //much more efficient than value += c;

				while (*c != ',' && *c != '}') { IS_NULL_CHAR(*c); c++; } //key : value pairs should be separated by commas

				containerUpdateFunc(information, key, value);

				if (*c == '}') break; //start parsing the next json object in the array
			}

			updateFunc(information, update_object);
		}
	}
};

#endif