// #include <orbis/SystemService.h>
#include "string.h"
#include "stdio.h"
#include "config.h"
#include "util.h"
#include "lang.h"

char lang_identifiers[LANG_STRINGS_NUM][LANG_ID_SIZE] = {
	FOREACH_STR(GET_STRING)};

// This is properly populated so that emulator won't crash if an user launches it without language INI files.
char lang_strings[LANG_STRINGS_NUM][LANG_STR_SIZE] = {
	"Connection Settings",																			  // STR_CONNECTION_SETTINGS
	"Site",																							  // STR_SITE
	"Local",																						  // STR_LOCAL
	"Remote",																						  // STR_REMOTE
	"Messages",																						  // STR_MESSAGES
	"Update Software",																				  // STR_UPDATE_SOFTWARE
	"Connect",																						  // STR_CONNECT
	"Disconnect",																					  // STR_DISCONNECT
	"Search",																						  // STR_SEARCH
	"Refresh",																						  // STR_REFRESH
	"Server",																						  // STR_SERVER
	"Username",																						  // STR_USERNAME
	"Password",																						  // STR_PASSWORD
	"Port",																							  // STR_PORT
	"Pasv",																							  // STR_PASV
	"Directory",																					  // STR_DIRECTORY
	"Filter",																						  // STR_FILTER
	"Yes",																							  // STR_YES
	"No",																							  // STR_NO
	"Cancel",																						  // STR_CANCEL
	"Continue",																						  // STR_CONTINUE
	"Close",																						  // STR_CLOSE
	"Folder",																						  // STR_FOLDER
	"File",																							  // STR_FILE
	"Type",																							  // STR_TYPE
	"Name",																							  // STR_NAME
	"Size",																							  // STR_SIZE
	"Date",																							  // STR_DATE
	"New Folder",																					  // STR_NEW_FOLDER
	"Rename",																						  // STR_RENAME
	"Delete",																						  // STR_DELETE
	"Upload",																						  // STR_UPLOAD
	"Download",																						  // STR_DOWNLOAD
	"Select All",																					  // STR_SELECT_ALL
	"Clear All",																					  // STR_CLEAR_ALL
	"Uploading",																					  // STR_UPLOADING
	"Downloading",																					  // STR_DOWNLOADING
	"Overwrite",																					  // STR_OVERWRITE
	"Don't Overwrite",																				  // STR_DONT_OVERWRITE
	"Ask for Confirmation",																			  // STR_ASK_FOR_CONFIRM
	"Don't Ask for Confirmation",																	  // STR_DONT_ASK_CONFIRM
	"Always use this option and don't ask again",													  // STR_ALLWAYS_USE_OPTION
	"Actions",																						  // STR_ACTIONS
	"Confirm",																						  // STR_CONFIRM
	"Overwrite Options",																			  // STR_OVERWRITE_OPTIONS
	"Properties",																					  // STR_PROPERTIES
	"Progress",																						  // STR_PROGRESS
	"Updates",																						  // STR_UPDATES
	"Are you sure you want to delete this file(s)/folder(s)?",										  // STR_DEL_CONFIRM_MSG
	"Canceling. Waiting for last action to complete",												  // STR_CANCEL_ACTION_MSG
	"Failed to upload file",																		  // STR_FAIL_UPLOAD_MSG
	"Failed to download file",																		  // STR_FAIL_DOWNLOAD_MSG
	"Failed to read contents of directory or folder does not exist.",								  // STR_FAIL_READ_LOCAL_DIR_MSG
	"426 Connection closed.",																		  // STR_CONNECTION_CLOSE_ERR_MSG
	"426 Remote Server has terminated the connection.",												  // STR_REMOTE_TERM_CONN_MSG
	"300 Failed Login. Please check your username or password.",									  // STR_FAIL_LOGIN_MSG
	"426 Failed. Connection timeout.",																  // STR_FAIL_TIMEOUT_MSG
	"Failed to delete directory",																	  // STR_FAIL_DEL_DIR_MSG
	"Deleting",																						  // STR_DELETING
	"Failed to delete file",																		  // STR_FAIL_DEL_FILE_MSG
	"Deleted",																						  // STR_DELETED
	"Link",																							  // STR_LINK
	"Share",																						  // STR_SHARE
	"310 Failed",																					  // STR_FAILED
	"310 Failed to create file on local",															  // STR_FAIL_CREATE_LOCAL_FILE_MSG
	"Install",																						  // STR_INSTALL
	"Installing",																					  // STR_INSTALLING
	"Success",																						  // STR_INSTALL_SUCCESS
	"Failed",																						  // STR_INSTALL_FAILED
	"Skipped",																						  // STR_INSTALL_SKIPPED
	"Checking connection to remote HTTP Server",													  // STR_CHECK_HTTP_MSG
	"Failed connecting to HTTP Server",																  // STR_FAILED_HTTP_CHECK
	"Remote is not a HTTP Server",																	  // STR_REMOTE_NOT_HTTP
	"Package not in the /data or /mnt/usbX folder",													  // STR_INSTALL_FROM_DATA_MSG
	"Package is already installed",																	  // STR_ALREADY_INSTALLED_MSG
	"Install from URL",																				  // STR_INSTALL_FROM_URL
	"Could not read package header info",															  // STR_CANNOT_READ_PKG_HDR_MSG
	"Favorite URLs",																				  // STR_FAVORITE_URLS
	"Slot",																							  // STR_SLOT
	"Edit",																							  // STR_EDIT
	"One Time Url",																					  // STR_ONETIME_URL
	"Not a valid Package",																			  // STR_NOT_A_VALID_PACKAGE
	"Waiting for Package to finish installing",														  // STR_WAIT_FOR_INSTALL_MSG
	"Failed to install pkg file. Please delete the tmp pkg manually",								  // STR_FAIL_INSTALL_TMP_PKG_MSG
	"Failed to obtain download URL",																  // STR_FAIL_TO_OBTAIN_GG_DL_MSG
	"Auto delete temporary downloaded pkg file after install",										  // STR_AUTO_DELETE_TMP_PKG
	"Protocol not supported",																		  // STR_PROTOCOL_NOT_SUPPORTED
	"Could not resolve hostname",																	  // STR_COULD_NOT_RESOLVE_HOST
	"Extract",																						  // STR_EXTRACT
	"Extracting",																					  // STR_EXTRACTING
	"Failed to extract",																			  // STR_FAILED_TO_EXTRACT
	"Extract Location",																				  // STR_EXTRACT_LOCATION
	"Compress",																						  // STR_COMPRESS
	"Zip Filename",																					  // STR_ZIP_FILE_PATH
	"Compressing",																					  // STR_COMPRESSING
	"Error occured while creating zip",																  // STR_ERROR_CREATE_ZIP
	"Unsupported compressed file format",															  // STR_UNSUPPORTED_FILE_FORMAT
	"Cut",																							  // STR_CUT
	"Copy",																							  // STR_COPY
	"Paste",																						  // STR_PASTE
	"Moving",																						  // STR_MOVING
	"Copying",																						  // STR_COPYING
	"Failed to move file",																			  // STR_FAIL_MOVE_MSG
	"Failed to copy file",																			  // STR_FAIL_COPY_MSG
	"Cannot move parent directory to sub subdirectory",												  // STR_CANT_MOVE_TO_SUBDIR_MSG
	"Cannot copy parent directory to sub subdirectory",												  // STR_CANT_COPY_TO_SUBDIR_MSG
	"Operation not supported",																		  // STR_UNSUPPORTED_OPERATION_MSG
	"Http Port",																					  // STR_HTTP_PORT
	"The content has already been installed. Do you want to continue installing",					  // STR_REINSTALL_CONFIRM_MSG
	"Remote package installation is not supported for protected servers.",							  // STR_REMOTE_NOT_SUPPORT_MSG
	"Remote HTTP Server not reachable.",															  // STR_CANNOT_CONNECT_REMOTE_MSG
	"Remote Package Install not possible. Would you like to download package and install?",			  // STR_DOWNLOAD_INSTALL_MSG
	"Checking remote server for Remote Package Install.",											  // STR_CHECKING_REMOTE_SERVER_MSG
	"RPI",																							  // STR_ENABLE_RPI
	"This option enables Remote Package Installation.",												  // STR_ENABLE_RPI_FTP_SMB_MSG
	"This option enables Remote Package Installation.",												  // STR_ENABLE_RPI_WEBDAV_MSG
	"Files",																						  // STR_FILES
	"Editor",																						  // STR_EDITOR
	"Save",																							  // STR_SAVE
	"Cannot edit files bigger than",																  // STR_MAX_EDIT_FILE_SIZE_MSG
	"Delete Selected Line",																			  // STR_DELETE_LINE
	"Insert Below Selected Line",																	  // STR_INSERT_LINE
	"Modified",																						  // STR_MODIFIED
	"Failed to obtain an access token from",														  // STR_FAIL_GET_TOKEN_MSG
	"Login Success. You may close the browser and return to the application",						  // STR_GET_TOKEN_SUCCESS_MSG
	"See, edit, create, and delete all of your Google Drive files",									  // STR_PERM_DRIVE
	"See, create, and delete its own configuration data in your Google Drive",						  // STR_PERM_DRIVE_APPDATA
	"See, edit, create, and delete only the specific Google Drive files you use with this app",		  // STR_PERM_DRIVE_FILE
	"View and manage metadata of files in your Google Drive",										  // STR_PERM_DRIVE_METADATA
	"See information about your Google Drive files",												  // STR_PERM_DRIVE_METADATA_RO
	"Google login failed",																			  // STR_GOOGLE_LOGIN_FAIL_MSG
	"Google login timed out",																		  // STR_GOOGLE_LOGIN_TIMEOUT_MSG
	"New File",																						  // STR_NEW_FILE
	"Settings",																						  // STR_SETTINGS
	"Client ID",																					  // STR_CLIENT_ID
	"Client Secret",																				  // STR_CLIENT_SECRET
	"Global",																						  // STR_GLOBAL
	"Google",																						  // STR_GOOGLE
	"Copy selected line",																			  // STR_COPY_LINE
	"Paste into selected line",																		  // STR_PASTE_LINE
	"Show hidden files",																			  // STR_SHOW_HIDDEN_FILES
	"Set Default Folder",																			  // STR_SET_DEFAULT_DIRECTORY
	"has being set as default direcotry",															  // STR_SET_DEFAULT_DIRECTORY_MSG
	"View Image",																					  // STR_VIEW_IMAGE
	"Package Information",																			  // STR_VIEW_PKG_INFO
	"NFS export path missing in URL",																  // STR_NFS_EXP_PATH_MISSING_MSG
	"Failed to init NFS context",																	  // STR_FAIL_INIT_NFS_CONTEXT
	"Failed to mount NFS share",																	  // STR_FAIL_MOUNT_NFS_MSG
	"Web Server",																					  // STR_WEB_SERVER
	"Enable",																						  // STR_ENABLE
	"Compressed Files Location",																	  // STR_COMPRESSED_FILE_PATH
	"Location of where compressed files are stored on the web server",								  // STR_COMPRESSED_FILE_PATH_MSG
	"AllDebrid",																					  // STR_ALLDEBRID
	"API Key",																						  // STR_API_KEY
	"Couldn't extract download url",																  // STR_CANT_EXTRACT_URL_MSG
	"Failed to install from URL",																	  // STR_FAIL_INSTALL_FROM_URL_MSG
	"InValid URL",																					  // STR_INVALID_URL
	"To use this function, an API Key needs to be configured in the ezRemote Client settings",		  // STR_ALLDEBRID_API_KEY_MISSING_MSG
	"Language",																						  // STR_LANGUAGE
	"Temp Directory",																				  // STR_TEMP_DIRECTORY
	"Real-Debrid",																					  // STR_REALDEBRID
	"Package install is running in the background. Don't close the app while install is in progress", // STR_BACKGROUND_INSTALL_INPROGRESS
	"Enable disk caching. Can improve package install speed in cases where connection to remote is slow, but breaks resuming of install",          // STR_ENABLE_DISC_CACHE_MSG
	"DC",                                                                                             // STR_ENABLE_DISK_CACHE
	"Install Via AllDebrid",                                                                          // STR_ENABLE_ALLDEBRID_MSG
	"Install Via RealDebrid",                                                                         // STR_ENABLE_REALDEBRID_MSG
	"Enable Disk Cache",                                                                              // STR_ENABLE_DISKCACHE_DESC
	"Cannot perform operation while activity is in progress",                                         // STR_ACTIVITY_IN_PROGRESS_MSG
	"ezRemote Direct Package Installer payload is not loaded",                                        // STR_ETAHEN_DPI_ERROR_MSG
};

bool needs_extended_font = false;

namespace Lang
{
	void SetTranslation(int32_t lang_idx)
	{
		char langFile[LANG_STR_SIZE * 2];
		char identifier[LANG_ID_SIZE], buffer[LANG_STR_SIZE];

		std::string lang = std::string(language);
		lang = Util::Trim(lang, " ");
		if (lang.size() > 0 && lang.compare("Default") != 0)
		{
			sprintf(langFile, "/data/homebrew/ezremote-client/assets/langs/%s.ini", lang.c_str());
		}
		/* else
		{
			switch (lang_idx)
			{
			case ORBIS_SYSTEM_PARAM_LANG_ITALIAN:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Italiano.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_SPANISH:
			case ORBIS_SYSTEM_PARAM_LANG_SPANISH_LA:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Spanish.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_GERMAN:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/German.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_PORTUGUESE_PT:
			case ORBIS_SYSTEM_PARAM_LANG_PORTUGUESE_BR:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Portuguese_BR.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_RUSSIAN:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Russian.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_DUTCH:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Dutch.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_FRENCH:
			case ORBIS_SYSTEM_PARAM_LANG_FRENCH_CA:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/French.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_POLISH:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Polish.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_JAPANESE:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Japanese.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_KOREAN:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Korean.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_CHINESE_S:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Simplified Chinese.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_CHINESE_T:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Traditional Chinese.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_INDONESIAN:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Indonesian.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_HUNGARIAN:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Hungarian.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_GREEK:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Greek.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_VIETNAMESE:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Vietnamese.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_TURKISH:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Turkish.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_ARABIC:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Arabic.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_ROMANIAN:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Romanian.ini");
				break;
			case ORBIS_SYSTEM_PARAM_LANG_NORWEGIAN:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/Norwegian.ini");
				break;
			default:
				sprintf(langFile, "%s", "/data/homebrew/ezremote-client/assets/langs/English.ini");
				break;
			}
		} */

		FILE *config = fopen(langFile, "r");
		if (config)
		{
			while (EOF != fscanf(config, "%[^=]=%[^\n]\n", identifier, buffer))
			{
				for (int i = 0; i < LANG_STRINGS_NUM; i++)
				{
					if (strcmp(lang_identifiers[i], identifier) == 0)
					{
						char *newline = nullptr, *p = buffer;
						while ((newline = strstr(p, "\\n")) != NULL)
						{
							newline[0] = '\n';
							int len = strlen(&newline[2]);
							memmove(&newline[1], &newline[2], len);
							newline[len + 1] = 0;
							p++;
						}
						strcpy(lang_strings[i], buffer);
					}
				}
			}
			fclose(config);
		}

		char buf[12];
		int num;
		sscanf(last_site, "%[^ ] %d", buf, &num);
		sprintf(display_site, "%s %d", lang_strings[STR_SITE], num);
	}
}