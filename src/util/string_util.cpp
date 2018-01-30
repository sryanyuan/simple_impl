#define _CRT_SECURE_NO_WARNINGS
#include "string_util.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

void printfln(const char* fmt, ...) {
    const int nBufferSize = 512;
    char szOutputBuf[nBufferSize];
    szOutputBuf[0] = '\0';

    va_list ap;
    va_start(ap, fmt);
    // Keep the last 2 buffer is writable to write \n and \0
    int nWrite = vsnprintf(szOutputBuf, sizeof(szOutputBuf) - 2, fmt, ap);
    if (nWrite > 0) {
        szOutputBuf[nWrite] = '\n';
        szOutputBuf[nWrite + 1] = '\0';
    }
    va_end(ap);

    printf(szOutputBuf);
}

int hasPrefixCi(const char* _pszSrc, const char* _pszPrefix) {
    int nSrcLen = strlen(_pszSrc);
    int nPfxLen = strlen(_pszPrefix);

    if (nPfxLen > nSrcLen) {
        return -1;
    }

    for (int i = 0; i < nPfxLen; i++) {
        if (tolower(_pszSrc[i]) != tolower(_pszPrefix[i])) {
            return -1;
        }
    }

    return 0;
}

int parseHost(const char* _pszHost, HostInfo* _pHostInfo) {
    int nSchema = 0;
    const char* pszSchemaHTTP = "http://";
    const char* pszSchemaHTTPS = "https://";

    if (0 == hasPrefixCi(_pszHost, pszSchemaHTTP)) {
        nSchema = 1;
        strcpy(_pHostInfo->szSchema, pszSchemaHTTP);
    } else if (0 == hasPrefixCi(_pszHost, pszSchemaHTTPS)) {
        nSchema = 2;
        strcpy(_pHostInfo->szSchema, pszSchemaHTTPS);
    }
    if (0 == nSchema) {
        printfln("invalid schema");
        return -1;
    }

    int nHostWi = 0;
    char szPort[6] = {0};
    const char* pszHostPart = _pszHost + (nSchema == 1 ? strlen(pszSchemaHTTP) : strlen(pszSchemaHTTPS));
    // Search host info
    int nHostLen = strlen(pszHostPart);
    int nPortWi = -1;
    for (int i = 0; i < nHostLen; i++) {
        if (pszHostPart[i] == ':') {
            // Found port part
            nPortWi = 0;
            _pHostInfo->szHost[nHostWi++] = '\0';
            continue;
        }
        if (nPortWi >= 0) {
            if (pszHostPart[i] < '0' ||
                pszHostPart[i] > '9') {
                    printfln("get invalid port value %c", pszHostPart[i]);
                    return -1;
            }
            if (nPortWi > sizeof(szPort) - 1) {
                printfln("port value out of range [1, 0xffff]");
                return -1;
            }
            szPort[nPortWi++] = pszHostPart[i];
        } else {
            if (nHostWi > sizeof(_pHostInfo->szHost) - 1) {
                printfln("host value out of range");
                return -1;
            }
            _pHostInfo->szHost[nHostWi++] = pszHostPart[i];
        }
    }
    // Validate port
    int nPort = 0;
    if (nPortWi >= 0) {
        nPort = atoi(szPort);
        if (nPort <= 0 ||
            nPort > 0xffff) {
                printfln("port value out of range");
                return -1;
        }
    } else {
        // Not found port
        _pHostInfo->szHost[nHostWi++] = '\0';
    }
    _pHostInfo->uPort = (unsigned short)nPort;

    return 0;
}

int copyUntil(const char* _pszSrc, char* _pDst, size_t _uDstSz, char _cUntil) {
    int nLastDstIndex = (int)_uDstSz - 2;
    if (nLastDstIndex < 0) {
        return -1;
    }

    size_t uSrcLen = strlen(_pszSrc);
    for (size_t i = 0; i < uSrcLen; i++) {
        if (i > size_t(nLastDstIndex)) {
            return -1;
        }
        if (_pszSrc[i] == _cUntil) {
            _pDst[i] = '\0';
            return 0;
        }
        _pDst[i] = _pszSrc[i];
    }

    // Until not found
    return -1;
}

const char* skipCharactors(const char* _pszSrc, char _cSkip) {
    size_t uSrcLen = strlen(_pszSrc);
    size_t uOffset = 0;

    for (size_t i = 0; i < uSrcLen; i++) {
        if (_pszSrc[i] == _cSkip) {
            uOffset++;
        } else {
            break;
        }
    }

    return _pszSrc + uOffset;
}