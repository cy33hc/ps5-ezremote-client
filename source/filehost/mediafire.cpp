#include <regex>
#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/element.h>
#include "httpclient/HTTPClient.h"

#include "common.h"
#include "config.h"
#include "mediafire.h"

#define VALIDATION_REGEX "https:\\/\\/www\\.mediafire\\.com\\/file\\/(.*)\\/(.*)\\/file"

MediaFireHost::MediaFireHost(const std::string &url) : FileHost(url)
{
}

bool MediaFireHost::IsValidUrl()
{
    std::regex regex(VALIDATION_REGEX);

    if (std::regex_match(url, regex))
        return true;
    return false;
}

std::string MediaFireHost::GetDownloadUrl()
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;
    CHTTPClient tmp_client([](const std::string& log){});
    tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
    tmp_client.SetCertificateFile(CACERT_FILE);

    if (tmp_client.Get(url, headers, res))
    {
        if (HTTP_SUCCESS(res.iCode))
        {
            lxb_status_t status;
            lxb_dom_element_t *element;
            lxb_html_document_t *document;
            lxb_dom_collection_t *collection;
            lxb_dom_attr_t *attr;

            document = lxb_html_document_create();
            status = lxb_html_document_parse(document, (lxb_char_t *)res.strBody.data(), res.strBody.size());
            if (status != LXB_STATUS_OK)
                return "";
            collection = lxb_dom_collection_make(&document->dom_document, 128);
            if (collection == NULL)
            {
                lxb_html_document_destroy(document);
                return "";
            }

            status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->body),
                                                collection, (const lxb_char_t *)"a", 1);
            if (status != LXB_STATUS_OK)
            {
                lxb_dom_collection_destroy(collection, true);
                lxb_html_document_destroy(document);
                return "";
            }

            std::string download_url;
            for (size_t i = 0; i < lxb_dom_collection_length(collection); i++)
            {
                element = lxb_dom_collection_element(collection, i);
                if (element->attr_id != nullptr)
                {
                    std::string a_id((char *)element->attr_id->value->data, element->attr_id->value->length);

                    if (a_id == "downloadButton")
                    {
                        size_t value_len;
                        const lxb_char_t *value = lxb_dom_element_get_attribute(element, (const lxb_char_t *)"href", 4, &value_len);
                        download_url = std::string((char *)value, value_len);
                        break;
                    }
                }
            }
            lxb_dom_collection_destroy(collection, true);
            lxb_html_document_destroy(document);

            return download_url;
        }
    }

    return "";
}
