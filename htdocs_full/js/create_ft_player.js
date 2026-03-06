// create player for face tracking
function drawFaceTracking(output_canvas, scale) {
	let painted = [false, false, false, false];
	for (let osdIdx = 0; osdIdx < this.osdLayers.length; osdIdx++) {
		for (const osd of this.osdLayers[osdIdx].osdArray.values()) {
			if (osd.result.status == "ok") {
				let res = osd.result;
				let detection = res.detection;
				let detection_count = detection.length;
			
				if (detection_count > 4) {
					detection_count = 4;
				}
				if (!this.tracking) {
					detection_count = 0;
				}
				
				for (let i=0; i<detection_count; i++) {
					let cidx = -1; // initial
					// if there is already a person in the same position, replace it
					for (let j=0; j<4; j++) {
						if (this.slotInfo[j].oid == detection[i].oid) {
							cidx = j;
							break;
						}
					}
					if (cidx < 0) {
						if (this.idMap.has(detection[i].oid)) {
							let count = this.idMap.get(detection[i].oid);
							if (count<20) {
								this.idMap.set(detection[i].oid, count+1);
								continue;
							} 
						} else {
							this.idMap.set(detection[i].oid, 0);
							continue
						}
					}
					if (cidx<0) { // not found, get new one position
						for (let j=0; j<4; j++) {
							if (this.slotInfo[j].oid < 0) {
								this.slotInfo[j] = { oid: detection[i].oid, time: new Date().getTime() };
								cidx = j;
								break;
							}
						}
					}
					if (cidx < 0) { // no new position, remove one with minimal time
						let min_time = this.slotInfo[0].time;
						cidx = 0;
						for (let j=1; j<4; j++) {
							if (this.slotInfo[j].time < min_time) {
								min_time = this.slotInfo[j].time;
								cidx = j;
							}
						}
					}
					if (cidx >=0) {
						this.slotInfo[cidx].time = new Date().getTime();
						this.slotInfo[cidx].oid = detection[i].oid;
						painted[cidx] = true;
					}

					let id_name = 'obj_canvas' + cidx;
					let tracking_canvas = document.getElementById(id_name);
					let destCtx = tracking_canvas.getContext('2d');
					let od = structuredClone(detection[i]);
					od = rectScale(od, scale);
					od = addMargin(od, 1.0, 1.0);
					od = adjustRect(od, output_canvas, tracking_canvas);
					if (typeof destCtx._cx == "undefined") {
						destCtx._cx = (od.minx + od.maxx)/2;
						destCtx._cy = (od.miny + od.maxy)/2;
						destCtx._width = od.maxx - od.minx + 1;
						destCtx._height = od.maxy - od.miny + 1;
						destCtx._speed = 0;
					} else {
						moveCamera(destCtx, od.minx, od.miny, od.maxx, od.maxy);
					}
					// draw image from "output_canvas" to "tracking_canvas"
					destCtx.drawImage(output_canvas, destCtx._cx - destCtx._width/2, destCtx._cy - destCtx._height/2, destCtx._width, destCtx._height, 0, 0, tracking_canvas.width, tracking_canvas.height);
					drawZoomInfo(output_canvas, destCtx, scale)
				}
			}
			// after loop, process those unpaint slots
			// clear zoom screen
			for (let i=0; i<4; i++) {
				if (!painted[i]) {
					let id_name = 'obj_canvas' + i;
					let tracking_canvas = document.getElementById(id_name);
					let destCtx = tracking_canvas.getContext('2d');
					let minx = (output_canvas.width - output_canvas.height)/2;
					let miny = 0;
					let maxx = output_canvas.width - minx - 1;
					let maxy = output_canvas.height - 1;
					if (typeof destCtx._cx == "undefined") {
						destCtx._cx = (minx + maxx)/2;
						destCtx._cy = (miny + maxy)/2;
						destCtx._width = maxx - minx + 1;
						destCtx._height = maxy - miny + 1;
						destCtx._speed = 0;
					} else {
						moveCamera(destCtx, minx, miny, maxx, maxy);
					}
					destCtx.drawImage(output_canvas, destCtx._cx - destCtx._width/2, destCtx._cy - destCtx._height/2, destCtx._width, destCtx._height, 0, 0, tracking_canvas.width, tracking_canvas.height);
					drawZoomInfo(output_canvas, destCtx, scale)
					destCtx.globalAlpha = 0.7;
					destCtx.beginPath();
					destCtx.fillStyle="#000000";
					destCtx.fillRect(0, 0, tracking_canvas.width, tracking_canvas.height);
					destCtx.fill();
					destCtx.globalAlpha = 1.0;
				}
			}
		} // for (const osd of this.osdLayers[osdIdx].osdArray.values()) {
	} // osd layer idx
}

createPlayer = () => {
	let name = "output_canvas";
	let videoPlayer = new AIVideo({ video: [name], osd: [{ canvas_name: name, accept_type:"facial_recognition"}]});
	let audioPlayer = null;
	let startModel = ["retinaface", "object_tracking"];
	videoPlayer.onImageReady = drawFaceTracking.bind(videoPlayer);
	let t = {
		kf_matrix: [
			1.0, 0.5, 0.1,
			0.0, 1.0, 0.5,
			0.0, 0.0, 0.3,
		],
		kf_noise: [0.1, 0.1],
		kf_maxDistThreshold: 80,
		kf_maxUnrefCount: 12,
		kf_maxTracking: 12,
		tracking_target: ["person", "car", "bicycle", "motorcycle", "bus", "train", "truck", "dog", "cat"]
	}

	return { video: videoPlayer, audio: audioPlayer, models: startModel, tracking: t };
}
