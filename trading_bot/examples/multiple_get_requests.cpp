//make multiple asynchronous http get requests

#include "exceptUtils.h" //needed for custom exception class
#include "socketUtils.h" //needed for the wsa and ssl context wrappers
#include "httpUtils.h"

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

        //placing the http client in an if or try/catch statement ensures that it will always be destroyed before ssl_context
        //ALL SOCKET OBJECTS MUST BE DESTROYED BEFORE THE SSL_CONTEXT
        try
        {
            //timeout in seconds
            int timeout = 10;

            //socket is blocking if true
            bool blocking = false;

            //specify the hostname - full url is https://jsonplaceholder.typicode.com/posts/1
            std::string host = "jsonplaceholder.typicode.com";

            //create the http clients
            http::httpClient clients[2] = {http::httpClient(ssl_context_wrapper, host, blocking, timeout), \
            http::httpClient(ssl_context_wrapper, host, blocking, timeout)};

            //initialize the http clients - connect to the host
            for (auto& client : clients) client.reConnect();

            //define the http parameters (in this case there are none)
            dictionary parameters;

            //define the http headers
            dictionary headers;

            //this is the only header needed for base http requests (not including Host)
            headers["Connection"] = "close"; //or keep-alive if sending multiple requests

            //define the path
            std::string path = "/posts/1";

            //write the initial http responses
            http::httpResponse responses[2] = {http::httpResponse(), http::httpResponse()};
            
            //clear out any information that might be there from a previous response
            for (auto& response : responses) response.clear();

            //prepare the http get requests for each client
            for (auto& client : clients) client.get(parameters, headers, path);

            //keep track of the responses we finished reading
            bool finished[2] = {false, false}; //true when the response is received
            int finished_reading = 0;

            //the current status of the current http response
            http::status current_status;

            //read both responses asynchronously
            while (finished_reading < 2)
            {
                for (int i = 0; i < 2; i++)
                {
                    //if we received the full response then go to the next client
                    if (finished[i]) continue;

                    try { current_status = clients[i].recvResponse(responses[i]); }

                    //thrown if the socket is closed unexpectedly
                    //most likely will happen when keep-alive connections live too long
                    catch (const SSLNoReturn&)
                    {
                        //if this happens restart the http client
                        responses[i].clear();

			clients[i].reConnect(); //reconnect to the host
			clients[i].get(parameters, headers, path); //re-prepare the get request

			current_status = clients[i].recvResponse(responses[i]);
                    }

                    if (current_status == http::status::TIMED_OUT)
                    {
                        throw exceptions::exception("One of the requests timed out.");
                    }
                    else if (current_status == http::status::RECEIVED_RESPONSE)
                    {
                        finished[i] = true;

                        finished_reading++;
                    }
                }
            }
            
            //print the response bodies
            for (auto& response : responses) std::cout << '\n' << response.message << std::endl;
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
