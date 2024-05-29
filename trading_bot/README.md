# Implementation Details
A low-level explaination of exactly how the bot works and what processes / methods are used to complete each task.

## Table of Contents
#### 1) How the bot works

<br/></br>

## Part 1 : How the bot works
How data is gathered and processed. <br>

After the bot is started, it obtains the time of day and weekday. If the time of day is past the specified end of day or the day is a Saturday or Sunday then the bot sleeps until 8am EST the next day. The bot retrieves todays opening and closing times and checks if they are their normal values (9:30am opening and 4pm closing). If they are not then today is (or should be) a market holiday and the bot will sleep until 8am of the next day. <br>

The bot will start by obtaining a list of all tradable assets on NYSE and NASDAQ and then will use multiple http clients to asynchronously gather daily closing prices and volumes to compute quantim price levels and other features which will then be used to decide which stocks to watch today. <br>

After that, the bot will start one websocket stream that gathers trade and bar updates and will listen for minute bar updates while using the http clients to compute the accumulated daily volumes of each stock. The websocket and http clients are run together asynchronously during this period. <br>

After that, the bot will subscribe to individual trade and quote updates on the first websocket and start the account update websocket and continue to run both of those asynchronously. If the bot reaches this stage before 8:01am or 9:31am when the bot uses market orders (the specified starting trading time for this bot) then the bot will wait until then to start trading. <br>

The bot will stop trading at 3:55pm and then will repeat the process described above.
