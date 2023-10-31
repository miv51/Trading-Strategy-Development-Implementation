
//Schedule events such as gathering data at the start of the day and start/stop trading

#ifndef TRADING_BOT_H
#define TRADING_BOT_H

#include "arrayUtils.h"
#include "modelUtils.h"
#include "jsonUtils.h"
#include "wsUtils.h"
#include "ntpUtils.h"
#include "httpUtils.h"
#include "socketUtils.h"
#include "exceptUtils.h"

#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <ctime>
#include <cmath>
#include <deque>

//#define TRADE_BOT_DEBUG
#define K0(n) ((1.1924 + 33.2383 * n + 56.2169 * n * n) / (1.0 + 43.6196 * n)) //cubic root is not necessary

const int past_days = 2000; //number of days we look back to gather data (includes non-trading days)
const int max_clients = 16; //maximum number of http clients used to gather data asynchronously

//errors we can safely ignore when submitting certain orders
const std::string order_not_open = "order is not open";
const std::string qty_le_filled = "qty must be > filled_qty";
const std::string qty_le_filled_bin = "qty must be \\u003e filled_qty"; // supposed to be the same as qty_le_filled - to the best of my knowledge this is a bug

struct bar;
struct symbol;
struct tradeOrBarUpdate;

typedef std::unordered_map<std::string, symbol> symbolContainer;
typedef array<bar, past_days> dailyBarContainer;

class symbolData;

void handleTradeUpdate(std::string&, dictionary&, symbolData&); //handle account updates from orders submitted by the bot

void updateDailyBar(bar&, const std::string&, const std::string&); //get the daily volume and closing price from a parsed json object
void updateDailyData(const bar&, dailyBarContainer&); //append the daily bar to a container
void updateIntradayData(const bar&, symbol&); //add the volume of this intraday bar to the cumulative traded volume over the day for a symbol

void getAvailableSymbols(std::vector<symbol>&, std::string&); //get available symbols to trade

void updateSymbol(symbol&, const std::string&, const std::string&); //update individual symbol data
void updateSymbolData(const symbol&, std::vector<symbol>&); //append symbol to symbol data container

double getBuyingPower(std::string&); //get non-marginable buying power and check for restrictions on the alpaca account

void updateTradeOrBarInfo(tradeOrBarUpdate&, const std::string&, const std::string&); //update information from json key : value pair
void updateSymbolData(const tradeOrBarUpdate&, symbolData&); //update features of the respective symbol

typedef JSONArrayParser<tradeOrBarUpdate, symbolData, updateTradeOrBarInfo, updateSymbolData> tradeAndBarParser;

void closeAllPositions(symbolData&, websocket&, websocket&);

//put all relevant features and model inputs for each ticker symbol here
struct symbol
{
	std::string exchange; //listing exchange
	std::string ticker; //ticker symbol
	std::string type; //symbol class - us_equity, crypto, forex, etc...

	bool active = false; //true if status == "active"
	bool tradable = false; //stock is tradable if true

	//not needed for long-biased strategies such as this one
	bool shortable = false; //true if the stock can be shorted
	bool can_borrow = false; //true if the stock can be easily borrowed

	bool is_an_outlier = false; //true if any of the features calculated with daily data is an outlier
	bool trading_permitted = false; //true if we can trade this stock

	double previous_days_close = 0.0;
	double average_volume = 0.0; //70-day average volume
	double mean = 0.0;
	double std = 0.0;
	double pp = 0.0;
	double pm = 0.0;
	double E0 = 0.0;
	double l = 0.0; //lambda

	long long dt = 0; //difference in time between the first and last filtered trade within the last rolling time period - in nanoseconds
	double dp = 0.0; //difference in price between the first and last filtered trade within the last rolling time period

	//int rolling_csum; //number of filtered trades that occured within the last rolling time period
	int rolling_vsum = 0; //total shares traded of all the filtered trades that occured within the last rolling time period
	int vsum = 0; //volume sum over the current day - updated every minute

	int new_n = 0; //n of the current quantum price level
	int n = 0; //n of the most recently hit quantum price level

	long long t = 0; //nanoseconds since midnight

	bool found_first_n = false; //true if this symbol already has a reference n - this prevents the bot from taking trades before n is initialized

	std::deque<long long> time_stamps; //timestamp in nanoseconds since midnight of each filtered trade for this stock traded in the past 2 seconds
	std::deque<double> prices; //prices of each filtered trade for this stock traded in the past 2 seconds
	std::deque<int> sizes; //number of shares traded for each filtered trade in the past 2 seconds

	//variables below are for quantum price level calculations

	int abs_n = 0; //absolute value of n

	double C0 = 0.0;
	double C1 = 0.0;

	double E = 0.0;

	double getPriceLevel(const int); //quantum price level calculation

	//variables below are for managing positions

	int quantity_owned = 0; //number of shares owned
	int quantity_pending = 0; //number of shares pending buy (pending sell if negative)
	int quantity_desired = 0; //number of shares the bot wants to own

	bool canceled_order = false; //true if the current pending order has been canceled
	bool waiting_for_update = false; //true if the bot is waiting on an update from the account update stream

	std::string order_id; //the id of the last order created or modified by the bot for a particular stock
	std::string replacement_order_id; //the id of the order that will replace the order with id order_id

	int order_quantity = 0; //number of shares placed in the last order created or modified by the bot for a particular stock
	int order_quantity_filled = 0; //number of shares filled in the last order created or modified by the bot for a particular stock

	double average_fill_price = 0.0; //average fill price of the shares that have currently been traded for this order
	double entry_price = 0.0; //price to set the buy or sell order at
	double limit_price = 0.0; //price of the last or current limit order

	void updatePosition(symbolData&, const bool); //manage this stock's position size - call every trade AND account update
};

//contains information about a trade or minute bar update
//does not contain all available information, just the information this bot needs
struct tradeOrBarUpdate //trade, bar, or quote update
{
	std::string T; //message type - expecting any of "t", "b", "q", "c", "x", "error", "success", "subscription"
	std::string msg; //error message

	int code = 0; //in case of an error
	int v = 0; //bar update - number of shares traded over the last bar period
	int s = 0; //trade update - number of shares traded

	double p = 0.0; //trade update - price that the trade occured at
	char x = '\0'; //trade update - the exchange the trade occured on

	double bp = 0.0; //quote update - price of the current best bid
	char bx = '\0'; //quote update - exchange of the current best bid

	std::string t; //trade update - time that the trade occured at in number of nanoseconds since epoch -- long long
	std::string c; //trade update - trade conditions
	std::string S; //trade and bar update - ticker symbol
};

//contains information about a bar
//does not contain all available information, just the information this bot needs
struct bar
{
	int v = 0; //number of shares traded over the last bar period

	double c = 0.0; //closing price of the last bar

	std::string t; //time that the trade occured at in number of nanoseconds since epoch -- long long
};

class symbolData : public symbolContainer
{
public:
	symbolData(const SSLContextWrapper&, const MLModel&);
	symbolData(const SSLContextWrapper&, const MLModel&, size_t);

	~symbolData();

	//information we need to make trading decisions

	double buying_power = 0.0;
	double risk_per_trade = 0.0;

	//information we need to submit/cancel/replace orders

	std::string account_endpoint;
	std::string body;

	time_t timeout = 0; //http response timeout for receiving sumbitted order responses

	http::httpResponse response; //read responses from sending order requests from here

	dictionary base_parameters;
	dictionary base_headers;
	dictionary order_data; //parsed data from the last handled order

	SSLContextWrapper& ssl_context_wrapper;
	MLModel& model;

	void submitOrder(const std::string&, const int&, const std::string&, const double&);
	void replaceOrder(const std::string&, const int&, const double&);
	void cancelOrder(const std::string&);
	void cancelAllOrders();
};

class tradingBot
{
public:
	tradingBot(const SSLContextWrapper&, const std::string, const std::string, const std::string, const std::string, const double, const double);

	void start(); //start the bot

private:
	double allocated_buying_power; //total amount of cash the bot is allowed to use
	double risk_per_trade; //amount of USD to risk on each trade

	//std::string ntp_server = "pool.ntp.org"; //network time protocol server

	std::string alpaca_api_key;
	std::string alpaca_secret_key;
	std::string account_endpoint;
	std::string trade_update_stream;

	SSLContextWrapper& ssl_context_wrapper;
	ntpClient time_proto;
};

#endif
