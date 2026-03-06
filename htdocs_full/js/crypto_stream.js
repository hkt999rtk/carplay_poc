// UMD helper for parsing and decrypting low-latency ChaCha media packets.
(function(root, factory) {
    if (typeof module === "object" && module.exports) {
        module.exports = factory();
    } else {
        root.CryptoStream = factory();
    }
}(typeof self !== "undefined" ? self : this, function() {
    var KEY_BYTES = new Uint8Array([
        0x43, 0x61, 0x72, 0x70, 0x6c, 0x61, 0x79, 0x50,
        0x6f, 0x43, 0x43, 0x68, 0x61, 0x43, 0x68, 0x61,
        0x53, 0x74, 0x72, 0x65, 0x61, 0x6d, 0x4b, 0x65,
        0x79, 0x2d, 0x76, 0x31, 0x21, 0x21, 0x21, 0x21
    ]);
    var HEADER_SIZE = 8;
    var VERSION = 1;
    var STREAM_VIDEO = 1;
    var STREAM_AUDIO = 2;

    function readBE32(bytes, offset) {
        return (((bytes[offset] << 24) >>> 0) |
                ((bytes[offset + 1] << 16) >>> 0) |
                ((bytes[offset + 2] << 8) >>> 0) |
                (bytes[offset + 3] >>> 0)) >>> 0;
    }

    function readLE32(bytes, offset) {
        return ((bytes[offset] >>> 0) |
                ((bytes[offset + 1] << 8) >>> 0) |
                ((bytes[offset + 2] << 16) >>> 0) |
                ((bytes[offset + 3] << 24) >>> 0)) >>> 0;
    }

    function writeLE32(bytes, offset, value) {
        bytes[offset] = value & 0xff;
        bytes[offset + 1] = (value >>> 8) & 0xff;
        bytes[offset + 2] = (value >>> 16) & 0xff;
        bytes[offset + 3] = (value >>> 24) & 0xff;
    }

    function rotl32(value, shift) {
        return ((value << shift) | (value >>> (32 - shift))) >>> 0;
    }

    function quarterRound(state, a, b, c, d) {
        state[a] = (state[a] + state[b]) >>> 0;
        state[d] ^= state[a];
        state[d] = rotl32(state[d], 16);

        state[c] = (state[c] + state[d]) >>> 0;
        state[b] ^= state[c];
        state[b] = rotl32(state[b], 12);

        state[a] = (state[a] + state[b]) >>> 0;
        state[d] ^= state[a];
        state[d] = rotl32(state[d], 8);

        state[c] = (state[c] + state[d]) >>> 0;
        state[b] ^= state[c];
        state[b] = rotl32(state[b], 7);
    }

    function chachaBlock(keyBytes, nonceBytes, counter) {
        var initial = new Uint32Array(16);
        var state = new Uint32Array(16);
        var output = new Uint8Array(64);
        var i;

        initial[0] = 0x61707865;
        initial[1] = 0x3320646e;
        initial[2] = 0x79622d32;
        initial[3] = 0x6b206574;
        for (i = 0; i < 8; i++) {
            initial[4 + i] = readLE32(keyBytes, i * 4);
        }
        initial[12] = counter >>> 0;
        initial[13] = readLE32(nonceBytes, 0);
        initial[14] = readLE32(nonceBytes, 4);
        initial[15] = readLE32(nonceBytes, 8);

        state.set(initial);
        for (i = 0; i < 10; i++) {
            quarterRound(state, 0, 4, 8, 12);
            quarterRound(state, 1, 5, 9, 13);
            quarterRound(state, 2, 6, 10, 14);
            quarterRound(state, 3, 7, 11, 15);
            quarterRound(state, 0, 5, 10, 15);
            quarterRound(state, 1, 6, 11, 12);
            quarterRound(state, 2, 7, 8, 13);
            quarterRound(state, 3, 4, 9, 14);
        }

        for (i = 0; i < 16; i++) {
            var value = (state[i] + initial[i]) >>> 0;
            writeLE32(output, i * 4, value);
        }
        return output;
    }

    function chacha20Xor(input, keyBytes, nonceBytes, counter) {
        var output = new Uint8Array(input.length);
        var offset = 0;
        var blockCounter = counter >>> 0;

        while (offset < input.length) {
            var block = chachaBlock(keyBytes, nonceBytes, blockCounter);
            var remaining = input.length - offset;
            var take = remaining > 64 ? 64 : remaining;
            for (var i = 0; i < take; i++) {
                output[offset + i] = input[offset + i] ^ block[i];
            }
            offset += take;
            blockCounter = (blockCounter + 1) >>> 0;
        }
        return output;
    }

    function base64ToBytes(text) {
        if (typeof atob === "function") {
            var decoded = atob(text);
            var out = new Uint8Array(decoded.length);
            for (var i = 0; i < decoded.length; i++) {
                out[i] = decoded.charCodeAt(i);
            }
            return out;
        }
        if (typeof Buffer !== "undefined") {
            return new Uint8Array(Buffer.from(text, "base64"));
        }
        throw new Error("No base64 decoder available");
    }

    function derivePacketNonce(sessionNonce, seqNo) {
        var nonce = new Uint8Array(sessionNonce);
        var tail = readLE32(nonce, 8);
        tail = (tail + (seqNo >>> 0)) >>> 0;
        writeLE32(nonce, 8, tail);
        return nonce;
    }

    function parseCryptoInit(obj) {
        if (!obj || obj.type !== "crypto_init" || !obj.result || !obj.result.session_nonce) {
            return null;
        }
        return base64ToBytes(obj.result.session_nonce);
    }

    function decryptPacket(binary, sessionNonce) {
        var bytes = binary instanceof Uint8Array ? binary : new Uint8Array(binary);
        var seqNo;
        var streamId;
        var nonce;
        var payload;

        if (bytes.length < HEADER_SIZE) {
            throw new Error("binary packet too short");
        }
        if (bytes[0] !== 0x43 || bytes[1] !== 0x48) {
            throw new Error("invalid crypto packet magic");
        }
        if (bytes[2] !== VERSION) {
            throw new Error("unsupported crypto packet version");
        }

        streamId = bytes[3];
        seqNo = readBE32(bytes, 4);
        nonce = derivePacketNonce(sessionNonce, seqNo);
        payload = chacha20Xor(bytes.subarray(HEADER_SIZE), KEY_BYTES, nonce, 1);
        return {
            streamId: streamId,
            seqNo: seqNo,
            payload: payload
        };
    }

    function StreamSession() {
        this.sessionNonce = null;
    }

    StreamSession.prototype.updateFromControl = function(obj) {
        var nonce = parseCryptoInit(obj);
        if (nonce == null) {
            return false;
        }
        this.sessionNonce = nonce;
        return true;
    };

    StreamSession.prototype.decryptBinary = function(binary) {
        if (this.sessionNonce == null) {
            return null;
        }
        return decryptPacket(binary, this.sessionNonce);
    };

    return {
        HEADER_SIZE: HEADER_SIZE,
        VERSION: VERSION,
        STREAM_VIDEO: STREAM_VIDEO,
        STREAM_AUDIO: STREAM_AUDIO,
        KEY_BYTES: KEY_BYTES,
        StreamSession: StreamSession,
        parseCryptoInit: parseCryptoInit,
        derivePacketNonce: derivePacketNonce,
        decryptPacket: decryptPacket,
        chacha20Xor: chacha20Xor
    };
}));
