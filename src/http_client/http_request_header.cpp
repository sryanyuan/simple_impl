#include "http_request_header.h"
#include <stdlib.h>
#include <string.h>

HTTPRequestHeader::HTTPRequestHeader(char* _pBuffer, size_t _uSz) {
    m_pBuffer = _pBuffer;
    m_uSize = _uSz;
    m_uWi = 0;
}

int HTTPRequestHeader::Append(const char* _pszValue, size_t _uSz) {
    if (NULL == m_pBuffer) {
        return -1;
    }
    size_t uAvailableSz = m_uSize - m_uWi;
    if (uAvailableSz < _uSz) {
        return -1;
    }

    memcpy(m_pBuffer + m_uWi, _pszValue, _uSz);
    m_uWi += _uSz;
    return 0;
}

int HTTPRequestHeader::WriteInt(const char* _pszHeader, int _nValue) {
    char szValue[16];
    szValue[0] = 0;
    itoa(_nValue, szValue, 10);

    return WriteString(_pszHeader, szValue);
}

int HTTPRequestHeader::TerminateString() {
    return Append("\0", 1);
}

int HTTPRequestHeader::WriteCRLF() {
    return Append("\r\n", 2);
}

int HTTPRequestHeader::WriteString(const char* _pszHeader, const char* _pszValue) {
    size_t uAvailableSz = m_uSize - m_uWi;
    size_t uKeyLen = strlen(_pszHeader);
    size_t uValueLen = strlen(_pszValue);
    const size_t uColonSize = 2;
    const size_t uCRLFSize = 2;

    if (uAvailableSz < uValueLen + uKeyLen + uColonSize + uCRLFSize) {
        return -1;
    }

    // Write header
    int nRet = 0;
    if ((nRet = Append(_pszHeader, uKeyLen)) != 0) {
        return nRet;
    }
    // Write colon
    if ((nRet = Append(": ", 2)) != 0) {
        return nRet;
    }
    // Write value
    if ((nRet = Append(_pszValue, uValueLen)) != 0) {
        return nRet;
    }
    // Write CRLF
    if ((nRet = Append("\r\n", 2)) != 0) {
        return nRet;
    }

    return 0;
}
