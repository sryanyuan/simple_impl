#ifndef _INC_HTTP_REQUEST_HEADER_
#define _INC_HTTP_REQUEST_HEADER_

class HTTPRequestHeader {
public:
    explicit HTTPRequestHeader(char* _pBuffer, size_t _uSz);

public:
    // Write format as header: value\r\n
    int WriteString(const char* _pszHeader, const char* _pszValue);
    int WriteInt(const char* _pszHeader, int _nValue);
    // Write control characters
    int WriteCRLF();
    int TerminateString();
    // Write raw data
    int Append(const char* _pszValue, size_t _uSz);

    inline const char* GetBuffer() {return m_pBuffer;}
    inline size_t GetLength() {return m_uWi;}

private:
    char* m_pBuffer;
    size_t m_uSize;
    size_t m_uWi;
};

#endif
