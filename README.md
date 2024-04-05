# Trading-Strategy-Development-Implementation
My full process for developing and implementing an algorithmic trading strategy starting from strategy idea all the way to implementing it in an automated trading bot.

## Disclaimer
This repository is for educational purposes only. USE THE CONTENT IN THIS REPOSITORY AT YOUR OWN RISK. THE AUTHOR <!--AND ALL AFFILIATES--> ASSUMES NO RESPONSIBILITY FOR YOUR TRADING RESULTS. Do not risk money that you are not willing to lose. There are almost certainly bugs in the code - nothing in this repository comes with ANY warranty.

## Table of Contents
#### 1) Strategy Idea
#### 2) Acquiring Data
#### 3) Analyzing Data
#### 4) Develop a Machine Learning Model
#### 5) Develop a trading strategy
#### 6) Implement the strategy
#### 7) Results from paper trading with real-time data
#### 8) Known Issues

<br/></br>

## Part 1 : Strategy Idea
Quantum Price Levels (QPLs) are price levels that are supposed to indicate where prices will most likely consolidate. Quantum price levels (QPLs) can be likened to discrete values, similar to how the energy levels of an atom fluctuate, representing possible price shifts of a financial instrument. These price levels have been studied on forex pairs but, to the best of my knowledge, not on equities. I attempted to develop a strategy that capitalizes on the upward transitions between these price levels in equities. This [video](https://www.youtube.com/watch?v=3rbJLFUHKww) goes into more detail about QPLs and the notebook <code/>Quantum_Price_Levels_Calc.ipynb</code> provides a python implementation and a step-by-step explaination to perform the calculation.

## Part 2 : Acquiring Data
All data was obtained from the Alpaca API while using their [Unlimited data plan](https://alpaca.markets/docs/market-data/#subscription-plans). All transitions between price levels were obtained from 2020-01-01 to 2023-12-05 using the python script <code/> transition_data_acq.py </code>. It took ~60 hours to analyze ~11.6TB of tick data with 30 threads evenly distributed across 6 3.5GHz processors. Stocks on days with less than 500 completed trading days were excluded and tick data with any of the conditions outlined in the first table below were excluded. See pages 23 and 67 of the [Tape Specification](https://www.ctaplan.com/publicdocs/ctaplan/CTS_Pillar_Output_Specification.pdf) for more details about exchange codes and trade conditions respectively. The features that were included in the dataset are outlined in the 2nd table below. <br>

Unfortunately, I cannot share the transition data since I used market data from a proprietary source to construct it.

Condition | Meaning
--- | ---
exchange == D | trade was reported from the FINRA ADF
U in conditions | sold out of sequence during pre/post market session
Z in conditions | sold out of sequence during regular market session
size < 100 | number of shares traded is less than 100

Feature | Description
--- | ---
vsum | cumulative volume over the current trading day - updated at the close of every minute bar
rolling_vsum | cumulative volume over the past two seconds - updated every filtered trade
rolling_csum | the number of trades that have occured in the past two seconds
dp | relative price change in the past two seconds - price of the most recent trade divided by the price of the last trade in a rolling two-second period
dt | time difference (in seconds) between the first and last trade in a rolling two-second period
n_time | time that the current transition occured at (in nanoseconds since epoch)
price | price that the trade, that completed the current transition, occured at
size | number of shares traded during the trade that completed the current transition
n | the principal quantum number for the current quantum price level
t_time | time that the next transition occured at
previous_days_close | the consolidated closing price of the previous day's regular market session
average_volume | the average daily volume of the past 70 trading days
mean | the mean of the relative returns used for the QPL calculation
std | standard deviation of the relative returns used for the QPL calculation
p(-dx) | estimated probability density at -dr from the mean (see <code/>Quantum_Price_Levels_Calc.ipynb</code>)
p(+dx) | estimated probability density at +dr from the mean (see <code/>Quantum_Price_Levels_Calc.ipynb</code>)
E0 | see <code/>Quantum_Price_Levels_Calc.ipynb</code>
lambda | see <code/>Quantum_Price_Levels_Calc.ipynb</code>
transition | the number of price levels the stock price will change over the next transition
throughtput | the number of bytes of data analysed to find the current transition
symbol | integer-encoded ticker (see <code/> transition_data_acq.py </code>)

## Part 3 : Analyzing Data
Exploratory Data Analysis (EDA) and Data Wrangling was performed on the data and the results are presented in <code/> transition_data_EDA_and_strategy_dev.ipynb </code>.

## Part 4 : Develop a Machine Learning Model
A predictive model - that predicts what price level that the price will reach next - was developed and the results are also presented in <code/> transition_data_EDA_and_strategy_dev.ipynb </code>. The table below shows the possible predictions.

Prediction value | Meaning
--- | ---
-1 | The price will move down at least one price level during todays trading session
0 | The price will stay around this level for the rest of todays trading session
+1 | The price will move up at least one price level during todays trading session

*NOTE : todays trading session includes the pre market session, so the beginning of the session would be 4am EST. <br>

## Part 5 : Develop an Automated Trading Strategy
From our EDA, we obtained a deep learning model that predicts upward (+1) transitions with ~50% accuracy during periods of liquidity, where at least 5 trades have occured in the last 2 seconds. 50% accuracy with a roughly 1 to 1 profit to loss ratio will not produce any profits, but as shown in <code/> transition_data_EDA_and_strategy_dev.ipynb </code>, the predicted probabilities of the final model are highly correlated with the model's accuracy. This allows us to be more selective about which trades we take. <br>

I enter a position with the intention of selling for a gain or continue holding at the next highest price level, and selling for a loss at the next lowest price level. I use the following inequality to determine which trades I take - where $P$, $G$, $R$, and $\delta$ is the predicted probability of a +1 transition from the model, potential gain, potential loss, and the absolute value of the spread (difference between the best bid and ask) of the most recent quote update for each trade respectively. <br>

$$\huge P(G + R) > R + \delta$$

I size my positions in such a way where each potential trade risks losing the same amount of capital. <br>

## Part 6 : Implement the strategy
This strategy was implemented in a trading bot designed with Visual Studio 2022 and was written in C++20. The source code for the bot is located in the <code/> trading_bot/workspace </code> folder and the only external dependency is [OpenSSL v3.1.2](https://slproweb.com/products/Win32OpenSSL.html), which contains two libraries named 1ibssl-3-x54.dll and libcrypto-3-x64.dll that need to be included in the same directory as the executable. For MacOS, brew install openssl 3.0 and build with the Cmake file <code/> CMakeLists.txt </code>. The bot needs the Alpaca [Unlimited data plan](https://alpaca.markets/docs/market-data/#subscription-plans) for the real-time data stream. The bot handles orders through REST API calls and recieves historical data from REST API's and real-time data from websocket communications. The bot was tested on Windows and MacOS platforms. Make sure to include <code/> model_weights.json </code> and <code/> scaler_info.json </code> in the same directory as the executable. They contain the model weights, and means and standard deviations of each feature respectively. The model can be retrained using <code/> retrain_model.py </code> (assuming you have transition data) and should be retrained at least every 4 months. <br>

The bot can be compiled to use market or limit orders but not both. <br>

When the bot uses limit orders, it is also allowed to trade pre market. In the event a buy (sell) order is not filled, it is canceled (replaced) either when the price of the respective stock reaches a different QPL or 5 minutes before the regular-market session ends (3:55pm EST). When a sell order is replaced before 3:55pm, it is re-adjusted to the most recent QPL. After 3:55pm, it is re-adjusted to the current bid minus dp minus one whole-penny or sub-penny increment. If an exception is thrown, the bot will attempt to close each open position within the specified timeout period before shutting itself down. The bot will not try to trade on weekends or market holidays - even the ones with early closing times. <br>

When the bot uses market orders, everything is the same as above except it only trades during regular market hours. <br>

## Part 7 : Results from paper trading with real-time data
After observing the orders placed by the bot over the past two months - on a day-by-day basis - (as of 12-26-2023), I have noticed that many of the orders ended up being canceled or replaced which lead to major drawbacks in P&L. Comparing the prediction signals generated by the bot in real-time and the ones obtained using <code/> transition_data_acq.py </code>, showed almost no discrepancies (estimated less than 1 in 100) and did not have a significant impact on P&L observed at the end of almost every trading day. This strongly suggests that the model is correctly implemented in the trading bot and the drawbacks in P&L are exclusively caused by how the strategy is executed (mainly execution time, liquidity, and volatility). I attempted (which failed) to address the liquidity issue by modifying an existing trading condition and adding another one which are both presented in the chart below.

Condition | Action
--- | ---
rolling_csum >= 10 | Modified
rolling_vsum * entry_price >= 10000 | Added

The fact that both rolling_vsum and rolling_csum have very low feature importances - as shown in <code/> transition_data_EDA_and_strategy_dev.ipynb </code> - suggests that the model should make predictions just as well on the subset of transitions with these additional constraints as the original set. The model was therefore re-evaluated (not re-trained) with those modified/additional conditions and as shown in the chart below, the simulated backtest still shows a net profit over the unseen two-month period with an initial starting capital of 500k USD and while risking 1000 USD per trade. <br>

![CHART](https://github.com/miv51/Trading-Strategy-Development-Implementation/blob/main/pl_over_time.png)

Although the trading strategy itself doesn't seem to be effective, the model used to generate trading signals maintained its predictive capability over the past two months. 

## Part 8 : Known Issues
* Can receive an unexpected 301 error when canceling and replacing orders (extremely rare occurrence) that will cause the bot to shut down (gracefully). This issue occurs because the bot does not handle rejected orders as intended.
* Can receive a 407 slow client error because of the large amount of incoming quote updates. When this occurs, it usually occurs around 9:30am EST or during 3:50pm - 4:00pm EST. This doesn't occur (at least not yet) when have the bot not clean the quote deques.
* The bot does not send orders to close all positions as expected when it uses limit orders.

