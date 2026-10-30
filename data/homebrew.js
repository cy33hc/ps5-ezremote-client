async function main() {
    const PAYLOAD = window.workingDir + '/ezremote-loader.elf';
    const ARGS = []
    const ENVVARS = {HOME: window.workingDir, LD_LIBRARY_PATH: window.workingDir};

    return {
        mainText: "ezRemote Client",
        secondaryText: 'File Manager for remote servers',
        onclick: async () => {
	    return {
		path: PAYLOAD,
        daemon: true,
		args: ARGS,
		env: ENVVARS
	    };
        }
    };
}
