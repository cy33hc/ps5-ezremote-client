# ezRemote Client

ezRemote Client is an application that allows you to connect the PS5 to remote FTP/SFTP, SMB(Windows Share), NFS, WebDAV, HTTP servers to transfer files. The interface is inspired by Filezilla client which provides a commander like GUI.

![Preview](/screenshot.jpg)

![Preview](/ezremote_client_web.png)

## Installation
1. Extract the **ezremote_client.zip** in to the **/data/homebrew** folder on the PS5.
2. Must load websrv payload. You won't tbe able to start ezRemote Client without this.
3. Install PS-ezRemoteClient.pkg (This create a shortcut on the dashboard to launch the app directly via websrv)

## Updating the app
1. Download the latest version from Releases and extract the **ezremote_client.zip** in to the **/data/homebrew** folder on the PS5 replacing everything. This will not overwrite any of your configs.
   
## Know Issues
<s>- Occasionally the app would crash if you cancelled the pkg install. This could leave some temporary files created in the "/data/homebrew/ezremote-client/tmp" folder. It is safe to delete them.</s>

## Usage
To distinguish between FTP, SMB, NFS, WebDAV or HTTP, the URL must be prefix with **ftp://**, **sftp://**, **smb://**, **nfs://**, **webdav://**, **webdavs://**, **http://** and **https://**

 - The url format for FTP is
   ```
   ftp://hostname[:port]
   sftp://hostname[:port]

     - hostname can be the textual hostname or an IP address. hostname is required
     - port is optional and defaults to 21(ftp) and 22(sftp) if not provided
   ```
   For Secure FTP (sftp), use of identity files is possible. Put both the **id_rsa** and **id_rsa.pub** into a folder in the PS5 hard drive. Then in the password field in the UI, instead of putting a password reference the folder where id_rsa and id_rsa.pub is place. Prefix the folder with **"file://"** and **do not** password protect the identity file.
   ```
   Example: If you had placed the id_rsa and id_rsa.pub files into the folder /data/ezremote-client,
   then in the password field enter file:///data/ezremote-client
   ```

 - The url format for SMB(Windows Share) is
   ```
   smb://hostname[:port]/sharename

     - hostname can be the textual hostname or an IP address. hostname is required
     - port is optional and defaults to 445 if not provided
     - sharename is required
   ```

 - The url format for NFS is
   ```
   nfs://hostname[:port]/export_path[?uid=<UID>&gid=<GID>]

     - hostname can be the textual hostname or an IP address. hostname is required
     - port is optional and defaults to 2049 if not provided
     - export_path is required
     - uid is the UID value to use when talking to the server. Defaults to 65534 if not specified.
     - gid is the GID value to use when talking to the server. Defaults to 65534 if not specified.

     Special characters in 'path' are escaped using %-hex-hex syntax.

     For example '?' must be escaped if it occurs in a path as '?' is also used to
     separate the path from the optional list of url arguments.

     Example:
     nfs://192.168.0.1/my?path?uid=1000&gid=1000
     must be escaped as
     nfs://192.168.0.1/my%3Fpath?uid=1000&gid=1000
   ```

 - The url format for WebDAV is
   ```
   webdav://hostname[:port]/[url_path]
   webdavs://hostname[:port]/[url_path]

     - hostname can be the textual hostname or an IP address. hostname is required
     - port is optional and defaults to 80(webdav) and 443(webdavs) if not provided
     - url_path is optional based on your WebDAV hosting requiremets
   ```

- The url format for HTTP Server is
   ```
   http://hostname[:port]/[url_path]
   https://hostname[:port]/[url_path]
     - hostname can be the textual hostname or an IP address. hostname is required
     - port is optional and defaults to 80(http) and 443(https) if not provided
     - url_path is optional based on your HTTP Server hosting requiremets
   ```
- For Internet Archive repos download URLs
  - Only supports parsing of the download URL (ie the URL where you see a list of files). Example
    |      |           |  |
    |----------|-----------|---|
    | ![archive_org_screen1](https://github.com/user-attachments/assets/b129b6cf-b938-4d7c-a3fa-61e1c633c1f6) | ![archive_org_screen2](https://github.com/user-attachments/assets/646106d1-e60b-4b35-b153-3475182df968)| ![image](https://github.com/user-attachments/assets/cad94de8-a694-4ef5-92a8-b87468e30adb) |

- For Myrient repos, entry **https://myrient.erista.me/files** in the server field.
  ![image](https://github.com/user-attachments/assets/b80e2bec-b8cc-4acc-9ab6-7b0dc4ef20e6)

- Support for browse and download  release artifacts from github repos. Under the server just enter the git repo of the homebrew eg https://github.com/cy33hc/ps4-ezremote-client
  ![image](https://github.com/user-attachments/assets/f8e931ea-f1d1-4af8-aed5-b0dfe661a230)

Tested with following WebDAV server:
 - **(Recommeded)** [Dufs](https://github.com/sigoden/dufs) - For hosting your own WebDAV server. (Recommended since this allow anonymous access which is required for Remote Package Install)
 - [SFTPgo](https://github.com/drakkan/sftpgo) - For local hosted WebDAV server. Can also be used as a webdav frontend for Cloud Storage like AWS S3, Azure Blob or Google Storage.
 - box.com (Note: delete folder does not work. This is an issue with box.com and not the app)
 - mega.nz (via the [megacmd tool](https://mega.io/cmd))
 - 4shared.com
 - drivehq.com

## Package Installer Feature
 - Only PS4 pkg's are supported
 - Can install pkg from the PS5 /data folder and subfolders, /mnt/usb folder and subfolders
 - Can install pkg from all the support HTTP Servers (Apache/Microsoft IIS/Ngnix/Serve/RClone/GitHub)
 - Can install pkg from WebDAV/NFS/SMB/SFTP/FTP Servers
 - Can install pkg inside ZIP files from any of the above Remote Servers
   
### Package installation modes
  RPI - means (R)emote (P)ackage (I)Install <br/>
  DC  - means (D)isk (C)ache
   
  - RPI [Disabled] - When RPI is disabled, the PKG is downloaded to the "Temp Directory" on the PS5 first and then installs it. This doubles the PKG file size used on the PS5. But this is the most stable of all modes. By default the temporary download pkg is deleted after install.
  - RPI [Enabled]  -  Installs the PKG directly to the PS5 without copying any content to the disk. Usually the fastest if you are on 1GB ethernet. But the negative is that there could be hundreds or ever thousands of request to the remote server to open the file, read 16MB chunk of content and then closes the file. The is known to cause issues on some NAS storage and slow network like wifi where latency kills.
  - DC [Enabled]   - This option is used in conjuction with RPI. If RPI is disabled, this option has no effect. With this option enabled, the app makes only 1 request to open the file, then read and stores the content of the pkg in 5MB chunks on disk, while in parrellel sending the data to the PS5 installer and deleting the completed temp file chunks until the completed pkg is installed and then pkg is closed. This saves on hundreds of Opening/Closing of the PKG on the remote. The negative is that approximately 100MB - 200MB of disk space is used and because it writes to disk, it's slower. This might help with installing from NAS with RPI enabled and increase speeds on wifi networks.
  
### Methods for increasing speed of install
  - Writing to the PS5's internal SSD seems awfully slow for some reason, if you disable RPI or enabe Disk Cache, try change the "Temp Directory" in the Settings Dialog to an external SSD that is exFAT connected to the USB3 port at the back of the console. For some reason, the speed of writing to the external SSD is faster. Usually the external usb SSD is mounted to /mnt/usb(X).
  - <del>From my personal testing it seems like I get 4x faster network speeds with kstuff compared to etaHen. Tested with the same pkg and same server, each time restarting and loading the payload and run the test. I ran the test 5 times just to make sure.</del>
    <br/>**UPDATE: Disabling "kstuff" in etaHEN 2.1b improves the install performance**
    <br/>**UPDATE May 6, 2025: You get best install performance if you don't load either etaHEN or kstuff**

## Features Native Application
 - Transfer files back and forth between PS5 and FTP/SMB/NFS/WebDAV server
 - Support for connecting to Http Servers like (Apache/Nginx,Microsoft IIS, Serve) with html directory listings to download or install pkg. 
 - Support for connecting to Archive.org and Myrient website. 
 - Create Zip files on PS5 local drive or usb drive
 - Extract from zip, 7zip and rar files
 - File management function include cut/copy/paste/rename/delete/new folder/file for files on PS5 local drive or usb or WebDAV Server.
 - Simple Text Editor to make simply changes to config text files. Limited to edit files over 32kb and limited to edit lines up to 1023 characters. If you try edit lines longer then 1023 characters, it will be truncated. For common text files with the following extensions (txt, log, ini, json, xml, html, conf, config) selecting them in the file browser with the X button will automatically open the Text Editor.
 
## Features in Web Interface ##
 - Copy/Move/Delete/Rename/Create files/folders
 - Extract 7zip, rar and zip files directly on the PS5
 - Compress files into zip directly on the PS5
 - Edit text files directly on the PS5
 - View all common image formats
 - Upload files to the PS5
 - Download files from the PS5

## How to access the Web Interface ##
You need to launch the "ezRemote Client" app on the PS5. Then on any device(laptop, tablet, phone etc..) with web browser goto to http://<ip_address_of_ps5>:9090 . That's all.

The port# can be changed from the "Global Settings" dialog in the PS5 app. Any changes to the web server settings needs a restart of the application to take effect.

## Controls
```
Triangle - Menu (after a file(s)/folder(s) is selected)
Cross - Select Button/TextBox
Circle - Un-Select the file list to navigate to other widgets or Close Dialog window in most cases
Square - Mark file(s)/folder(s) for Delete/Rename/Upload/Download
R1 - Navigate to the Remote list of files
L1 - Navigate to the Local list of files
L2 - To go up a directory from current directory
Options Button - Exit Application
```

## Multi Language Support
The appplication support following languages.

**Note:** Due to new strings added, there are about 31 missing translations for all the languagess. Please help by downloading this [Template](https://github.com/cy33hc/ps5-ezremote-client/blob/master/data/assets/langs/English.ini), make your changes and submit an issue with the file attached for the language.

The following languages have translations.
```
Dutch
English
French
German
Italiano
Japanese
Korean
Polish
Portuguese_BR
Russian
Spanish
Simplified Chinese
Traditional Chinese
Arabic
Catalan
Croatian
Euskera
Galego
Greek
Hungarian
Indonesian
Romanian
Ryukyuan
Thai
Turkish
Ukrainian
Vietnamese
```
**HELP:** There are no language translations for the following languages, therefore not support yet. Please help expand the list by submitting translation for the following languages. If you would like to help, please download this [Template](https://github.com/cy33hc/ps5-ezremote-client/blob/master/data/assets/langs/English.ini), make your changes and submit an issue with the file attached.
```
Finnish
Swedish
Danish
Norwegian
Czech
```
or any other language that you have a traslation for.


