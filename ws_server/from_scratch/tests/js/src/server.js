import { WebSocketServer } from 'ws';
import { writeFileSync } from 'node:fs';

const DEFAULT_PORT = 19112;
const DEFAULT_TEXT_COUNT = 120;
const DEFAULT_BINARY_COUNT = 120;
const DEFAULT_PAYLOAD_SIZE = 64;
const DEFAULT_CLIENTS = 1;

function pad(index) {
	return String(index).padStart(3, '0');
}

function makeServerText(index) {
	return `server-text-${pad(index)}`;
}

function makeClientText(index) {
	return `client-text-${pad(index)}`;
}

function makeServerBinary(index, length) {
	const buf = Buffer.alloc(length);
	for (let i = 0; i < length; i += 1)
		buf[i] = (0xA0 ^ index ^ i) & 0xFF;
	return buf;
}

function makeClientBinary(index, length) {
	const buf = Buffer.alloc(length);
	for (let i = 0; i < length; i += 1)
		buf[i] = (0x5A + index + i) & 0xFF;
	return buf;
}

function parseArgs(argv) {
	const opts = {
		port: DEFAULT_PORT,
		textCount: DEFAULT_TEXT_COUNT,
		binaryCount: DEFAULT_BINARY_COUNT,
		payloadSize: DEFAULT_PAYLOAD_SIZE,
		clientCount: DEFAULT_CLIENTS,
		mode: 'concurrent',
		staggerMs: 0,
		reportPath: null
	};

	for (let i = 2; i < argv.length; i += 1) {
		const arg = argv[i];
		if (arg === '--port') {
			opts.port = parseInt(argv[++i], 10);
		} else if (arg === '--text-count') {
			opts.textCount = parseInt(argv[++i], 10);
		} else if (arg === '--binary-count') {
			opts.binaryCount = parseInt(argv[++i], 10);
		} else if (arg === '--payload-size') {
			opts.payloadSize = parseInt(argv[++i], 10);
		} else if (arg === '--clients') {
			opts.clientCount = parseInt(argv[++i], 10);
		} else if (arg === '--mode') {
			opts.mode = argv[++i];
		} else if (arg === '--stagger-ms') {
			opts.staggerMs = parseInt(argv[++i], 10);
		} else if (arg === '--report') {
			opts.reportPath = argv[++i];
		} else {
			throw new Error(`Unknown argument: ${arg}`);
		}
	}

	return opts;
}

function writeReport(path, payload) {
	if (!path)
		return;
	writeFileSync(path, JSON.stringify(payload, null, 2), 'utf8');
}

async function main() {
	const options = parseArgs(process.argv);
	const metrics = {
		connectionsOpened: 0,
		connectionsClosed: 0,
		initialTextSent: 0,
		initialBinarySent: 0,
		clientTextReceived: 0,
		clientBinaryReceived: 0,
		textEchoed: 0,
		binaryEchoed: 0,
		error: null,
		clients: []
	};

	const startTime = Date.now();
	let exitCode = 0;
	let finalized = false;

	const wss = new WebSocketServer({ port: options.port });

	function finalize() {
		if (finalized)
			return;
		finalized = true;
		const durationMs = Date.now() - startTime;
		writeReport(options.reportPath, {
			scenario: 'js_server_c_client',
			mode: options.mode,
			client_count: options.clientCount,
			stagger_ms: options.staggerMs,
			status: exitCode === 0 ? 'passed' : 'failed',
			duration_ms: durationMs,
			server: metrics
		});
		wss.close(() => {
			process.exit(exitCode);
		});
	}

	function ensureClientSlot(clientId) {
		if (!metrics.clients[clientId]) {
			metrics.clients[clientId] = {
				clientId,
				initialTextSent: 0,
				initialBinarySent: 0,
				clientTextReceived: 0,
				clientBinaryReceived: 0,
				textEchoed: 0,
				binaryEchoed: 0,
				closeCode: null,
				error: null
			};
		}
		return metrics.clients[clientId];
	}

	function markError(message, clientMetric) {
		if (!metrics.error)
			metrics.error = message;
		if (clientMetric && !clientMetric.error)
			clientMetric.error = message;
		exitCode = 1;
	}

	wss.on('listening', () => {
		console.log(JSON.stringify({
			event: 'ready',
			port: options.port,
			mode: options.mode
		}));
	});

	wss.on('error', (err) => {
		markError(`server error: ${err.message}`);
		finalize();
	});

	wss.on('connection', (socket) => {
		const clientId = metrics.connectionsOpened;
		if (clientId >= options.clientCount) {
			markError('too many clients connected');
			socket.close(1013, 'unexpected client');
			return;
		}

		const clientMetric = ensureClientSlot(clientId);
		metrics.connectionsOpened += 1;

		try {
			for (let i = 0; i < options.textCount; i += 1) {
				const text = makeServerText(i);
				socket.send(text);
				clientMetric.initialTextSent += 1;
				metrics.initialTextSent += 1;
			}

			for (let i = 0; i < options.binaryCount; i += 1) {
				const payload = makeServerBinary(
					i,
					options.payloadSize
				);
				socket.send(payload);
				clientMetric.initialBinarySent += 1;
				metrics.initialBinarySent += 1;
			}
		} catch (err) {
			markError(`send failure: ${err.message}`, clientMetric);
			socket.close(1011, 'send failure');
			return;
		}

		socket.on('message', (data, isBinary) => {
			try {
				if (!isBinary) {
					const text =
						typeof data === 'string'
							? data
							: data.toString();
					const expected = makeClientText(
						clientMetric.clientTextReceived
					);
					if (text !== expected)
						throw new Error('text payload mismatch');
					clientMetric.clientTextReceived += 1;
					metrics.clientTextReceived += 1;
					socket.send(text);
					clientMetric.textEchoed += 1;
					metrics.textEchoed += 1;
				} else {
					const expected = makeClientBinary(
						clientMetric.clientBinaryReceived,
						options.payloadSize
					);
					const incoming =
						data instanceof Buffer
							? data
							: Buffer.from(data);
					if (!incoming.equals(expected))
						throw new Error('binary payload mismatch');
					clientMetric.clientBinaryReceived += 1;
					metrics.clientBinaryReceived += 1;
					socket.send(incoming, { binary: true });
					clientMetric.binaryEchoed += 1;
					metrics.binaryEchoed += 1;
				}
			} catch (err) {
				markError(`mismatch: ${err.message}`, clientMetric);
				socket.close(1011, 'mismatch');
			}
		});

		socket.on('close', (code) => {
			clientMetric.closeCode = code;
			metrics.connectionsClosed += 1;
			if (metrics.connectionsClosed >= options.clientCount || exitCode !== 0)
				finalize();
		});

		socket.on('error', (err) => {
			markError(`connection error: ${err.message}`, clientMetric);
			socket.close(1011, 'connection error');
		});
	});
}

main().catch((err) => {
	console.error(err);
	process.exit(1);
});
