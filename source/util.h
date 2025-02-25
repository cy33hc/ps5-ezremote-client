#ifndef UTIL_H
#define UTIL_H

#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdarg.h>
#include <sys/time.h>
// #include "base64.h"
// #include "openssl/md5.h"
#include "common.h"
#include "lang.h"

namespace Util
{

    static inline void utf16_to_utf8(const uint16_t *src, uint8_t *dst)
    {
        int i;
        for (i = 0; src[i]; i++)
        {
            if ((src[i] & 0xFF80) == 0)
            {
                *(dst++) = src[i] & 0xFF;
            }
            else if ((src[i] & 0xF800) == 0)
            {
                *(dst++) = ((src[i] >> 6) & 0xFF) | 0xC0;
                *(dst++) = (src[i] & 0x3F) | 0x80;
            }
            else if ((src[i] & 0xFC00) == 0xD800 && (src[i + 1] & 0xFC00) == 0xDC00)
            {
                *(dst++) = (((src[i] + 64) >> 8) & 0x3) | 0xF0;
                *(dst++) = (((src[i] >> 2) + 16) & 0x3F) | 0x80;
                *(dst++) = ((src[i] >> 4) & 0x30) | 0x80 | ((src[i + 1] << 2) & 0xF);
                *(dst++) = (src[i + 1] & 0x3F) | 0x80;
                i += 1;
            }
            else
            {
                *(dst++) = ((src[i] >> 12) & 0xF) | 0xE0;
                *(dst++) = ((src[i] >> 6) & 0x3F) | 0x80;
                *(dst++) = (src[i] & 0x3F) | 0x80;
            }
        }

        *dst = '\0';
    }

    static inline void utf8_to_utf16(const uint8_t *src, uint16_t *dst)
    {
        int i;
        for (i = 0; src[i];)
        {
            if ((src[i] & 0xE0) == 0xE0)
            {
                *(dst++) = ((src[i] & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) | (src[i + 2] & 0x3F);
                i += 3;
            }
            else if ((src[i] & 0xC0) == 0xC0)
            {
                *(dst++) = ((src[i] & 0x1F) << 6) | (src[i + 1] & 0x3F);
                i += 2;
            }
            else
            {
                *(dst++) = src[i];
                i += 1;
            }
        }

        *dst = '\0';
    }

    static inline uint16_t LE16(const uint8_t *bytes)
    {
        return (bytes[0]) | (bytes[1] << 8);
    }

    static inline uint32_t LE32(const uint8_t *bytes)
    {
        return (bytes[0]) | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
    }

    static inline uint64_t LE64(const uint8_t *bytes)
    {
        return (uint64_t)bytes[0] | ((uint64_t)bytes[1] << 8) | ((uint64_t)bytes[2] << 16) | ((uint64_t)bytes[3] << 24) | ((uint64_t)bytes[4] << 32) | ((uint64_t)bytes[5] << 40) | ((uint64_t)bytes[6] << 48) | ((uint64_t)bytes[7] << 56);
    }

    static inline uint16_t BE16(const uint8_t *bytes)
    {
        return (bytes[1]) | (bytes[0] << 8);
    }

    static inline uint32_t BE32(const uint8_t *bytes)
    {
        return (bytes[3]) | (bytes[2] << 8) | (bytes[1] << 16) | (bytes[0] << 24);
    }

    static inline uint64_t BE64(const uint8_t *bytes)
    {
        return (uint64_t)bytes[7] | ((uint64_t)bytes[6] << 8) | ((uint64_t)bytes[5] << 16) | ((uint64_t)bytes[4] << 24) | ((uint64_t)bytes[3] << 32) | ((uint64_t)bytes[2] << 40) | ((uint64_t)bytes[1] << 48) | ((uint64_t)bytes[0] << 56);
    }

    static inline std::string &Ltrim(std::string &str, std::string chars)
    {
        str.erase(0, str.find_first_not_of(chars));
        return str;
    }

    static inline std::string &Rtrim(std::string &str, std::string chars)
    {
        str.erase(str.find_last_not_of(chars) + 1);
        return str;
    }

    // trim from both ends (in place)
    static inline std::string &Trim(std::string &str, std::string chars)
    {
        return Ltrim(Rtrim(str, chars), chars);
    }

    static inline void ReplaceAll(std::string &data, std::string toSearch, std::string replaceStr)
    {
        size_t pos = data.find(toSearch);
        while (pos != std::string::npos)
        {
            data.replace(pos, toSearch.size(), replaceStr);
            pos = data.find(toSearch, pos + replaceStr.size());
        }
    }

    static inline std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); });
        return s;
    }

    static inline bool EndsWith(std::string const &value, std::string const &ending)
    {
        if (ending.size() > value.size())
            return false;
        return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
    }

    static inline std::vector<std::string> Split(const std::string &str, const std::string &delimiter)
    {
        std::string text = std::string(str);
        std::vector<std::string> tokens;
        size_t pos = 0;
        while ((pos = text.find(delimiter)) != std::string::npos)
        {
            if (text.substr(0, pos).length() > 0)
                tokens.push_back(text.substr(0, pos));
            text.erase(0, pos + delimiter.length());
        }
        if (text.length() > 0)
        {
            tokens.push_back(text);
        }

        return tokens;
    }

    static inline std::string ToString(int value)
    {
        std::ostringstream myObjectStream;
        myObjectStream << value;
        return myObjectStream.str();
    }

    /*
        static inline std::string UrlHash(const std::string &text)
        {
            std::vector<unsigned char> res(16);
            MD5((const unsigned char *)text.c_str(), text.length(), res.data());

            std::string out;
            Base64::Encode(res.data(), res.size(), out);
            Util::ReplaceAll(out, "=", "_");
            Util::ReplaceAll(out, "+", "_");
            out = out + ".pkg";
            return out;
        }

        static inline void Notify(const char *fmt, ...)
        {
            OrbisNotificationRequest request;

            va_list args;
            va_start(args, fmt);
            vsprintf(request.message, fmt, args);
            va_end(args);

            request.type = OrbisNotificationRequestType::NotificationRequest;
            request.unk3 = 0;
            request.useIconImageUri = 0;
            request.targetId = -1;
            sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
        }
    */

    static inline void SetupPreviousFolder(const std::string &path, DirEntry *entry)
    {
        memset(entry, 0, sizeof(DirEntry));
        if (path[path.length() - 1] == '/' && path.length() > 1)
        {
            strlcpy(entry->directory, path.c_str(), path.length() - 1);
        }
        else
        {
            sprintf(entry->directory, "%s", path.c_str());
        }
        sprintf(entry->name, "%s", "..");
        sprintf(entry->path, "%s", entry->directory);
        sprintf(entry->display_size, "%s", lang_strings[STR_FOLDER]);
        entry->file_size = 0;
        entry->isDir = true;
        entry->selectable = false;
    }

    static uint64_t GetTick()
    {
        static struct timeval tick;
        gettimeofday(&tick, NULL);
        return tick.tv_sec * 1000000 + tick.tv_usec;
    }

    static inline size_t NthOccurrence(const std::string &str, const std::string &findMe, int nth, size_t start_pos = 0, size_t end_pos = INT_MAX)
    {
        size_t prev_pos = std::string::npos;
        size_t pos = start_pos;
        int cnt = 0;

        while (cnt != nth)
        {
            pos += 1;
            pos = str.find(findMe, pos);
            if (pos > end_pos)
                return prev_pos;
                
            if (pos == std::string::npos)
            {
                if (cnt == 0)
                    return std::string::npos;
                else
                    break;
            }
            prev_pos = pos;
            cnt++;
        }
        return pos;
    }

    static inline size_t CountOccurrence(const std::string &str, const std::string &findMe, size_t start_pos = 0, size_t end_pos = INT_MAX)
    {
        size_t pos = start_pos;
        int cnt = 0;
        while (true)
        {
            pos += 1;
            pos = str.find(findMe, pos);
            if (pos > end_pos)
                return cnt;
                
            if (pos == std::string::npos)
            {
                break;
            }
            pos += 1;
            cnt++;
        }
        return cnt;
    }
}
#endif
