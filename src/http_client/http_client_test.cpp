#include "http_client.h"
#include "../util/socket_util.h"
#include "../util/string_util.h"
#include <windows.h>

int http_client_test_main(int argc, char* argv[]) {
    WSAInitializer wsaInit;
    if (0 != wsaInit.Init()) {
        printfln("winsock2 initialize failed");
        return -1;
    }

    HTTPClient client;
    HTTPClient::Response rsp;

    for (int i = 0; i < 5; i++) {
        printfln("***********REQUEST %d*************", i);
        client.Get("http://gocode.cc", "/1", &rsp);
        rsp.Print();
        printfln("************************************\r\n");

        Sleep(i * 20 * 1000);
    }

    return 0;
}
