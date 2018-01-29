#define _CRT_SECURE_NO_WARNINGS
#include "http_client.h"
#include "http_request_header.h"
#include "../util/string_util.h"
#include "../util/socket_util.h"
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

static const char* s_szHTTPVersion[] = {
    "HTTP/1.1",
    "HTTP/1.0",
    NULL
};

static const char* s_szUserAgentValue = "Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.87 Safari/537.36";

HTTPClient::HTTPClient() {
    m_nHTTPVersion = HTTPVersion1_1;
    m_nHeaderConnection = ConnectionNone;
}

HTTPClient::~HTTPClient() {
    // Close all keep-alive fd
    for (KeepAliveSocketMap::const_iterator it = m_mapKeepAliveSockets.begin();
        it != m_mapKeepAliveSockets.end();
        it++) {
            closesocket(it->second);
    }
    m_mapKeepAliveSockets.clear();
}

int HTTPClient::ReadHeaderValue(const char* _pRecv, int _nRecvSize, const char* _pszHeaderKey, char _pOutbuf[], int _nOutputLen) {
    // Get content length
    if (0 != hasPrefixCi(_pRecv, _pszHeaderKey)) {
        // Not found prefix
        return 1;
    }
    int nHeaderKeyLength = strlen(_pszHeaderKey);
    int nReadOffset = nHeaderKeyLength;
    for (;;) {
        if (nReadOffset >= _nRecvSize) {
            // Parse error
            return -1;
        }
        // Skip all space
        if (_pRecv[nReadOffset] == ' ') {
            nReadOffset++;
            nHeaderKeyLength++;
            continue;
        }

        if (nReadOffset - nHeaderKeyLength > _nOutputLen - 1) {
            return -1;
        }
        if (_pRecv[nReadOffset] == '\r') {
            // End
            _pOutbuf[nReadOffset - nHeaderKeyLength] = '\0';
            return 0;
        }

        _pOutbuf[nReadOffset - nHeaderKeyLength] = _pRecv[nReadOffset];
        nReadOffset++;
    }
}

int HTTPClient::GetKeepAliveFDKey(HostInfo* _pHostInfo, std::string& _refKey) {
    std::string xKey = _pHostInfo->szSchema;
    xKey += _pHostInfo->szHost;
    if (0 != _pHostInfo->uPort) {
        char szPort[6];
        xKey += ":";
        xKey += itoa(int(_pHostInfo->uPort), szPort, 10);
    }
    _refKey = xKey;
    return 0;
}

int HTTPClient::GetKeepAliveFD(HostInfo* _pHostInfo) {
    std::string xKey;
    GetKeepAliveFDKey(_pHostInfo, xKey);

    KeepAliveSocketMap::const_iterator it = m_mapKeepAliveSockets.find(xKey);
    if (it == m_mapKeepAliveSockets.end()) {
        // Not found
        return 0;
    }
    return it->second;
}

int HTTPClient::DelKeepAliveFD(HostInfo* _pHostInfo) {
    std::string xKey;
    GetKeepAliveFDKey(_pHostInfo, xKey);

    KeepAliveSocketMap::const_iterator it = m_mapKeepAliveSockets.find(xKey);
    if (it == m_mapKeepAliveSockets.end()) {
        // Not found
        return 0;
    }

    m_mapKeepAliveSockets.erase(it);
    return 0;
}

int HTTPClient::SetKeepAliveFD(HostInfo* _pHostInfo, int _nFD) {
    std::string xKey;
    GetKeepAliveFDKey(_pHostInfo, xKey);

    KeepAliveSocketMap::iterator it = m_mapKeepAliveSockets.find(xKey);
    if (it != m_mapKeepAliveSockets.end()) {
        printfln("keep alive fd already exists, do update, key = %s", xKey.c_str());
        it->second = _nFD;
        return 0;
    }

    m_mapKeepAliveSockets.insert(make_pair(xKey, _nFD));
    return 0;
}

int HTTPClient::Get(const char* _pszHost, const char* _pszPath, Response* _pRsp) {
    _pRsp->m_bSuccess = false;
    HostInfo hostInfo;
    if (0 != parseHost(_pszHost, &hostInfo)) {
        printfln("parse host error");
        return -1;
    }
    HOSTENT* pHostEnt = gethostbyname(hostInfo.szHost);
    if (NULL == pHostEnt) {
        printfln("search %s host info error", _pszHost);
        return -1;
    }
    if (pHostEnt->h_addrtype != AF_INET) {
        printfln("address family %d not support", pHostEnt->h_addrtype);
        return -1;
    }
    if (NULL == pHostEnt->h_addr_list) {
        printfln("addr list is empty");
        return -1;
    }

    const char* pszIP = NULL;
    int nAddrListIndex = 0;
    for (int nAddrListIndex = 0; ; nAddrListIndex++) {
        if (NULL == pHostEnt->h_addr_list[nAddrListIndex]) {
            break;
        }
        // Skip
        pszIP = pHostEnt->h_addr_list[nAddrListIndex];
        break;
    }
    if (NULL == pszIP) {
        printfln("get host info failed");
        return -1;
    }
    char szIP[20];
    const char* pszConnectIP = NULL;
    if (NULL == inet_ntop(pHostEnt->h_addrtype, (PVOID)pszIP, szIP, sizeof(szIP))) {
        // Convert failed, maybe already an ip address
        pszConnectIP = hostInfo.szHost;
    } else {
        pszConnectIP = szIP;
    }

    // Get fd from keep-alive pool first
    int nFD = GetKeepAliveFD(&hostInfo);
    if (0 == nFD) {
        unsigned int uConnectPort = hostInfo.uPort;
        if (0 == uConnectPort) {
            uConnectPort = 80;
        }
        nFD = SocketConnect(pszConnectIP, uConnectPort);
        if (nFD <= 0) {
            printfln("connect to host %s failed", hostInfo.szHost);
            return -1;
        }
    } else {
        printfln("using keep-alive socket");
    }

    // Do request
    int nRet = internalGet(nFD, &hostInfo, _pszPath, _pRsp);

    return nRet;
}

int HTTPClient::internalGet(int _nFD, const HostInfo* _pInfo, const char* _pszPath, Response* _pRsp) {
    int nRet = 0;
    char szHeader[10240];
    szHeader[0] = '\0';
    HTTPRequestHeader header(szHeader, sizeof(szHeader));

    _pRsp->m_bSuccess = false;
    // Write request header
    // Write method
    if ((nRet = header.Append("GET ", 4)) != 0) {
        return nRet;
    }
    // Write path
    if ((nRet = header.Append(_pszPath, strlen(_pszPath))) != 0) {
        return nRet;
    }
    // Write http protocol version
    if ((nRet = header.Append(s_szHTTPVersion[m_nHTTPVersion], strlen(s_szHTTPVersion[m_nHTTPVersion]))) != 0) {
        return nRet;
    }
    // Write CRLF
    if ((nRet = header.WriteCRLF()) != 0) {
        return nRet;
    }

    // Write request headers
    // Write host
    if ((nRet = header.WriteString("Host", _pInfo->szHost)) != 0) {
        return nRet;
    }
    // Write user-agent
    if ((nRet = header.WriteString("User-Agent", s_szUserAgentValue)) != 0) {
        return nRet;
    }
    // Write accept
    if ((nRet = header.WriteString("Accept", "text/html")) != 0) {
        return nRet;
    }
    // CRLF end with empty line
    if ((nRet = header.WriteCRLF()) != 0) {
        return nRet;
    }

    // End of string stream
    if ((nRet = header.TerminateString()) != 0) {
        return nRet;
    }

    // Send header
    nRet = send(_nFD, header.GetBuffer(), header.GetLength(), 0);
    if (nRet < 0) {
        return -1;
    }
    printfln("send %d bytes for request %s", nRet, _pszPath);

    // Read response
    if ((nRet = readResponse(_nFD, _pRsp)) != 0) {
        return nRet;
    }

    return 0;
}

int HTTPClient::readResponse(int _nFD, Response* _pRsp) {
    char szRecv[256];
    szRecv[0] = '\0';
    int nRet = 0;
    // First, read response header
    // Protocol version, status code , status message
    nRet = SocketReadLine(_nFD, szRecv, sizeof(szRecv));
    if (0 != nRet) {
        return -1;
    }
    // Do parse
    const char* pszHTTPVersion = NULL;
    for (int i = HTTPVersion1_1; i < HTTPVersionTotal; i++) {
        if (0 == hasPrefixCi(szRecv, s_szHTTPVersion[i])) {
            pszHTTPVersion = s_szHTTPVersion[i];
            _pRsp->m_nHTTPVersion = i;
            break;
        }
    }
    if (NULL == pszHTTPVersion) {
        printfln("can't find http version");
        return -1;
    }
    int nReadOffset = 0;
    int nStatusCode = 0;
    char szStatusCode[10];
    szStatusCode[0] = '\0';
    int nStatusCodeWi = -1;
    nReadOffset = strlen(pszHTTPVersion);
    const char* pszStatusMsgPtr = NULL;
    for (nReadOffset; nReadOffset < nRet; nReadOffset++) {
        if (isspace(szRecv[nReadOffset])) {
            // Not parsing status code, parse status code
            if (-1 == nStatusCodeWi) {
                nStatusCodeWi = 0;
                continue;
            } else {
                // Parsing status code done
                szStatusCode[nStatusCodeWi] = '\0';
                nStatusCodeWi = -2;
            }
            // Do next parsing until \r\n
            continue;
        }

        if (nStatusCodeWi >= 0) {
            // Parsing status code
            if (szRecv[nReadOffset] < '0' ||
                szRecv[nReadOffset] > '9') {
                    printfln("invalid status code, not number");
                    return -1;
            }
            szStatusCode[nStatusCodeWi++] = szRecv[nReadOffset];
        } else if (-2 == nStatusCodeWi) {
            // Find until meet \r\n
            if (szRecv[nReadOffset] == '\r') {
                break;
            }
            if (NULL == pszStatusMsgPtr) {

            }
        }
    }

    return 0;
}
