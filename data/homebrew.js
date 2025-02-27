async function main() {
    const PAYLOAD = window.workingDir + '/ezremote_client.elf';
    const ARGS = []
    const ENVVARS = {};

    return {
        mainText: "ezRemote Client",
        secondaryText: 'File Manager for remote servers',
        onclick: async () => {
	    return {
		path: PAYLOAD,
		args: ARGS,
		env: ENVVARS
	    };
        }
    };
}
