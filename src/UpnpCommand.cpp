#include <variant>

#include "DLNAModule.h"
#include "UpnpCommand.h"
#include "base64.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "ixml.h"
#include "upnptools.h"
#include "config.h"

#include "logger.h"
#include "URLHandler.h"

template <typename T>
const std::string CreateResponse(const std::string& version, const std::string& method, const std::string& request, const T& result, int status)
    requires std::is_same_v<T, std::vector<Item>> || std::is_same_v<T, std::string> || std::is_same_v<T, std::nullptr_t>
{
    using namespace rapidjson;
    rapidjson::Document response(kObjectType);
    auto& allocator = response.GetAllocator();
    response.AddMember("version", Value().SetString(version.data(), version.length(), allocator), allocator);
    response.AddMember("method", Value().SetString(method.data(), method.length(), allocator), allocator);
    response.AddMember("request_body", Value().SetString(request.data(), request.length(), allocator), allocator);

    if constexpr (std::is_same_v<T, std::vector<Item>>)
    {
        rapidjson::Value resultList(kArrayType);
        for (auto& it : result)
            resultList.PushBack(Value().SetObject()
                .AddMember("objid", Value().SetString(it.objectID.data(), it.objectID.size(), allocator), allocator)
                .AddMember("filename", Value().SetString(it.title.data(), it.title.size(), allocator), allocator)
                .AddMember("url", Value().SetString(it.psz_resource_url.data(), it.psz_resource_url.size(), allocator), allocator)
                .AddMember("type", it.media_type, allocator)
                .AddMember("date", Value().SetString(it.psz_date.data(), it.psz_date.size(), allocator), allocator)
                .AddMember("duration", Value().SetString(it.psz_duration.data(), it.psz_duration.size(), allocator), allocator)
                .AddMember("subtitle", Value().SetString(it.psz_subtitle.data(), it.psz_subtitle.size(), allocator), allocator)
                .AddMember("audio", Value().SetString(it.psz_audio_url.data(), it.psz_audio_url.size(), allocator), allocator)
                .AddMember("genre", Value().SetString(it.psz_genre.data(), it.psz_genre.size(), allocator), allocator)
                .AddMember("album", Value().SetString(it.psz_album.data(), it.psz_album.size(), allocator), allocator)
                .AddMember("albumArtist", Value().SetString(it.psz_album_artist.data(), it.psz_album_artist.size(), allocator), allocator)
                .AddMember("albumArtURI", Value().SetString(it.psz_albumArt.data(), it.psz_albumArt.size(), allocator), allocator)
                .AddMember("originalTrackNumber", Value().SetString(it.psz_orig_track_nb.data(), it.psz_orig_track_nb.size(), allocator), allocator)
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
        []<bool flag = false>()
    {
        static_assert(flag, "Unsupported type");
    }();

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
    auto [req_json, OnBrowseResultCallback] = cookie;
    delete (&cookie);

    IXML_Document* p_response = UpnpActionComplete_get_ActionResult((UpnpActionComplete*)p_event);
    if (!p_response)
    {
        Log(LogLevel::Error, "No response from browse() action");
        return -1;
    }

    std::string response1;
    std::visit([&](auto&& var) {
        using T = std::decay_t<decltype(var)>;
    if constexpr (std::is_same_v<T, std::string>)
    {
        response1 = CreateResponse("1.0", "DLNABrowseResponse", req_json, var, 0);
    }
    else if constexpr (std::is_same_v<T, int>)
        response1 = CreateResponse("1.0", "DLNABrowseResponse", req_json, nullptr, var);
    else static_assert(false);
        }, Resolve(p_response));

    std::string response2;
    std::visit([&](auto&& var) {
        using T = std::decay_t<decltype(var)>;
    if constexpr (std::is_same_v<T, std::vector<Item>>)
        response2 = CreateResponse("2.0", "DLNABrowseResponse", req_json, var, 0);
    else if constexpr (std::is_same_v<T, int>)
        response2 = CreateResponse("2.0", "DLNABrowseResponse", req_json, nullptr, var);
    else static_assert(false);
        }, Resolve2(p_response));
    //std::string StartingIndex = "0";
    //std::string RequestedCount = "5000";
    //const char* psz_TotalMatches = "0";
    //const char* psz_NumberReturned = "0";
    //long  l_reqCount = 0;
    //do {
    //    psz_TotalMatches = ixmlElement_getFirstChildElementValue((IXML_Element*)p_response, "TotalMatches");
    //    psz_NumberReturned = ixmlElement_getFirstChildElementValue((IXML_Element*)p_response, "NumberReturned");

    //    StartingIndex = std::to_string(std::stol(psz_NumberReturned) + std::stol(StartingIndex));
    //    l_reqCount = std::stol(psz_TotalMatches) - std::stol(StartingIndex);
    //    RequestedCount = std::to_string(l_reqCount);
    //} while (l_reqCount);

    CHECK_VARIABLE(response2, "%s");
    ixmlDocument_free(p_response);

    if (OnBrowseResultCallback)
    {
        OnBrowseResultCallback(response1.data());
        OnBrowseResultCallback(response2.data());
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
        * psz_albumArt;

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
    psz_albumArt = ixmlElement_getFirstChildElementValue(itemElement, "upnp:albumArtURI");
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
    file.title = title ? title : "";
    file.media_type = media_type;
    file.psz_artist = psz_artist ? psz_artist : "";
    file.psz_genre = psz_genre ? psz_genre : "";
    file.psz_album = psz_album ? psz_album : "";
    file.psz_date = psz_date ? psz_date : "";
    file.psz_orig_track_nb = psz_orig_track_nb ? psz_orig_track_nb : "";
    file.psz_album_artist = psz_album_artist ? psz_album_artist : "";
    file.psz_albumArt = psz_albumArt ? psz_albumArt : "";

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
            file.psz_resource_url = psz_resource_url;

            const char* psz_duration = ixmlElement_getAttribute(p_resource, "duration");
            file.psz_duration = psz_duration ? psz_duration : "";

            const char* psz_subtitle = ixmlElement_getAttribute(p_resource, "pv:subtitleFileUri");
            file.psz_subtitle = psz_subtitle ? psz_subtitle : "";
        }
        else if (strncmp(rez_type, "http-get:*:image/", 17) == 0)
            switch (media_type)
            {
            case Item::IMAGE:
            {
                const char* psz_resource_url = ixmlElement_getFirstChildElementValue(p_resource, "res");
                if (!psz_resource_url)
                    return {};
                file.psz_resource_url = psz_resource_url;

                const char* psz_duration = ixmlElement_getAttribute(p_resource, "duration");
                file.psz_duration = psz_duration ? psz_duration : "";
            }
            break;
            case Item::VIDEO:
            case Item::AUDIO:
            {
                psz_albumArt = ixmlElement_getFirstChildElementValue(p_resource, "res");
                file.psz_albumArt = psz_albumArt ? psz_albumArt : "";
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
                file.psz_resource_url = psz_resource_url;

                const char* psz_duration = ixmlElement_getAttribute(p_resource, "duration");
                file.psz_duration = psz_duration ? psz_duration : "";
            }
            else
            {
                const char* psz_audio_url = ixmlElement_getFirstChildElementValue(p_resource, "res");
                file.psz_duration = psz_audio_url ? psz_audio_url : "";
            }
        }
    }
    ixmlNodeList_free(p_resource_list);
    return file;
}

//std::variant<rapidjson::Value, int> Browse(const std::string& uuid, const std::string& objid, auto& allocator)
//{
//    std::string location;
//    {
//        std::lock_guard<std::mutex> lock(DLNAModule::GetInstance().UpnpDeviceMapMutex);
//        auto it = DLNAModule::GetInstance().UpnpDeviceMap.find(uuid);
//        if (it != DLNAModule::GetInstance().UpnpDeviceMap.end())
//        {
//            location = it->second.location;
//            Log(LogLevel::Info, "BrowseRequest: ObjID=%s, name=%s, location=%s", objid.c_str(), it->second.friendlyName.c_str(), it->second.location.c_str());
//
//        }
//    }
//    std::string StartingIndex = "0";
//    std::string RequestedCount = "5000";
//    const char* psz_TotalMatches = "0";
//    const char* psz_NumberReturned = "0";
//    long  l_reqCount = 0;
//
//
//
//    do
//    {
//        IXML_Document* p_response = BrowseAction(objid.c_str(),
//            "BrowseDirectChildren",
//            "*",
//            StartingIndex.c_str(),
//            // Some servers don't understand "0" as "no-limit"
//            RequestedCount.c_str(), /* RequestedCount */
//            "", /* SortCriteria */
//            location.c_str()
//        );
//
//    } while (l_reqCount);
//    return resultList;
//}

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
    return BrowseAction(objid, "BrowseDirectChildren", "*", "0", "10000", "", server->location.data(), new Cookie(json, OnBrowseResultCallback)) == 0;
}