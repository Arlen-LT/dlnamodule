#pragma once
#include <string>

#include "ixml.h"

struct URLInfo
{
    char* protocol = nullptr;
    char* username = nullptr;
    char* password = nullptr;
    char* host = nullptr;
    unsigned int port = 0;
    char* path = nullptr;
    char* option = nullptr;
    char* buffer = nullptr; /* to be freed */
};

std::string ReplaceAll(const char* src, int srcLen, const char* old_value, const char* new_value);
std::string ConvertHTMLtoXML(const char* src);
std::string GetIconURL(IXML_Element* device, const char* baseURL);
char* iri2uri(const char* iri);
char* DecodeUri(char* str);
bool IsUriValidate(const char* str, const char* extras);
int ParseUrl(URLInfo* url, const char* str);