#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <json-c/json.h>
#include "httpclient/HTTPClient.h"
#include "clients/webdav.h"
#include "clients/remote_client.h"
#include "clients/smbclient.h"
#include "clients/sftpclient.h"
#include "clients/ftpclient.h"
#include "clients/nfsclient.h"
#include "clients/webdav.h"
#include "clients/apache.h"
#include "clients/archiveorg.h"
#include "clients/iis.h"
#include "clients/github.h"
#include "clients/myrient.h"
#include "clients/nginx.h"
#include "clients/npxserve.h"
#include "clients/rclone.h"
#include "dbglogger.h"

#include "server/http_server.h"
#include "util.h"
#include "config.h"
#include "windows.h"
#include "lang.h"
#include "fs.h"
#include "sfo.h"
#include "sceAppInstUtil.h"
#include "sceUserService.h"
#include "sceSystemService.h"
#include "installer.h"

struct BgProgressCheck
{
	ArchivePkgInstallData *archive_pkg_data;
	SplitPkgInstallData *split_pkg_data;
	content_id_t content_id;
	std::string hash;
	std::string url;
	std::string title;
};

static bool sceAppInst_done = false;

static std::map<std::string, ArchivePkgInstallData *> archive_pkg_install_data_list;
static std::map<std::string, SplitPkgInstallData *> split_pkg_install_data_list;

namespace INSTALLER
{
    static int FtpCallback(int64_t xfered, void *arg)
    {
        return 1;
    }

	int Init(void)
	{
		int ret;

		if (sceAppInst_done)
		{
			return 0;
		}

		ret = sceAppInstUtilInitialize();
		if (ret)
		{
			return -1;
		}

		sceAppInst_done = true;

		return 0;
	}

	void Exit(void)
	{
	}

	std::string GetRemotePkgTitle(RemoteClient *client, const std::string &path, pkg_header *header)
	{
		if (BE32(header->pkg_magic) != PS4_PKG_MAGIC)
		{
			return std::string((char*)header->pkg_content_id);
		}

		size_t entry_count = BE32(header->pkg_entry_count);
		uint32_t entry_table_offset = BE32(header->pkg_table_offset);
		uint64_t entry_table_size = entry_count * sizeof(pkg_table_entry);
		void *entry_table_data = malloc(entry_table_size);

		int ret = client->GetRange(path, entry_table_data, entry_table_size, entry_table_offset);
		if (ret == 0)
		{
			free(entry_table_data);
			return "";
		}

		pkg_table_entry *entries = (pkg_table_entry *)entry_table_data;
		void *param_sfo_data = nullptr;
		uint32_t param_sfo_offset = 0;
		uint32_t param_sfo_size = 0;
		for (size_t i = 0; i < entry_count; ++i)
		{
			if (BE32(entries[i].id) == PKG_ENTRY_ID_PARAM_SFO)
			{
				param_sfo_offset = BE32(entries[i].offset);
				param_sfo_size = BE32(entries[i].size);
				break;
			}
		}
		free(entry_table_data);

		std::string title;
		if (param_sfo_offset > 0 && param_sfo_size > 0)
		{
			param_sfo_data = malloc(param_sfo_size);
			int ret = client->GetRange(path, param_sfo_data, param_sfo_size, param_sfo_offset);
			if (ret)
			{
				const char *tmp_title = SFO::GetString((const char *)param_sfo_data, param_sfo_size, "TITLE");
				if (tmp_title != nullptr)
					title = std::string(tmp_title);
			}
			free(param_sfo_data);
		}

		return title;
	}
 
	std::string GetLocalPkgTitle(const std::string &path, pkg_header *header)
	{
		size_t entry_count = BE32(header->pkg_entry_count);
		uint32_t entry_table_offset = BE32(header->pkg_table_offset);
		uint64_t entry_table_size = entry_count * sizeof(pkg_table_entry);
		void *entry_table_data = malloc(entry_table_size);

		FILE *fd = FS::OpenRead(path);
		FS::Seek(fd, entry_table_offset);
		FS::Read(fd, entry_table_data, entry_table_size);

		pkg_table_entry *entries = (pkg_table_entry *)entry_table_data;
		void *param_sfo_data = NULL;
		uint32_t param_sfo_offset = 0;
		uint32_t param_sfo_size = 0;
		void *icon0_png_data = NULL;
		uint32_t icon0_png_offset = 0;
		uint32_t icon0_png_size = 0;
		for (size_t i = 0; i < entry_count; ++i)
		{
			if (BE32(entries[i].id) == PKG_ENTRY_ID_PARAM_SFO)
			{
				param_sfo_offset = BE32(entries[i].offset);
				param_sfo_size = BE32(entries[i].size);
				break;
			}
		}
		free(entry_table_data);

		std::string title;
		if (param_sfo_offset > 0 && param_sfo_size > 0)
		{
			param_sfo_data = malloc(param_sfo_size);
			FS::Seek(fd, param_sfo_offset);
			FS::Read(fd, param_sfo_data, param_sfo_size);
			const char *tmp_title = SFO::GetString((const char *)param_sfo_data, param_sfo_size, "TITLE");
			if (tmp_title != nullptr)
				title = std::string(tmp_title);
			free(param_sfo_data);
		}

		return title;
	}

	std::string StoreBgInstallHostData(RemoteSettings *settings, const std::string &path)
	{
		std::string hash = Util::UrlHash(settings->server + path + settings->username + settings->password + std::to_string(settings->type));
		json_object *history_item_obj = json_object_new_object();
		json_object_object_add(history_item_obj, "hash", json_object_new_string(hash.c_str()));
		json_object_object_add(history_item_obj, "url", json_object_new_string(settings->server));
		json_object_object_add(history_item_obj, "path", json_object_new_string(path.c_str()));
		json_object_object_add(history_item_obj, "username", json_object_new_string(settings->username));
		json_object_object_add(history_item_obj, "password", json_object_new_string(settings->password));
		json_object_object_add(history_item_obj, "type", json_object_new_int(settings->type));

		if (settings->type == CLIENT_TYPE_HTTP_SERVER)
		{
			json_object_object_add(history_item_obj, "http_server_type", json_object_new_string(settings->http_server_type));
		}

		const char *params_str = json_object_to_json_string(history_item_obj);
		json_object_put(history_item_obj);
		
		CHTTPClient::HttpResponse res;
		CHTTPClient::HeadersMap headers;
		CHTTPClient tmp_client([](const std::string& log){});
		tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
		tmp_client.SetCertificateFile(CACERT_FILE);
		headers["Content-Type"] = "application/json";

		std::string store_bg_install_data_url = std::string("http://localhost:") + std::to_string(http_int_server_port) + "/store_bg_install_data";
		if (tmp_client.Post(store_bg_install_data_url, headers, params_str, res))
		{
			if (HTTP_SUCCESS(res.iCode))
			{
	  		}
			else
			{
				return "";
			}
		}
		return hash;
	}

	std::string getRemoteUrl(const std::string path, bool encodeUrl)
	{
		if (remote_settings->type == CLIENT_TYPE_HTTP_SERVER && strcmp(remote_settings->http_server_type, HTTP_SERVER_GITHUB) == 0)
		{
			GithubClient *tmp_client = (GithubClient*) remoteclient;
			return tmp_client->GetDownloadUrl(path);
		}
		if ( strlen(remote_settings->username) == 0 &&
		     strlen(remote_settings->password) == 0 &&
			 (remote_settings->type == CLIENT_TYPE_WEBDAV ||
			   (remote_settings->type == CLIENT_TYPE_HTTP_SERVER && strcmp(remote_settings->http_server_type, HTTP_SERVER_ARCHIVEORG) == 0)
			  )
		   )
		{
			std::string full_url = WebDAVClient::GetHttpUrl(remote_settings->server + path);
			size_t scheme_pos = full_url.find("://");
			if (scheme_pos == std::string::npos)
				return "";
			size_t root_pos = full_url.find("/", scheme_pos + 3);
			std::string host = full_url.substr(0, root_pos);
			std::string path = full_url.substr(root_pos);

			if (encodeUrl)
			{
				path = httplib::detail::encode_url(path);
			}

			return host + path;
		}
		else
		{
			std::string hash = StoreBgInstallHostData(remote_settings, path);
			std::string full_url = std::string("http://localhost:") + std::to_string(http_int_server_port) + "/bg_install/" + hash;
			return full_url;
		}

		return "";
	}

	void *CleanArchivePkgDataThread(void *argp)
	{
		ArchivePkgInstallData *archive_pkg_data = (ArchivePkgInstallData*)argp;
		archive_pkg_data->stop_write_thread = true;
		if (!archive_pkg_data->split_file->IsClosed())
			archive_pkg_data->split_file->Close();
		pthread_join(archive_pkg_data->thread, NULL);
		delete (archive_pkg_data->split_file);
		free(archive_pkg_data);
		return nullptr;
	}

	void *CleanSplitPkgDataThread(void *argp)
	{
		SplitPkgInstallData *split_pkg_data = (SplitPkgInstallData*)argp;
		if (!split_pkg_data->split_file->IsClosed())
			split_pkg_data->split_file->Close();
		pthread_join(split_pkg_data->thread, NULL);
		delete (split_pkg_data->split_file);
		if (split_pkg_data->delete_client)
			delete (split_pkg_data->remote_client);
		free(split_pkg_data);
		return nullptr;
	}

	void *CheckBgInstallTaskThread(void *argp)
	{
		bool completed = false;
		BgProgressCheck *bg_check_data = (BgProgressCheck *)argp;
		int ret;

		PlayGoInfo playgo_info;
		SceAppInstallPkgInfo pkg_info;
		memset(&playgo_info, 0, sizeof(playgo_info));
		
		for (size_t i = 0; i < SCE_NUM_LANGUAGES; i++) {
			strncpy(playgo_info.languages[i], "", sizeof(language_t) - 1);
		}	

		for (size_t i = 0; i < SCE_NUM_IDS; i++) {
			strncpy(playgo_info.playgo_scenario_ids[i], "", sizeof(playgo_scenario_id_t) - 1);
			strncpy(*playgo_info.content_ids, "", sizeof(content_id_t) - 1);
		}	

		MetaInfo metainfo = (MetaInfo){
			.uri = bg_check_data->url.c_str(),
			.ex_uri = "",
			.playgo_scenario_id = "",
			.content_id = "",
			.content_name = bg_check_data->title.c_str(),
			.icon_url = ""
		};

		ret = InstallWithDirectPackageInstaller(bg_check_data->url);
		if (ret != 0)
		{
			return 0;
		}

		SceAppInstallStatusInstalled progress_info;
		while (strcmp(progress_info.status, "playable") != 0 && strcmp(progress_info.status, "none") != 0 )
		{
			ret = sceAppInstUtilGetInstallStatus(bg_check_data->content_id, &progress_info);
			if (ret || (progress_info.error_info.error_code != 0))
			goto finish;

			bytes_to_download = progress_info.total_size;
			bytes_transfered = progress_info.downloaded_size;
			sceSystemServicePowerTick();
			usleep(500000);
		}

	finish:
		if (bg_check_data->archive_pkg_data != nullptr)
		{
			ret = pthread_create(&bk_clean_thid, NULL, CleanArchivePkgDataThread, bg_check_data->archive_pkg_data);
			RemoveArchivePkgInstallData(bg_check_data->hash);
			free(bg_check_data);
		}
		else if (bg_check_data->split_pkg_data != nullptr)
		{
			ret = pthread_create(&bk_clean_thid, NULL, CleanSplitPkgDataThread, bg_check_data->split_pkg_data);
			RemoveSplitPkgInstallData(bg_check_data->hash);
			free(bg_check_data);
		}
		activity_inprogess = false;
		file_transfering = false;
		Windows::SetModalMode(false);
		return nullptr;
	}

	bool canInstallRemotePkg(const std::string &url)
	{
		return true;
	}

	int InstallRemotePkg(const std::string &url, pkg_header *header, std::string title)
	{
		if (url.empty())
			return 0;

		std::string cid = std::string((char *)header->pkg_content_id);
		cid = cid.substr(cid.find_first_of("-") + 1, 9);
		std::string display_title = title.length() > 0 ? title : cid;

		int ret;	

		PlayGoInfo playgo_info;
		SceAppInstallPkgInfo pkg_info;
		memset(&playgo_info, 0, sizeof(playgo_info));
		
		for (size_t i = 0; i < SCE_NUM_LANGUAGES; i++) {
			strncpy(playgo_info.languages[i], "", sizeof(language_t) - 1);
		}	

		for (size_t i = 0; i < SCE_NUM_IDS; i++) {
			strncpy(playgo_info.playgo_scenario_ids[i], "", sizeof(playgo_scenario_id_t) - 1);
			strncpy(*playgo_info.content_ids, "", sizeof(content_id_t) - 1);
		}	

		MetaInfo metainfo = (MetaInfo){
			.uri = url.c_str(),
			.ex_uri = "",
			.playgo_scenario_id = "",
			.content_id = "",
			.content_name = display_title.c_str(),
			.icon_url = ""
		};

		ret = InstallWithDirectPackageInstaller(url);
		if (ret != 0)
		{
			return 0;
		}

		return 1;
	}

	int InstallLocalPkg(const std::string &path)
	{
		int ret;
		pkg_header header;
		bool completed = false;

		memset(&header, 0, sizeof(header));
		if (FS::Head(path.c_str(), (void *)&header, sizeof(header)) == 0)
			return 0;

		if (BE32(header.pkg_magic) != PS4_PKG_MAGIC)
			return 0;

		return InstallLocalPkg(path, &header, false);
	}

	int InstallLocalPkg(const std::string &path, pkg_header *header, bool remove_after_install)
	{
		int ret;

		if (strncmp(path.c_str(), "/data/", 6) != 0 &&
			strncmp(path.c_str(), "/user/data/", 11) != 0 &&
			strncmp(path.c_str(), "/mnt/usb", 8) != 0)
			return -1;

		std::string filename = path.substr(path.find_last_of("/") + 1);
		char filepath[1024];
		snprintf(filepath, 1023, "%s", path.c_str());
		if (strncmp(path.c_str(), "/data/", 6) == 0)
			snprintf(filepath, 1023, "/user%s", path.c_str());

		PlayGoInfo playgo_info;
		SceAppInstallPkgInfo pkg_info;
		memset(&playgo_info, 0, sizeof(playgo_info));
		
		for (size_t i = 0; i < SCE_NUM_LANGUAGES; i++) {
			strncpy(playgo_info.languages[i], "", sizeof(language_t) - 1);
		}

		for (size_t i = 0; i < SCE_NUM_IDS; i++) {
			strncpy(playgo_info.playgo_scenario_ids[i], "", sizeof(playgo_scenario_id_t) - 1);
			strncpy(*playgo_info.content_ids, "", sizeof(content_id_t) - 1);
		}

		std::string title;
		if (BE32(header->pkg_magic) == PS4_PKG_MAGIC)
		{
			title = GetLocalPkgTitle(filepath, header);
		}
		else
		{
			title = filename;
		}
		MetaInfo metainfo = (MetaInfo){
			.uri = filepath,
			.ex_uri = "",
			.playgo_scenario_id = "",
			.content_id = "",
			.content_name = title.c_str(),
			.icon_url = ""
		};

		ret = InstallWithDirectPackageInstaller(filepath);
		if (ret != 0)
		{
			goto err;
		}

		if (!remove_after_install)
		{
			return 1;
		} 

		sprintf(activity_message, "%s", lang_strings[STR_WAIT_FOR_INSTALL_MSG]);
		bytes_to_download = header->pkg_content_size;
		bytes_transfered = 0;
		prev_tick = Util::GetTick();

		SceAppInstallStatusInstalled progress_info;
		while (strcmp(progress_info.status, "playable") != 0 && strcmp(progress_info.status, "none") != 0 )
		{
			ret = sceAppInstUtilGetInstallStatus((const char *)header->pkg_content_id, &progress_info);
			if (ret || (progress_info.error_info.error_code != 0))
				return -3;

			bytes_to_download = progress_info.total_size;
			bytes_transfered = progress_info.downloaded_size;
			sceSystemServicePowerTick();
		}

		if (remove_after_install)
			FS::Rm(path);
		return 1;

	err:
		if (remove_after_install)
			FS::Rm(path);
		return 0;
	}

	bool ExtractLocalPkg(const std::string &path, const std::string sfo_path, const std::string icon_path)
	{
		pkg_header tmp_hdr;
		FS::Head(path, &tmp_hdr, sizeof(pkg_header));

		if (BE32(tmp_hdr.pkg_magic) != PS4_PKG_MAGIC)
			return false;

		size_t entry_count = BE32(tmp_hdr.pkg_entry_count);
		uint32_t entry_table_offset = BE32(tmp_hdr.pkg_table_offset);
		uint64_t entry_table_size = entry_count * sizeof(pkg_table_entry);
		void *entry_table_data = malloc(entry_table_size);

		FILE *fd = FS::OpenRead(path);
		FS::Seek(fd, entry_table_offset);
		FS::Read(fd, entry_table_data, entry_table_size);

		pkg_table_entry *entries = (pkg_table_entry *)entry_table_data;
		void *param_sfo_data = NULL;
		uint32_t param_sfo_offset = 0;
		uint32_t param_sfo_size = 0;
		void *icon0_png_data = NULL;
		uint32_t icon0_png_offset = 0;
		uint32_t icon0_png_size = 0;
		short items = 0;
		for (size_t i = 0; i < entry_count; ++i)
		{
			switch (BE32(entries[i].id))
			{
			case PKG_ENTRY_ID_PARAM_SFO:
				param_sfo_offset = BE32(entries[i].offset);
				param_sfo_size = BE32(entries[i].size);
				items++;
				break;
			case PKG_ENTRY_ID_ICON0_PNG:
				icon0_png_offset = BE32(entries[i].offset);
				icon0_png_size = BE32(entries[i].size);
				items++;
				break;
			default:
				continue;
			}

			if (items == 2)
				break;
		}
		free(entry_table_data);

		if (param_sfo_offset > 0 && param_sfo_size > 0)
		{
			param_sfo_data = malloc(param_sfo_size);
			FILE *out = FS::Create(sfo_path);
			FS::Seek(fd, param_sfo_offset);
			FS::Read(fd, param_sfo_data, param_sfo_size);
			FS::Write(out, param_sfo_data, param_sfo_size);
			FS::Close(out);
			free(param_sfo_data);
		}

		if (icon0_png_offset > 0 && icon0_png_size > 0)
		{
			icon0_png_data = malloc(icon0_png_size);
			FILE *out = FS::Create(icon_path);
			FS::Seek(fd, icon0_png_offset);
			FS::Read(fd, icon0_png_data, icon0_png_size);
			FS::Write(out, icon0_png_data, icon0_png_size);
			FS::Close(out);
			free(icon0_png_data);
		}

		FS::Close(fd);
		return true;
	}

	bool ExtractRemotePkg(const std::string &path, const std::string sfo_path, const std::string icon_path)
	{
		pkg_header tmp_hdr;
		if (!remoteclient->Head(path, &tmp_hdr, sizeof(pkg_header)))
			return false;

		if (BE32(tmp_hdr.pkg_magic) != PS4_PKG_MAGIC)
			return false;

		size_t entry_count = BE32(tmp_hdr.pkg_entry_count);
		uint32_t entry_table_offset = BE32(tmp_hdr.pkg_table_offset);
		uint64_t entry_table_size = entry_count * sizeof(pkg_table_entry);
		void *entry_table_data = malloc(entry_table_size);

		if (!remoteclient->GetRange(path, entry_table_data, entry_table_size, entry_table_offset))
			return false;

		pkg_table_entry *entries = (pkg_table_entry *)entry_table_data;
		void *param_sfo_data = NULL;
		uint32_t param_sfo_offset = 0;
		uint32_t param_sfo_size = 0;
		void *icon0_png_data = NULL;
		uint32_t icon0_png_offset = 0;
		uint32_t icon0_png_size = 0;
		short items = 0;
		for (size_t i = 0; i < entry_count; ++i)
		{
			switch (BE32(entries[i].id))
			{
			case PKG_ENTRY_ID_PARAM_SFO:
				param_sfo_offset = BE32(entries[i].offset);
				param_sfo_size = BE32(entries[i].size);
				items++;
				break;
			case PKG_ENTRY_ID_ICON0_PNG:
				icon0_png_offset = BE32(entries[i].offset);
				icon0_png_size = BE32(entries[i].size);
				items++;
				break;
			default:
				continue;
			}

			if (items == 2)
				break;
		}
		free(entry_table_data);

		if (param_sfo_offset > 0 && param_sfo_size > 0)
		{
			param_sfo_data = malloc(param_sfo_size);
			FILE *out = FS::Create(sfo_path);
			if (!remoteclient->GetRange(path, param_sfo_data, param_sfo_size, param_sfo_offset))
			{
				FS::Close(out);
				return false;
			}
			FS::Write(out, param_sfo_data, param_sfo_size);
			FS::Close(out);
			free(param_sfo_data);
		}

		if (icon0_png_offset > 0 && icon0_png_size > 0)
		{
			icon0_png_data = malloc(icon0_png_size);
			FILE *out = FS::Create(icon_path);
			if (!remoteclient->GetRange(path, icon0_png_data, icon0_png_size, icon0_png_offset))
			{
				FS::Close(out);
				return false;
			}
			FS::Write(out, icon0_png_data, icon0_png_size);
			FS::Close(out);
			free(icon0_png_data);
		}

		return true;
	}

	ArchivePkgInstallData *GetArchivePkgInstallData(const std::string &hash)
	{
		return archive_pkg_install_data_list[hash];
	}

	void AddArchivePkgInstallData(const std::string &hash, ArchivePkgInstallData *pkg_data)
	{
		std::pair<std::string, ArchivePkgInstallData *> pair = std::make_pair(hash, pkg_data);
		archive_pkg_install_data_list.erase(hash);
		archive_pkg_install_data_list.insert(pair);
	}

	void RemoveArchivePkgInstallData(const std::string &hash)
	{
		archive_pkg_install_data_list.erase(hash);
	}

	bool InstallArchivePkg(const std::string &path, ArchivePkgInstallData *pkg_data, bool bg)
	{
		int ret = 0;
		pkg_header header;
		pkg_data->split_file->Read((char *)&header, sizeof(pkg_header), 0);

		std::string cid = std::string((char *)header.pkg_content_id);
		cid = cid.substr(cid.find_first_of("-") + 1, 9);
		std::string display_title = cid;

		std::string hash = Util::UrlHash(path);
		std::string full_url = std::string("http://localhost:") + std::to_string(http_server_port) + "/archive_inst/" + hash;
		AddArchivePkgInstallData(hash, pkg_data);

		if (!bg)
		{
			PlayGoInfo playgo_info;
			SceAppInstallPkgInfo pkg_info;
			memset(&playgo_info, 0, sizeof(playgo_info));
			
			for (size_t i = 0; i < SCE_NUM_LANGUAGES; i++) {
				strncpy(playgo_info.languages[i], "", sizeof(language_t) - 1);
			}

			for (size_t i = 0; i < SCE_NUM_IDS; i++) {
				strncpy(playgo_info.playgo_scenario_ids[i], "", sizeof(playgo_scenario_id_t) - 1);
				strncpy(*playgo_info.content_ids, "", sizeof(content_id_t) - 1);
			}

			MetaInfo metainfo = (MetaInfo){
				.uri = full_url.c_str(),
				.ex_uri = "",
				.playgo_scenario_id = "",
				.content_id = "",
				.content_name = display_title.c_str(),
				.icon_url = ""
			};

			ret = InstallWithDirectPackageInstaller(full_url);
			if (ret)
			{
				ret = 0;
				goto finish;
			}

			file_transfering = true;
			bytes_to_download = header.pkg_content_size;
			bytes_transfered = 0;
			prev_tick = Util::GetTick();

			SceAppInstallStatusInstalled progress_info;
			while (strcmp(progress_info.status, "playable") != 0 && strcmp(progress_info.status, "none") != 0 )
			{
				ret = sceAppInstUtilGetInstallStatus((const char *)header.pkg_content_id, &progress_info);
				if (ret || (progress_info.error_info.error_code != 0))
					return 0;
	
				bytes_to_download = progress_info.total_size;
				bytes_transfered = progress_info.downloaded_size;
				sceSystemServicePowerTick();
			}
		}
		else
		{
			BgProgressCheck *bg_check_data = (BgProgressCheck *)malloc(sizeof(BgProgressCheck));
			memset(bg_check_data, 0, sizeof(BgProgressCheck));
			bg_check_data->archive_pkg_data = pkg_data;
			bg_check_data->split_pkg_data = nullptr;
			bg_check_data->url = full_url;
			bg_check_data->title = display_title;
			snprintf(bg_check_data->content_id, sizeof(bg_check_data->content_id), "%s", (char *)header.pkg_content_id);
			bg_check_data->hash = hash;
			ret = pthread_create(&bk_install_thid, NULL, CheckBgInstallTaskThread, bg_check_data);
			return 1;
		}
		ret = 1;
	finish:
		ret = pthread_create(&bk_clean_thid, NULL, CleanArchivePkgDataThread, pkg_data);
		RemoveArchivePkgInstallData(hash);
		return ret;
	}

	SplitPkgInstallData *GetSplitPkgInstallData(const std::string &hash)
	{
		return split_pkg_install_data_list[hash];
	}

	void AddSplitPkgInstallData(const std::string &hash, SplitPkgInstallData *pkg_data)
	{
		std::pair<std::string, SplitPkgInstallData *> pair = std::make_pair(hash, pkg_data);
		split_pkg_install_data_list.erase(hash);
		split_pkg_install_data_list.insert(pair);
	}

	void RemoveSplitPkgInstallData(const std::string &hash)
	{
		split_pkg_install_data_list.erase(hash);
	}

	bool InstallSplitPkg(const std::string &path, SplitPkgInstallData *pkg_data, bool bg)
	{
		int ret = 0;
		pkg_header header;
		pkg_data->split_file->Read((char *)&header, sizeof(pkg_header), 0);

		std::string cid = std::string((char *)header.pkg_content_id);
		cid = cid.substr(cid.find_first_of("-") + 1, 9);
		std::string display_title = cid;

		std::string hash = Util::UrlHash(path);
		std::string full_url = std::string("http://localhost:") + std::to_string(http_server_port) + "/split_inst/" + hash;
		AddSplitPkgInstallData(hash, pkg_data);

		if (!bg)
		{
			PlayGoInfo playgo_info;
			SceAppInstallPkgInfo pkg_info;
			memset(&playgo_info, 0, sizeof(playgo_info));
			
			for (size_t i = 0; i < SCE_NUM_LANGUAGES; i++) {
				strncpy(playgo_info.languages[i], "", sizeof(language_t) - 1);
			}

			for (size_t i = 0; i < SCE_NUM_IDS; i++) {
				strncpy(playgo_info.playgo_scenario_ids[i], "", sizeof(playgo_scenario_id_t) - 1);
				strncpy(*playgo_info.content_ids, "", sizeof(content_id_t) - 1);
			}

			MetaInfo metainfo = (MetaInfo){
				.uri = full_url.c_str(),
				.ex_uri = "",
				.playgo_scenario_id = "",
				.content_id = "",
				.content_name = display_title.c_str(),
				.icon_url = ""
			};

			ret = InstallWithDirectPackageInstaller(full_url);
			if (ret)
			{
				ret = 0;
				goto finish;
			}

			file_transfering = true;
			bytes_to_download = pkg_data->size;
			bytes_transfered = 0;
			prev_tick = Util::GetTick();

			SceAppInstallStatusInstalled progress_info;
			while (strcmp(progress_info.status, "playable") != 0 && strcmp(progress_info.status, "none") != 0 )
			{
				ret = sceAppInstUtilGetInstallStatus((const char *)header.pkg_content_id, &progress_info);
				if (ret || (progress_info.error_info.error_code != 0))
					return 0;
	
				bytes_to_download = progress_info.total_size;
				bytes_transfered = progress_info.downloaded_size;
				sceSystemServicePowerTick();
			}
		}
		else
		{
			BgProgressCheck *bg_check_data = (BgProgressCheck *)malloc(sizeof(BgProgressCheck));
			memset(bg_check_data, 0, sizeof(BgProgressCheck));
			bg_check_data->split_pkg_data = pkg_data;
			bg_check_data->archive_pkg_data = nullptr;
			bg_check_data->url = full_url;
			bg_check_data->title = display_title;
			snprintf(bg_check_data->content_id, sizeof(bg_check_data->content_id), "%s", (char *)header.pkg_content_id);
			bg_check_data->hash = hash;
			ret = pthread_create(&bk_install_thid, NULL, CheckBgInstallTaskThread, bg_check_data);
			return 1;
		}
		ret = 1;
	finish:
		pthread_create(&bk_clean_thid, NULL, CleanSplitPkgDataThread, pkg_data);
		RemoveSplitPkgInstallData(hash);
		activity_inprogess = false;
		file_transfering = false;
		Windows::SetModalMode(false);
		return ret;
	}
	
	bool IsDirectPackageInstallerEnabled()
	{
		char buffer[256];
		in_addr_t in_addr;
		in_addr_t server_addr;
		int sockfd;
		ssize_t i;
		ssize_t ret;
		struct hostent *hostent;
		struct sockaddr_in sockaddr_in;
		unsigned short server_port = 9040;
	
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd == -1)
		{
			return false;
		}
	
		/* Prepare sockaddr_in. */
		hostent = gethostbyname("127.0.0.1");
		if (hostent == NULL)
		{
			printf("error: gethostbyname(\"%s\")\n", "127.0.0.1");
			return false;
		}
	
		in_addr = inet_addr(inet_ntoa(*(struct in_addr *)*(hostent->h_addr_list)));
		if (in_addr == (in_addr_t)-1)
		{
			printf("error: inet_addr(\"%s\")\n", *(hostent->h_addr_list));
			return false;
		}
	
		sockaddr_in.sin_addr.s_addr = in_addr;
		sockaddr_in.sin_family = AF_INET;
		sockaddr_in.sin_port = htons(server_port);
		/* Do the actual connection. */
		if (connect(sockfd, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in)) == -1)
		{
			printf("Couldn't connect to ELF loader\n");
			return false;
		}

		return true;
	}

	int StartDirectPackageInstaller()
	{
		char buffer[8192];
		in_addr_t in_addr;
		in_addr_t server_addr;
		int filefd;
		int sockfd;
		ssize_t i;
		ssize_t read_return;
		struct hostent *hostent;
		struct sockaddr_in sockaddr_in;
		unsigned short server_port = 9021;
	
		if (IsDirectPackageInstallerEnabled())
			return 0;

		filefd = open(DPI_ELF_PATH, O_RDONLY);
		if (filefd == -1)
		{
			return -1;
		}
	
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd == -1)
		{
			return -1;
		}
	
		/* Prepare sockaddr_in. */
		hostent = gethostbyname("127.0.0.1");
		if (hostent == NULL)
		{
			return -1;
		}
	
		in_addr = inet_addr(inet_ntoa(*(struct in_addr *)*(hostent->h_addr_list)));
		if (in_addr == (in_addr_t)-1)
		{
			return -1;
		}
	
		sockaddr_in.sin_addr.s_addr = in_addr;
		sockaddr_in.sin_family = AF_INET;
		sockaddr_in.sin_port = htons(server_port);
		/* Do the actual connection. */
		if (connect(sockfd, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in)) == -1)
		{
			return -1;
		}
	
		while (1)
		{
			read_return = read(filefd, buffer, 8192);
			if (read_return == 0)
				break;
			if (read_return == -1)
			{
				return -1;
			}
			if (write(sockfd, buffer, read_return) == -1)
			{
				return -1;
			}
		}
	
		close(filefd);
		close(sockfd);
	
		return 0;
	}

	int InstallWithDirectPackageInstaller(const std::string &url)
	{
		char buffer[256];
		in_addr_t in_addr;
		in_addr_t server_addr;
		int sockfd;
		ssize_t i;
		ssize_t ret;
		struct hostent *hostent;
		struct sockaddr_in sockaddr_in;
		unsigned short server_port = 9040;
	
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd == -1)
		{
			return -1;
		}
	
		/* Prepare sockaddr_in. */
		int yes = 1;
		setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof(int));

		hostent = gethostbyname("127.0.0.1");
		if (hostent == NULL)
		{
			return -1;
		}
	
		in_addr = inet_addr(inet_ntoa(*(struct in_addr *)*(hostent->h_addr_list)));
		if (in_addr == (in_addr_t)-1)
		{
			return -1;
		}
	
		sockaddr_in.sin_addr.s_addr = in_addr;
		sockaddr_in.sin_family = AF_INET;
		sockaddr_in.sin_port = htons(server_port);
		/* Do the actual connection. */
		if (connect(sockfd, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in)) == -1)
		{
			return -1;
		}
	
		ret = send(sockfd, url.c_str(), url.length(), MSG_DONTWAIT);
		if (ret < 0)
		{
			goto cleanup;
		}

		memset(buffer, 0, sizeof buffer);
		ret = read(sockfd, buffer, 256);
		if (ret <= 0)
		{
			goto cleanup;
		}

		ret = atoi(buffer);

	cleanup:
		shutdown(sockfd, SHUT_RDWR);
		close(sockfd);
	
		return ret;
	}

	std::string EzRemoteServerVersion()
	{
		CHTTPClient::HttpResponse res;
        CHTTPClient::HeadersMap headers;
        CHTTPClient tmp_client([](const std::string& log){});
        tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
        tmp_client.SetCertificateFile(CACERT_FILE);
		tmp_client.SetTimeout(1);

		std::string version_url = std::string("http://localhost:") + std::to_string(http_int_server_port) + "/version";
        if (tmp_client.Get(version_url, headers, res))
        {
			if (HTTP_SUCCESS(res.iCode))
            {
				std::string version = std::string(res.strBody.data(), res.strBody.size());
				return version;
			}
		}

		return "";
	}

	int StartEzRemoteServer()
	{
		char buffer[8192];
		in_addr_t in_addr;
		in_addr_t server_addr;
		int filefd;
		int sockfd;
		ssize_t i;
		ssize_t read_return;
		struct hostent *hostent;
		struct sockaddr_in sockaddr_in;
		unsigned short server_port = 9021;
	
		if (!EzRemoteServerVersion().empty())
			return 0;

		filefd = open(SERVER_ELF_PATH, O_RDONLY);
		if (filefd == -1)
		{
			return -1;
		}
	
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd == -1)
		{
			return -1;
		}
	
		/* Prepare sockaddr_in. */
		hostent = gethostbyname("127.0.0.1");
		if (hostent == NULL)
		{
			return -1;
		}
	
		in_addr = inet_addr(inet_ntoa(*(struct in_addr *)*(hostent->h_addr_list)));
		if (in_addr == (in_addr_t)-1)
		{
			return -1;
		}
	
		sockaddr_in.sin_addr.s_addr = in_addr;
		sockaddr_in.sin_family = AF_INET;
		sockaddr_in.sin_port = htons(server_port);
		/* Do the actual connection. */
		if (connect(sockfd, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in)) == -1)
		{
			return -1;
		}
	
		while (1)
		{
			read_return = read(filefd, buffer, 8192);
			if (read_return == 0)
				break;
			if (read_return == -1)
			{
				return -1;
			}
			if (write(sockfd, buffer, read_return) == -1)
			{
				return -1;
			}
		}
	
		close(filefd);
		close(sockfd);

		return 0;
	}

    RemoteClient *GetRemoteClient(int site_idx)
    {
        RemoteClient *tmp_client = nullptr;
        RemoteSettings *tmp_settings = &site_settings[sites[site_idx]];

		return GetRemoteClient(tmp_settings);
    }

	RemoteClient *GetRemoteClient(RemoteSettings *settings)
	{
		RemoteClient *tmp_client = nullptr;;

        if (settings->type == CLIENT_TYPE_HTTP_SERVER)
        {
            if (strcmp(remote_settings->http_server_type, HTTP_SERVER_APACHE) == 0)
                tmp_client = new ApacheClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_MS_IIS) == 0)
                tmp_client = new IISClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_NGINX) == 0)
                tmp_client = new NginxClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_NPX_SERVE) == 0)
                tmp_client = new NpxServeClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_RCLONE) == 0)
                tmp_client = new RCloneClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_ARCHIVEORG) == 0)
                tmp_client = new ArchiveOrgClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_GITHUB) == 0)
                tmp_client = new GithubClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_MYRIENT) == 0)
                tmp_client = new MyrientClient();
        }
        else if (settings->type == CLIENT_TYPE_WEBDAV)
        {
            tmp_client = new WebDAVClient();
        }
        else if (settings->type == CLIENT_TYPE_SMB)
        {
            tmp_client = new SmbClient();
        }
        else if (settings->type == CLIENT_TYPE_SFTP)
        {
            tmp_client = new SFTPClient();
        }
        else if (settings->type == CLIENT_TYPE_FTP)
        {
            tmp_client = new FtpClient();
            FtpClient *ftp_client = (FtpClient*) tmp_client;
            ftp_client->SetCallbackXferFunction(FtpCallback);
        }
        else if (settings->type == CLIENT_TYPE_NFS)
        {
            tmp_client = new NfsClient();
        }

        tmp_client->Connect(settings->server, settings->username, settings->password, false);

        return tmp_client;
	}
}
