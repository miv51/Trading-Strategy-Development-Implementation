//use a websocket

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
            bool blocking = true;

            //recv returns whatever this flag is set to when a ping frame is received
            bool signal_on_control = false;

            //create the websocket
            websocket websocket_client(ssl_context_wrapper, "streamer.finance.yahoo.com", blocking, signal_on_control, timeout);

            //initialize the websocket - connect to the host
            websocket_client.reInit();

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

            //open the websocket connection
            websocket_client.open(headers, path.c_str(), response);

            //status code should be 101 in most cases
            std::cout << "Received status code : " << response.status_code << " - with the following message : " << response.status_message << std::endl;

            //if we dont get 101 then something went wrong
            if (response.status_code != 101) throw exceptions::exception("Could not open the websocket connection.");

            //subscribe to receive updates for BTC
            websocket_client.send("{\"subscribe\": [\"BTC-USD\"]}", WS_TEXT_FRAME);

            //write individual messages to last_message
            std::string last_message;

            //print incoming messages from the websocket
            while (true)
            {
                if (websocket_client.recv(last_message)) std::cout << last_message << std::endl;
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
