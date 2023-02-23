#include <sstream>

#include "URLHandler.h"

std::string ReplaceAll(const char* src, int srcLen, const char* oldValue, const char* newValue)
{
    int lenSrc = srcLen;
    int lenSrcOldValue = strlen(oldValue);
    int lenSrcTarValue = strlen(newValue);
    int maxOldValueCount = lenSrc / lenSrcOldValue;
    const char** posAllOldValue = new const char* [maxOldValueCount];
    int findCount = 0;
    const char* pCur = strstr(src, oldValue);
    while (pCur)
    {
        posAllOldValue[findCount] = pCur;
        findCount++;
        pCur += strlen(oldValue);
        pCur = strstr(pCur, oldValue);
    }

    char* newChar = new char[srcLen + (lenSrcTarValue - lenSrcOldValue) * findCount + 1]; 
    const char* pSrcCur = src;
    char* pTarCur = newChar;
    for (int iIndex = 0; iIndex < findCount; ++iIndex)
    {
        int cpyLen = posAllOldValue[iIndex] - pSrcCur;
        memcpy(pTarCur, pSrcCur, cpyLen);
        pTarCur += cpyLen;
        memcpy(pTarCur, newValue, lenSrcTarValue);
        pSrcCur = posAllOldValue[iIndex] + lenSrcOldValue;
        pTarCur += lenSrcTarValue;
    }

    int cpyLen = src + srcLen - pSrcCur;
    memcpy(pTarCur, pSrcCur, cpyLen);
    pTarCur += cpyLen;

    newChar[srcLen + (lenSrcTarValue - lenSrcOldValue) * findCount] = '\0';
    std::string ret(newChar);
    delete[] newChar;
    delete[] posAllOldValue;

    return ret;
}

std::string ConvertHTMLtoXML(const char* src)
{
    std::string srcstr(src);

    int lengthBefore = 0;
    do
    {
        lengthBefore = srcstr.length();
        srcstr = ReplaceAll(srcstr.data(), srcstr.length(), "\xc3\x97", "x");
    } while (lengthBefore != srcstr.length());

    do
    {
        lengthBefore = srcstr.length();
        srcstr = ReplaceAll(srcstr.data(), srcstr.length(), "&amp;", "&");
    } while (lengthBefore != srcstr.length());

    do
    {
        lengthBefore = srcstr.length();
        srcstr = ReplaceAll(srcstr.data(), srcstr.length(), "&quot;", "\"");
    } while (lengthBefore != srcstr.length());

    do
    {
        lengthBefore = srcstr.length();
        srcstr = ReplaceAll(srcstr.data(), srcstr.length(), "&gt;", ">");
    } while (lengthBefore != srcstr.length());

    do
    {
        lengthBefore = srcstr.length();
        srcstr = ReplaceAll(srcstr.data(), srcstr.length(), "&lt;", "<");
    } while (lengthBefore != srcstr.length());

    do
    {
        lengthBefore = srcstr.length();
        srcstr = ReplaceAll(srcstr.data(), srcstr.length(), "&apos;", "'");
    } while (lengthBefore != srcstr.length());

    do
    {
        lengthBefore = srcstr.length();
        srcstr = ReplaceAll(srcstr.data(), srcstr.length(), "<unknown>", "unknown");
    } while (lengthBefore != srcstr.length());

    return srcstr;
}

std::string GetIconURL(IXML_Element* device, const char* baseURL)
{
    std::string res;
    URLInfo url;
    IXML_NodeList* iconLists = nullptr;
    IXML_Element* iconList = nullptr;

    if (ParseUrl(&url, baseURL) < 0)
        goto end;

    iconLists = ixmlElement_getElementsByTagName(device, "iconList");
    iconList = (IXML_Element*)ixmlNodeList_item(iconLists, 0);
    ixmlNodeList_free(iconLists);
    if (iconList != nullptr)
    {
        IXML_NodeList* icons = ixmlElement_getElementsByTagName(iconList, "icon");
        if (icons != nullptr)
        {
            unsigned int maxWidth = 0;
            unsigned int maxHeight = 0;
            for (unsigned int i = 0; i < ixmlNodeList_length(icons); ++i)
            {
                IXML_Element* icon = (IXML_Element*)ixmlNodeList_item(icons, i);
                const char* widthStr = ixmlElement_getFirstChildElementValue(icon, "width");
                const char* heightStr = ixmlElement_getFirstChildElementValue(icon, "height");
                if (widthStr == nullptr || heightStr == nullptr)
                    continue;
                unsigned int width = atoi(widthStr);
                unsigned int height = atoi(heightStr);
                if (width <= maxWidth || height <= maxHeight)
                    continue;
                const char* iconUrl = ixmlElement_getFirstChildElementValue(icon, "url");
                if (iconUrl == nullptr)
                    continue;
                maxWidth = width;
                maxHeight = height;
                res = iconUrl;
            }
            ixmlNodeList_free(icons);
        }
    }

    if (!res.empty())
    {
        std::ostringstream oss;
        oss << url.protocol << "://" << url.host << ":" << url.port << res;
        res = oss.str();
    }

end:
    if (url.host != nullptr)
        free(url.host);
    if (url.buffer != nullptr)
        free(url.buffer);
    return res;
}

char* iri2uri(const char* iri)
{
    const char urihex[] = "0123456789ABCDEF";
    size_t a = 0, u = 0;

    size_t i = 0;
    for (i = 0; iri[i] != '\0'; i++)
    {
        unsigned char c = iri[i];

        if (c < 128)
            a++;
        else
            u++;
    }

    if ((a + u) > (SIZE_MAX / 4))
    {
        errno = ENOMEM;
        return NULL;
    }

    char* uri = (char*)calloc(a + 3 * u + 1, sizeof(char));
    for (char* p = uri; *iri != '\0'; iri++)
    {
        unsigned char c = *iri;

        if (c < 128)
            *(p++) = c;
        else
        {
            *(p++) = '%';
            *(p++) = urihex[c >> 4];
            *(p++) = urihex[c & 0xf];
        }
    }
    return uri;
}

char* DecodeUri(char* str)
{
    char* in = str, * out = str;
    if (in == NULL)
        return NULL;

    char c;
    while ((c = *(in++)) != '\0')
    {
        if (c == '%')
        {
            char hex[3];

            if (!(hex[0] = *(in++)) || !(hex[1] = *(in++)))
                return NULL;
            hex[2] = '\0';
            *(out++) = strtoul(hex, NULL, 0x10);
        }
        else
            *(out++) = c;
    }
    *out = '\0';
    return str;
}

bool IsUriValidate(const char* str, const char* extras)
{
    if (!str)
        return false;

    for (size_t i = 0; str[i] != '\0'; i++)
    {
        unsigned char c = str[i];
        /* These are the _unreserved_ URI characters (RFC3986 ��2.3) */
        if (std::isalpha(c)
            || std::isdigit(c)
            || strchr("-._~!$&'()*+,;=", c) != NULL
            || strchr(extras, c) != NULL)
        {
            continue;
        }

        if (c == '%'
            && std::isxdigit(static_cast<unsigned char>(str[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(str[i + 2])))
        {
            i += 2;
            continue;
        }
        return false;
    }
    return true;
}

int ParseUrl(URLInfo* url, const char* str)
{
    if (str == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    int ret = 0;

    char* uri = iri2uri(str);
    if (uri == NULL)
        return -1;
    url->buffer = uri;

    /* URI scheme */
    char* cur = uri;
    char* next = uri;
    while ((*next >= 'A' && *next <= 'Z') || (*next >= 'a' && *next <= 'z')
        || (*next >= '0' && *next <= '9') || memchr("+-.", *next, 3) != NULL)
        next++;

    if (*next == ':')
    {
        *(next++) = '\0';
        url->protocol = cur;
        cur = next;
    }

    /* Fragment */
    next = strchr(cur, '#');
    if (next != NULL)
    {
#if 0  /* TODO */
        * (next++) = '\0';
        url->psz_fragment = next;
#else
        * next = '\0';
#endif
    }

    /* Query parameters */
    next = strchr(cur, '?');
    if (next != NULL)
    {
        *(next++) = '\0';
        url->option = next;
    }

    /* Authority */
    if (strncmp(cur, "//", 2) == 0)
    {
        cur += 2;

        /* Path */
        next = strchr(cur, '/');
        if (next != NULL)
        {
            *next = '\0'; /* temporary nul, reset to slash later */
            url->path = next;
        }
        /*else
            url->psz_path = "/";*/

            /* User name */
        next = strrchr(cur, '@');
        if (next != NULL)
        {
            *(next++) = '\0';
            url->username = cur;
            cur = next;

            /* Password (obsolete) */
            next = strchr(url->username, ':');
            if (next != NULL)
            {
                *(next++) = '\0';
                url->password = next;
                DecodeUri(url->password);
            }
            DecodeUri(url->username);
        }

        /* Host name */
        if (*cur == '[' && (next = strrchr(cur, ']')) != NULL)
        {   /* Try IPv6 numeral within brackets */
            *(next++) = '\0';
            url->host = strdup(cur + 1);

            if (*next == ':')
                next++;
            else
                next = NULL;
        }
        else
        {
            next = strchr(cur, ':');
            if (next != NULL)
                *(next++) = '\0';

            const char* host = DecodeUri(cur);
            for (const char* p = host; *p; p++)
            {
                if (((unsigned char)*p) >= 0x80)
                {
                    errno = ENOSYS;
                    ret = -1;
                    break;
                }
            }

            if (ret != -1)
                url->host = strdup(host);
        }

        if (url->host != NULL && !IsUriValidate(url->host, ":"))
        {
            free(url->host);
            url->host = NULL;
            errno = EINVAL;
            ret = -1;
        }

        /* Port number */
        if (next != NULL && *next)
        {
            char* end;
            unsigned long port = strtoul(next, &end, 10);

            if (strchr("0123456789", *next) == NULL || *end || port > UINT_MAX)
            {
                errno = EINVAL;
                ret = -1;
            }

            url->port = port;
        }

        if (url->path != NULL)
            *url->path = '/'; /* restore leading slash */
    }
    else
    {
        url->path = cur;
    }

    if (url->path != NULL && !IsUriValidate(url->path, "/@:"))
    {
        url->path = NULL;
        errno = EINVAL;
        ret = -1;
    }

    return ret;
}