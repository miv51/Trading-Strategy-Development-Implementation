
// qpl_bot_strategy_equities.cpp : This file contains the 'main' function. Program execution begins and ends there.
//


#include "exceptUtils.h"
#include "tradingBot.h"

#include <stdexcept>
#include <iostream>
#include <string>

bool isNonNegativeNumeric(const std::string& string) //check to see if this string can be converted to a non-negative float
{
    bool has_decimal = false;

    for (const char &character : string)
    {
        if (character == '.')
        {
            if (has_decimal) return false;

            has_decimal = true;

            continue;
        }
        if (character < 48 || character > 57) return false; // char(48) == '0' and char(57) == '9'
    }
    
    return true;
}

double promptFloatInput(const char* message)
{
    std::string text_input;

    std::cout << std::endl;
    std::cout << message;

    while (true)
    {
        std::cin >> text_input;

        if (isNonNegativeNumeric(text_input)) return std::stod(text_input);

        std::cout << "Input must be a non-negative number : ";
    }
}

int main()
{
    std::string text_input;
    std::string account_endpoint = "paper-api.alpaca.markets";
    std::string trade_update_stream = "paper-api.alpaca.markets";

    double risk_per_trade; //amount of cash to risk per trade
    double allocated_buying_power; //amount of cash to allow the bot to use

    //Keep sensitive information like this out of the source code
    std::string alpaca_api_key;
    std::string alpaca_secret_key;

    //ask the user for the parameters
    do
    {
        risk_per_trade = promptFloatInput("Enter the amount of cash (in USD) you want to risk per trade : ");
        allocated_buying_power = promptFloatInput("Enter the amount of cash (in USD) you want to allow this bot to use : ");

        std::cout << std::endl;
        std::cout << "Enter your Alpaca api key : ";
        std::cin >> alpaca_api_key;

        std::cout << std::endl;
        std::cout << "Enter your Alpaca secret key : ";
        std::cin >> alpaca_secret_key;

        std::cout << std::endl;
        std::cout << "Type 'YES' without the quotes if you want to use a live alpaca account." << std::endl;
        std::cout << "Type anything else (cannot be blank) if you want to use a paper account : ";
        std::cin >> text_input;

        if (text_input == "YES")
        {
            account_endpoint = "api.alpaca.markets";
            trade_update_stream = "api.alpaca.markets";
        }

        std::cout << std::endl;
        std::cout << "Double check all of your parameters and type 'continue' without the quotes to start the bot." << std::endl;
        std::cout << "Type anything else (cannot be blank) to re-enter your parameters : ";
        std::cin >> text_input;
    }
    while (text_input != "continue");

    if (account_endpoint == "api.alpaca.markets") std::cout << "This bot will trade LIVE." << std::endl << std::endl;
    else std::cout << "This bot will trade on PAPER." << std::endl << std::endl;

#ifdef _WIN32

    WSAWrapper wsa_wrapper; //needed on Windows only - destructor must be called after all sockets are closed

#endif

    SSLContextWrapper ssl_context; //destructor must be called after all sockets are closed
    
    try
    {
        tradingBot bot(ssl_context, account_endpoint, trade_update_stream, alpaca_api_key, alpaca_secret_key, allocated_buying_power, risk_per_trade);

        try { bot.start(); }
        catch (const std::runtime_error& runtime_error) { throw runtime_error; }
        catch (const exceptions::exception& exception) { throw exception; }
        catch (const SSLNoReturn& no_return) { throw no_return; }
        catch (const std::exception& exception) { throw exception; }
    }
    catch (const std::runtime_error& runtime_error) { std::cout << "Runtime Error caught : " << runtime_error.what() << std::endl; }
    catch (const SSLNoReturn&) { std::cout << "Exception caught : A socket or websocket closed unexpectedly." << std::endl; }
    catch (const exceptions::exception& exception) { std::cout << "Exception caught : " << exception.what() << std::endl; }
    catch (const std::exception& exception) { std::cout << "Base Exception caught : " << exception.what() << std::endl; }
    catch (...) { std::cout << "Unknown Exception Caught." << std::endl; }

    std::cout << std::endl << "Type anything and press enter to exit.";
    std::cin >> text_input;
}
