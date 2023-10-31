
#include "jsonUtils.h"

void preProcessJSON(std::string& json)
{
    size_t str_length = json.size();

    if (str_length <= 2) throw exceptions::exception("Cannot parse empty json."); //{}
    if (json[0] != '{' || json[str_length - 1] != '}') throw exceptions::exception("Received an unexpected json format.");
    
    json.erase(0, 1);
    json.pop_back();
    json += ',';
}

void preProcessJSONArray(std::string& json_array)
{
    size_t str_length = json_array.size();

    if (str_length <= 2) throw exceptions::exception("Cannot parse empty json array."); //[]
    if (json_array[0] != '[' || json_array[str_length - 1] != ']') throw exceptions::exception("Received an unexpected json array format.");

    json_array.erase(0, 1);
    json_array.pop_back();
    json_array += ',';
}

void parseJSON(dictionary& pairs, const std::string& json)
{
    std::string key;
    std::string value;

    bool on_key = true; //true if we are evaluating a key

    short int under_array = 0; //should be 0 by the time parsing is done
    short int under_object = 0; //should be 0 by the time parsing is done
    short int under_quote = 0; //should be 0 by the time parsing is done

    for (const char& c : json)
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
        else if (under_object)
        {
            if (c == '}') under_object--;
            else if (c == '{') under_object++;

            value += c;
        }
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
            pairs[key] = value;

            key.clear();
            value.clear();

            on_key = true;
        }
        else if (c == ' ') continue;
        else if (on_key) key += c;
        else value += c;
    }

    if (under_array || under_object || under_quote || !on_key) throw std::runtime_error("Invalid JSON format.");
}
