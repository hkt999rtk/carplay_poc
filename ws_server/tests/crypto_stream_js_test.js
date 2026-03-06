const assert = require("assert");
const path = require("path");

const CryptoStream = require(path.join(__dirname, "../../htdocs/js/crypto_stream.js"));

function writeBe32(bytes, offset, value) {
    bytes[offset] = (value >>> 24) & 0xff;
    bytes[offset + 1] = (value >>> 16) & 0xff;
    bytes[offset + 2] = (value >>> 8) & 0xff;
    bytes[offset + 3] = value & 0xff;
}

const sessionNonce = new Uint8Array([
    0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c
]);
const payload = new Uint8Array([0x65, 0x88, 0x99, 0xaa, 0xbb, 0xcc]);
const seqNo = 11;
const packet = new Uint8Array(CryptoStream.HEADER_SIZE + payload.length);
const nonce = CryptoStream.derivePacketNonce(sessionNonce, seqNo);
const ciphertext = CryptoStream.chacha20Xor(payload, CryptoStream.KEY_BYTES, nonce, 1);

packet[0] = 0x43;
packet[1] = 0x48;
packet[2] = CryptoStream.VERSION;
packet[3] = CryptoStream.STREAM_VIDEO;
writeBe32(packet, 4, seqNo);
packet.set(ciphertext, CryptoStream.HEADER_SIZE);

const decoded = CryptoStream.decryptPacket(packet, sessionNonce);
assert.strictEqual(decoded.streamId, CryptoStream.STREAM_VIDEO);
assert.strictEqual(decoded.seqNo, seqNo);
assert.deepStrictEqual(Array.from(decoded.payload), Array.from(payload));

console.log("crypto_stream_js_test: ok");
