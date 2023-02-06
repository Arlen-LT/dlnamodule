#pragma once
#include <string>

#include "upnp.h"

struct item
{
    enum MEDIA_TYPE
    {
        VIDEO = 0,
        AUDIO,
        IMAGE,
        CONTAINER
    }media_type;

    std::string objectID,
        title,
        psz_artist,
        psz_genre,
        psz_album,
        psz_date,
        psz_orig_track_nb,
        psz_album_artist,
        psz_albumArt,
        psz_resource_url;

    item(const char* objectID,
        const char* title,
        const char* psz_artist,
        const char* psz_genre,
        const char* psz_album,
        const char* psz_date,
        const char* psz_orig_track_nb,
        const char* psz_album_artist,
        const char* psz_albumArt,
        const char* psz_resource_url) :
        objectID(objectID ? objectID : ""),
        title(title ? title : ""),
        psz_artist(psz_artist ? psz_artist : ""),
        psz_genre(psz_genre ? psz_genre : ""),
        psz_album(psz_album ? psz_album : ""),
        psz_date(psz_date ? psz_date : ""),
        psz_orig_track_nb(psz_orig_track_nb ? psz_orig_track_nb : ""),
        psz_album_artist(psz_album_artist ? psz_album_artist : ""),
        psz_albumArt(psz_albumArt ? psz_albumArt : ""),
        psz_resource_url(psz_resource_url ? psz_resource_url : "")
    {}
};

using BrowseDLNAFolderCallback = std::add_pointer<void(const char*)>::type;
using Cookie = std::tuple<std::string, BrowseDLNAFolderCallback>;

int BrowseAction(const char* objectID, const char* flag, const char* filter, const char* startingIndex, const char* requestCount, const char* sortCriteria, const char* controlUrl, Cookie* p_cookie);
static int UpnpSendActionCallBack(Upnp_EventType eventType, const void* p_event, void* p_cookie);
bool BrowseFolderByUnity(const char* json, BrowseDLNAFolderCallback OnBrowseResultCallback);
std::optional<item> TryParseItem(IXML_Element* itemElement, bool AsDirectory);
IXML_Document* parseBrowseResult(IXML_Document* p_doc);