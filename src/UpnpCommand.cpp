#include <variant>

#include "DLNAModule.h"
#include "UpnpCommand.h"
#include "base64.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "ixml.h"
#include "upnptools.h"

#include "logger.h"
#include "URLHandler.h"

template <typename T>
const std::string CreateResponse(const std::string& version, const std::string& method, rapidjson::Value& request, const T& result, int status)
    requires std::is_same_v<T, std::vector<Item>> || std::is_same_v<T, std::string> || std::is_same_v<T, std::nullptr_t>
{
    using namespace rapidjson;
    rapidjson::Document response(kObjectType);
    auto& allocator = response.GetAllocator();
    response.AddMember("version", Value().SetString(version.data(), version.length(), allocator), allocator);
    response.AddMember("method", Value().SetString(method.data(), method.length(), allocator), allocator);
    response.AddMember("request_body", request.Move(), allocator);

    if constexpr (std::is_same_v<T, std::vector<Item>>)
    {
        rapidjson::Value resultList(kArrayType);
        for (const Item& it : result)
            resultList.PushBack(Value().SetObject()
                .AddMember("objid", Value().SetString(it.objectID.data(), it.objectID.size(), allocator), allocator)
                .AddMember("filename", Value().SetString(it.filename.data(), it.filename.size(), allocator), allocator)
                .AddMember("url", Value().SetString(it.url.data(), it.url.size(), allocator), allocator)
                .AddMember("type", it.media_type, allocator)
                .AddMember("date", Value().SetString(it.date.data(), it.date.size(), allocator), allocator)
                .AddMember("duration", Value().SetString(it.duration.data(), it.duration.size(), allocator), allocator)
                .AddMember("size", Value().SetString(it.size.data(), it.size.size(), allocator), allocator)
                .AddMember("resolution", Value().SetString(it.resolution.data(), it.resolution.size(), allocator), allocator)
                .AddMember("subtitle", Value().SetString(it.subtitle.data(), it.subtitle.size(), allocator), allocator)
                .AddMember("audio", Value().SetString(it.audio_url.data(), it.audio_url.size(), allocator), allocator)
                .AddMember("genre", Value().SetString(it.genre.data(), it.genre.size(), allocator), allocator)
                .AddMember("album", Value().SetString(it.album.data(), it.album.size(), allocator), allocator)
                .AddMember("albumArtist", Value().SetString(it.album_artist.data(), it.album_artist.size(), allocator), allocator)
                .AddMember("albumArtURI", Value().SetString(it.albumArtURI.data(), it.albumArtURI.size(), allocator), allocator)
                .AddMember("originalTrackNumber", Value().SetString(it.orig_track_nb.data(), it.orig_track_nb.size(), allocator), allocator)
                , allocator);

        response.AddMember("results", resultList, allocator);
    }
    else if constexpr (std::is_same_v<T, std::string>) {
        const std::string& b64result = base64_encode(result, false);
        response.AddMember("results", Value().SetArray().PushBack(Value().SetString(b64result.c_str(), b64result.length(), allocator), allocator), allocator);
    }
    else if constexpr (std::is_same_v<T, std::nullptr_t>)
        response.AddMember("results", "", allocator);
    else
        static_assert(always_false<T>, "Unsupported type");

    response.AddMember("status", status, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    response.Accept(writer);

    return buffer.GetString();
}

std::variant<std::string, int> Resolve(IXML_Document* p_response)
{
    char* rawHTML = ixmlDocumenttoString(p_response);
    if (rawHTML == nullptr)
        return -3;

    std::string result = ConvertHTMLtoXML(rawHTML);
    ixmlFreeDOMString(rawHTML);

    IXML_Document* parseDoc = ixmlParseBuffer(result.data());
    if (parseDoc == nullptr)
    {
        Log(LogLevel::Error, "Parse result to XML format failed");
        return -4;
    }

    char* tmp = ixmlDocumenttoString(parseDoc);
    if (!tmp)
        return -5;

    result = tmp;
    ixmlFreeDOMString(tmp);

    if (result.empty())
    {
        Log(LogLevel::Error, "Browse failed");
        return -6;
    }
    return result;
}

std::variant<std::vector<Item>, int> Resolve2(IXML_Document* p_response)
{
    IXML_Document* p_result = parseBrowseResult(p_response);
    if (!p_result)
    {
        Log(LogLevel::Error, "browse() response parsing failed");
        return -1;
    }

    std::vector<Item> itemVector;
    IXML_NodeList* containerNodeList = ixmlDocument_getElementsByTagName(p_result, "container");
    if (containerNodeList)
    {
        for (unsigned int i = 0; i < ixmlNodeList_length(containerNodeList); i++)
        {
            auto itemElement = (IXML_Element*)ixmlNodeList_item(containerNodeList, i);
            auto&& opt = TryParseItem(itemElement, true);
            if (opt)
                itemVector.push_back(std::move(opt.value()));
        }
        ixmlNodeList_free(containerNodeList);
    }

    IXML_NodeList* itemNodeList = ixmlDocument_getElementsByTagName(p_result, "item");
    if (itemNodeList)
    {
        for (unsigned int i = 0; i < ixmlNodeList_length(itemNodeList); i++)
        {
            auto itemElement = (IXML_Element*)ixmlNodeList_item(itemNodeList, i);
            auto&& opt = TryParseItem(itemElement, false);
            if (opt)
                itemVector.push_back(std::move(opt.value()));
        }
        ixmlNodeList_free(itemNodeList);
    }
    ixmlDocument_free(p_result);
    return itemVector;
}

static int UpnpSendActionCallBack(Upnp_EventType eventType, const void* p_event, void* p_cookie)
{
    if (eventType != UPNP_CONTROL_ACTION_COMPLETE)
        return -1;

    CHECK_VARIABLE(p_cookie, "%p");
    auto& cookie = *static_cast<Cookie*>(p_cookie);

#if __clang__ // lambda can capture struct-binding since C++20, supported by MSVC and GCC but not Clang(<=15.0)
    rapidjson::Document& request = std::get<0>(cookie);
    BrowseDLNAFolderCallback OnBrowseResultCallback = std::get<BrowseDLNAFolderCallback>(cookie);
#else
    auto& [request, OnBrowseResultCallback] = cookie;
#endif

    IXML_Document* p_response = UpnpActionComplete_get_ActionResult((UpnpActionComplete*)p_event);
    if (!p_response)
    {
        Log(LogLevel::Error, "No response from browse() action");
        return -1;
    }
    Log(LogLevel::Debug, "%s", ixmlPrintDocument(p_response));

    std::string response;
    if (strcmp(request["version"].GetString(), "1.0") == 0)
    {
        std::visit([&](auto&& var) {
            using T = std::decay_t<decltype(var)>;
        if constexpr (std::is_same_v<T, std::string>)
        {
            response = CreateResponse("1.0", "DLNABrowseResponse", request, var, 0);
        }
        else if constexpr (std::is_same_v<T, int>)
            response = CreateResponse("1.0", "DLNABrowseResponse", request, nullptr, var);
        else static_assert(false);
            }, Resolve(p_response));
    }
    else if (strcmp(request["version"].GetString(), "2.0") == 0)
    {
        std::visit([&](auto&& var) {
            using T = std::decay_t<decltype(var)>;
        if constexpr (std::is_same_v<T, std::vector<Item>>)
            response = CreateResponse("2.0", "DLNABrowseResponse", request, var, 0);
        else if constexpr (std::is_same_v<T, int>)
            response = CreateResponse("2.0", "DLNABrowseResponse", request, nullptr, var);
        else static_assert(always_false<T>, "Unsupported type");
            }, Resolve2(p_response));
    }

    ixmlDocument_free(p_response);
    delete (&cookie);

    if (OnBrowseResultCallback)
    {
        OnBrowseResultCallback(response.data());
    }
    return 0;
}

int BrowseAction(const char* objectID,
    const char* flag,
    const char* filter,
    const char* startingIndex,
    const char* requestCount,
    const char* sortCriteria,
    const char* controlUrl,
    Cookie* p_cookie)
{
    extern const char* CONTENT_DIRECTORY_SERVICE_TYPE;
    IXML_Document* actionDoc = nullptr;
    IXML_Document* browseResultXMLDocument = nullptr;

    int res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "ObjectID", objectID);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "BrowseFlag", flag);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "Filter", filter);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "StartingIndex", startingIndex);
    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "RequestedCount", requestCount);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    res = UpnpAddToAction(&actionDoc, "Browse",
        CONTENT_DIRECTORY_SERVICE_TYPE, "SortCriteria", sortCriteria);

    if (res != UPNP_E_SUCCESS)
    {
        goto browseActionCleanup;
    }

    CHECK_VARIABLE(p_cookie, "%p");
    res = UpnpSendActionAsync(DLNAModule::GetInstance().handle,
        controlUrl,
        CONTENT_DIRECTORY_SERVICE_TYPE,
        nullptr, /* ignored in SDK, must be NULL */
        actionDoc,
        UpnpSendActionCallBack,
        p_cookie);

    if (res != UPNP_E_SUCCESS)
    {
        Log(LogLevel::Error, "UpnpSendActionAsync return %s", UpnpGetErrorMessage(res));
        goto browseActionCleanup;
    }

browseActionCleanup:
    ixmlDocument_free(actionDoc);
    return res;
}

#if _WIN32
int vasprintf(char** strp, const char* format, va_list ap)
{
    int len = _vscprintf(format, ap);
    if (len == -1)
        return -1;
    char* str = (char*)malloc((size_t)len + 1);
    if (!str)
        return -1;
    int retval = vsnprintf(str, len + 1, format, ap);
    if (retval == -1) {
        free(str);
        return -1;
    }
    *strp = str;
    return retval;
}

int asprintf(char** strp, const char* fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vasprintf(strp, fmt, ap);
    va_end(ap);
    return ret;
}
#endif

/*
 * Extracts the result document from a SOAP response
 */
IXML_Document* parseBrowseResult(IXML_Document* p_doc)
{
    assert(p_doc);

    // ixml*_getElementsByTagName will ultimately only case the pointer to a Node
    // pointer, and pass it to a private function. Don't bother have a IXML_Document
    // version of getChildElementValue
    const char* psz_raw_didl = ixmlElement_getFirstChildElementValue((IXML_Element*)p_doc, "Result");

    if (!psz_raw_didl)
        return NULL;

    /* First, try parsing the buffer as is */
    IXML_Document* p_result_doc = ixmlParseBuffer(psz_raw_didl);
    if (!p_result_doc) {
        /* Missing namespaces confuse the ixml parser. This is a very ugly
         * hack but it is needeed until devices start sending valid XML.
         *
         * It works that way:
         *
         * The DIDL document is extracted from the Result tag, then wrapped into
         * a valid XML header and a new root tag which contains missing namespace
         * definitions so the ixml parser understands it.
         *
         * If you know of a better workaround, please oh please fix it */
        const char* psz_xml_result_fmt = "<?xml version=\"1.0\" ?>"
            "<Result xmlns:sec=\"urn:samsung:metadata:2009\">%s</Result>";

        char* psz_xml_result_string = NULL;
        if (-1 == asprintf(&psz_xml_result_string,
            psz_xml_result_fmt,
            psz_raw_didl))
            return NULL;

        p_result_doc = ixmlParseBuffer(psz_xml_result_string);
        free(psz_xml_result_string);
    }

    if (!p_result_doc)
        return NULL;

    IXML_NodeList* p_elems = ixmlDocument_getElementsByTagName(p_result_doc,
        "DIDL-Lite");

    IXML_Node* p_node = ixmlNodeList_item(p_elems, 0);
    ixmlNodeList_free(p_elems);

    return (IXML_Document*)p_node;
}

std::optional<Item> TryParseItem(IXML_Element* itemElement, bool AsDirectory)
{
    const char* objectID,
        * title,
        * psz_artist,
        * psz_genre,
        * psz_album,
        * psz_date,
        * psz_orig_track_nb,
        * psz_album_artist,
        * psz_albumArtURI;

    Item::MEDIA_TYPE media_type;
    objectID = ixmlElement_getAttribute(itemElement, "id");
    if (!objectID)
        return {};
    title = ixmlElement_getFirstChildElementValue(itemElement, "dc:title");
    if (!title)
        return {};
    const char* psz_subtitles = ixmlElement_getFirstChildElementValue(itemElement, "sec:CaptionInfo");
    if (!psz_subtitles &&
        !(psz_subtitles = ixmlElement_getFirstChildElementValue(itemElement, "sec:CaptionInfoEx")))
        psz_subtitles = ixmlElement_getFirstChildElementValue(itemElement, "pv:subtitlefile");
    psz_artist = ixmlElement_getFirstChildElementValue(itemElement, "upnp:artist");
    psz_genre = ixmlElement_getFirstChildElementValue(itemElement, "upnp:genre");
    psz_album = ixmlElement_getFirstChildElementValue(itemElement, "upnp:album");
    psz_date = ixmlElement_getFirstChildElementValue(itemElement, "dc:date");
    psz_orig_track_nb = ixmlElement_getFirstChildElementValue(itemElement, "upnp:originalTrackNumber");
    psz_album_artist = ixmlElement_getFirstChildElementValue(itemElement, "upnp:albumArtist");
    psz_albumArtURI = ixmlElement_getFirstChildElementValue(itemElement, "upnp:albumArtURI");
    const char* psz_media_type = ixmlElement_getFirstChildElementValue(itemElement, "upnp:class");
    if (strncmp(psz_media_type, "object.item.videoItem", 21) == 0)
        media_type = Item::VIDEO;
    else if (strncmp(psz_media_type, "object.item.audioItem", 21) == 0)
        media_type = Item::AUDIO;
    else if (strncmp(psz_media_type, "object.item.imageItem", 21) == 0)
        media_type = Item::IMAGE;
    else if (strncmp(psz_media_type, "object.container", 16) == 0)
        media_type = Item::CONTAINER;
    else
        return {};

    Item file;
    file.objectID = objectID ? objectID : "";
    file.filename = title ? title : "";
    file.media_type = media_type;
    file.artist = psz_artist ? psz_artist : "";
    file.genre = psz_genre ? psz_genre : "";
    file.album = psz_album ? psz_album : "";
    file.date = psz_date ? psz_date : "";
    file.orig_track_nb = psz_orig_track_nb ? psz_orig_track_nb : "";
    file.album_artist = psz_album_artist ? psz_album_artist : "";
    file.albumArtURI = psz_albumArtURI ? psz_albumArtURI : "";

    if (AsDirectory)
    {
        if (file.media_type != Item::CONTAINER)
            Log(LogLevel::Error, "Unexpected type in container enumeration");
        return file;
    }

    /* Try to extract all resources in DIDL */
    IXML_NodeList* p_resource_list = ixmlDocument_getElementsByTagName((IXML_Document*)itemElement, "res");
    if (!p_resource_list)
        return {};
    int list_lenght = ixmlNodeList_length(p_resource_list);
    if (list_lenght <= 0) {
        ixmlNodeList_free(p_resource_list);
        return {};
    }

    for (int index = 0; index < list_lenght; index++)
    {
        IXML_Element* p_resource = (IXML_Element*)ixmlNodeList_item(p_resource_list, index);
        const char* rez_type = ixmlElement_getAttribute(p_resource, "protocolInfo");

        if (strncmp(rez_type, "http-get:*:video/", 17) == 0 && media_type == Item::VIDEO)
        {
            const char* psz_resource_url = ixmlElement_getFirstChildElementValue(p_resource, "res");
            if (!psz_resource_url)
                return {};
            file.url = psz_resource_url;

            const char* psz_duration = ixmlElement_getAttribute(p_resource, "duration");
            file.duration = psz_duration ? psz_duration : "";

            const char* psz_size = ixmlElement_getAttribute(p_resource, "size");
            file.size = psz_size ? psz_size : "";

            const char* psz_resolution = ixmlElement_getAttribute(p_resource, "resolution");
            file.resolution = psz_resolution ? psz_resolution : "";

            const char* psz_subtitle = ixmlElement_getAttribute(p_resource, "pv:subtitleFileUri");
            file.subtitle = psz_subtitle ? psz_subtitle : "";
        }
        else if (strncmp(rez_type, "http-get:*:image/", 17) == 0)
            switch (media_type)
            {
            case Item::IMAGE:
            {
                const char* psz_resource_url = ixmlElement_getFirstChildElementValue(p_resource, "res");
                if (!psz_resource_url)
                    return {};
                file.url = psz_resource_url;

                const char* psz_duration = ixmlElement_getAttribute(p_resource, "duration");
                file.duration = psz_duration ? psz_duration : "";
            }
            break;
            case Item::VIDEO:
            case Item::AUDIO:
            {
                psz_albumArtURI = ixmlElement_getFirstChildElementValue(p_resource, "res");
                file.albumArtURI = psz_albumArtURI ? psz_albumArtURI : "";
            }
            break;
            case Item::CONTAINER:
                Log(LogLevel::Warning, "Unexpected object.container in item enumeration");
                continue;
            }
        else if (strncmp(rez_type, "http-get:*:text/", 16) == 0)
            const char* psz_text_url = ixmlElement_getFirstChildElementValue(p_resource, "res");
        else if (strncmp(rez_type, "http-get:*:audio/", 17) == 0)
        {
            if (media_type == Item::AUDIO)
            {
                const char* psz_resource_url = ixmlElement_getFirstChildElementValue(p_resource, "res");
                if (!psz_resource_url)
                    return {};
                file.url = psz_resource_url;

                const char* psz_duration = ixmlElement_getAttribute(p_resource, "duration");
                file.duration = psz_duration ? psz_duration : "";
            }
            else
            {
                const char* psz_audio_url = ixmlElement_getFirstChildElementValue(p_resource, "res");
                file.audio_url = psz_audio_url ? psz_audio_url : "";
            }
        }
    }
    ixmlNodeList_free(p_resource_list);
    return file;
}

bool BrowseFolderByUnity(const char* json, BrowseDLNAFolderCallback OnBrowseResultCallback)
{
    if (!json || !OnBrowseResultCallback)
        return false;

    using namespace rapidjson;
    rapidjson::Document request, arguments;
    request.Parse(json);
    if (int errorCode = request.GetParseError())
    {
        Log(LogLevel::Error, "GetParseError£º%d", errorCode);
        return false;
    }

    if (!request.HasMember("arguments"))
    {
        Log(LogLevel::Error, "No arguments parsed");
        return false;
    }

    arguments.CopyFrom(request["arguments"], request.GetAllocator());
    arguments.ParseInsitu(const_cast<char*>(request["arguments"].GetString()));

    auto uuid = arguments["uuid"].GetString();
    auto objid = arguments["objid"].GetString();
    if (!uuid || !objid)
    {
        Log(LogLevel::Error, "Broken arguments in browse request");
        return false;
    }

    auto&& server = [](const std::string& uuid)->std::optional<UpnpDevice>
    {
        std::lock_guard<std::mutex> lock(DLNAModule::GetInstance().UpnpDeviceMapMutex);
        auto it = DLNAModule::GetInstance().UpnpDeviceMap.find(uuid);
        if (it != DLNAModule::GetInstance().UpnpDeviceMap.end())
            return it->second;
        return {};
    }(uuid);

    Log(LogLevel::Info, "BrowseRequest: ObjID=%s, name=%s, location=%s", objid, server->friendlyName.c_str(), server->location.c_str());
    return BrowseAction(objid, "BrowseDirectChildren", "*", "0", "10000", "", server->location.data(), new Cookie(std::move(request), OnBrowseResultCallback)) == 0;
}