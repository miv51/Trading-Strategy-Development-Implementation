# Implementation Details
A low-level explaination of exactly how the bot works and what processes / methods are used to complete each task.

## Table of Contents
#### 1) Explaination of source code
#### 2) How the bot works

<br/></br>

## Part 1 : Explaination of source code
Explainations and demonstrations of the most significant lower-level modules. All files referenced are in the <code/>examples</code> folder.

#### Json Utilities
This module is used to parse json objects with known structure. It includes a function for parsing a single json object and a method for parsing json arrays separately. <code/>json_parse.cpp</code> and <code/>json_array_parse.cpp</code> contain an example of parsing a single json object and array respectively. The examples should yield the following outputs.

<br> The following output is for the single json object.

```bash
Original Json Object - {"key0":"value0", "key1":3.1415, "key2":null}

Parsed Json Object key : value pairs
key0 : value0
key1 : 3.1415
key2 : null

Original Json Object - {"jsonArray":[0, 1, 2, 4, -6], "nestedJsonObject":{"a":"Hello World!", "b":false}}

Parsed Json Object key : value pairs
jsonArray : [0, 1, 2, 4, -6]
nestedJsonObject : {"a":"Hello World!", "b":false}
```

<br> The following output is for the array of json objects.

```bash

s : FAKE
t : 2001-05-11T:09:42:00Z
v : 10295
n : 205
c : 22.05
o : 21.77
h : 22.25
l : 21.6

s : BOGUS
t : 2001-05-11T:11:25:00Z
v : 328166
n : 622
c : 4
o : 3.5
h : 4.2
l : 3.48
```

#### Websocket Utilities
This module is used to listen for updates from data streams. It is a basic implementation of [The Websocket Protocol](https://datatracker.ietf.org/doc/html/rfc6455) that does not handle continuation frames - since there shouldn't be any for this user case. Non-blocking I/O is also supported; in that case, when <code/> websocket::recv </code> is called, it will return immediately if it receives no frame header and will read the full message before returning otherwise. If it is a blocking socket, it will return once it has recieved a message. <br>

<code/>single_ws_blocking.cpp</code> and <code/>multiple_ws_non_blocking.cpp</code> contain an example of printing messages from a single blocking websocket and multiple non-blocking websockets respectively. Both of these examples use the yahoo finance data stream which sends proto-buffered messages, and these messages are not readable in the form that they are sent so if you decide to test those two examples then expect that.

#### Http Utilities
This module is used to perform http get, patch, post, and delete requests. Like the websocket module, the http module also supports both blocking and non-blocking I/O. <code/>single_get_request.cpp</code> and <code/>multiple_get_requests.cpp</code> contain an example performing a single http get request and multiple asynchronous get requests. <br>

#### Socket Utilities
This module is used to manage SSL resources and wrap sockets and socket operations. The websocket and http modules heavily utilize this module. <br>

#### Model Utilities
This module contains the predictive model used to predict transition probabilities and is used to productionize trained tensorflow models. It is a raw and highly efficient (for this bot) implementation of a multilayer perceptron (MLP) and allows me to dynamically load model weights for fixed model archetectures. <br>

<br>

## Part 2 : How the bot works
How data is gathered and processed. <br>

After the bot is started, it obtains the time of day and weekday. If the time of day is past the specified end of day or the day is a Saturday or Sunday then the bot sleeps until 3:55am EST the next day. The bot retrieves todays opening and closing times and checks if they are their normal values (9:30am opening and 4pm closing). If they are not then today is (or should be) a market holiday and the bot will sleep until 3:55am of the next day. <br>

The bot will start by obtaining a list of all tradable assets on NYSE and NASDAQ and then will use multiple http clients to asynchronously gather daily closing prices and volumes to compute quantim price levels and other features which will then be used to decide which stocks to watch today. <br>

After that, the bot will start one websocket stream that gathers trade and bar updates and will listen for minute bar updates while using the http clients to compute the accumulated daily volumes of each stock. The websocket and http clients are run together asynchronously during this period. <br>

After that, the bot will subscribe to individual trade updates on the first websocket and start the account update websocket and continue to run both of those asynchronously. If the bot reaches this stage before 4:01am (the specified starting trading time for this bot) then the bot will wait until then to start trading. <br>

The bot will stop trading at some specified time and then will repeat the process described above.


