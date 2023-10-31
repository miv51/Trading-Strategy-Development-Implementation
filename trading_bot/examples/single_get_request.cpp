//make an http get request

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
            bool blocking = true;

            //specify the hostname - full url is https://jsonplaceholder.typicode.com/posts/1
            std::string host = "jsonplaceholder.typicode.com";

            //create the http client
            http::httpClient http_client(ssl_context_wrapper, host, blocking, timeout);

            //initialize the http client - connect to the host
            http_client.reConnect();

            //define the http parameters (in this case there are none)
            dictionary parameters;

            //define the http headers
            dictionary headers;

            //this is the only header needed for base http requests (not including Host)
            headers["Connection"] = "close"; //or keep-alive if sending multiple requests

            //define the path
            std::string path = "/posts/1";

            //write the initial http response to response
            http::httpResponse response;

            //clear out any information that might be there from a previous response
            response.clear();

            //perform the http get request
            //can use http::get for individual requests
            http_client.get(response, parameters, headers, path);

            //if we dont get 200 then something went wrong
            if (response.status_code != 200) throw exceptions::exception("Http get request failed.");

            //print the fields from the response header
            for (const auto& pair : response.fields) std::cout << pair.first << " : " << pair.second << std::endl;

            //print the response body
            std::cout << '\n' << response.message << std::endl;
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
