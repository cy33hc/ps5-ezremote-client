#include <cstring>
#include <json-c/json.h>
#include "sfo.h"

static constexpr uint32_t SFO_MAGIC = 0x46535000;

namespace SFO {
    const char* GetString(const char* buffer, size_t size, const char *name)
    {
        if (size < sizeof(SfoHeader))
            return nullptr;

        const SfoHeader* header = reinterpret_cast<const SfoHeader*>(buffer);
        const SfoEntry* entries =
                reinterpret_cast<const SfoEntry*>(buffer + sizeof(SfoHeader));

        if (header->magic != SFO_MAGIC)
            return nullptr;

        if (size < sizeof(SfoHeader) + header->count * sizeof(SfoEntry))
            return nullptr;

        for (uint32_t i = 0; i < header->count; i++) {
            const char* key = reinterpret_cast<const char*>(buffer + header->keyofs + entries[i].nameofs);
            if (strcmp(key, name) == 0)
                return reinterpret_cast<const char*>(buffer + header->valofs + entries[i].dataofs);
        }
        
        return {};
    }

    std::map<std::string, std::string> GetParams(const char* buffer, size_t size)
    {
        std::map<std::string, std::string> out;

        if (size < sizeof(SfoHeader))
            return out;

        const SfoHeader* header = reinterpret_cast<const SfoHeader*>(buffer);
        const SfoEntry* entries =
                reinterpret_cast<const SfoEntry*>(buffer + sizeof(SfoHeader));

        if (header->magic != SFO_MAGIC)
            return out;

        if (size < sizeof(SfoHeader) + header->count * sizeof(SfoEntry))
            return out;

        for (uint32_t i = 0; i < header->count; i++) {
            const char* key = reinterpret_cast<const char*>(buffer + header->keyofs + entries[i].nameofs);
            if (entries[i].type == 2)
            {
                const char* value = reinterpret_cast<const char*>(buffer + header->valofs + entries[i].dataofs);
                out.insert(std::make_pair(key, value));
            }
            else
            {
                uint32_t *value = (uint32_t *)(buffer + header->valofs + entries[i].dataofs);
                out.insert(std::make_pair(key, std::to_string(*value)));
            }
        }

        return out;
    }

    std::map<std::string, std::string> GetParamsFromParamJson(const char* buffer, size_t size)
    {
        std::map<std::string, std::string> out;
        const char* value;

        json_object *parent_obj = json_tokener_parse(buffer);
        
        value = json_object_get_string(json_object_object_get(parent_obj, "contentId"));
        if (value != nullptr)
            out.insert(std::make_pair("contentId", value));

        value = json_object_get_string(json_object_object_get(parent_obj, "contentVersion"));
        if (value != nullptr)
            out.insert(std::make_pair("contentVersion", value));

        value = json_object_get_string(json_object_object_get(parent_obj, "requiredSystemSoftwareVersion"));
        if (value != nullptr)
            out.insert(std::make_pair("requiredSystemSoftwareVersion", value));

        json_object *localizedParameters = json_object_object_get(parent_obj, "localizedParameters");
        if (localizedParameters != nullptr)
        {
            const char* defaultLanguage = json_object_get_string(json_object_object_get(localizedParameters, "defaultLanguage"));

            if (defaultLanguage != nullptr)
            {
                json_object *lang_region = json_object_object_get(localizedParameters, defaultLanguage);
                if (lang_region != nullptr)
                {
                    const char* titleName = json_object_get_string(json_object_object_get(lang_region, "titleName"));
                    if (titleName != nullptr)
                        out.insert(std::make_pair("titleName", titleName));
                }
            }
        }

        value = json_object_get_string(json_object_object_get(parent_obj, "sdkVersion"));
        if (value != nullptr)
            out.insert(std::make_pair("sdkVersion", value));

        value = json_object_get_string(json_object_object_get(parent_obj, "titleId"));
        if (value != nullptr)
            out.insert(std::make_pair("titleId", value));

        return out;
    }
}