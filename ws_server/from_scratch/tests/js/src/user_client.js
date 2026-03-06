import WebSocket from 'ws';

const DEFAULT_HOST = '127.0.0.1';
const DEFAULT_PORT = 9001;
const DEFAULT_PATH = '/';
const DEFAULT_TIMEOUT_MS = 5000;
const DEFAULT_MESSAGE = 'hello-from-user-client';

function parseArgs(argv) {
	const opts = {
		host: DEFAULT_HOST,
		port: DEFAULT_PORT,
		path: DEFAULT_PATH,
		timeoutMs: DEFAULT_TIMEOUT_MS,
		message: DEFAULT_MESSAGE
	};

	for (let i = 2; i < argv.length; i += 1) {
		const arg = argv[i];
		if (arg === '--host') {
			opts.host = argv[++i];
		} else if (arg === '--port') {
			opts.port = parseInt(argv[++i], 10);
		} else if (arg === '--path') {
			opts.path = argv[++i];
		} else if (arg === '--timeout-ms') {
			opts.timeoutMs = parseInt(argv[++i], 10);
		} else if (arg === '--message') {
			opts.message = argv[++i];
		} else {
			throw new Error(`Unknown argument: ${arg}`);
		}
	}

	if (!opts.path.startsWith('/'))
		opts.path = `/${opts.path}`;

	return opts;
}

function buildUrl({ host, port, path }) {
	return `ws://${host}:${port}${path}`;
}

async function run() {
	const options = parseArgs(process.argv);
	const url = buildUrl(options);
	const message = options.message;

	await new Promise((resolve, reject) => {
		let settled = false;
		const ws = new WebSocket(url);

		const timer = setTimeout(() => {
			done(new Error('timeout waiting for echo'));
		}, options.timeoutMs);

		function done(err) {
			if (settled)
				return;
			settled = true;
			clearTimeout(timer);
			try {
				ws.close();
			} catch (closeErr) {
				// ignore close errors, primary error already captured
			}
			if (err)
				reject(err);
			else
				resolve();
		}

		ws.on('open', () => {
			ws.send(message);
		});

		ws.on('message', (data) => {
			if (data.toString() === message)
				done();
			else
				done(new Error(`unexpected payload: ${data}`));
		});

		ws.on('close', (code, reason) => {
			if (!settled)
				done(new Error(`connection closed before echo (code=${code}, reason=${reason})`));
		});

		ws.on('error', (err) => {
			done(err);
		});
	});
}

run()
	.then(() => {
		console.log('✅ user_client succeeded');
	})
	.catch((err) => {
		console.error('⚠️ user_client failed:', err);
		process.exit(1);
	});
