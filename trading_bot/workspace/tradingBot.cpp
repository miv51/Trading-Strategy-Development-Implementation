
#include "tradingBot.h"

tradingBot::tradingBot(const SSLContextWrapper& SSL_context_wrapper, const std::string Account_endpoint, const std::string Trade_update_stream,
	const std::string Alpaca_api_key, const std::string Alpaca_secret_key, const double Allocated_buying_power,
	const double Risk_per_trade)
	: alpaca_api_key(Alpaca_api_key),
	alpaca_secret_key(Alpaca_secret_key),
	account_endpoint(Account_endpoint),
	trade_update_stream(Trade_update_stream),
	allocated_buying_power(Allocated_buying_power),
	risk_per_trade(Risk_per_trade),
	ssl_context_wrapper(const_cast<SSLContextWrapper&>(SSL_context_wrapper)),
	time_proto()
{}

inline void sleepFor(time_t sleep_time)
{
	std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
}

inline double roundPrice(const double price) //limit prices under 1 USD can have up to four decimal places, otherwise they can have up to two
{
	if (price < 1.0) return std::round(price * 10000.0) / 10000.0;

	return std::round(price * 100.0) / 100.0;
}

//perform http get request until it is completed without the socket closing (this happens very rarely)
inline void getUntil(const SSLContextWrapper& ssl_context_wrapper, http::httpResponse& response, const dictionary& parameters, const dictionary& headers,
	const std::string& host, const std::string& path, time_t timeout, int allowed_retries)
{
	while (allowed_retries > 0)
	{
		try { http::get(ssl_context_wrapper, response, parameters, headers, host, path, timeout); return; }
		catch (const SSLNoReturn&)
		{
			response.clear();
			sleepFor(timeout);
		}

		allowed_retries--;
	}

	throw exceptions::exception(std::string("Http get request failed to complete within ") + std::to_string(allowed_retries) + std::string(" attempts."));
}

void tradingBot::start()
{
	try
	{
		while (true)
		{
			time_proto.update();

#ifdef USE_MARKET_ORDERS

			//market orders can only be executed during regular market hours (9:30am - 4pm EST)
			const int start_hour = 9;
			const int start_minute = 31;

			const int data_start_hour = 9; //start gathering data at this hour
			const int data_start_minute = 26; //start gathering data at this minute of the specified data_start_hour
#else
			const int start_hour = 8;
			const int start_minute = 0;

			const int data_start_hour = 8;
			const int data_start_minute = 0;
#endif

#ifndef TRADE_BOT_DEBUG

			if (time_proto.iso_weekday == 6) //if today is saturday sleep until monday
			{
				std::cout << "SLEEPING UNTIL MONDAY" << std::endl;

				time_t sleep_time = time_proto.getSecondsSinceEpoch(2, data_start_hour, data_start_minute, 0) - time(nullptr); //sleep until the starting time of the next day

				std::this_thread::sleep_for(std::chrono::seconds(sleep_time));

				continue;
			}

			//if current time is past 3:50pm EST or today is sunday then sleep until the next day
			if (time_proto.iso_weekday == 0 || time(nullptr) > time_proto.getSecondsSinceEpoch(0, 15, 50, 0))
			{
				std::cout << "SLEEPING UNTIL TOMORROW" << std::endl;

				time_t sleep_time = time_proto.getSecondsSinceEpoch(1, data_start_hour, data_start_minute, 0) - time(nullptr); //sleep until starting time of the next day

				std::this_thread::sleep_for(std::chrono::seconds(sleep_time));

				continue;
			}

			time_t sleep_time = time_proto.getSecondsSinceEpoch(0, data_start_hour, data_start_minute, 0) - time(nullptr); //sleep until starting time of today

			if (sleep_time > 0) sleepFor(sleep_time);

			/*
			check to see if today is a market holiday
				if the market is not open today sleep until tomorrow
				if the market closes early sleep until tomorrow
				otherwise set the close time to 3:55pm EST
			*/

			time_t trading_start_time = time_proto.getSecondsSinceEpoch(0, start_hour, start_minute, 0);
			time_t trading_end_time = time_proto.getSecondsSinceEpoch(0, 15, 55, 0);
#else
			time_t trading_start_time = time_proto.getSecondsSinceEpoch(0, 0, 0, 0);
			time_t trading_end_time = time_proto.getSecondsSinceEpoch(2, 0, 0, 0);
#endif
			time_t timeout = 10; //timeout in seconds
			int allowed_retries = 6; //number of times some requests can be retried

			dictionary headers;
			dictionary parameters;

			http::httpResponse response;

			parameters["date_type"] = "TRADING";
			parameters["start"] = std::string(time_proto.date);
			parameters["end"] = std::string(time_proto.date);

			headers["APCA-API-SECRET-KEY"] = alpaca_secret_key;
			headers["APCA-API-KEY-ID"] = alpaca_api_key;
			headers["User-Agent"] = "c++20-requests";
			headers["Connection"] = "close";

			dictionary response_data; //data from the last full http response
			JSONParser json_parser; //general json parser for individual json objects

#ifndef TRADE_BOT_DEBUG

			getUntil(ssl_context_wrapper, response, parameters, headers, account_endpoint, "/v2/calendar", timeout, allowed_retries);

			if (response.status_code != 200) throw exceptions::exception(std::to_string(response.status_code) + " status code not accounted for.");
			if (response.message.size() < 4) //on some holidays, an empty array is received - so assume today is a holiday
			{
				std::cout << "RECEIVED NO DATA FROM CALENDAR - ASSUMING TODAY IS A HOLIDAY - SLEEPING UNTIL TOMORROW" << std::endl;

				time_t sleep_time = time_proto.getSecondsSinceEpoch(1, data_start_hour, data_start_minute, 0) - time(nullptr); //sleep until starting time of the next day

				sleepFor(sleep_time);

				continue;
			}

			json_parser.parseJSON(response_data, response.message);

			if (response_data.find("close") == response_data.end()) throw exceptions::exception("Could not obtain today's closing time.");
			if (response_data.find("open") == response_data.end()) throw exceptions::exception("Could not obtain today's opening time.");

			//if the closing time is not 4pm or the starting time is not 9:30am then today is a holiday (including early-close) sleep until the next day
			if (response_data["close"] != std::string("16:00") || response_data["open"] != std::string("09:30"))
			{
				std::cout << "TODAY IS A HOLIDAY - SLEEPING UNTIL TOMORROW" << std::endl;

				time_t sleep_time = time_proto.getSecondsSinceEpoch(1, data_start_hour, data_start_minute, 0) - time(nullptr); //sleep until starting time of the next day

				sleepFor(sleep_time);

				continue;
			}
#endif
			/*
			get the current capital in your alpaca account
				check to make sure there are no restrictions on your account
				set the available trading capital
			*/

			response_data.clear();
			parameters.clear();
			response.clear();

			getUntil(ssl_context_wrapper, response, parameters, headers, account_endpoint, "/v2/account", timeout, allowed_retries);

			if (response.status_code != 200) throw exceptions::exception(std::to_string(response.status_code) + " status code not accounted for.");

			double non_marginable_buying_power = getBuyingPower(response.message); //I don't use margin

			if (non_marginable_buying_power < allocated_buying_power) throw exceptions::exception("Not enough cash to allocate to this bot.");
			if (allocated_buying_power < risk_per_trade) throw exceptions::exception("Not enough cash to risk on a single trade.");

			/*
			get all available stocks to trade
			*/

			parameters["status"] = "active";
			parameters["asset_class"] = "us_equity";

			response.clear();

			getUntil(ssl_context_wrapper, response, parameters, headers, account_endpoint, "/v2/assets", timeout, allowed_retries);

			if (response.status_code != 200) throw exceptions::exception(std::to_string(response.status_code) + " status code not accounted for.");

			std::vector<symbol> symbols;

			symbols.reserve(10000);

			//gather all active symbols
			getAvailableSymbols(symbols, response.message);

			if (!symbols.size()) throw exceptions::exception("No symbols available to trade.");

			size_t num_symbols_left = symbols.size() - 1;

			/*
			gather daily data and calculate daily features for symbols
			*/

			parameters.clear();

			char start_date[11];
			char yesterday[11];

			time_proto.getPastDate(start_date, past_days);
			time_proto.getPastDate(yesterday, 1);

			parameters["timeframe"] = "1Day";
			parameters["start"] = std::string(start_date);
			parameters["end"] = std::string(yesterday);
			parameters["limit"] = "10000";
			parameters["adjustment"] = "all";
			parameters["feed"] = "sip";

			//the next connection will be reused
			headers["Connection"] = "keep-alive";

			JSONArrayParser<bar, dailyBarContainer, updateDailyBar, updateDailyData> dailyParser; //used to parse arrays of daily bars

			MLModel model; //contains the MLP and inlier ranges for the inputs
			time_t START = time(nullptr);

			//load model ranges and weights here
			//both of the files must be in the same directory as the executable on Windows (home directory on Mac and Linux - the directory that contains the Desktop folder)
			model.loadWeights("C:\\Users\\Michael\\Desktop\\qpl_bot_strategy_equities\\x64\\Debug\\model_weights.json");
			model.loadScales("C:\\Users\\Michael\\Desktop\\qpl_bot_strategy_equities\\x64\\Debug\\scaler_info.json");

			//create multiple http clients with non-blocking I/O to retrieve data - using to many might cause the bot to exceed the api call rate limit
			const int num_clients = (max_clients > num_symbols_left) ? num_symbols_left : max_clients; //number of http clients to use for asynchronous data retrieval

			std::vector<dailyBarContainer> daily_bars;
			std::vector<http::httpClient> data_clients;
			std::vector<http::httpResponse> responses;
			std::vector<dictionary> client_parameters;
			std::vector<dictionary> client_headers;
			std::vector<size_t> current_symbols; //the indices of the symbols that are currently being evaluated

			//checking out of a static array is much faster than doing so out of a vector
			array<bool, max_clients> retired; //retired[i] is true if data_clients[i] is done being used to gather data

			client_parameters.reserve(num_clients);
			current_symbols.reserve(num_clients);
			client_headers.reserve(num_clients);
			data_clients.reserve(num_clients);
			daily_bars.reserve(num_clients);
			responses.reserve(num_clients);
			
			int i; //loop index

			for (i = 0; i < num_clients; i++)
			{
				data_clients.push_back(http::httpClient(ssl_context_wrapper, "data.alpaca.markets", false, timeout));
				responses.push_back(http::httpResponse());

				client_parameters.push_back(parameters);
				client_headers.push_back(headers);
				retired.push_back(false);

				client_parameters[i]["symbols"] = symbols[num_symbols_left].ticker;

				current_symbols.push_back(num_symbols_left);
				daily_bars.push_back(dailyBarContainer());

				num_symbols_left--;

				responses[i].clear();

				data_clients[i].reConnect(); //connect to the host
				data_clients[i].get(client_parameters[i], client_headers[i], "/v2/stocks/bars"); //prepare the get request
			}

			int active_clients = num_clients; //number of http clients that are still retrieving data
			http::status current_status; //current status of the current response being received
			bool last_page = false; //true if the last page of the current request has just been fully read

			int lookback_period = 0; //the lookback period for the qpl calculation
			bar* bar_ptr = nullptr; //bar pointer used to iterate through daily bars

			double average_volume = 0.0; //70-day daily average volume

			double r_min = 0.0; //minimum relative return - for each stock
			double r_max = 0.0; //maximum relative return - for each stock

			double mean = 0.0; //average relative return - for each stock
			double std = 0.0; //standard deviation of relative returns - for each stock

			int total_count = 0; //total number of returns used to calculate the mean, std, and probability density

			int pp_partial_count = 0; //number of returns used to calculate the probability density at mean + dr
			int pm_partial_count = 0; //number of returns used to calculate the probability density at mean - dr

			double r_scale = 0.0; //bin width per unit return
			double dr = 0.0;

			int drp1 = 0; //index of the bin at mean + dr
			int drm1 = 0; //index of the bin at mean - dr

			double pm = 0.0; //p(mean - dr)
			double pp = 0.0; //p(mean + dr)

			double rps = 0.0;
			double rms = 0.0;

			double C0 = 0.0;
			double C1 = 0.0;

			double l = 0.0; //lambda
			double l_numerator = 0.0; //numerator of lambda
			double l_denominator = 0.0; //denominator of lambda

			//use non-blocking IO to read data from multiple sockets in a single thread
			while (active_clients > 0)
			{
				for (i = 0; i < num_clients; i++)
				{
					if (retired[i]) continue; //if we are no longer using this client then move on to the next one

					http::httpClient& current_client = data_clients[i];
					http::httpResponse& current_response = responses[i];

					try { current_status = current_client.recvResponse(current_response); }
					catch (const SSLNoReturn&) //the connection was closed
					{
						current_response.clear();

						current_client.reConnect(); //reconnect to the host
						current_client.get(client_parameters[i], client_headers[i], "/v2/stocks/bars"); //prepare the get request

						continue;
					}

					if (current_status == http::status::TIMED_OUT) throw exceptions::exception("Timed out while retrieving stock data.");
					if (current_status == http::status::RECEIVED_RESPONSE)
					{
						if (current_response.status_code == 429)
						{
							throw exceptions::exception("Exceeded the Alpaca API Rate limit. Reduce the maximum number of http clients retrieving data.");
						}

						if (current_response.status_code != 200)
						{
							throw exceptions::exception("Received an unexpected status code : " + std::to_string(current_response.status_code)\
								+ " - with the following message : " + current_response.status_message);
						}

						response_data.clear();
						response_data.rehash(4);

						json_parser.parseJSON(response_data, current_response.message);

						symbol& current_symbol = symbols[current_symbols[i]];

						if (response_data.find("bars") != response_data.end())
						{
							if (response_data["bars"].size() > 2) //if json exists and is not empty
							{
								json_parser.parseJSON(response_data, response_data["bars"]);

								//read data from daily bars and append it somewhere from here
								dailyParser.parseJSONArray(response_data[current_symbol.ticker], daily_bars[i]);

								if (response_data.find("next_page_token") == response_data.end()) last_page = true;
								else if (response_data["next_page_token"] == "null") last_page = true;
								else last_page = false;
							}
							else last_page = true;
						}
						else last_page = true;

						if (last_page) //compute daily features and send get request for the next stock
						{
							//compute daily features here
							current_symbol.is_an_outlier = true; //if all inlier conditions are met this will be set to false

							if (daily_bars[i].size() >= model.ranges.min_completed_trading_days) //I require at least 500 closing prices to calculate the price levels
							{
								bar_ptr = &daily_bars[i].back();

								//check that the previous day's closing price is not an outlier
								if (bar_ptr->c >= model.ranges.min_previous_days_closing_price && bar_ptr->c <= model.ranges.max_previous_days_closing_price)
								{
									current_symbol.previous_days_close = bar_ptr->c;

									//calculate the daily average volume
									average_volume = 0;

									//assumes MIN_COMPLETED_TRADING_DAYS >= AVERAGE_VOLUME_PERIOD
									for (bar_ptr; bar_ptr > &daily_bars[i].back() - model.ranges.average_volume_period; bar_ptr--) average_volume += bar_ptr->v;

									average_volume /= model.ranges.average_volume_period;

									//check that the average volume is not an outlier
									if (average_volume >= model.ranges.min_average_volume && average_volume <= model.ranges.max_average_volume)
									{
										current_symbol.average_volume = average_volume;
										lookback_period = (model.ranges.lookback_period > daily_bars[i].size()) ? (daily_bars[i].size() - 1) : model.ranges.lookback_period;

										total_count = 0;
										mean = 0.0;

										//calculate the relative returns - use the variable c to store them
										for (bar_ptr = &daily_bars[i].back(); bar_ptr > &daily_bars[i].back() - lookback_period; bar_ptr--)
										{
											if ((bar_ptr - 1)->c > 0.0)
											{
												bar_ptr->c = bar_ptr->c / (bar_ptr - 1)->c;

												total_count++;
												mean += bar_ptr->c;
											}
											else bar_ptr->c = -1.0; //minimum should be 0.0, any value with -1.0 will not be used to calculate std
										}

										if (total_count) mean /= total_count;

										//check that the mean is not an outlier
										if (mean >= model.ranges.min_mean && mean <= model.ranges.max_mean && total_count)
										{
											current_symbol.mean = mean;
											std = 0.0;

											//calculate standard deviation
											for (bar_ptr = &daily_bars[i].back(); bar_ptr > &daily_bars[i].back() - lookback_period; bar_ptr--)
											{
												if (bar_ptr->c >= 0.0) std += (bar_ptr->c - mean) * (bar_ptr->c - mean);
											}

											std /= total_count;

											//check that the standard deviation is not an outlier
											if (std >= model.ranges.min_std * model.ranges.min_std && std <= model.ranges.max_std * model.ranges.max_std)
											{
												r_min = 999999999.0;
												r_max = -999999999.0;

												total_count = 0; //count the number of inliers

												//find the minimum and maximum returns that are within STD_MAX standard deviations from the mean
												for (bar_ptr = &daily_bars[i].back(); bar_ptr > &daily_bars[i].back() - lookback_period; bar_ptr--)
												{
													if ((bar_ptr->c - mean) * (bar_ptr->c - mean) <= std * model.ranges.std_max * model.ranges.std_max)
													{
														if (bar_ptr->c >= 0.0)
														{
															total_count++;

															if (bar_ptr->c < r_min) r_min = bar_ptr->c;
															if (bar_ptr->c > r_max) r_max = bar_ptr->c;
														}
													}
												}

												//calculate lambda and the ground state energy
												if (r_max > r_min && total_count && r_min >= 0.0)
												{
													std = sqrt(std);

													current_symbol.std = std;

													r_scale = (model.ranges.number_of_bins - 1.0) / (r_max - r_min);
													dr = 2.0 * std * model.ranges.std_max / model.ranges.number_of_bins; //assumes that NUMBER_OF_BINS != 0

													drp1 = static_cast<int>(r_scale * (mean + dr - r_min));
													drm1 = static_cast<int>(r_scale * (mean - dr - r_min));

													pp_partial_count = 0;
													pm_partial_count = 0;

													//find the number of returns that have the same bin index as drp1 and drm1
													for (bar_ptr = &daily_bars[i].back(); bar_ptr > &daily_bars[i].back() - lookback_period; bar_ptr--)
													{
														if (bar_ptr->c >= r_min)
														{
															if (bar_ptr->c <= r_max)
															{
																if (static_cast<int>(r_scale * (bar_ptr->c - r_min)) == drp1) pp_partial_count++;
																if (static_cast<int>(r_scale * (bar_ptr->c - r_min)) == drm1) pm_partial_count++;
															}
														}
													}

													pp = static_cast<double>(pp_partial_count) / static_cast<double>(total_count); //p(mean+dr)
													pm = static_cast<double>(pm_partial_count) / static_cast<double>(total_count); //p(mean-dr)

													if (pp >= model.ranges.min_ppdx && pp <= model.ranges.max_ppdx && pm >= model.ranges.min_pmdx && pm <= model.ranges.max_pmdx)
													{
														rps = (mean + dr) * (mean + dr);
														rms = (mean - dr) * (mean - dr);

														l_denominator = rps * rps * pp - rms * rms * pm;

														current_symbol.pp = pp;
														current_symbol.pm = pm;

														//if the denominator for lambda is not zero then calculate lambda
														if (l_denominator)
														{
															l_numerator = rms * pm - rps * pp;

															l = l_numerator / l_denominator;

															if (l < 0.0) l = -l; //absolute value of lambda

															//check that lambda is not an outlier
															if (l >= model.ranges.min_lambda && l <= model.ranges.max_lambda)
															{
																//calculate ground state energy E0 - then thats it

																C0 = -K0(0.0) * l;
																C1 = sqrt(0.25 * C0 * C0 - 1.0 / 27.0);

																current_symbol.l = l;
																current_symbol.E0 = cbrt(-0.5 * C0 + C1) + cbrt(-0.5 * C0 - C1);

																if (current_symbol.E0 != 0.0)
																{
																	//the stock satisfies the inlier conditions that only depend on daily data
																	current_symbol.is_an_outlier = false;

																	//wait until a specified start trading time to allow trading
																	current_symbol.trading_permitted = false;
																}
															}
														}
													}
												}
											}
										}
									}
								}
							}

							daily_bars[i].clear();

							client_parameters[i]["page_token"] = "";

							if (num_symbols_left > 0)
							{
								current_symbols[i] = num_symbols_left;
								client_parameters[i]["symbols"] = symbols[num_symbols_left].ticker;

								num_symbols_left--;

								current_response.clear();

								current_client.get(client_parameters[i], client_headers[i], "/v2/stocks/bars"); //prepare the get request
							}
							else
							{
								active_clients--;
								retired[i] = true;
							}
						}
						else
						{
							client_parameters[i]["page_token"] = response_data["next_page_token"];

							current_response.clear();

							current_client.get(client_parameters[i], client_headers[i], "/v2/stocks/bars"); //prepare the get request
						}
					}
				}
			}

			num_symbols_left = 0; //symbols left to trade

			for (symbol& Symbol : symbols) { if (!Symbol.is_an_outlier) num_symbols_left++; }

			if (num_symbols_left <= 0) throw exceptions::exception("No stocks available to trade.");

			symbolData final_symbols(ssl_context_wrapper, model);

			final_symbols.account_endpoint = account_endpoint;
			final_symbols.risk_per_trade = risk_per_trade;
			final_symbols.buying_power = allocated_buying_power;
			final_symbols.base_headers = headers;
			final_symbols.timeout = timeout;

			std::vector<std::string> tickers;

			tickers.reserve(num_symbols_left);

			std::cout << "WATCHING " << num_symbols_left << " SYMBOLS TODAY." << std::endl;

			num_symbols_left--;

			for (symbol& Symbol : symbols)
			{
				if (!Symbol.is_an_outlier) tickers.push_back(Symbol.ticker);
			}

			final_symbols.initializeKeys(tickers);

			for (symbol& Symbol : symbols)
			{
				if (!Symbol.is_an_outlier) final_symbols[Symbol.ticker] = Symbol;
			}

			/*
			perpare the same set of non-blocking clients to obtain volume from past minute bars of the current day for the remaining stocks
			start the data websocket
			subscribe to minute bar updates to update todays' cumulative volume sums (vsum for each stock)
			run the websocket and clients asynchronously
			*/

			JSONArrayParser<bar, symbol, updateDailyBar, updateIntradayData> intradayParser; //used to parse arrays of intraday bars
			tradeAndBarParser updateParser; //used to parse trade and bar updates

			active_clients = num_clients;

			//construct the data websocket

			websocket data_ws(ssl_context_wrapper, "stream.data.alpaca.markets", false, false, timeout); //this websocket receives trade and bar updates

			std::string last_msg; //the last message received by the data or account websocket

			//prepare the subscription messages

			std::string symbol_list = "";

			for (const std::string& ticker : tickers) symbol_list += "\"" + ticker + "\",";

			symbol_list.pop_back();

			std::string bar_sub_msg = "{\"action\": \"subscribe\", \"bars\": [" + symbol_list + "]}";
			std::string trade_and_quote_sub_msg = "{\"action\": \"subscribe\", \"quotes\": [" + symbol_list + "], \"trades\": [" + symbol_list + "]}";

			//open the websocket connection and send the bar subscription message

			dictionary ws_headers;

			ws_headers["APCA-API-KEY-ID"] = alpaca_api_key;
			ws_headers["APCA-API-SECRET-KEY"] = alpaca_secret_key;
			ws_headers["Upgrade"] = "websocket";
			ws_headers["Connection"] = "Upgrade";
			ws_headers["Sec-WebSocket-Version"] = "13";
			ws_headers["Sec-Websocket-Key"] = generateRandomBase64String(16);

			response.clear();

			data_ws.reInit();
			data_ws.open(ws_headers, "/v2/sip", response);

			if (response.status_code != 101) //should be 101 switching protocols
			{
				throw exceptions::exception(std::to_string(response.status_code) + " status code not accounted for.");
			}

			//the first message should indicate a successful connection

			while (!data_ws.recv(last_msg)) continue; //ADD TIMEOUT MECH

			response_data.clear();

			json_parser.parseJSON(response_data, last_msg); //expecting a JSON array with one json object

			if (response_data.find("msg") == response_data.end()) throw exceptions::exception("Could not confirm connection for the data websocket.");
			if (response_data["msg"] != "connected") throw exceptions::exception("Unexpected message received; Expecting \"connected\".");
			if (response_data.find("T") == response_data.end()) throw exceptions::exception("Could not confirm connection for the data websocket.");
			if (response_data["T"] != "success") throw exceptions::exception("Connection attempt for the data websocket was unsuccessful.");

			//the second message should indicate successful authentication

			while (!data_ws.recv(last_msg)) continue; //ADD TIMEOUT MECH

			response_data.clear();

			json_parser.parseJSON(response_data, last_msg); //expecting a JSON array with one json object

			if (response_data.find("msg") == response_data.end()) throw exceptions::exception("Could not verify authentication for the data websocket.");
			if (response_data["msg"] != "authenticated") throw exceptions::exception("Unexpected message received; Expecting \"authenticated\".");
			if (response_data.find("T") == response_data.end()) throw exceptions::exception("Could not verify authentication for the data websocket.");
			if (response_data["T"] != "success") throw exceptions::exception("Authentication attempt for the data websocket was unsuccessful.");

			//send the bar subscription message and wait for the resulting subscription confirmation

			data_ws.send(bar_sub_msg, WS_TEXT_FRAME);

			while (!data_ws.recv(last_msg)) continue; //ADD TIMEOUT MECH

			response_data.clear();

			json_parser.parseJSON(response_data, last_msg); //expecting a JSON array with one json object

			if (response_data.find("T") == response_data.end()) throw exceptions::exception("Could not subscribe to bar updates.");
			if (response_data["T"] != "subscription") throw exceptions::exception("Could not subscribe to bar updates.");

			//wait until the first bar update message is received
			//I didn't include a timeout mechanism here so that the bot won't stop if started long before ...
			//... the market open time or during trading periods where we may not expect many bar updates
			
			while (!data_ws.recv(last_msg)) continue;

			//parse the message, update vsums, then set the end time to one minute behind the timestamp received

			if (last_msg.size() >= 2) updateParser.parseJSONArray(last_msg, final_symbols);
			else throw exceptions::exception("Did not receive the first minute bar update.");
			
			//reinitialize the clients to gather minute data

			parameters["timeframe"] = "1Min";
			parameters["start"] = std::string(time_proto.date) + "T00:00:00Z";

			//get the timestamp of the last bar update - it should be one minute behind the current time
			//it should also be formatted as such : YYYY-MM-DDTHH:MM:00Z

			std::string end_date = std::string(updateParser.get_info().t);

			if (end_date.substr(14, 2) == "00") //if minute is 0 go to previous hour
			{
				if (end_date.substr(11, 2) == "00") throw exceptions::exception("Invalid intraday end date."); //if hour is 0 then the end date is less than the start date

				int previous_hour = convert<int>(end_date.substr(11, 2)) - 1;

				if (previous_hour < 10) end_date = end_date.substr(0, 11) + "0" + std::to_string(previous_hour) + ":59:00Z";
				else end_date = end_date.substr(0, 11) + std::to_string(previous_hour) + ":59:00Z";
			}
			else
			{
				int previous_minute = convert<int>(end_date.substr(14, 2)) - 1;

				if (previous_minute < 10) end_date = end_date.substr(0, 14) + "0" + std::to_string(previous_minute) + ":00Z";
				else end_date = end_date.substr(0, 14) + std::to_string(previous_minute) + ":00Z";
			}

			parameters["end"] = end_date;

			client_parameters.clear();
			current_symbols.clear();
			retired.clear();

			for (i = 0; i < num_clients; i++)
			{
				client_parameters.push_back(parameters);
				retired.push_back(false);

				client_parameters[i]["symbols"] = tickers[num_symbols_left];

				current_symbols.push_back(num_symbols_left);

				num_symbols_left--;

				responses[i].clear();

				data_clients[i].reConnect();
				data_clients[i].get(client_parameters[i], client_headers[i], "/v2/stocks/bars");
			}

			//run the websocket and http clients asynchronously
			std::string ticker; //ticker symbol for the current stock

			while (active_clients > 0)
			{
				//check for websocket message here

				if (data_ws.recv(last_msg)) updateParser.parseJSONArray(last_msg, final_symbols);

				//continue receiving intraday data from clients

				for (i = 0; i < num_clients; i++)
				{
					if (retired[i]) continue;

					http::httpClient& current_client = data_clients[i];
					http::httpResponse& current_response = responses[i];

					try { current_status = current_client.recvResponse(current_response); }
					catch (const SSLNoReturn&)
					{
						current_response.clear();

						current_client.reConnect();
						current_client.get(client_parameters[i], client_headers[i], "/v2/stocks/bars");

						continue;
					}

					if (current_status == http::status::TIMED_OUT) throw exceptions::exception("Timed out while retrieving stock data.");
					if (current_status == http::status::RECEIVED_RESPONSE)
					{
						if (current_response.status_code == 429)
						{
							throw exceptions::exception("Exceeded the Alpaca API Rate limit. Reduce the maximum number of http clients retrieving data.");
						}

						if (current_response.status_code != 200)
						{
							throw exceptions::exception("Received an unexpected status code : " + std::to_string(current_response.status_code)\
								+ " - with the following message : " + current_response.status_message);
						}

						response_data.clear();

						json_parser.parseJSON(response_data, current_response.message);

						ticker = tickers[current_symbols[i]];

						if (response_data.find("bars") != response_data.end())
						{
							if (response_data["bars"].size() > 2)
							{
								json_parser.parseJSON(response_data, response_data["bars"]);

								//read data from minute bars and add the volumes to vsums
								intradayParser.parseJSONArray(response_data[ticker], final_symbols[ticker]);

								if (response_data.find("next_page_token") == response_data.end()) last_page = true;
								else if (response_data["next_page_token"] == "null") last_page = true;
								else last_page = false;
							}
							else last_page = true;
						}
						else last_page = true;

						if (last_page)
						{
							client_parameters[i]["page_token"] = "";

							if (num_symbols_left > 0)
							{
								current_symbols[i] = num_symbols_left;
								client_parameters[i]["symbols"] = tickers[num_symbols_left];

								num_symbols_left--;

								current_response.clear();

								current_client.get(client_parameters[i], client_headers[i], "/v2/stocks/bars");
							}
							else
							{
								active_clients--;
								retired[i] = true;
							}
						}
						else
						{
							client_parameters[i]["page_token"] = response_data["next_page_token"];

							current_response.clear();

							current_client.get(client_parameters[i], client_headers[i], "/v2/stocks/bars");
						}
					}
				}
			}

			/*
			when the clients finish gathering data up to the same minute the websocket was started, ...
			... start the account updates websocket, subscribe to trade updates on the data websocket, ...
			... and then and run both websockets asynchronous
			*/

			websocket account_ws(ssl_context_wrapper, trade_update_stream, false, false, timeout); //this websocket receives account updates

			ws_headers.erase("APCA-API-KEY-ID");
			ws_headers.erase("APCA-API-SECRET-KEY");

			ws_headers["Sec-Websocket-Key"] = generateRandomBase64String(16); //generate a new key for the account update websocket

			response.clear();

			account_ws.reInit();
			account_ws.open(ws_headers, "/stream", response);

			if (response.status_code != 101) //should be 101 switching protocols
			{
				throw exceptions::exception(std::to_string(response.status_code) + " status code not accounted for.");
			}

			//send an authorization message to start listening for account updates and wait for confirmation
			//the response message should indicate a successful authentication

			account_ws.send("{\"action\": \"auth\", \"key\": \"" + alpaca_api_key + "\", \"secret\": \"" + alpaca_secret_key + "\"}", WS_TEXT_FRAME);

			while (!account_ws.recv(last_msg)) continue; //ADD TIMEOUT MECH

			response_data.clear();

			json_parser.parseJSON(response_data, last_msg);

			if (response_data.find("stream") == response_data.end()) throw exceptions::exception("Unknown message received from the account websocket.");
			if (response_data["stream"] != "authorization") throw exceptions::exception("Unexpected message received from the account websocket.");
			if (response_data.find("data") == response_data.end()) throw exceptions::exception("Could not confirm authorization for the account websocket.");

			json_parser.parseJSON(response_data, response_data["data"]);

			if (response_data.find("action") == response_data.end()) throw exceptions::exception("Unknown message received from the account websocket.");
			if (response_data["action"] != "authenticate") throw exceptions::exception("Could not confirm authorization for the account websocket.");
			if (response_data.find("status") == response_data.end()) throw exceptions::exception("Unknown message received from the account websocket.");
			if (response_data["status"] != "authorized") throw exceptions::exception("Authorization for the account websocket failed.");

			//send a message to start listening for account updates and wait for confirmation
			//the second message should indicate an active trade updates stream

			account_ws.send("{\"action\": \"listen\", \"data\": {\"streams\": [\"trade_updates\"]}}", WS_TEXT_FRAME);

			while (!account_ws.recv(last_msg)) continue; //ADD TIMEOUT MECH

			response_data.clear();

			json_parser.parseJSON(response_data, last_msg);

			if (response_data.find("stream") == response_data.end()) throw exceptions::exception("Unknown message received from the account websocket.");
			if (response_data["stream"] != "listening") throw exceptions::exception("Unexpected message received from the account websocket.");
			if (response_data.find("data") == response_data.end()) throw exceptions::exception("Could not subscribe to trade updates for the account websocket.");

			json_parser.parseJSON(response_data, response_data["data"]);

			if (response_data.find("streams") == response_data.end()) throw exceptions::exception("The account websocket is not listening to any streams.");
			if (response_data["streams"].find("\"trade_updates\"") == std::string::npos) throw exceptions::exception("The account websocket is not listening for trade updates.");

			//send the trade and quote subscription message and wait for the resulting subscription confirmation

			data_ws.send(trade_and_quote_sub_msg, WS_TEXT_FRAME);

			while (!data_ws.recv(last_msg)) continue; //ADD TIMEOUT MECH

			response_data.clear();

			json_parser.parseJSON(response_data, last_msg); //will raise an error if a bar update is received before the subscription confirmation

			if (response_data.find("T") == response_data.end()) throw exceptions::exception("Could not subscribe to trade and quote updates.");
			if (response_data["T"] != "subscription") throw exceptions::exception("Could not subscribe to trade and quote updates.");
			
			time_t END = time(nullptr);

			std::cout << "TOOK ~" << double(END - START) / 60.0 << " MINUTES TO GATHER DATA AND INITIALIZE BOT." << std::endl;

			/*
			wait until trading start time
			start trading
			*/

			dictionary last_trade_update;

			auto current_symbol_iterator = final_symbols.begin();
			auto start_symbol_iterator = final_symbols.begin();
			auto end_symbol_iterator = final_symbols.end();

			//while waiting to start trading handle account and minute bar updates
			//if current time >= trading start time then start trading (go to next loop)
			while (time(nullptr) <= trading_start_time)
			{
				if (account_ws.recv(last_msg)) handleTradeUpdate(last_msg, last_trade_update, final_symbols); //expecting individual json objects
				if (data_ws.recv(last_msg)) updateParser.parseJSONArray(last_msg, final_symbols); //handle bar, trade, and quote updates, and errors

				//if the bot isn't busy handling updates then it can spend some time cleaning the quote deques
				else cleanQuoteDeque(current_symbol_iterator, start_symbol_iterator, end_symbol_iterator);
			}

			//start trading

			char current_time[9];

			time_proto.getCurrentTime(current_time);

			std::cout << "STARTED TRADING AT " << current_time << std::endl;

			current_symbol_iterator = start_symbol_iterator;

			while (current_symbol_iterator < end_symbol_iterator) (current_symbol_iterator++)->trading_permitted = true;

			try
			{
				//if current time >= trading end time then stop trading
				while (time(nullptr) <= trading_end_time)
				{
					if (account_ws.recv(last_msg)) //expecting individual json objects - 41% of runtime spent here
					{
						//std::cout << last_msg << std::endl << std::endl << std::endl;
						handleTradeUpdate(last_msg, last_trade_update, final_symbols);
					}

					if (data_ws.recv(last_msg)) updateParser.parseJSONArray(last_msg, final_symbols); //49% of runtime spent here
					else cleanQuoteDeque(current_symbol_iterator, start_symbol_iterator, end_symbol_iterator);
				}

				//stop trading

				time_proto.getCurrentTime(current_time);

				std::cout << "STOPPED TRADING AT " << current_time << std::endl;

				closeAllPositions(final_symbols, data_ws, account_ws);
			}
			catch (const std::runtime_error& runtime_error) { closeAllPositions(final_symbols, data_ws, account_ws); throw runtime_error; }
			catch (const exceptions::exception& exception) { closeAllPositions(final_symbols, data_ws, account_ws); throw exception; }
			catch (const SSLNoReturn& no_return) { std::cout << "LAST MSG : " << last_msg << std::endl; closeAllPositions(final_symbols, data_ws, account_ws); throw no_return; }
			catch (const std::exception& exception) { closeAllPositions(final_symbols, data_ws, account_ws); throw exception; }
		}
	}
	catch (const std::runtime_error& runtime_error) { throw runtime_error; }
	catch (const exceptions::exception& exception) { throw exception; }
	catch (const SSLNoReturn& no_return) { throw no_return; }
	catch (const std::exception& exception) { throw exception; }
}

/*
Since many of the keys in trade, quote, and bar updates are only 1 or 2 characters long,
we can greatly speed up the parsing process by using switch statements - which require numerical
or enumerated types - instead of if/else statements by integer-encoding the strings.

This only works for small strings (integer-type values can only be so large).
*/

constexpr inline size_t encodeString(const std::string& string)
{
	size_t h = 0;

	for (const char& c : string) { if (256 * h + c <= h && c != '\0') return 0; h = 256 * h + c; }

	return h;
}

//needed on non-windows machines
constexpr inline size_t encodeString(const char* string)
{
	size_t h = 0;
	const char* c = string;

	while (*c != '\0') { if (256 * h + *c <= h && *c != '\0') return 0; h = 256 * h + *(c++); }

	return h;
}

void updateDailyBar(bar& daily_bar, const std::string& key, const std::string& value)
{
	//faster than if/else statements - only works when key strings are small

	switch (encodeString(key))
	{
		case encodeString("t"): { daily_bar.t = value; break; }
		case encodeString("c"): { daily_bar.c = convert<double>(value); break; }
		case encodeString("v"): { daily_bar.v = convert<long long>(value); break; }
		default: break;
	}
}

void updateDailyData(const bar& daily_bar, dailyBarContainer& daily_bars)
{
	daily_bars.push_back(daily_bar);
}

void updateIntradayData(const bar& intraday_bar, symbol& Symbol)
{
	Symbol.vsum += intraday_bar.v;
}

void getAvailableSymbols(std::vector<symbol>& symbols, std::string& assets_json)
{
	JSONArrayParser<symbol, std::vector<symbol>, updateSymbol, updateSymbolData> symbol_data_parser;

	symbol_data_parser.parseJSONArray(assets_json, symbols); //expecting a single-level json array
}

void updateSymbol(symbol& Symbol, const std::string& key, const std::string& value)
{
	//some of the strings are too large for integer encoding
	//not a big deal since this is only used to gather data about symbols and wont affect the performance of the bot at all

	if (key == "class") Symbol.type = value;
	else if (key == "exchange") Symbol.exchange = value;
	else if (key == "symbol") Symbol.ticker = value;
	else if (key == "status") Symbol.active = (value == "active");
	else if (key == "tradable") Symbol.tradable = (value == "true");
	else if (key == "shortable") Symbol.shortable = (value == "true");
	else if (key == "easy_to_borrow") Symbol.can_borrow = (value == "true");
}

void updateSymbolData(const symbol& Symbol, std::vector<symbol>& symbols)
{
	if (!Symbol.active) return;
	if (!Symbol.tradable) return;
	if (Symbol.exchange != "NYSE" && Symbol.exchange != "NASDAQ") return;

	//if symbol is not all capital letters
	for (const char& c : Symbol.ticker)
	{
		if (c < 65 || c > 90) return; //char(65) through char(90) are 'A' through 'Z'
	}

	symbols.push_back(Symbol);
}

double getBuyingPower(std::string& account_info_json)
{
	dictionary account_info;
	JSONParser json_parser;

	json_parser.parseJSON(account_info, account_info_json);

	if (account_info.find("trading_blocked") != account_info.end())
	{
		if (account_info["trading_blocked"] != "false") throw exceptions::exception("Trading is blocked on this account.");
	}
	else throw exceptions::exception("Value for trading_blocked not found.");

	if (account_info.find("trade_suspended_by_user") != account_info.end())
	{
		if (account_info["trade_suspended_by_user"] != "false") throw exceptions::exception("Trading on this account is suspended by the account owner.");
	}
	else throw exceptions::exception("Value for trade_suspended_by_user not found.");

	if (account_info.find("account_blocked") != account_info.end())
	{
		if (account_info["account_blocked"] != "false") throw exceptions::exception("This account is blocked.");
	}
	else throw exceptions::exception("Value for account_blocked not found.");

	if (account_info.find("non_marginable_buying_power") != account_info.end()) return convert<long double>(account_info["non_marginable_buying_power"]);
	else throw exceptions::exception("Could not obtain available cash to trade.");

	return 0.0L;
}

void updateTradeOrBarInfo(tradeOrBarUpdate& information, const std::string& key, const std::string& value)
{
	switch (encodeString(key))
	{
		case encodeString("T"): { information.T = value; break; }
		case encodeString("S"): { information.S = value; break; }
		case encodeString("s"): { information.s = convert<int>(value); break; }
		case encodeString("p"): { information.p = convert<double>(value); break; }
		case encodeString("t"): { information.t = value; break; }
		case encodeString("x"): { information.x = value[0]; break; }
		case encodeString("c"): { information.c = value; break; }
		case encodeString("v"): { information.v = convert<long long>(value); break; }
		case encodeString("bx"): { information.bx = value[0]; break; }
		case encodeString("bp"): { information.bp = convert<double>(value); break; }
		case encodeString("ax"): { information.ax = value[0]; break; }
		case encodeString("ap"): { information.ap = convert<double>(value); break; }
		case encodeString("code"): { information.code = convert<int>(value); break; }
		case encodeString("msg"): { information.msg = value; break; }
		default: break;
	}
}

void updateSymbolData(const tradeOrBarUpdate& update, symbolData& symbol_data)
{
	if (update.T == "q") //this is a quote update
	{
		//if (update.bx == 'D') return; //exchange is the FINRA ADF

		symbol& current_symbol = symbol_data[update.S];

		//calculate current time in nanoseconds since midnight - convert the current hour, minute, and last whole second
		//record ask and bid prices in order to calculate the spread - used to account for slippage

		current_symbol.quote_times.push_back(convertUTC(update.t));
		current_symbol.bid_prices.push_back(update.bp);
		current_symbol.ask_prices.push_back(update.ap);
	}
	else if (update.T == "t") //this is a trade update
	{
		if (update.x == 'D') return; //exchange is the FINRA ADF
		if (update.s < 100) return; //size < 100

		for (const char& c : update.c)
		{
			if (c == 'U' || c == 'Z') return; //sold out of sequence if true
		}

		if (update.p <= 0.0) return;

		symbol& current_symbol = symbol_data[update.S];

		//calculate current time in nanoseconds since midnight - convert the current hour, minute, and last whole second
		current_symbol.t = convertUTC(update.t);

		current_symbol.time_stamps.push_back(current_symbol.t);
		current_symbol.prices.push_back(update.p);
		current_symbol.sizes.push_back(update.s);

		current_symbol.rolling_vsum += update.s;

		current_symbol.dt = current_symbol.t - current_symbol.time_stamps.front();
		current_symbol.dp = update.p / current_symbol.prices.front();

		while (current_symbol.dt >= symbol_data.model.ranges.rolling_period)
		{
			current_symbol.rolling_vsum -= current_symbol.sizes.front();

			current_symbol.time_stamps.pop_front();
			current_symbol.prices.pop_front();
			current_symbol.sizes.pop_front();

			current_symbol.dt = current_symbol.t - current_symbol.time_stamps.front();
			current_symbol.dp = update.p / current_symbol.prices.front();
		}

		/*
		quote and trade streams have different delay times so it is possible to receive a quote before a - trade that occured before the quote
		because of that, we need to record all incoming quotes and discard those that occured before the current trade

		if the quote deques are large enough, then the bot might stall and shutdown so we iterate starting from the back (the most recent quote update)
		in order to find the latest quote update that occured before the current trade
		*/

		auto quote_time_ptr = current_symbol.quote_times.rbegin();
		auto quote_time_end = current_symbol.quote_times.rend();

		auto bid_price_ptr = current_symbol.bid_prices.rbegin();
		auto ask_price_ptr = current_symbol.ask_prices.rbegin();

		//bid and ask prices are only appended and removed with their respective quote times so there is no need to check all three pointers
		while (quote_time_ptr != quote_time_end)
		{
			if (*quote_time_ptr < current_symbol.t)
			{
				current_symbol.old_t = *quote_time_ptr;
				current_symbol.last_bid = *bid_price_ptr;
				current_symbol.last_ask = *ask_price_ptr;
				current_symbol.has_past_quote = true;

				break;
			}

			++quote_time_ptr;
			++bid_price_ptr;
			++ask_price_ptr;
		}
		
		//see if this stock's price reached a new quantum price level
		while (update.p <= current_symbol.getPriceLevel(-1)) current_symbol.new_n--;
		while (update.p >= current_symbol.getPriceLevel(+1)) current_symbol.new_n++;

		if (!current_symbol.found_first_n) //if n is not initialized
		{
			current_symbol.n = current_symbol.new_n;
			current_symbol.found_first_n = true;

			return;
		}

		//if a new price level is reached determine what our position size should be
		if (current_symbol.n != current_symbol.new_n && current_symbol.has_past_quote)
		{
			/*
			check the rest of the outlier conditions here
			only consider holding, entering, or adjusting a position if all outlier conditions are met
			otherwise close an existing position, cancel an existing buy order, or adjust an existing sell order
			*/

			//skip checking time_of_day

			/*
			since the buy signals were generated using continuous price levels (not rounded to penny or sub-penny increments)
			in the backtesting, we need to do the same in this bot. But the entry price must be rounded properly (as also done in
			the backtesting)
			*/

			double current_price = current_symbol.getPriceLevel(0);

			if (update.p > current_price) current_price = update.p;

			current_symbol.entry_price = roundPrice(current_price);
			current_symbol.quantity_desired = 0;

			//if at least ROLLING_PERIOD_MIN_TRADES trades have occured within the last ROLLING_PERIOD
			//current_symbol.rolling_csum = current_symbol.sizes.size();
			if (current_symbol.sizes.size() >= 10 + 0 * symbol_data.model.ranges.rolling_period_min_trades && \
				current_symbol.sizes.size() <= symbol_data.model.ranges.rolling_period_max_trades)
			{
				if (current_symbol.new_n >= symbol_data.model.ranges.min_n && current_symbol.new_n <= symbol_data.model.ranges.max_n)
				{
					if (current_symbol.vsum >= symbol_data.model.ranges.min_vsum && current_symbol.vsum <= symbol_data.model.ranges.max_vsum)
					{
						if (update.s >= symbol_data.model.ranges.min_size && update.s <= symbol_data.model.ranges.max_size)
						{
							if (current_symbol.rolling_vsum >= symbol_data.model.ranges.rolling_volume_min && \
								current_symbol.rolling_vsum <= symbol_data.model.ranges.rolling_volume_max && current_symbol.rolling_vsum * current_price >= 10000.0)
							{
								if (current_symbol.dp >= symbol_data.model.ranges.min_dp && current_symbol.dp <= symbol_data.model.ranges.max_dp)
								{
									float relative_volume = static_cast<double>(current_symbol.vsum) / current_symbol.average_volume;

									if (relative_volume >= symbol_data.model.ranges.min_rvol && relative_volume <= symbol_data.model.ranges.max_rvol)
									{
										float dt = static_cast<long double>(current_symbol.dt) / 1000000000.0L; //convert nanoseconds to seconds

										if (dt >= symbol_data.model.ranges.min_dt && dt <= symbol_data.model.ranges.max_dt)
										{
											//if the rest of the outlier conditions are satisfied, calculate potential gain, loss, and probability of success

											double slippage = current_symbol.last_ask - current_symbol.last_bid;

											if (slippage < 0.0) slippage = 0.0;

											double potential_gain_per_share = current_symbol.getPriceLevel(1) - current_price - slippage;
											double potential_loss_per_share = current_price - current_symbol.getPriceLevel(-1) + slippage;

											float time_of_day = static_cast<long double>(current_symbol.t) / 60000000000.0L; //convert nanoseconds to minute of day

											//predict the probability of the next transition being +1 price level
											float probability_of_success = symbol_data.model.predict(time_of_day, relative_volume, current_symbol.new_n, current_symbol.mean,
												current_symbol.dp, current_symbol.std, dt, current_symbol.vsum, current_symbol.average_volume, current_symbol.previous_days_close,
												current_symbol.sizes.size(), current_symbol.rolling_vsum, current_symbol.pm, update.s, current_symbol.pp, current_symbol.l);

											//probability_of_success = godSays(); //see how well the bot handles orders when making random buy and sell decisions
											
											//based on those variables, decide whether or not to hold, enter, or adjust a position
											if (probability_of_success * (potential_gain_per_share + potential_loss_per_share) > potential_loss_per_share)
											{
												if (potential_gain_per_share > 0.0 && potential_loss_per_share > 0.0 && current_symbol.entry_price > 0.0)
												{
													//calculate the maximum number of shares the bot should hold
													current_symbol.quantity_desired = static_cast<int>(symbol_data.risk_per_trade / potential_loss_per_share);
//#ifdef TRADE_BOT_DEBUG
													//*
													std::cout << "XXXPRED Symbol : " << update.S << " - time_of_day : " << time_of_day << " - relative_volume : " << relative_volume;
													std::cout << " - n : " << current_symbol.new_n << " - mean : " << current_symbol.mean << " - dp : " << current_symbol.dp;
													std::cout << " - std : " << current_symbol.std << " - dt : " << dt << " - vsum : " << current_symbol.vsum;
													std::cout << " - average_volume : " << current_symbol.average_volume << " - previous_days_close : " << current_symbol.previous_days_close;
													std::cout << " - rolling_csum : " << current_symbol.sizes.size() << " - rolling_vsum : " << current_symbol.rolling_vsum;
													std::cout << " - pm : " << current_symbol.pm << " - size : " << update.s << " - pp : " << current_symbol.pp;
													std::cout << " - lambda : " << current_symbol.l << " - chance_of_+1_transition : " << probability_of_success;
													std::cout << " - reward_per_share : " << potential_gain_per_share << " - risk_per_share : " << potential_loss_per_share;
													std::cout << " - last_ask : " << current_symbol.last_ask << " - last_bid : " << current_symbol.last_bid;
													std::cout << " - time_passed_since_last_quote[ns] : " << current_symbol.t - current_symbol.old_t << std::endl;
//#endif
													//*/

													/*
													std::cout << "Buy " << current_symbol.quantity_desired << " shares of " << update.S << " at " << current_symbol.entry_price << std::endl;
													std::cout << "\tSell for a gain at " << potential_gain_per_share + current_symbol.entry_price << std::endl;
													std::cout << "\tSell for a loss at " << current_symbol.entry_price - potential_loss_per_share << std::endl;
													std::cout << "\tHas a " << 100.0 * probability_of_success << "% chance of succeeding." << std::endl << std::endl;

													current_symbol.quantity_desired = 0;
													*/
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}

		/*
		adjust our current position to match max_possible_shares(keeping our risk - per - trade constant)
		*/

		//only place orders if trading is permitted
		if (current_symbol.trading_permitted)
		{
			try { current_symbol.updatePosition(symbol_data); }
			catch (const std::runtime_error& runtime_error) { std::cout << "RUNTIME ERROR FROM POSITION UPDATE" << std::endl; throw runtime_error; }
			catch (const exceptions::exception& exception)
			{
				std::cout << "EXCEPTION FROM POSITION UPDATE" << std::endl;

				if (symbol_data.response.status_code == 301)
				{
					std::cout << "RESPONSE FIELDS" << std::endl;

					for (const auto pair : symbol_data.response.fields) { std::cout << pair.first << " : " << pair.second << std::endl; }

					std::cout << std::endl;
					std::cout << "ORDER FIELDS" << std::endl;

					for (const auto pair : symbol_data.order_data) { std::cout << pair.first << " : " << pair.second << std::endl; }

					std::cout << std::endl;
					std::cout << "TICKER : " << current_symbol.ticker << std::endl;
					std::cout << "ORDER ID : " << current_symbol.order_id << std::endl;
					std::cout << "REPLACEMENT ORDER ID : " << current_symbol.replacement_order_id << std::endl;
				}

				throw exception;
			}
			catch (const SSLNoReturn& no_return) { std::cout << "SSL NO RETURN FROM POSITION UPDATE" << std::endl; throw no_return; }
			catch (const std::exception& exception) { std::cout << "BASE EXCEPTION FROM POSITION UPDATE" << std::endl; throw exception; }
		}
		else current_symbol.quantity_desired = 0;

		current_symbol.n = current_symbol.new_n;
	}
	else if (update.T == "b") symbol_data[update.S].vsum += update.v; //this is a bar update
	else if (update.T == "error") //something went wrong or will go wrong
	{
		throw exceptions::exception(std::string("Error : \"") + update.msg + std::string("\" occured with status code - ") + std::to_string(update.code) + std::string("."));
	}
	else if (update.T == "subscription") std::cout << "SUBSCRIPTION CONFIRMATION RECEIVED" << std::endl;

#ifdef TRADE_BOT_DEBUG

	else if (update.T == "c")
	{
		std::cout << "TRADE CORRECTION FOR " << update.S << " FOUND AT " << update.t << std::endl;
	}

#endif
}

void handleTradeUpdate(std::string& last_msg, dictionary& trade_update_info, symbolData& final_symbols)
{
	trade_update_info.clear();
	final_symbols.json_parser.parseJSON(trade_update_info, last_msg);

	//skip checking for stream type

	if (trade_update_info.find("data") == trade_update_info.end())
	{
		for (const auto& pair : trade_update_info) std::cout << pair.first << " : " << pair.second << std::endl;

		throw exceptions::exception("No data field received for account update.");
	}

	if (trade_update_info["data"].size() <= 2)
	{
		for (const auto& pair : trade_update_info) std::cout << pair.first << " : " << pair.second << std::endl;

		throw exceptions::exception("Empty data field received for account update.");
	}

	final_symbols.json_parser.parseJSON(trade_update_info, trade_update_info["data"]);

	if (trade_update_info.find("order") == trade_update_info.end())
	{
		for (const auto& pair : trade_update_info) std::cout << pair.first << " : " << pair.second << std::endl;

		throw exceptions::exception("No order field received for account update.");
	}

	if (trade_update_info["order"].size() <= 2)
	{
		for (const auto& pair : trade_update_info) std::cout << pair.first << " : " << pair.second << std::endl;

		throw exceptions::exception("Empty order field received for account update.");
	}

	final_symbols.json_parser.parseJSON(trade_update_info, trade_update_info["order"]);

	trade_update_info.erase("data");
	trade_update_info.erase("order");

	//skipping checks for all of the needed values

	std::string& Symbol = trade_update_info["symbol"];
	std::string& order_type = trade_update_info["type"];
	std::string& order_side = trade_update_info["side"];
	std::string& order_id = trade_update_info["id"];
	std::string& event = trade_update_info["event"];

	//having issues with rejected order updates - for debugging
	if (event == std::string("rejected")) std::cout << "FAKE UPDATE : " << Symbol << std::endl;

	if (!final_symbols.contains(Symbol)) return; //if we get an order for a symbol that this bot is not watching then ignore it

	symbol& current_symbol = final_symbols[Symbol];

	bool rejected_replacement = false;

	//having issues with rejected order updates - for debugging
	if (event == std::string("rejected"))
	{
		std::cout << "FAKE UPDATE : " << Symbol << " " << event << " " << order_type << " " << order_side << " at N/A for N/A shares";
		std::cout << " - remaining buying power : " << final_symbols.buying_power;
		std::cout << " - avg fill price : N/A";
		std::cout << " - desired qty : " << current_symbol.quantity_desired;
		std::cout << " - pending qty : " << current_symbol.quantity_pending;
		std::cout << " - owned qty : " << current_symbol.quantity_owned;
		std::cout << " - update time : " << trade_update_info["updated_at"] << std::endl << std::endl;
	}

	//replacement orders don't change current_symbol.order_id, so if one of those orders is rejected, the bot will most likely ignore it without the following line of code
	if (event == "rejected" && current_symbol.replacement_order_id == order_id) rejected_replacement = true;

	//when the bot is active do not sell off any position it enters, because it will ignore that update
	else if (current_symbol.order_id != order_id) return; //if this order was not sent by the bot then ignore it

	int quantity_filled = convert<int>(trade_update_info["filled_qty"]);
	int quantity = convert<int>(trade_update_info["qty"]);

	double average_fill_price = 0.0;
	double limit_price = 0.0;

	if (trade_update_info["filled_avg_price"] != "null") average_fill_price = convert<double>(trade_update_info["filled_avg_price"]);
	if (trade_update_info["limit_price"] != "null") limit_price = convert<double>(trade_update_info["limit_price"]);

	if (event == "fill")
	{
		if (order_side == "buy")
		{
			//this trade update confirms that all of our shares were successfully purchased so these shares are available to sell
			current_symbol.quantity_owned += quantity_filled - current_symbol.order_quantity_filled;
			current_symbol.quantity_pending -= quantity_filled - current_symbol.order_quantity_filled;

			//if our limit order was filled at an average price lower than our limit price then we need to add that difference back to our buying power
			if (order_type == "limit") final_symbols.buying_power += (limit_price - average_fill_price) * (quantity_filled - current_symbol.order_quantity_filled);
			else if (order_type == "market") final_symbols.buying_power -= average_fill_price * quantity_filled - current_symbol.order_quantity_filled * current_symbol.average_fill_price;
		}
		else if (order_type == "limit" || order_type == "market")
		{
			final_symbols.buying_power += average_fill_price * quantity_filled - current_symbol.order_quantity_filled * current_symbol.average_fill_price;

			current_symbol.quantity_pending += quantity_filled - current_symbol.order_quantity_filled;
			current_symbol.quantity_owned -= quantity_filled - current_symbol.order_quantity_filled;
		}

		//if our order has a pending replacement, then we make the replacement the current order so the bot will recognize it when its status changes
		current_symbol.order_id = current_symbol.replacement_order_id;
		current_symbol.replacement_order_id.clear();

		//current_symbol.order_id.clear();
	}
	else if (event == "partial_fill")
	{
		if (order_side == "buy")
		{
			current_symbol.quantity_owned += quantity_filled - current_symbol.order_quantity_filled;
			current_symbol.quantity_pending -= quantity_filled - current_symbol.order_quantity_filled;

			if (order_type == "limit") final_symbols.buying_power += (limit_price - average_fill_price) * (quantity_filled - current_symbol.order_quantity_filled);
			else if (order_type == "market") final_symbols.buying_power -= average_fill_price * quantity_filled - current_symbol.order_quantity_filled * current_symbol.average_fill_price;
		}
		else if (order_type == "limit" || order_type == "market")
		{
			final_symbols.buying_power += average_fill_price * quantity_filled - current_symbol.order_quantity_filled * current_symbol.average_fill_price;

			current_symbol.quantity_pending += quantity_filled - current_symbol.order_quantity_filled;
			current_symbol.quantity_owned -= quantity_filled - current_symbol.order_quantity_filled;
		}

		current_symbol.average_fill_price = average_fill_price;
		current_symbol.order_quantity_filled = quantity_filled;
		current_symbol.order_quantity = quantity;
	}
	else if (event == "replaced" || event == "canceled" || event == "rejected" || event == "expired") //'replaced' sends the order that is being replaced (not the new order)
	{
		if (order_side == "buy")
		{
			current_symbol.quantity_pending -= quantity - quantity_filled;

			if (order_type == "limit") final_symbols.buying_power += limit_price * (quantity - quantity_filled);
			else if (order_type == "market") {}
		}
		else if (order_type == "limit" || order_type == "market") current_symbol.quantity_pending += quantity - quantity_filled;

		if (rejected_replacement) current_symbol.replacement_order_id.clear();
		else if (event == "replaced") //set order_id to replacement_order_id
		{
			current_symbol.order_id = current_symbol.replacement_order_id;
			current_symbol.replacement_order_id.clear();
		}
		else current_symbol.order_id.clear();
	}
	else if (event == "new")
	{
		if (false) //if we receive an order that the bot did not make or initialize
		{
			if (order_side == "buy")
			{
				current_symbol.quantity_pending += quantity;

				if (order_type == "limit") final_symbols.buying_power -= limit_price * quantity;
				else if (order_type == "market") {}
			}
			else if (order_type == "limit" || order_type == "market") current_symbol.quantity_pending -= quantity;
		}

		current_symbol.average_fill_price = average_fill_price;
		current_symbol.order_quantity_filled = quantity_filled;
		current_symbol.order_quantity = quantity;
	}
	else
	{
		current_symbol.average_fill_price = average_fill_price;
		current_symbol.order_quantity_filled = quantity_filled;
		current_symbol.order_quantity = quantity;
	}

	//there are other order statuses that are not explicitly considered - https://alpaca.markets/docs/trading/orders/#order-lifecycle

	std::cout << Symbol << " " << event << " " << order_type << " " << order_side << " at " << limit_price << " for " << quantity << " shares";
	std::cout << " - remaining buying power : " << final_symbols.buying_power;
	std::cout << " - avg fill price : " << average_fill_price;
	std::cout << " - desired qty : " << current_symbol.quantity_desired;
	std::cout << " - pending qty : " << current_symbol.quantity_pending;
	std::cout << " - owned qty : " << current_symbol.quantity_owned;
	std::cout << " - update time : " << trade_update_info["updated_at"] << std::endl << std::endl;

	current_symbol.last_update_status = event;
	current_symbol.waiting_for_update = false;

	//if the bot changes its directional bias, it might need to place a new order after canceling one or vice versa
	if (current_symbol.trading_permitted && (event == "canceled" || event == "new")) current_symbol.updatePosition(final_symbols);
}

//auto type should be iterator for symbol data
void cleanQuoteDeque(auto& current_symbol_iterator, const auto& start_symbol_iterator, const auto& end_symbol_iterator)
{
	if (current_symbol_iterator->quote_times.size() < 2) //go to the next symbol if no quote data needs to be removed from the quote deques of the current symbol
	{
		if (++current_symbol_iterator >= end_symbol_iterator) current_symbol_iterator = start_symbol_iterator;
		return;
	}

	size_t number_of_quotes_to_be_removed = current_symbol_iterator->quote_times.size() - 1; //we know the size is at least 2 because of the if statement above

	//limit the number of quotes we remove so the bot won't slow down
	if (number_of_quotes_to_be_removed > max_allowed_quote_removals) number_of_quotes_to_be_removed = max_allowed_quote_removals;

	//remove all but the most recent quote data that occurs before the most recent trade update
	while (number_of_quotes_to_be_removed > 0 && *(current_symbol_iterator->quote_times.begin() + 1) < current_symbol_iterator->t)
	{
		current_symbol_iterator->quote_times.pop_front();
		current_symbol_iterator->bid_prices.pop_front();
		current_symbol_iterator->ask_prices.pop_front();

		--number_of_quotes_to_be_removed;
	}

	if (number_of_quotes_to_be_removed > 0) //true if none - but one - quote update occured before the most recent trade
	{
		if (++current_symbol_iterator >= end_symbol_iterator) current_symbol_iterator = start_symbol_iterator;
	}
}

double symbol::getPriceLevel(const int n_diff) //n_diff is difference from new_n
{
	abs_n = (new_n + n_diff >= 0) ? (new_n + n_diff) : -(new_n + n_diff);

	C0 = -l * K0(abs_n);
	C1 = sqrt(0.25 * C0 * C0 - 1.0 / 27.0); //assumes the argument is non-negative (l was checked while collecting data)

	E = (2.0 * abs_n + 1.0) * (cbrt(-0.5 * C0 + C1) + cbrt(-0.5 * C0 - C1)) / E0;
	E = 1.0 + 0.21 * std * E;

	if (new_n + n_diff >= 0) return previous_days_close * E;

	return previous_days_close / E;
}

#ifdef USE_MARKET_ORDERS

void symbol::updatePosition(symbolData& symbol_data)
{
	if (waiting_for_update) return; //don't place another order until the previous one has been received by the trade update stream
	if (quantity_desired > quantity_owned + quantity_pending) //add more shares
	{
		if (quantity_pending < 0)
		{
			if (!canceled_order)
			{
				//cancel the sell order
				symbol_data.cancelOrder(order_id);

				//404 might be returned if we try to cancel an order right after it is filled (and the bot didn't get the update yet)
				if (symbol_data.response.status_code != 204 && symbol_data.response.status_code != 404)
				{
					throw exceptions::exception("When canceling sell order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
				}

				canceled_order = true;
				waiting_for_update = true;
			}
		}
		else
		{
			int quantity_attainable = static_cast<int>(symbol_data.buying_power / entry_price);
			int quantity_remaining = quantity_desired - quantity_owned - quantity_pending;

			if (quantity_attainable <= 0) return;
			if (quantity_attainable < quantity_remaining) quantity_remaining = quantity_attainable;

			//place a market buy order
			symbol_data.submitOrder(ticker, quantity_remaining, "buy");
			
			if (symbol_data.response.status_code != 200)
			{
				throw exceptions::exception("When sending buy order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
			}

			quantity_pending += quantity_remaining;
			order_id = symbol_data.order_data["id"];
			canceled_order = false;
			waiting_for_update = true;
		}
	}
	else if (quantity_desired < quantity_owned + quantity_pending) //liquidate some shares
	{
		if (quantity_pending > 0)
		{
			if (!canceled_order)
			{
				//cancel the buy order
				symbol_data.cancelOrder(order_id);

				//404 might be returned if we try to cancel an order right after it is filled (and the bot didn't get the update yet)
				if (symbol_data.response.status_code != 204 && symbol_data.response.status_code != 404)
				{
					throw exceptions::exception("When canceling buy order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
				}

				canceled_order = true;
				waiting_for_update = true;
			}
		}
		else
		{
			int quantity_remaining = quantity_owned + quantity_pending - quantity_desired;

			//place a market sell order
			symbol_data.submitOrder(ticker, quantity_remaining, "sell");

			if (symbol_data.response.status_code != 200)
			{
				throw exceptions::exception("When sending sell order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
			}

			quantity_pending -= quantity_remaining;
			order_id = symbol_data.order_data["id"];
			canceled_order = false;
			waiting_for_update = true;
		}
	}
}

#else

void symbol::updatePosition(symbolData& symbol_data)
{
	if (waiting_for_update) return; //don't place another order until the previous one has been received by the trade update stream
	if (quantity_desired > quantity_owned + quantity_pending) //add more shares
	{
		if (quantity_pending > 0)
		{
			if (last_update_status == "pending_new") return; //cannot replace orders with this status

			int quantity_attainable = static_cast<int>((symbol_data.buying_power + limit_price * order_quantity) / entry_price);
			int quantity_remaining = quantity_desired - quantity_owned - quantity_pending + order_quantity;

			if (quantity_attainable <= 0) return;
			if (quantity_attainable < quantity_remaining) quantity_remaining = quantity_attainable;
			if (quantity_remaining == order_quantity && limit_price == entry_price) return;

			//replace the buy order
			symbol_data.replaceOrder(order_id, quantity_remaining, entry_price);

			//possible 404 - if the order is filled or canceled when the replacement is sent
			//possible 422 - if the order is closed right after or while the replacement is sent
			if (symbol_data.response.status_code != 200 && symbol_data.response.status_code != 202 && symbol_data.response.status_code != 201 && \
				symbol_data.response.status_code != 204 && symbol_data.response.status_code != 404)
			{
				if (symbol_data.response.status_code == 422)
				{
					if (symbol_data.order_data.find("message") != symbol_data.order_data.end())
					{
						std::cout << "Failed to replace buy order for " << ticker << " because " << symbol_data.order_data["message"] << std::endl;
						
						//the order was filled before the replacement was sent if any of the conditions below are true
						if (symbol_data.order_data["message"] == order_not_open) return;
						if (symbol_data.order_data["message"] == qty_le_filled) return;
						if (symbol_data.order_data["message"] == qty_le_filled_bin) return;
					}
				}

				throw exceptions::exception("When replacing buy order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
			}

			//if no 404
			if (symbol_data.response.status_code != 404)
			{
				symbol_data.buying_power -= entry_price * (quantity_remaining - order_quantity_filled);

				order_quantity = quantity_remaining;
				quantity_pending += quantity_remaining - order_quantity_filled;
				replacement_order_id = symbol_data.order_data["id"];
				limit_price = entry_price;
				canceled_order = false;
				waiting_for_update = true;
			}
		}
		else if (quantity_pending < 0)
		{
			if (!canceled_order)
			{
				//cancel the sell order
				symbol_data.cancelOrder(order_id);

				//404 might be returned if we try to cancel an order right after it is filled (and the bot didn't get the update yet)
				if (symbol_data.response.status_code != 204 && symbol_data.response.status_code != 404)
				{
					throw exceptions::exception("When canceling sell order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
				}

				canceled_order = true;
				waiting_for_update = true;
			}
		}
		else
		{
			int quantity_attainable = static_cast<int>(symbol_data.buying_power / entry_price);
			int quantity_remaining = quantity_desired - quantity_owned - quantity_pending;

			if (quantity_attainable <= 0) return;
			if (quantity_attainable < quantity_remaining) quantity_remaining = quantity_attainable;

			//place a buy order
			symbol_data.submitOrder(ticker, quantity_remaining, "buy", entry_price);
			symbol_data.buying_power -= entry_price * quantity_remaining;

			if (symbol_data.response.status_code != 200)
			{
				throw exceptions::exception("When sending buy order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
			}

			quantity_pending += quantity_remaining;
			order_id = symbol_data.order_data["id"];
			limit_price = entry_price;
			canceled_order = false;
			waiting_for_update = true;
		}
	}
	else if (quantity_desired < quantity_owned + quantity_pending) //liquidate some shares
	{
		if (quantity_pending > 0)
		{
			if (!canceled_order)
			{
				//cancel the buy order
				symbol_data.cancelOrder(order_id);

				//404 might be returned if we try to cancel an order right after it is filled (and the bot didn't get the update yet)
				if (symbol_data.response.status_code != 204 && symbol_data.response.status_code != 404)
				{
					throw exceptions::exception("When canceling buy order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
				}

				canceled_order = true;
				waiting_for_update = true;
			}
		}
		else if (quantity_pending < 0)
		{
			int quantity_remaining = quantity_owned + quantity_pending - quantity_desired + order_quantity;

			if (quantity_remaining == order_quantity && limit_price == entry_price) return;

			if (last_update_status == "pending_new") return; //cannot replace orders with this status

			//replace the sell order
			symbol_data.replaceOrder(order_id, quantity_remaining, entry_price);

			//possible 404 - if the order is filled or canceled when the replacement is sent
			//possible 422 - if the order is closed right after or while the replacement is sent
			if (symbol_data.response.status_code != 200 && symbol_data.response.status_code != 202 && symbol_data.response.status_code != 201 && \
				symbol_data.response.status_code != 204 && symbol_data.response.status_code != 404)
			{
				if (symbol_data.response.status_code == 422)
				{
					if (symbol_data.order_data.find("message") != symbol_data.order_data.end())
					{
						std::cout << "Failed to replace sell order for " << ticker << " because " << symbol_data.order_data["message"] << std::endl;

						//the order was filled before the replacement was sent if any of the conditions below are true
						if (symbol_data.order_data["message"] == order_not_open) return;
						if (symbol_data.order_data["message"] == qty_le_filled) return;
						if (symbol_data.order_data["message"] == qty_le_filled_bin) return;
					}
				}

				throw exceptions::exception("When replacing sell order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
			}

			//if no 404 error
			if (symbol_data.response.status_code != 404)
			{
				order_quantity = quantity_remaining;
				quantity_pending -= quantity_remaining - order_quantity_filled;
				replacement_order_id = symbol_data.order_data["id"];
				limit_price = entry_price;
				canceled_order = false;
				waiting_for_update = true;
			}
		}
		else
		{
			int quantity_remaining = quantity_owned + quantity_pending - quantity_desired;

			//place a sell order
			symbol_data.submitOrder(ticker, quantity_remaining, "sell", entry_price);

			if (symbol_data.response.status_code != 200)
			{
				throw exceptions::exception("When sending sell order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
			}

			quantity_pending -= quantity_remaining;
			order_id = symbol_data.order_data["id"];
			limit_price = entry_price;
			canceled_order = false;
			waiting_for_update = true;
		}
	}
	else if (quantity_pending != 0) //adjust the price of the current order
	{
		int quantity_remaining = order_quantity; //quantity_owned + quantity_pending - quantity_desired is always zero in this case

		if (quantity_pending > 0)
		{
			//number of shares we can own with the available buying power
			int quantity_attainable = static_cast<int>((symbol_data.buying_power + limit_price * order_quantity) / entry_price);

			if (quantity_attainable <= 0) return;
			if (quantity_attainable < quantity_remaining) quantity_remaining = quantity_attainable;
		}

		if (quantity_remaining == order_quantity && limit_price == entry_price) return;

		if (last_update_status == "pending_new") return; //cannot replace orders with this status

		symbol_data.replaceOrder(order_id, quantity_remaining, entry_price);

		//possible 404 - if the order is filled or canceled when the replacement is sent
		//possible 422 - if the order is closed right after or while the replacement is sent
		if (symbol_data.response.status_code != 200 && symbol_data.response.status_code != 202 && symbol_data.response.status_code != 201 && \
			symbol_data.response.status_code != 204 && symbol_data.response.status_code != 404)
		{
			if (symbol_data.response.status_code == 422)
			{
				if (symbol_data.order_data.find("message") != symbol_data.order_data.end())
				{
					if (quantity_pending < 0) std::cout << "Failed to replace sell order for " << ticker << " because " << symbol_data.order_data["message"] << std::endl;
					else std::cout << "Failed to replace buy order for " << ticker << " because " << symbol_data.order_data["message"] << std::endl;

					//the order was filled before the replacement was sent if any of the conditions below are true
					if (symbol_data.order_data["message"] == order_not_open) return;
					if (symbol_data.order_data["message"] == qty_le_filled) return;
					if (symbol_data.order_data["message"] == qty_le_filled_bin) return;
				}
			}

			if (quantity_pending < 0)
			{
				throw exceptions::exception("When replacing sell order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
			}
			else throw exceptions::exception("When replacing buy order - " + std::to_string(symbol_data.response.status_code) + " : " + symbol_data.response.status_message);
		}

		if (symbol_data.response.status_code != 404) //if no 404 error
		{
			if (quantity_pending > 0)
			{
				symbol_data.buying_power -= entry_price * (quantity_remaining - order_quantity_filled);

				quantity_pending += quantity_remaining - order_quantity_filled;
			}
			else quantity_pending -= quantity_remaining - order_quantity_filled; //quantity_pending is less than 0

			order_quantity = quantity_remaining;
			replacement_order_id = symbol_data.order_data["id"];
			limit_price = entry_price;
			canceled_order = false;
			waiting_for_update = true;
		}
	}
}

#endif

symbolData::symbolData(const SSLContextWrapper& SSL_context_wrapper, const MLModel& Model)
	: ssl_context_wrapper(const_cast<SSLContextWrapper&>(SSL_context_wrapper)), model(const_cast<MLModel&>(Model)), symbolContainer() {}

symbolData::~symbolData() {}

void symbolData::submitOrder(const std::string& Symbol, const int& quantity, const std::string& side, const double& limit_price)
{
	body = "{\"symbol\":\"" + Symbol + "\", \"qty\":" + std::to_string(quantity) + ", \"side\":\"" + side \
		+ "\", \"type\":\"limit\", \"time_in_force\":\"day\", \"limit_price\":" + std::to_string(roundPrice(limit_price)) + ", \"extended_hours\":true}";

	base_headers["Content-Length"] = std::to_string(body.size());
	//base_headers["Content-Type"] = "application/json";

	order_data.clear();
	response.clear();

	http::post(ssl_context_wrapper, response, base_parameters, base_headers, account_endpoint, "/v2/orders", body, timeout);

	if (response.message.size() > 2) json_parser.parseJSON(order_data, response.message);

	base_headers.erase("Content-Length");
	//base_headers.erase("Content-Type");
}

void symbolData::submitOrder(const std::string& Symbol, const int& quantity, const std::string& side)
{
	body = "{\"symbol\":\"" + Symbol + "\", \"qty\":" + std::to_string(quantity) + ", \"side\":\"" + side \
		+ "\", \"type\":\"market\", \"time_in_force\":\"day\"}";

	base_headers["Content-Length"] = std::to_string(body.size());
	//base_headers["Content-Type"] = "application/json";

	order_data.clear();
	response.clear();

	http::post(ssl_context_wrapper, response, base_parameters, base_headers, account_endpoint, "/v2/orders", body, timeout);

	if (response.message.size() > 2) json_parser.parseJSON(order_data, response.message);

	base_headers.erase("Content-Length");
	//base_headers.erase("Content-Type");
}

void symbolData::replaceOrder(const std::string& order_id, const int& quantity, const double& limit_price)
{
	body = "{\"qty\":" + std::to_string(quantity) + ", \"limit_price\":" + std::to_string(roundPrice(limit_price)) + "}";

	base_headers["Content-Length"] = std::to_string(body.size());
	//base_headers["Content-Type"] = "application/json";

	order_data.clear();
	response.clear();

	http::patch(ssl_context_wrapper, response, base_parameters, base_headers, account_endpoint, "/v2/orders/" + order_id, body, timeout);

	if (response.message.size() > 2) json_parser.parseJSON(order_data, response.message);

	base_headers.erase("Content-Length");
	//base_headers.erase("Content-Type");
}

void symbolData::cancelOrder(const std::string& order_id)
{
	order_data.clear();
	response.clear();

	http::del(ssl_context_wrapper, response, base_parameters, base_headers, account_endpoint, "/v2/orders/" + order_id, timeout);
}

void symbolData::cancelAllOrders()
{
	order_data.clear();
	response.clear();

	base_headers["Content-Length"] = "0";

	http::del(ssl_context_wrapper, response, base_parameters, base_headers, account_endpoint, "/v2/orders", timeout);

	base_headers.erase("Content-Length");
}

void symbolData::closeAllPositions()
{
	order_data.clear();
	response.clear();

	base_headers["Content-Length"] = "0";

	http::del(ssl_context_wrapper, response, base_parameters, base_headers, account_endpoint, "/v2/positions", timeout);

	base_headers.erase("Content-Length");
}

#ifdef USE_MARKET_ORDERS

void closeAllPositions(symbolData& final_symbols, websocket& data_ws, websocket& account_ws)
{
	std::cout << "CLOSING EXISTING POSITIONS" << std::endl;

	//cancel existing orders and liquidate current positions

	final_symbols.cancelAllOrders();
	final_symbols.closeAllPositions();
}

#else

void closeAllPositions(symbolData& final_symbols, websocket& data_ws, websocket& account_ws)
{
	std::cout << "CLOSING EXISTING POSITIONS" << std::endl;

	//cancel existing orders and liquidate current positions

	try {
	tradeAndBarParser updateParser;
	dictionary last_trade_update;

	int total_shares_owned = 0;
	int total_shares_pending = 0;

	time_t early_stop_time = time(nullptr);

	std::string last_msg;

	for (auto& pair : final_symbols)
	{
		pair.second.quantity_desired = 0;
		pair.second.trading_permitted = false;

		total_shares_owned += pair.second.quantity_owned;
		total_shares_pending += pair.second.quantity_pending;
	}

	while (total_shares_owned > 0 || total_shares_pending != 0)
	{
		//try to liquidate all positions and cancel all orders within the timeout period
		if (time(nullptr) > early_stop_time) break;

		for (int i = 0; i < 100; i++)
		{
			account_ws.recv(last_msg);

			if (last_msg.size() > 2) //expecting individual json objects
			{
				handleTradeUpdate(last_msg, last_trade_update, final_symbols);
			}

			data_ws.recv(last_msg);

			if (last_msg.size() > 2) updateParser.parseJSONArray(last_msg, final_symbols);
		}

		total_shares_owned = 0;
		total_shares_pending = 0;

		for (auto& pair : final_symbols)
		{
			total_shares_owned += pair.second.quantity_owned;
			total_shares_pending += pair.second.quantity_pending;
		}
	}}
	catch (const std::runtime_error& runtime_error) { std::cout << "RUNTIME ERROR WHEN CLOSING ALL POSITIONS" << std::endl; throw runtime_error; }
	catch (const exceptions::exception& exception)
	{
		std::cout << "EXCEPTION WHEN CLOSING ALL POSITIONS" << std::endl;

		if (final_symbols.response.status_code == 301)
		{
			std::cout << "RESPONSE FIELDS" << std::endl;

			for (const auto pair : final_symbols.response.fields) { std::cout << pair.first << " : " << pair.second << std::endl; }

			std::cout << std::endl;
			std::cout << "ORDER FIELDS" << std::endl;

			for (const auto pair : final_symbols.order_data) { std::cout << pair.first << " : " << pair.second << std::endl; }

			std::cout << std::endl;
			//std::cout << "TICKER : " << current_symbol.ticker << std::endl;
			//std::cout << "ORDER ID : " << current_symbol.order_id << std::endl;
			//std::cout << "REPLACEMENT ORDER ID : " << current_symbol.replacement_order_id << std::endl;
		}

		throw exception;
	}
	catch (const SSLNoReturn& no_return) { std::cout << "SSL NO RETURN WHEN CLOSING ALL POSITIONS" << std::endl; throw no_return; }
	catch (const std::exception& exception) { std::cout << "BASE EXCEPTION WHEN CLOSING ALL POSITIONS" << std::endl; throw exception; }
}

#endif
