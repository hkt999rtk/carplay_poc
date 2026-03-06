(function() {
    "use strict";

    function setStatus(message) {
        var node = document.getElementById("status");
        if (node != null) {
            node.textContent = message;
        }
    }

    function readStreamUrl() {
        return "ws://" + window.location.hostname + ":8081";
    }

    class VideoSurface extends Player {
        constructor(canvasId) {
            super();
            this.outputCanvas = document.getElementById(canvasId);
            this.onRenderFrameComplete = (frame) => {
                this.renderFrameSource(frame.canvasObj.canvas, frame.width, frame.height);
            };
        }

        renderFrameSource(source, width, height) {
            var ctx;
            var scale;
            var drawWidth;
            var drawHeight;
            var left;
            var top;

            if (this.outputCanvas == null) {
                return;
            }

            ctx = this.outputCanvas.getContext("2d");
            scale = Math.min(this.outputCanvas.width / width, this.outputCanvas.height / height);
            drawWidth = Math.round(width * scale);
            drawHeight = Math.round(height * scale);
            left = Math.floor((this.outputCanvas.width - drawWidth) / 2);
            top = Math.floor((this.outputCanvas.height - drawHeight) / 2);

            ctx.fillStyle = "#000000";
            ctx.fillRect(0, 0, this.outputCanvas.width, this.outputCanvas.height);
            ctx.drawImage(source, 0, 0, width, height, left, top, drawWidth, drawHeight);
        }
    }

    class AudioSurface {
        constructor(canvasId) {
            this.outputCanvas = document.getElementById(canvasId);
            this.destCtx = this.outputCanvas != null ? this.outputCanvas.getContext("2d") : null;
            this.audioContext = null;
            this.nextPlaybackTime = 0;
            this.sampleRate = 16000;
            this.maxQueuedSeconds = 0.25;
            this.audioBlocks = [];
            this.drawTime = 0;
            this.resumePlayback = this.resumePlayback.bind(this);
            window.addEventListener("pointerdown", this.resumePlayback, { passive: true });
            window.addEventListener("keydown", this.resumePlayback);
            this.drawFrame();
        }

        ensureAudioContext() {
            var AudioContextCtor;

            if (this.audioContext != null) {
                return this.audioContext;
            }

            AudioContextCtor = window.AudioContext || window.webkitAudioContext;
            if (typeof AudioContextCtor !== "function") {
                return null;
            }

            try {
                this.audioContext = new AudioContextCtor();
            } catch (err) {
                console.log("AudioContext init failed:", err);
                this.audioContext = null;
            }
            return this.audioContext;
        }

        resumePlayback() {
            var ctx = this.ensureAudioContext();

            if (ctx != null && ctx.state === "suspended") {
                ctx.resume().catch(function(err) {
                    console.log("AudioContext resume failed:", err);
                });
            }
        }

        queuePlayback(audioData) {
            var ctx = this.ensureAudioContext();
            var buffer;
            var channel;
            var source;
            var now;
            var startAt;
            var i;

            if (ctx == null || audioData.length === 0) {
                return;
            }

            this.resumePlayback();
            now = ctx.currentTime;
            startAt = this.nextPlaybackTime;
            if (startAt < now) {
                startAt = now + 0.02;
            }
            if ((startAt - now) > this.maxQueuedSeconds) {
                startAt = now + 0.02;
            }

            buffer = ctx.createBuffer(1, audioData.length, this.sampleRate);
            channel = buffer.getChannelData(0);
            for (i = 0; i < audioData.length; i++) {
                channel[i] = audioData[i] / 32768.0;
            }

            source = ctx.createBufferSource();
            source.buffer = buffer;
            source.connect(ctx.destination);
            source.start(startAt);
            this.nextPlaybackTime = startAt + buffer.duration;
        }

        setupData(audioData) {
            var audioBlockSize = 24;
            var waveformData = audioData;
            var drawData;
            var stepSize;
            var fidx;
            var i;

            if (audioData.length > audioBlockSize) {
                drawData = new Int16Array(audioBlockSize);
                stepSize = audioData.length / audioBlockSize;
                fidx = 0.0;
                for (i = 0; i < audioBlockSize; i++) {
                    drawData[i] = audioData[Math.floor(fidx)];
                    fidx += stepSize;
                }
                waveformData = drawData;
            }

            this.audioBlocks.push(waveformData);
            while (this.audioBlocks.length > 8) {
                this.audioBlocks.shift();
            }
            this.queuePlayback(audioData);
            this.drawWave();
        }

        drawFrame() {
            var ctx;
            var width;
            var height;
            var x;

            if (this.destCtx == null || this.outputCanvas == null) {
                return;
            }

            ctx = this.destCtx;
            width = this.outputCanvas.width;
            height = this.outputCanvas.height;
            ctx.clearRect(0, 0, width, height);
            ctx.fillStyle = "#05080c";
            ctx.fillRect(0, 0, width, height);
            ctx.strokeStyle = "#204130";
            ctx.beginPath();
            ctx.moveTo(0, height / 2);
            ctx.lineTo(width, height / 2);
            for (x = 0; x <= width; x += width / 10) {
                ctx.moveTo(x, 0);
                ctx.lineTo(x, height);
            }
            ctx.stroke();
        }

        drawWave() {
            var now;
            var ctx;
            var width;
            var height;
            var divFactor;
            var blockWidth;
            var blockIndex;
            var sampleIndex;
            var x;
            var y;
            var data;

            if (this.destCtx == null || this.outputCanvas == null) {
                return;
            }

            now = Date.now();
            if ((now - this.drawTime) < 66) {
                return;
            }
            this.drawTime = now;
            this.drawFrame();

            ctx = this.destCtx;
            width = this.outputCanvas.width;
            height = this.outputCanvas.height;
            divFactor = 24576;
            blockWidth = this.audioBlocks.length > 0 ? (width / this.audioBlocks.length) : width;

            ctx.strokeStyle = "#ffffff";
            ctx.beginPath();
            ctx.moveTo(0, height / 2);
            for (blockIndex = 0; blockIndex < this.audioBlocks.length; blockIndex++) {
                data = this.audioBlocks[blockIndex];
                for (sampleIndex = 0; sampleIndex < data.length; sampleIndex++) {
                    x = (blockIndex + (sampleIndex / data.length)) * blockWidth;
                    y = (data[sampleIndex] / divFactor) * height + height / 2;
                    ctx.lineTo(x, y);
                }
            }
            ctx.stroke();
        }
    }

    class StreamClient {
        constructor(url, videoSurface, audioSurface) {
            this.url = url;
            this.videoSurface = videoSurface;
            this.audioSurface = audioSurface;
            this.cryptoSession = new CryptoStream.StreamSession();
            this.ws = null;
            this.reconnectTimer = null;
            this.connect();
        }

        connect() {
            var ws = new WebSocket(this.url);

            this.ws = ws;
            ws.binaryType = "arraybuffer";
            setStatus("Connecting to " + this.url + " ...");

            ws.onopen = () => {
                setStatus("Connected. Starting video/audio streams.");
                ws.send(JSON.stringify({ cmd: "start_stream", param: { mirror: 1 } }));
                ws.send(JSON.stringify({ cmd: "start_audio_stream" }));
            };

            ws.onclose = () => {
                setStatus("Disconnected. Retrying...");
                this.scheduleReconnect();
            };

            ws.onerror = () => {
                setStatus("WebSocket error. Retrying...");
            };

            ws.onmessage = (evt) => {
                if (evt.data instanceof ArrayBuffer) {
                    this.handleBinary(evt.data);
                } else {
                    this.handleText(evt.data);
                }
            };
        }

        scheduleReconnect() {
            if (this.reconnectTimer != null) {
                return;
            }
            this.reconnectTimer = window.setTimeout(() => {
                this.reconnectTimer = null;
                this.cryptoSession = new CryptoStream.StreamSession();
                this.connect();
            }, 1000);
        }

        handleText(text) {
            var obj;

            try {
                obj = JSON.parse(text);
            } catch (err) {
                return;
            }

            if (this.cryptoSession.updateFromControl(obj)) {
                setStatus("Crypto session ready. Waiting for media.");
            }
        }

        handleBinary(buffer) {
            var decoded;
            var audioData;

            try {
                decoded = this.cryptoSession.decryptBinary(buffer);
            } catch (err) {
                console.log("drop encrypted packet:", err.message);
                return;
            }

            if (decoded == null) {
                return;
            }

            if (decoded.streamId === CryptoStream.STREAM_VIDEO) {
                this.videoSurface.decode(decoded.payload);
                return;
            }

            if (decoded.streamId === CryptoStream.STREAM_AUDIO) {
                audioData = new Int16Array(
                    decoded.payload.buffer,
                    decoded.payload.byteOffset,
                    Math.floor(decoded.payload.byteLength / 2)
                );
                this.audioSurface.setupData(audioData);
            }
        }
    }

    document.addEventListener("DOMContentLoaded", function() {
        var videoSurface = new VideoSurface("output_canvas");
        var audioSurface = new AudioSurface("sound_canvas");
        window.streamClient = new StreamClient(readStreamUrl(), videoSurface, audioSurface);
    });
}());
