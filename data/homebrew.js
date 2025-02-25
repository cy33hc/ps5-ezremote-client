async function main() {
    const PAYLOAD = window.workingDir + '/ezremote_client.elf';
    const ARGS = []
    const ENVVARS = {};

    return {
        mainText: "ezRemote Client",
        secondaryText: 'File manager for remote servers like FTP,SFTP,SMB,NFS,WebDAV,HTTP Server',
        onclick: async () => {
	    return {
		path: PAYLOAD,
		args: ARGS,
		env: ENVVARS
	    };
        }
    };
}
