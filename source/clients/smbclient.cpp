#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <cstring>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include "fs.h"
#include "lang.h"
#include "clients/smbclient.h"
#include "windows.h"
#include "util.h"

SmbClient::SmbClient()
{
}

SmbClient::~SmbClient()
{
}

int SmbClient::Connect(const std::string &url, const std::string &user, const std::string &pass, bool send_ping)
{
	struct smb2_url *smb_url;

	smb2 = smb2_init_context();
	if (smb2 == NULL)
	{
		sprintf(response, "Failed to init SMB context");
		return 0;
	}

	smb_url = smb2_parse_url(smb2, url.c_str());
	if (smb_url == NULL || smb_url->share == NULL || strlen(smb_url->share) == 0)
	{
		sprintf(response, "Invalid SMB Url");
		return 0;
	}

	if (pass.length() > 0)
		smb2_set_password(smb2, pass.c_str());
	smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
	smb2_set_version(smb2, SMB2_VERSION_ANY);
	smb2_set_timeout(smb2, 30);

	if (smb2_connect_share(smb2, smb_url->server, smb_url->share, user.c_str()) < 0)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	smb2_destroy_url(smb_url);
	//max_read_size = smb2_get_max_read_size(smb2);
	//max_write_size = smb2_get_max_write_size(smb2);
	connected = true;

	return 1;
}

/*
 * SmbLastResponse - return a pointer to the last response received
 */
const char *SmbClient::LastResponse()
{
	return (const char *)response;
}

/*
 * IsConnected - return true if connected to remote
 */
bool SmbClient::IsConnected()
{
	return connected;
}

/*
 * Ping - return true if connected to remote
 */
bool SmbClient::Ping()
{
	connected = smb2_echo(smb2) == 0;
	return connected;
}

/*
 * SmbQuit - disconnect from remote
 *
 * return 1 if successful, 0 otherwise
 */
int SmbClient::Quit()
{
	smb2_destroy_context(smb2);
	smb2 = NULL;
	connected = false;
	return 1;
}

/*
 * SmbMkdir - create a directory at server
 *
 * return 1 if successful, 0 otherwise
 */
int SmbClient::Mkdir(const std::string &ppath)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	if (smb2_mkdir(smb2, path.c_str()) != 0)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}
	return 1;
}

/*
 * SmbRmdir - remove directory and all files under directory at remote
 *
 * return 1 if successful, 0 otherwise
 */
int SmbClient::_Rmdir(const std::string &ppath)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	if (smb2_rmdir(smb2, path.c_str()) != 0)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}
	return 1;
}

/*
 * SmbRmdir - remove directory and all files under directory at remote
 *
 * return 1 if successful, 0 otherwise
 */
int SmbClient::Rmdir(const std::string &path, bool recursive)
{
	if (stop_activity)
		return 1;

	std::vector<DirEntry> list = ListDir(path);
	int ret;
	for (int i = 0; i < list.size(); i++)
	{
		if (stop_activity)
			return 1;

		if (list[i].isDir && recursive)
		{
			if (strcmp(list[i].name, "..") == 0)
				continue;
			ret = Rmdir(list[i].path, recursive);
			if (ret == 0)
			{
				sprintf(status_message, "%s %s", lang_strings[STR_FAIL_DEL_DIR_MSG], list[i].path);
				return 0;
			}
		}
		else
		{
			sprintf(activity_message, "%s %s\n", lang_strings[STR_DELETING], list[i].path);
			ret = Delete(list[i].path);
			if (ret == 0)
			{
				sprintf(status_message, "%s %s", lang_strings[STR_FAIL_DEL_FILE_MSG], list[i].path);
				return 0;
			}
		}
	}
	ret = _Rmdir(path);
	if (ret == 0)
	{
		sprintf(status_message, "%s %s", lang_strings[STR_FAIL_DEL_DIR_MSG], path.c_str());
		return 0;
	}

	return 1;
}

/*
 * SmbGet - issue a GET command and write received data to output
 *
 * return 1 if successful, 0 otherwise
 */

int SmbClient::Get(const std::string &outputfile, const std::string &ppath, uint64_t offset)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	if (!Size(path.c_str(), &bytes_to_download))
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	struct smb2fh* in = smb2_open(smb2, path.c_str(), O_RDONLY);
	if (in == NULL)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	FILE* out = FS::Create(outputfile);
	if (out == NULL)
	{
		sprintf(response, "%s", lang_strings[STR_FAILED]);
		return 0;
	}

	uint8_t *buff = (uint8_t*)malloc(max_read_size);
	int count = 0;
	bytes_transfered = 0;
	prev_tick = Util::GetTick();
	while ((count = smb2_read(smb2, in, buff, max_read_size)) > 0)
	{
		if (count < 0)
		{
			sprintf(response, "%s", smb2_get_error(smb2));
			FS::Close(out);
			smb2_close(smb2, in);
			free((void*)buff);
			return 0;
		}
		FS::Write(out, buff, count);
		bytes_transfered += count;
	}
	FS::Close(out);
	smb2_close(smb2, in);
	free((void*)buff);
	return 1;
}

int SmbClient::Get(SplitFile *split_file, const std::string &ppath, uint64_t offset)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");

	struct smb2fh *in = smb2_open(smb2, path.c_str(), O_RDONLY);
	if (in == NULL)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	uint8_t *buff = (uint8_t *)malloc(max_read_size);
	int count = 0;

	while ((count = smb2_read(smb2, in, buff, max_read_size)) > 0)
	{
		if (count < 0)
		{
			sprintf(response, "%s", smb2_get_error(smb2));
			smb2_close(smb2, in);
			free((void *)buff);
			return 0;
		}
		if (split_file->Write((char*)buff, count) < 0)
		{
			smb2_close(smb2, in);
			free((void *)buff);
			return 0;
		}
	}

	smb2_close(smb2, in);
	free((void *)buff);
	return 1;
}

int SmbClient::GetRange(const std::string &ppath, DataSink &sink, uint64_t size, uint64_t offset)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	struct smb2fh *in = smb2_open(smb2, path.c_str(), O_RDONLY);
	if (in == NULL)
	{
		return 0;
	}

	int ret = this->GetRange((void *)in, sink, size, offset);
	smb2_close(smb2, in);

	return ret;
}

int SmbClient::GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset)
{
	struct smb2fh *in = (struct smb2fh *)fp;

	smb2_lseek(smb2, in, offset, SEEK_SET, NULL);

	uint8_t *buff = (uint8_t *)malloc(max_read_size);
	int count = 0;
	size_t bytes_remaining = size;
	do
	{
		size_t bytes_to_read = std::min<size_t>(max_read_size, bytes_remaining);
		count = smb2_read(smb2, in, buff, bytes_to_read);
		if (count > 0)
		{
			bytes_remaining -= count;
			bool ok = sink.write((char *)buff, count);
			if (!ok)
			{
				free((uint8_t *)buff);
				return 0;
			}
		}
		else
		{
			break;
		}
	} while (1);

	free((char *)buff);

	return 1;
}


int SmbClient::GetRange(const std::string &ppath, void *buffer, uint64_t size, uint64_t offset)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	uint64_t filesize;
	if (!Size(path.c_str(), &filesize))
	{
		return 0;
	}

	struct smb2fh *in = smb2_open(smb2, path.c_str(), O_RDONLY);
	if (in == NULL)
	{
		return 0;
	}

	int ret = this->GetRange(in, buffer, size, offset);
	smb2_close(smb2, in);

	return ret;
}

int SmbClient::GetRange(void *fp, void *buffer, uint64_t size, uint64_t offset)
{
	struct smb2fh *in = (struct smb2fh *)fp;

	smb2_lseek(smb2, in, offset, SEEK_SET, NULL);

	size_t bytes_remaining = size;
	uint8_t *buff = (uint8_t*)buffer;
	int count = 0;
	int total = 0;
	do
	{
		count = smb2_read(smb2, in, buff, bytes_remaining);
		if (count > 0)
		{
			bytes_remaining -= count;
			buff += count;
			total += count;
		}
		else
		{
			break;
		}
	} while (1);
	
	if (total != size)
		return 0;
	return 1;
}

int SmbClient::Copy(const std::string &ffrom, const std::string &tto)
{
	sprintf(response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
	return 0;
}

int SmbClient::Move(const std::string &ffrom, const std::string &tto)
{
	sprintf(response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
	return 0;
}

bool SmbClient::FileExists(const std::string &ppath)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	smb2_stat_64 st;
	int ret = smb2_stat(smb2, path.c_str(), &st);
	if (ret != 0)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return false;
	}

	return true;
}

/*
 * SmbPut - issue a PUT command and send data from input
 *
 * return 1 if successful, 0 otherwise
 */
int SmbClient::Put(const std::string &inputfile, const std::string &ppath, uint64_t offset)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");

	bytes_to_download = FS::GetSize(inputfile);
	if (bytes_to_download < 0)
	{
		sprintf(response, "%s", lang_strings[STR_FAILED]);
		return 0;
	}

	FILE* in = FS::OpenRead(inputfile);
	if (in == NULL)
	{
		sprintf(response, "%s", lang_strings[STR_FAILED]);
		return 0;
	}
	
	struct smb2fh* out = smb2_open(smb2, path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
	if (out == NULL)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	uint8_t* buff = (uint8_t*)malloc(max_write_size);
	int count = 0;
	bytes_transfered = 0;
	prev_tick = Util::GetTick();
	while ((count = FS::Read(in, buff, max_write_size)) > 0)
	{
		if (count < 0)
		{
			sprintf(response, "%s", lang_strings[STR_FAILED]);
			FS::Close(in);
			smb2_close(smb2, out);
			free(buff);
			return 0;
		}
		smb2_write(smb2, out, buff, count);
		bytes_transfered += count;
	}
	FS::Close(in);
	smb2_close(smb2, out);
	free(buff);

	return 1;

}

int SmbClient::Rename(const std::string &src, const std::string &dst)
{
	std::string path1 = std::string(src);
	std::string path2 = std::string(dst);
	path1 = Util::Trim(path1, "/");
	path2 = Util::Trim(path2, "/");
	if (smb2_rename(smb2, path1.c_str(), path2.c_str()) != 0)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	return 1;
}

int SmbClient::Delete(const std::string &ppath)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	if (smb2_unlink(smb2, path.c_str()) != 0)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	return 1;
}

int SmbClient::Size(const std::string &ppath, uint64_t *size)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	smb2_stat_64 st;
	if (smb2_stat(smb2, path.c_str(), &st) != 0)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}
	*size = st.smb2_size;

	return 1;
}

std::vector<DirEntry> SmbClient::ListDir(const std::string &path)
{
	std::vector<DirEntry> out;
	DirEntry entry;
	Util::SetupPreviousFolder(path, &entry);
	out.push_back(entry);

	struct smb2dir *dir;
	struct smb2dirent *ent;

	std::string ppath = std::string(path);
	dir = smb2_opendir(smb2, Util::Ltrim(ppath, "/").c_str());
	if (dir == NULL)
	{
		sprintf(status_message, "%s - %s", lang_strings[STR_FAIL_READ_LOCAL_DIR_MSG], smb2_get_error(smb2));
		return out;
	}

	while ((ent = smb2_readdir(smb2, dir)))
	{
		DirEntry entry;
		memset(&entry, 0, sizeof(entry));

		entry.selectable = true;
		snprintf(entry.directory, 511, "%s", path.c_str());
		snprintf(entry.name, 255, "%s", ent->name);
		entry.file_size = ent->st.smb2_size;
		if (path.length() > 0 && path[path.length() - 1] == '/')
		{
			sprintf(entry.path, "%s%s", path.c_str(), ent->name);
		}
		else
		{
			sprintf(entry.path, "%s/%s", path.c_str(), ent->name);
		}

		time_t t = (time_t)ent->st.smb2_mtime;
		struct tm tm = *localtime(&t);
		entry.modified.day = tm.tm_mday;
		entry.modified.month = tm.tm_mon + 1;
		entry.modified.year = tm.tm_year + 1900;
		entry.modified.hours = tm.tm_hour;
		entry.modified.minutes = tm.tm_min;
		entry.modified.seconds = tm.tm_sec;

		switch (ent->st.smb2_type)
		{
		case SMB2_TYPE_LINK:
			entry.isLink = true;
			entry.file_size = 0;
			sprintf(entry.display_size, "%s", lang_strings[STR_LINK]);
			break;
		case SMB2_TYPE_FILE:
			DirEntry::SetDisplaySize(&entry);
			break;
		case SMB2_TYPE_DIRECTORY:
			entry.isDir = true;
			entry.file_size = 0;
			sprintf(entry.display_size, "%s", lang_strings[STR_FOLDER]);
			break;
		}
		if (strcmp(entry.name, "..") != 0 && strcmp(entry.name, ".") != 0)
			out.push_back(entry);
	}
	smb2_closedir(smb2, dir);

	return out;
}

std::string SmbClient::GetPath(std::string ppath1, std::string ppath2)
{
	std::string path1 = ppath1;
	std::string path2 = ppath2;
	path1 = Util::Rtrim(Util::Trim(path1, " "), "/");
	path2 = Util::Rtrim(Util::Trim(path2, " "), "/");
	path1 = path1 + "/" + path2;
	return Util::Ltrim(path1, "/");
}

int SmbClient::Head(const std::string &ppath, void *buffer, uint64_t len)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	if (!Size(path.c_str(), &bytes_to_download))
	{
		return 0;
	}

	struct smb2fh* in = smb2_open(smb2, path.c_str(), O_RDONLY);
	if (in == NULL)
	{
		return 0;
	}

	int count = smb2_read(smb2, in, (uint8_t*)buffer, len);
	smb2_close(smb2, in);
	if (count != len)
		return 0;

	return 1;
}

ClientType SmbClient::clientType()
{
	return CLIENT_TYPE_SMB;
}

uint32_t SmbClient::SupportedActions()
{
	return REMOTE_ACTION_ALL ^ REMOTE_ACTION_CUT ^ REMOTE_ACTION_COPY ^ REMOTE_ACTION_PASTE;
}

void *SmbClient::Open(const std::string &ppath, int flags)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");

	struct smb2fh *in = smb2_open(smb2, path.c_str(), flags);
	return in;
}

void SmbClient::Close(void *fp)
{
	smb2_close(smb2, (struct smb2fh *)fp);
}
