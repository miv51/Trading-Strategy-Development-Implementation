
//parse a single json object

#include "jsonUtils.h"

#include <iostream>
#include <string>

int main()
{
    //the json objects we will parse
    std::string json_object0 = "{\"key0\":\"value0\", \"key1\":3.1415, \"key2\":null}";
    std::string json_object1 = "{\"jsonArray\":[0, 1, 2, 4, -6], \"nestedJsonObject\":{\"a\":\"Hello World!\", \"b\":false}}";

    std::cout << "Original Json Object - " << json_object0 << "\n\n";

    dictionary key_value_pairs0; //typedef std::unordered_map<std::string, std::string> dictionary;

    preProcessJSON(json_object0); //transforms {"key0":"value0", "key1":3.1415, "key2":null} into "key0":"value0", "key1":3.1415, "key2":null,
    parseJSON(key_value_pairs0, json_object0); //adds key value pairs to a dictionary

    std::cout << "Parsed Json Object key : value pairs" << std::endl;

    for (const auto& pair : key_value_pairs0) std::cout << pair.first << " : " << pair.second << std::endl;

    std::cout << std::endl;

    std::cout << "Original Json Object - " << json_object1 << "\n\n";

    dictionary key_value_pairs1;

    preProcessJSON(json_object1);
    parseJSON(key_value_pairs1, json_object1);

    std::cout << "Parsed Json Object key : value pairs" << std::endl;

    for (const auto& pair : key_value_pairs1) std::cout << pair.first << " : " << pair.second << std::endl;

    return 0;
}
