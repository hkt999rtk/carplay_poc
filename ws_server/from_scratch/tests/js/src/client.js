import net from 'node:net';
import crypto from 'node:crypto';
import { writeFileSync } from 'node:fs';

const DEFAULT_HOST = '127.0.0.1';
const DEFAULT_PORT = 19113;
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
		host: DEFAULT_HOST,
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
		if (arg === '--host') {
			opts.host = argv[++i];
		} else if (arg === '--port') {
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

function delay(ms) {
	return new Promise((resolve) => setTimeout(resolve, ms));
}

class BufferReader {
	constructor(socket) {
		this.socket = socket;
		this.buffer = Buffer.alloc(0);
		this.waiters = [];
		this.closed = false;
		this.error = null;

		socket.on('data', (chunk) => {
			this.buffer = Buffer.concat([this.buffer, chunk]);
			this.resolveWaiters();
		});

		socket.on('close', () => {
			this.closed = true;
			this.resolveWaiters();
		});

		socket.on('error', (err) => {
			this.error = err;
			this.resolveWaiters();
		});
	}

	resolveWaiters() {
		const waiters = this.waiters;
		this.waiters = [];
		for (const resolve of waiters)
			resolve();
	}

	async ensureBytes(count) {
		while (this.buffer.length < count) {
			if (this.error)
				throw this.error;
			if (this.closed)
				throw new Error('socket closed');
			await new Promise((resolve) => this.waiters.push(resolve));
		}
	}

	async waitForDelimiter(delimiter) {
		while (true) {
			const idx = this.buffer.indexOf(delimiter);
			if (idx !== -1)
				return idx;
			if (this.error)
				throw this.error;
			if (this.closed)
				throw new Error('socket closed before delimiter');
			await new Promise((resolve) => this.waiters.push(resolve));
		}
	}

	readBytes(count) {
		const slice = this.buffer.slice(0, count);
		this.buffer = this.buffer.slice(count);
		return slice;
	}
}

async function performHandshake(socket, reader, options) {
	const key = crypto.randomBytes(16).toString('base64');
	const request =
		`GET /js-client HTTP/1.1\r\n` +
		`Host: ${options.host}:${options.port}\r\n` +
		'Upgrade: websocket\r\n' +
		'Connection: Upgrade\r\n' +
		`Sec-WebSocket-Key: ${key}\r\n` +
		'Sec-WebSocket-Version: 13\r\n' +
		'\r\n';
	socket.write(request);

	const delimiter = Buffer.from('\r\n\r\n', 'ascii');
	const idx = await reader.waitForDelimiter(delimiter);
	const headerBuf = reader.readBytes(idx + delimiter.length);
	const headerText = headerBuf.toString('utf8');

	const lines = headerText.split('\r\n').filter((line) => line.length > 0);
	if (lines.length === 0)
		throw new Error('empty handshake response');
	const statusLine = lines[0];
	if (!statusLine.startsWith('HTTP/1.1 101'))
		throw new Error(`unexpected status line: ${statusLine}`);
}

async function readFrame(reader) {
	await reader.ensureBytes(2);
	const header = reader.readBytes(2);
	const opcode = header[0] & 0x0F;
	const masked = (header[1] & 0x80) !== 0;
	let payloadLength = header[1] & 0x7F;

	if (payloadLength === 126) {
		await reader.ensureBytes(2);
		const ext = reader.readBytes(2);
		payloadLength = ext.readUInt16BE(0);
	} else if (payloadLength === 127) {
		await reader.ensureBytes(8);
		const ext = reader.readBytes(8);
		const high = ext.readUInt32BE(0);
		const low = ext.readUInt32BE(4);
		payloadLength = (high * 0x100000000) + low;
	}

	let maskKey = null;
	if (masked) {
		await reader.ensureBytes(4);
		maskKey = reader.readBytes(4);
	}

	await reader.ensureBytes(payloadLength);
	let payload = reader.readBytes(payloadLength);

	if (masked && maskKey != null) {
		const unmasked = Buffer.alloc(payloadLength);
		for (let i = 0; i < payloadLength; i += 1)
			unmasked[i] = payload[i] ^ maskKey[i % 4];
		payload = unmasked;
	}

	return { opcode, payload };
}

function sendFrame(socket, opcode, payload) {
	const maskKey = crypto.randomBytes(4);
	const length = payload.length;
	let header = null;

	if (length <= 125) {
		header = Buffer.alloc(2);
		header[0] = 0x80 | opcode;
		header[1] = 0x80 | length;
	} else if (length <= 0xFFFF) {
		header = Buffer.alloc(4);
		header[0] = 0x80 | opcode;
		header[1] = 0x80 | 126;
		header.writeUInt16BE(length, 2);
	} else {
		header = Buffer.alloc(10);
		header[0] = 0x80 | opcode;
		header[1] = 0x80 | 127;
		const high = Math.floor(length / 0x100000000);
		const low = length >>> 0;
		header.writeUInt32BE(high, 2);
		header.writeUInt32BE(low, 6);
	}

	const maskedPayload = Buffer.alloc(length);
	for (let i = 0; i < length; i += 1)
		maskedPayload[i] = payload[i] ^ maskKey[i % 4];

	socket.write(header);
	socket.write(maskKey);
	if (length > 0)
		socket.write(maskedPayload);
}

async function runSingleClient(options, clientId) {
	const metrics = {
		clientId,
		serverTextReceived: 0,
		serverBinaryReceived: 0,
		clientTextSent: 0,
		clientBinarySent: 0,
		textEchoReceived: 0,
		binaryEchoReceived: 0,
		closeCode: null,
		error: null
	};

	const socket = net.createConnection({
		host: options.host,
		port: options.port
	});

	try {
		await new Promise((resolve, reject) => {
			socket.once('connect', resolve);
			socket.once('error', reject);
		});

		const reader = new BufferReader(socket);
		await performHandshake(socket, reader, options);

		for (let i = 0; i < options.textCount; i += 1) {
			const frame = await readFrame(reader);
			if (frame.opcode !== 0x1)
				throw new Error('expected text frame from server');
			const text = frame.payload.toString('utf8');
			const expected = makeServerText(metrics.serverTextReceived);
			if (text !== expected)
				throw new Error('server text payload mismatch');
			metrics.serverTextReceived += 1;
		}

		for (let i = 0; i < options.binaryCount; i += 1) {
			const frame = await readFrame(reader);
			if (frame.opcode !== 0x2)
				throw new Error('expected binary frame from server');
			const expected = makeServerBinary(
				metrics.serverBinaryReceived,
				options.payloadSize
			);
			if (!frame.payload.equals(expected))
				throw new Error('server binary payload mismatch');
			metrics.serverBinaryReceived += 1;
		}

		for (let i = 0; i < options.textCount; i += 1) {
			const text = makeClientText(metrics.clientTextSent);
			sendFrame(socket, 0x1, Buffer.from(text, 'utf8'));
			metrics.clientTextSent += 1;

			const echo = await readFrame(reader);
			if (echo.opcode !== 0x1)
				throw new Error('expected text echo');
			if (echo.payload.toString('utf8') !== text)
				throw new Error('text echo mismatch');
			metrics.textEchoReceived += 1;
		}

		for (let i = 0; i < options.binaryCount; i += 1) {
			const payload = makeClientBinary(
				metrics.clientBinarySent,
				options.payloadSize
			);
			sendFrame(socket, 0x2, payload);
			metrics.clientBinarySent += 1;

			const echo = await readFrame(reader);
			if (echo.opcode !== 0x2)
				throw new Error('expected binary echo');
			if (!echo.payload.equals(payload))
				throw new Error('binary echo mismatch');
			metrics.binaryEchoReceived += 1;
		}

		const closePayload = Buffer.alloc(2);
		closePayload.writeUInt16BE(1000, 0);
		sendFrame(socket, 0x8, closePayload);

		const closeFrame = await readFrame(reader);
		if (closeFrame.opcode !== 0x8)
			throw new Error('expected close frame from server');
		const code = closeFrame.payload.length >= 2
			? closeFrame.payload.readUInt16BE(0)
			: 1005;
		metrics.closeCode = code;
		if (code !== 1000)
			throw new Error(`unexpected close code ${code}`);
	} catch (err) {
		if (!metrics.error)
			metrics.error = err.message;
	} finally {
		socket.end();
		await new Promise((resolve) => socket.once('close', resolve));
	}

	return metrics;
}

async function runClients(options) {
	if (options.mode === 'sequential') {
		const results = [];
		for (let i = 0; i < options.clientCount; i += 1) {
			if (i > 0 && options.staggerMs > 0)
				await delay(options.staggerMs);
			results.push(await runSingleClient(options, i));
		}
		return results;
	}

	const tasks = [];
	for (let i = 0; i < options.clientCount; i += 1) {
		const startDelay = options.staggerMs * i;
		tasks.push(
			(delay(startDelay).then(() => runSingleClient(options, i)))
		);
	}
	return Promise.all(tasks);
}

async function main() {
	const options = parseArgs(process.argv);
	const startTime = Date.now();

	let results;
	try {
		results = await runClients(options);
	} catch (err) {
		console.error(`client runner error: ${err.message}`);
		process.exit(1);
	}

	let failures = 0;
	let aggregateServerText = 0;
	let aggregateServerBinary = 0;
	let aggregateTextEcho = 0;
	let aggregateBinaryEcho = 0;

	for (const result of results) {
		aggregateServerText += result.serverTextReceived;
		aggregateServerBinary += result.serverBinaryReceived;
		aggregateTextEcho += result.textEchoReceived;
		aggregateBinaryEcho += result.binaryEchoReceived;
		if (result.error)
			failures += 1;
		if (result.closeCode !== 1000)
			failures += 1;
	}

	const durationMs = Date.now() - startTime;
	const payload = {
		scenario: 'c_server_js_client',
		mode: options.mode,
		client_count: options.clientCount,
		stagger_ms: options.staggerMs,
		status: failures > 0 ? 'failed' : 'passed',
		duration_ms: durationMs,
		client_aggregate: {
			serverTextReceived: aggregateServerText,
			serverBinaryReceived: aggregateServerBinary,
			textEchoReceived: aggregateTextEcho,
			binaryEchoReceived: aggregateBinaryEcho
		},
		clients: results.map((result) => ({
			client_id: result.clientId,
			server_text_received: result.serverTextReceived,
			server_binary_received: result.serverBinaryReceived,
			client_text_sent: result.clientTextSent,
			client_binary_sent: result.clientBinarySent,
			text_echo_received: result.textEchoReceived,
			binary_echo_received: result.binaryEchoReceived,
			close_code: result.closeCode,
			status: result.error ? 'failed' : 'passed',
			error_message: result.error || null
		}))
	};

	writeReport(options.reportPath, payload);

	if (failures > 0) {
		console.error(`client error: ${failures} failures detected`);
		process.exit(1);
	}

	console.log(`js_client: success with ${options.clientCount} clients (${options.mode}) in ${durationMs} ms`);
}

main().catch((err) => {
	console.error(err);
	process.exit(1);
});
