//use multiple websockets asynchronously

#include "exceptUtils.h" //needed for custom exception class
#include "socketUtils.h" //needed for the wsa and ssl context wrappers
#include "httpUtils.h" //needed for the http response object
#include "wsUtils.h"

#include <stdexcept>
#include <iostream>
#include <string>


int main()
{
    try
    {
#ifdef _WIN32

        WSAWrapper wsa_wrapper; //needed on Windows only - destructor must be called after all sockets are closed

#endif

        SSLContextWrapper ssl_context_wrapper; //destructor must be called after all sockets are closed

        //placing the websocket in an if or try/catch statement ensures that it will always be destroyed before ssl_context
        //ALL SOCKET OBJECTS MUST BE DESTROYED BEFORE THE SSL_CONTEXT
        try
        {
            //timeout in seconds
            int timeout = 10;

            //socket is blocking if true
            bool blocking = false;

            //specify the hostname
            std::string host = "streamer.finance.yahoo.com";

            //create the websockets
            websocket websocket_client1(ssl_context_wrapper, host, blocking, timeout);
            websocket websocket_client2(ssl_context_wrapper, host, blocking, timeout);

            //initialize the websockets - connect to the host
            websocket_client1.reInit();
            websocket_client2.reInit();

            //define websocket headers
            dictionary headers;

            //there four headers are a must for any websocket connection (not including Host)
            headers["Upgrade"] = "websocket";
            headers["Connection"] = "Upgrade";
            headers["Sec-WebSocket-Version"] = "13";
            headers["Sec-Websocket-Key"] = generateRandomBase64String(16);

            //define the path (in this case there is none for 'wss://streamer.finance.yahoo.com/')
            std::string path = "/";

            //write the initial http response to response
            http::httpResponse response;

            //clear out any information that might be there from a previous response
            response.clear();

            //open the first websocket connection
            websocket_client1.open(headers, path.c_str(), response);

            //if we dont get 101 then something went wrong
            if (response.status_code != 101) throw exceptions::exception("Could not open the websocket connection.");

            //clear out the information from the first response
            response.clear();

            //open the second websocket connection
            websocket_client2.open(headers, path.c_str(), response);

            //if we dont get 101 then something went wrong
            if (response.status_code != 101) throw exceptions::exception("Could not open the websocket connection.");

            //subscribe to receive trade updates for BTC and ETH
            websocket_client1.send("{\"subscribe\": [\"BTC-USD\"]}", WS_TEXT_FRAME);
            websocket_client2.send("{\"subscribe\": [\"ETH-USD\"]}", WS_TEXT_FRAME);

            //write individual messages from each websocket
            std::string last_message1;
            std::string last_message2;

            //print incoming messages from both websockets
            while (true)
            {
                //non-blocking sockets will return if no message is pending and will not block execution

                //check for a pending message from the first websocket
                websocket_client1.recv(last_message1);

                //an empty string indicates no message was pending
                if (last_message1.size()) std::cout << "FROM WEBSOCKET 1 : " << last_message1 << "\n\n";

                //check for a pending message from the second websocket
                websocket_client2.recv(last_message2);

                if (last_message2.size()) std::cout << "FROM WEBSOCKET 2 : " << last_message2 << "\n\n";
            }
        }
        catch (const exceptions::exception& exception)
        {
            std::cout << "Exception caught : " << exception.what() << std::endl;
        }
        catch (const std::runtime_error& runtime_error)
        {
            std::cout << "Runtime Error caught : " << runtime_error.what() << std::endl;
        }
        catch (const std::exception& exception)
        {
            std::cout << "Base Exception caught : " << exception.what() << std::endl;
        }
    }
    catch (const exceptions::exception& exception)
    {
        std::cout << " - Exception caught : " << exception.what() << std::endl;
    }
    catch (const std::runtime_error& runtime_error)
    {
        std::cout << " - Runtime Error caught : " << runtime_error.what() << std::endl;
    }
    catch (const std::exception& exception)
    {
        std::cout << " - Base Exception caught : " << exception.what() << std::endl;
    }
    
    return 0;
}
