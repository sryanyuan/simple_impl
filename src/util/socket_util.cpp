#include "socket_util.h"
#include <WinSock2.h>
#include <WS2tcpip.h>

WSAInitializer::WSAInitializer() {
    
}

WSAInitializer::~WSAInitializer() {
    WSACleanup();
}

int WSAInitializer::Init() {
    WSADATA ws;
    return WSAStartup(MAKEWORD(2, 2), &ws);
}

int SocketConnect(const char* _pszIP, unsigned short _uPort) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd <= 0) {
        return -1;
    }
    sockaddr_in caddr = {0};
    caddr.sin_family = AF_INET;
    caddr.sin_port = ntohs(_uPort);
    if (inet_pton(AF_INET, _pszIP, &caddr.sin_addr) < 0) {
        closesocket(fd);
        return -2;
    }

    if (connect(fd, (const sockaddr*)&caddr, sizeof(sockaddr_in)) < 0) {
        closesocket(fd);
        return -3;
    }

    return fd;
}

int SocketReadLine(int _nFD, char* _pBuf, size_t _uSz) {
    // Read one by one until \r\n
    // We should add \0 to the end of the buf
    size_t i = 0;
    int nRet = 0;
    char cRecv = 0;
    bool bError = false;

    for (i = 0; i < _uSz - 1 - 2;) {
        nRet = recv(_nFD, &cRecv, 1, 0);
        if (nRet <= 0) {
            // Error occurs
            if (nRet == 0) {
                // Connection closed
                return 1;
            }
            int nErrCode = WSAGetLastError();
            bError = true;
            break;
        }

        if ('\r' == cRecv) {
            // Next is \n ?
            nRet = recv(_nFD, &cRecv, 1, MSG_PEEK);
            if (nRet <= 0 || cRecv != '\n') {
                // Error
                bError = true;
                break;
            }

            // Skip the \n for next reading
            recv(_nFD, &cRecv, 1, 0);
            _pBuf[i] = '\r';
            _pBuf[i + 1] = '\n';
            i += 2;
            break;
        }

        _pBuf[i] = cRecv;
        i++;
    }

    _pBuf[i] = '\0';

    if (bError) {
        return 0;
    }

    return i;
}
