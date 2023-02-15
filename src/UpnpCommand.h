#pragma once
#include <string>
#include <optional>
#include <variant>

#include "ixml.h"
#include "upnp.h"
#include "rapidjson/document.h"

struct Item
{
    enum MEDIA_TYPE
    {
        VIDEO = 0,
        AUDIO,
        IMAGE,
        CONTAINER
    }media_type;

    std::string objectID,
        filename,
        url,
        duration,
        date,
        size,
        resolution,
        subtitle,
        audio_url,
        artist,
        genre,
        album,
        orig_track_nb,
        album_artist,
        albumArtURI;

    //Item() {}
    //Item(Item&& other) noexcept
    //    :objectID(std::move(other.objectID))
    //    , title(std::move(title))
    //    , psz_resource_url(std::move(other.psz_resource_url))
    //    , media_type(std::exchange(other.media_type, 0))
    //    , psz_artist(std::move(other.psz_artist))
    //    , psz_genre(std::move(other.psz_genre))
    //    , psz_album(std::move(other.psz_album))
    //    , psz_date(std::move(other.psz_date))
    //    , psz_orig_track_nb(std::move(other.psz_orig_track_nb))
    //    , psz_album_artist(std::move(other.psz_album_artist))
    //    , psz_albumArt(std::move(other.psz_albumArt))
    //{}
};

using BrowseDLNAFolderCallback = std::add_pointer<void(const char*)>::type;
using Cookie = std::tuple<rapidjson::Document, BrowseDLNAFolderCallback>;

int BrowseAction(const char* objectID, const char* flag, const char* filter, const char* startingIndex, const char* requestCount, const char* sortCriteria, const char* controlUrl, Cookie* p_cookie);
std::variant<std::vector<Item>, int> Resolve2(IXML_Document * p_response);
static int UpnpSendActionCallBack(Upnp_EventType eventType, const void* p_event, void* p_cookie);
bool BrowseFolderByUnity(const char* json, BrowseDLNAFolderCallback OnBrowseResultCallback);
std::optional<Item> TryParseItem(IXML_Element* itemElement, bool AsDirectory);
IXML_Document* parseBrowseResult(IXML_Document* p_doc);

// Make a way to use static_assert(false) while this template is specialized.  Cf. https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2593r0.html
template <typename...> inline constexpr bool always_false = false;