// simple_impl.cpp : �������̨Ӧ�ó������ڵ㡣
//
#include "http_client/http_client.h"
#include "util/socket_util.h"
#include "util/string_util.h"

int main(int argc, char* argv[])
{
    WSAInitializer wsaInit;
    if (0 != wsaInit.Init()) {
        printfln("winsock2 initialize failed");
        return -1;
    }

    HTTPClient client;
    HTTPClient::Response rsp;
    client.Get("http://gocode.cc", "", &rsp);

	return 0;
}
