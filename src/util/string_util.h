#ifndef _INC_UTIL_
#define _INC_UTIL_

struct HostInfo {
    char szSchema[10];
    char szHost[512];
    unsigned short uPort;

    HostInfo() {
        szSchema[0] = '\0';
        szHost[0] = '\0';
        uPort = 0;
    }
};
int parseHost(const char* _pszHost, HostInfo* _pHostInfo);

void printfln(const char* fmt, ...);
// Check if a string has prefix string using low case
int hasPrefixCi(const char* _pszSrc, const char* _pszPrefix);

int copyUntil(const char* _pszSrc, char* _pDst, size_t _uDstSz, char _cUntil);
const char* skipCharactors(const char* _pszSrc, char _cSkip);

#endif