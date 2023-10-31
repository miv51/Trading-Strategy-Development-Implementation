
//custom json utilities needed specifically for this bot

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include "exceptUtils.h"

#include <string>
#include <stdexcept>

#ifndef TYPEDEF_DICTIONARY
#define TYPEDEF_DICTIONARY

#include <unordered_map>

typedef std::unordered_map<std::string, std::string> dictionary;

#endif

void preProcessJSON(std::string&);
void preProcessJSONArray(std::string&);

/*
parses one layer of a json object - assumes json string doesn't have the outer curly brackets
example : {"key0":value0, "key1":value1} should be "key0":value0, "key1":value1
does not check for valid data types, it just parses based on token values
*/

void parseJSON(dictionary&, const std::string&);

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

	//assumes no square brackets contain the array objects and the last object has a comma at the end
	//example : [{"a":"b", "c":4}, {"a":"d", "c":6}] should be {"a":"b", "c":4}, {"a":"d", "c":6},
	void parseJSONArray(const std::string& json, updateObject& update_object)
	{
		key.clear();
		value.clear();

		bool on_key = true; //true if we are evaluating a key

		short int under_array = 0; //should be 0 by the time parsing is done
		short int under_object = 0; //should be 0 by the time parsing is done
		short int under_quote = 0; //should be 0 by the time parsing is done

		for (const char& c : json)
		{
			if (under_object)
			{
				if (under_quote)
				{
					if (c == '"') under_quote--;
					else if (on_key) key += c;
					else value += c;
				}
				else if (under_array)
				{
					if (c == ']') under_array--;
					else if (c == '[') under_array++;

					value += c;
				}
				else if (under_object > 1)
				{
					if (c == '}') under_object--;
					else if (c == '{') under_object++;

					value += c;
				}
				else if (c == '}') under_object--;
				else if (c == '{')
				{
					under_object++;

					value += c;
				}
				else if (c == '[')
				{
					under_array++;

					value += c;
				}
				else if (c == '"') under_quote++;
				else if (c == ':') on_key = false;
				else if (c == ',')
				{
					containerUpdateFunc(information, key, value);

					key.clear();
					value.clear();

					on_key = true;
				}
				else if (c == ' ') continue;
				else if (on_key) key += c;
				else value += c;
			}
			else if (c == '{') under_object++;
			else if (c == ',')
			{
				containerUpdateFunc(information, key, value);

				key.clear();
				value.clear();

				on_key = true;

				updateFunc(information, update_object);
			}
		}

		if (under_array || under_object || under_quote || !on_key) throw std::runtime_error("Invalid JSON format.");
	}
};

#endif
