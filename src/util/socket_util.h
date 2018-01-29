#ifndef _INC_SOCKET_UTIL_
#define _INC_SOCKET_UTIL_

class WSAInitializer {
public:
    WSAInitializer();
    ~WSAInitializer();

public:
    int Init();
};

int SocketConnect(const char* _pszIP, unsigned short _uPort);
int SocketReadLine(int _nFD, char* _pBuf, size_t _uSz);

#endif