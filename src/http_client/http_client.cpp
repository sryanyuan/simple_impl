#define _CRT_SECURE_NO_WARNINGS
#include "http_client.h"
#include "http_request_header.h"
#include "../util/string_util.h"
#include "../util/socket_util.h"
#include <algorithm>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <io.h>
#pragma comment(lib, "ws2_32.lib")

static const char* s_szHTTPVersion[] = {
    "UNKNOWN",
    "HTTP/1.1",
    "HTTP/1.0",
    NULL
};

static const char* s_szUserAgentValue = "Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.87 Safari/537.36";

/*
HTTPClient::Response
*/
HTTPClient::Response::HeaderParserMap HTTPClient::Response::m_mapHeaderParser;

void HTTPClient::Response::InitializeHeaderParsers() {
    std::string xKey = "connection";
    m_mapHeaderParser.insert(std::make_pair(xKey, &HTTPClient::Response::ParseHeaderConnection));
    xKey = "content-type";
    m_mapHeaderParser.insert(std::make_pair(xKey, &HTTPClient::Response::ParseHeaderContentType));
}

int HTTPClient::Response::ParseHeaderRaw(const char* _pszLine) {
    char szHeaderKey[32];
    int nRet = copyUntil(_pszLine, szHeaderKey, sizeof(szHeaderKey), ':');
    if (0 != nRet) {
        return nRet;
    }
    const char* pszHeaderValue = _pszLine + strlen(szHeaderKey);
    const char* pszReadPtr = skipCharactors(pszHeaderValue, ':');
    if (pszHeaderValue == pszReadPtr) {
        return -1;
    }
    pszHeaderValue = skipCharactors(pszReadPtr, ' ');
    if (pszHeaderValue == pszReadPtr) {
        return -1;
    }
    std::string strValue = pszHeaderValue;
    if (strValue.size() < 2) {
        return -1;
    }
    if (strValue.at(strValue.size() - 1) != '\n' ||
        strValue.at(strValue.size() - 2) != '\r') {
            return -1;
    }
    strValue = strValue.substr(0, strValue.size() - 2);
    std::string xSearchKey = szHeaderKey;
    transform(xSearchKey.begin(), xSearchKey.end(), xSearchKey.begin(), tolower);

    m_xHeaderValue.insert(std::make_pair(xSearchKey, strValue));

    // External callback
    HeaderParserMap::const_iterator it = m_mapHeaderParser.find(xSearchKey);
    if (it == m_mapHeaderParser.end()) {
        return 0;
    }
    nRet = it->second(this, strValue.c_str());
    if (0 != nRet) {
        return nRet;
    }

    return 0;
}

int HTTPClient::Response::ParseHeaderConnection(Response* _pRsp, const char* _pszValue) {
    if (stricmp(_pszValue, "keep-alive")) {
        _pRsp->m_nHeaderConnection = ConnectionKeepAlive;
    } else if (stricmp(_pszValue, "close")) {
        _pRsp->m_nHeaderConnection = ConnectionClose;
    } else {
        _pRsp->m_nHeaderConnection = ConnectionNone;
    }

    return 0;
}

int HTTPClient::Response::ParseHeaderContentType(Response* _pRsp, const char* _pszValue) {
    _pRsp->m_strContentType = _pszValue;

    return 0;
}

void HTTPClient::Response::Print() {
    if (m_body.IsBad()) {
        printfln("**Bad");
        return;
    }
    printfln(m_bSuccess ? "**success" : "**failed");
    if (HTTPVersionNone == m_nHTTPVersion) {
        return;
    }
    printfln(s_szHTTPVersion[m_nHTTPVersion]);
    printfln("**Status code %d, message %s", m_nStatusCode, m_strStatusMsg.c_str());
    printfln("**Headers");
    for (Response::HeaderKeyValue::const_iterator it = m_xHeaderValue.begin();
        it != m_xHeaderValue.end();
        ++it) {
            printfln("**%s: %s", it->first.c_str(), it->second.c_str());
    }
    printfln("**Body length %d", m_body.GetLength());
    if (m_body.GetLength() > 0) {
        puts(m_body.GetBuffer());
        printf("\r\n");
    }
}

/*
HTTPClient
*/
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
    bool bFDFromCache = false;
    unsigned int uConnectPort = hostInfo.uPort;
    if (0 == uConnectPort) {
        uConnectPort = 80;
    }
    if (0 == nFD) {
        nFD = SocketConnect(pszConnectIP, uConnectPort);
        if (nFD <= 0) {
            printfln("connect to host %s failed", hostInfo.szHost);
            return -1;
        }
    } else {
        printfln("using keep-alive socket");
        bFDFromCache = true;
    }

    // Do request
    int nRet = 0;
    for (;;) {
        nRet = internalGet(nFD, &hostInfo, _pszPath, _pRsp);
        if (nRet != 0) {
            // Close socket
            closesocket(nFD);
            nFD = INVALID_SOCKET;
            // If the fd is keep-alive, clear it from cache
            if (bFDFromCache) {
                DelKeepAliveFD(&hostInfo);

                if (1 == nRet) {
                    // Connection error, create socket again
                    nFD = SocketConnect(pszConnectIP, uConnectPort);
                    if (nFD <= 0) {
                        printfln("connect to host %s failed", hostInfo.szHost);
                        return -1;
                    }
                    bFDFromCache = false;

                    printfln("keep-alive socket reset, create new connection");
                    continue;
                }
            }

            break;
        } else {
            // If connection is keep-alive, cache
            bool bCached = false;
            Response::HeaderKeyValue::const_iterator it = _pRsp->m_xHeaderValue.find("connection");
            if (it != _pRsp->m_xHeaderValue.end()) {
                if (it->second == "keep-alive" &&
                    !bFDFromCache) {
                    bCached = true;
                    SetKeepAliveFD(&hostInfo, nFD);
                }
            }
            if (!bCached &&
                !bFDFromCache) {
                closesocket(nFD);
                nFD = INVALID_SOCKET;
            }

            break;
        }
    }
    
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
    const char* pszPath = _pszPath;
    if (NULL == pszPath ||
        0 == strlen(pszPath)) {
            pszPath = "/";
    }
    if ((nRet = header.Append(pszPath, strlen(pszPath))) != 0) {
        return nRet;
    }
    if ((nRet = header.Append(" ", 1)) != 0) {
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
    if (nRet == 0) {
        // Socket disconnected, if using keep-alive socket, check the socket has closed by server
        return 1;
    }
    printfln("send %d bytes for request %s, header content:\r\n%s", nRet, pszPath, header.GetBuffer());

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
    if (nRet <= 0) {
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

    const char* pReadPtr = szRecv + strlen(pszHTTPVersion);
    const char* pStatusCodePtr = skipCharactors(pReadPtr, ' ');
    if (pReadPtr == pStatusCodePtr) {
        printfln("get status code failed");
        return -1;
    }
    char szStatusCode[10];
    if ((nRet = copyUntil(pStatusCodePtr, szStatusCode, sizeof(szStatusCode), ' ')) != 0) {
        return nRet;
    }
    int nStatusCode = atoi(szStatusCode);
    if (0 == nStatusCode) {
        printfln("read status code failed");
        return -1;
    }
    _pRsp->m_nStatusCode = nStatusCode;

    pReadPtr = pStatusCodePtr + strlen(szStatusCode);
    const char* pStatusMsgPtr = skipCharactors(pReadPtr, ' ');
    if (pReadPtr == pStatusMsgPtr) {
        printfln("get status msg failed");
        return -1;
    }
    char szStatusMsg[128];
    if ((nRet = copyUntil(pStatusMsgPtr, szStatusMsg, sizeof(szStatusMsg), '\r')) != 0) {
        printfln("read status msg failed");
        return nRet;
    }
    _pRsp->m_strStatusMsg = szStatusMsg;

    // Read header fields
    for (;;) {
        nRet = SocketReadLine(_nFD, szRecv, sizeof(szRecv));
        if (nRet <= 0) {
            return -1;
        }

        if (2 == nRet) {
            // Header terminate
            break;
        }
        if ((nRet = _pRsp->ParseHeaderRaw(szRecv)) != 0) {
            return nRet;
        }
    }

    // Parse body
    int nContentLength = 0;
    Response::HeaderKeyValue::const_iterator it = _pRsp->m_xHeaderValue.find("content-length");
    if (it != _pRsp->m_xHeaderValue.end()) {
        nContentLength = atoi(it->second.c_str());
    }
    if (0 != nContentLength) {
        _pRsp->m_body.Reset(nContentLength + 1);
        nRet = _pRsp->m_body.ReadFromFD(_nFD, size_t(nContentLength));
        if (nRet != 0) {
            return nRet;
        }
    }
    // Parse chunk
    int nChunkSize = -1;
    it = _pRsp->m_xHeaderValue.find("transfer-encoding");
    if (it != _pRsp->m_xHeaderValue.end()) {
        if (it->second == "chunked") {
            nChunkSize = 0;
        }
    }
    if (nChunkSize >= 0) {
        for (;;) {
            // Read chunk length
            if ((nRet = SocketReadLine(_nFD, szRecv, sizeof(szRecv))) <= 0) {
                return nRet;
            }
            if (nRet < 2) {
                return -1;
            }

            char szHex[16];
            if ((nRet = copyUntil(szRecv, szHex, sizeof(szHex), '\r')) != 0) {
                return nRet;
            }
            nRet = sscanf(szHex, "%x", &nChunkSize);
            if (1 != nRet) {
                return -1;
            }

            // Read chunk body
            if (0 == nChunkSize) {
                // Read trailer, read all line until empty line just CRLF
                // Here we just ignore the trailer part
                if ((nRet = SocketReadLine(_nFD, szRecv, sizeof(szRecv))) <= 0) {
                    return -1;
                }
                if (2 == nRet) {
                    break;
                }
            } else {
                if ((nRet = _pRsp->m_body.ReadFromFD(_nFD, size_t(nChunkSize))) != 0) {
                    return nRet;
                }
                // Skip CRLF
                if ((nRet = SocketReadLine(_nFD, szRecv, sizeof(szRecv))) != 2) {
                    return -1;
                }
            }
        }
    }

    _pRsp->m_bSuccess = true;
    _pRsp->m_body.TerminateString();

    return 0;
}
