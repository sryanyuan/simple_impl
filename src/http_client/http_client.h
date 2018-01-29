#ifndef _INC_HTTP_CLIENT_
#define _INC_HTTP_CLIENT_

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <map>
#include <string>

const int MinAllocateSize = 1024 * 4;

struct HostInfo;

class ResponseBody {
public:
    ResponseBody() {
        Reset(0);
    }
    ~ResponseBody() {
        Reset(0);
    }

public:
    void Reset(size_t _uSize) {
        if (NULL != _uSize) {
            delete [] m_pBuffer;
            m_pBuffer = NULL;
        }

        m_pBuffer = NULL;
        m_uLen = m_uWPtr = 0;
        m_bBad = false;

        if (0 == _uSize) {
            return;
        }
        m_pBuffer = new char[_uSize];
        if (NULL == m_pBuffer) {
            m_bBad = true;
            return;
        }
        m_uLen = _uSize;
    }

    int Append(const char* _pData, size_t _uSize) {
        if (m_bBad) {
            return -1;
        }
        size_t uAvailableSize = m_uLen - m_uWPtr;
        if (uAvailableSize < _uSize) {
            // Reallocate memory
            size_t uAllocSize = (m_uLen + _uSize) * 2;
            m_pBuffer = (char*)realloc(m_pBuffer, uAllocSize);
            if (NULL == m_pBuffer) {
                m_bBad = true;
                return -1;
            }
        }
        memcpy(m_pBuffer, _pData, _uSize);
        return 0;
    }

    inline bool IsBad()     {return m_bBad;}

private:
    char* m_pBuffer;
    size_t m_uLen;
    size_t m_uWPtr;
    bool m_bBad;
};

typedef std::map<std::string, int> KeepAliveSocketMap;

class HTTPClient {
public:
    enum {
        HTTPVersion1_1,
        HTTPVersion1_0,
        HTTPVersionTotal,
    };
    enum {
        ConnectionNone,
        ConnectionClose,
        ConnectionKeepAlive,
        ConnectionTotal,
    };

public:
    class Response {
    public:
        Response() {
            m_bSuccess = false;
            m_nHeaderConnection = ConnectionNone;
            m_nHTTPVersion = HTTPVersion1_1;
        }

    public:
        bool m_bSuccess;
        int m_nStatusCode;
        std::string m_strStatusMsg;
        int m_nHTTPVersion;
        int m_nHeaderConnection;
        ResponseBody m_body;
    };

public:
    HTTPClient();
    ~HTTPClient();

public:
    void SetHTTPVersion(int _nVersion) {
        if (_nVersion < 0 ||
            _nVersion >= HTTPVersionTotal) {
                return;
        }
        m_nHTTPVersion = _nVersion;
    }

    void SetHTTPHeaderConnection(int _nConnection) {
        if (_nConnection < 0 ||
            _nConnection >= ConnectionTotal) {
                return;
        }
        m_nHeaderConnection = _nConnection;
    }

public:
    int Get(const char* _pszHost, const char* _pszPath, Response* _pRsp);

private:
    int internalGet(int _nFD, const HostInfo* _pInfo, const char* _pszPath, Response* _pRsp);

    int readResponse(int _nFD, Response* _pRsp);

private:
    int GetKeepAliveFDKey(HostInfo* _pHostInfo, std::string& _refKey);
    int SetKeepAliveFD(HostInfo* _pHostInfo, int _nFD);
    int DelKeepAliveFD(HostInfo* _pHostInfo);
    int GetKeepAliveFD(HostInfo* _pHostInfo);

    int ReadHeaderValue(const char* _pRecv, int _nRecvSize, const char* _pszHeaderKey, char _pOutBuf[], int _nOutputLen);

private:
    int m_nHTTPVersion;
    int m_nHeaderConnection;
    KeepAliveSocketMap m_mapKeepAliveSockets;
};

#endif