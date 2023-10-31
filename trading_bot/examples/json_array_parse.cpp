
//parse a json array

#include "jsonUtils.h"

#include <iostream>
#include <vector>
#include <string>

struct container; //holds information fron the json object that is currently being parsed
typedef std::vector<container> updateObject; //update this object with the most recently parsed json object

//update the container object using this function with the most recently obtained key : value pair
void containerUpdateFunction(container& container_object, const std::string& key, const std::string& value);

//update the updateObject using this function with the information in the container
void objectUpdateFunction(const container& container_object, updateObject& update_object);

struct container
{
    std::string t; //timestamp
    std::string s; //symbol

    int v; //volume
    int n; //number of trades executed
    
    double c; //close
    double o; //open
    double h; //high
    double l; //low
};

void containerUpdateFunction(container& container_object, const std::string& key, const std::string& value)
{
    if (key == "t") container_object.t = value;
    if (key == "s") container_object.s = value;

    else if (key == "v") container_object.v = std::stoi(value);
    else if (key == "n") container_object.n = std::stoi(value);

    else if (key == "c") container_object.c = std::stod(value);
    else if (key == "o") container_object.o = std::stod(value);
    else if (key == "h") container_object.h = std::stod(value);
    else if (key == "l") container_object.l = std::stod(value);
}

void objectUpdateFunction(const container& container_object, updateObject& update_object)
{
    update_object.push_back(container_object);
}

int main()
{
    //parse an array of candle sticks (format is real but the data is not)
    std::string json_array = "[{\"t\":\"2001-05-11T:09:42:00Z\", \"v\":10295, \"c\":22.05, \"o\":21.77, \"l\":21.60, \"h\":22.25, \"n\":205, \"s\":\"FAKE\"},\
        {\"t\":\"2001-05-11T:11:25:00Z\", \"v\":328166, \"c\":4.00, \"o\":3.5, \"l\":3.48, \"h\":4.2, \"n\":622, \"s\":\"BOGUS\"}]";

    JSONArrayParser<container, updateObject, containerUpdateFunction, objectUpdateFunction> array_parser;

    updateObject candle_stick_array;

    preProcessJSONArray(json_array);
    array_parser.parseJSONArray(json_array, candle_stick_array);

    //print the candle sticks
    for (const container& candle_stick : candle_stick_array)
    {
        std::cout << std::endl;
        std::cout << "s : " << candle_stick.s << std::endl;
        std::cout << "t : " << candle_stick.t << std::endl;
        std::cout << "v : " << candle_stick.v << std::endl;
        std::cout << "n : " << candle_stick.n << std::endl;
        std::cout << "c : " << candle_stick.c << std::endl;
        std::cout << "o : " << candle_stick.o << std::endl;
        std::cout << "h : " << candle_stick.h << std::endl;
        std::cout << "l : " << candle_stick.l << std::endl;
    }

    return 0;
}
