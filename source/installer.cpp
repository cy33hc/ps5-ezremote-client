#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include "clients/webdav.h"
#include "clients/remote_client.h"
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
#include "dbglogger.h"

struct BgProgressCheck
{
	ArchivePkgInstallData *archive_pkg_data;
	SplitPkgInstallData *split_pkg_data;
	content_id_t content_id;
	std::string hash;
};

static bool sceAppInst_done = false;

static std::map<std::string, ArchivePkgInstallData *> archive_pkg_install_data_list;
static std::map<std::string, SplitPkgInstallData *> split_pkg_install_data_list;

namespace INSTALLER
{
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

	std::string getRemoteUrl(const std::string path, bool encodeUrl)
	{
		if (strlen(remote_settings->username) == 0 && strlen(remote_settings->password) == 0 &&
			(remoteclient->clientType() == CLIENT_TYPE_WEBDAV || remoteclient->clientType() == CLIENT_TYPE_HTTP_SERVER))
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
			std::string encoded_path = httplib::detail::encode_url(path);
			std::string encoded_site_name = httplib::detail::encode_url(remote_settings->site_name);
			std::string full_url = std::string("http://localhost:") + std::to_string(http_server_port) + "/rmt_inst/" + encoded_site_name + encoded_path;
			return full_url;
		}

		return "";
	}

	void *CheckBgInstallTaskThread(void *argp)
	{
		bool completed = false;
		BgProgressCheck *bg_check_data = (BgProgressCheck *)argp;
		int ret;

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
			bg_check_data->archive_pkg_data->stop_write_thread = true;
			pthread_join(bg_check_data->archive_pkg_data->thread, NULL);
			delete (bg_check_data->archive_pkg_data->split_file);
			free(bg_check_data->archive_pkg_data);
			RemoveArchivePkgInstallData(bg_check_data->hash);
			free(bg_check_data);
		}
		else if (bg_check_data->split_pkg_data != nullptr)
		{
			bg_check_data->split_pkg_data->stop_write_thread = true;
			bg_check_data->split_pkg_data->split_file->Close();
			pthread_join(bg_check_data->split_pkg_data->thread, NULL);
			delete (bg_check_data->split_pkg_data->split_file);
			if (bg_check_data->split_pkg_data->delete_client)
				delete (bg_check_data->split_pkg_data->remote_client);
			free(bg_check_data->split_pkg_data);
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

	int InstallRemotePkg(const std::string &url, pkg_header *header, std::string title, bool prompt)
	{
		if (url.empty())
			return 0;

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

		std::string cid = std::string((char *)header->pkg_content_id);
		cid = cid.substr(cid.find_first_of("-") + 1, 9);
		std::string display_title = title.length() > 0 ? title : cid;

		MetaInfo metainfo = (MetaInfo){
			.uri = url.c_str(),
			.ex_uri = "",
			.playgo_scenario_id = "",
			.content_id = "",
			.content_name = display_title.c_str(),
			.icon_url = ""
		};

		ret = sceAppInstUtilInstallByPackage(&metainfo, &pkg_info, &playgo_info);
		if (ret != 0)
		{
			return 0;
		}
		Util::Notify("%s queued", display_title.c_str());

		if (prompt)
		{
			file_transfering = true;
			sprintf(activity_message, "%s", lang_strings[STR_WAIT_FOR_INSTALL_MSG]);
			bytes_to_download = header->pkg_content_size;
			bytes_transfered = 0;
			prev_tick = Util::GetTick();
	
			SceAppInstallStatusInstalled progress_info;
			while (strcmp(progress_info.status, "playable") != 0 && strcmp(progress_info.status, "none") != 0 )
			{
				ret = sceAppInstUtilGetInstallStatus((const char *)header->pkg_content_id, &progress_info);
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
			bg_check_data->archive_pkg_data = nullptr;
			bg_check_data->split_pkg_data = nullptr;
			snprintf(bg_check_data->content_id, sizeof(bg_check_data->content_id), "%s", header->pkg_content_id);
			bg_check_data->hash = "";
			ret = pthread_create(&bk_install_thid, NULL, CheckBgInstallTaskThread, bg_check_data);
			return 1;
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

		if (BE32(header.pkg_magic) != PS4_PKG_MAGIC && BE32(header.pkg_magic) != PS5_PKG_MAGIC)
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

		ret = sceAppInstUtilInstallByPackage(&metainfo, &pkg_info, &playgo_info);
		if (ret != 0)
		{
			goto err;
		}

		if (!remove_after_install)
		{
			Util::Notify("%s queued", (char *)header->pkg_content_id);
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

		std::string cid = std::string((char *)header.pkg_content_id);
		cid = cid.substr(cid.find_first_of("-") + 1, 9);
		std::string display_title = cid;

		std::string hash = Util::UrlHash(path);
		std::string full_url = std::string("http://localhost:") + std::to_string(http_server_port) + "/archive_inst/" + hash;
		AddArchivePkgInstallData(hash, pkg_data);

		MetaInfo metainfo = (MetaInfo){
			.uri = full_url.c_str(),
			.ex_uri = "",
			.playgo_scenario_id = "",
			.content_id = "",
			.content_name = display_title.c_str(),
			.icon_url = ""
		};

		ret = sceAppInstUtilInstallByPackage(&metainfo, &pkg_info, &playgo_info);
		if (ret)
		{
			ret = 0;
			goto finish;
		}

		Util::Notify("%s queued", display_title.c_str());

		if (!bg)
		{
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
			snprintf(bg_check_data->content_id, sizeof(bg_check_data->content_id), "%s", (char *)header.pkg_content_id);
			bg_check_data->hash = hash;
			ret = pthread_create(&bk_install_thid, NULL, CheckBgInstallTaskThread, bg_check_data);
			return 1;
		}
		ret = 1;
	finish:
		pkg_data->stop_write_thread = true;
		pthread_join(pkg_data->thread, NULL);
		delete (pkg_data->split_file);
		free(pkg_data);
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

		ret = sceAppInstUtilInstallByPackage(&metainfo, &pkg_info, &playgo_info);
		if (ret)
		{
			ret = 0;
			goto finish;
		}

		Util::Notify("%s queued", display_title.c_str());

		if (!bg)
		{
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
			snprintf(bg_check_data->content_id, sizeof(bg_check_data->content_id), "%s", (char *)header.pkg_content_id);
			bg_check_data->hash = hash;
			ret = pthread_create(&bk_install_thid, NULL, CheckBgInstallTaskThread, bg_check_data);
			return 1;
		}
		ret = 1;
	finish:
		pkg_data->stop_write_thread = true;
		pkg_data->split_file->Close();
		pthread_join(pkg_data->thread, NULL);
		usleep(2000000);
		delete (pkg_data->split_file);
		if (pkg_data->delete_client)
		{
			delete (pkg_data->remote_client);
		}
		free(pkg_data);
		RemoveSplitPkgInstallData(hash);
		activity_inprogess = false;
		file_transfering = false;
		Windows::SetModalMode(false);
		return ret;
	}
	
}
