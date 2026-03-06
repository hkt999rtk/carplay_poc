
/*
for (const osd of this.osdArray.values()) {
	if (osd.result.status == "ok") {
		let res = osd.result
		let detection = res.detection;

		// draw box
		ctx.lineWidth = 2 * scale;
		for (let i=0; i<detection.length; i++) {
			ctx.beginPath();
			let od = detection[i];
			ctx.strokeStyle = classToColor(od.class);
			let bw = od.maxx - od.minx + 1;
			let bh = od.maxy - od.miny + 1;
			ctx.rect(od.minx * scale, od.miny * scale, bw * scale, bh * scale);
			ctx.stroke();
		}

		// draw class label
		ctx.font = "50px Arial";
		for (let i=0; i<detection.length; i++) {
			let od = detection[i];
			ctx.fillStyle = classToColor(od.class);
			//let labelName = od.class + " " + od.score.toFixed(0) + "%";
			ctx.fillText(od.class, od.minx * scale, (od.miny - 5) * scale);
		}
	}
}
*/

function drawSingleTracking(output_canvas, scale) {
	for (let osdIdx = 0; osdIdx < this.osdLayers.length; osdIdx++) {
		for (const osd of this.osdLayers[osdIdx].osdArray.values()) {
			if (osd.result.status == "ok") {
				let res = osd.result;
				let detection = res.detection;
			
				// union the tracking
				let union = getUnion(detection, this.tracking);
				union = addMargin(union, 1.1, 2.0);
				union = rectScale(union, scale);

				// draw tracking result
				let id_name = 'obj_canvas0';
				let tracking_canvas = document.getElementById(id_name);
				let destCtx = tracking_canvas.getContext('2d');
				union = adjustRect(union, output_canvas, tracking_canvas);
				if (typeof destCtx._cx == "undefined") {
					destCtx._cx = (union.minx + union.maxx)/2;
					destCtx._cy = (union.miny + union.maxy)/2;
					destCtx._width = union.maxx - union.minx + 1;
					destCtx._height = union.maxy - union.miny + 1;
					destCtx._speed = 0;
				} else {
					moveCamera(destCtx, union.minx, union.miny, union.maxx, union.maxy);
				}
				destCtx.globalAlpha = 1.0;
				destCtx.drawImage(output_canvas, destCtx._cx - destCtx._width/2, destCtx._cy - destCtx._height/2, destCtx._width, destCtx._height, 0, 0, tracking_canvas.width, tracking_canvas.height);
				drawZoomInfo(output_canvas, destCtx, scale)
			}
		}
	} // osd layer idx
}

createPlayer = () => {
	let name = "output_canvas";
	let videoPlayer = new AIVideo({ video: [name], osd: [{ canvas_name: name, accept_type:"object_detection"}]});
	let audioPlayer = null;
	let startModel = ["yolo4t"];
	videoPlayer.onImageReady = drawSingleTracking.bind(videoPlayer);
	let t = {
		kf_matrix: [
			1.0, 0.5, 0.1,
			0.0, 1.0, 0.5,
			0.0, 0.0, 0.3,
		],
		kf_noise: [0.1, 0.1],
		kf_maxDistThreshold: 80,
		kf_maxUnrefCount: 4,
		kf_maxTracking: 12,
		tracking_target: ["person", "car", "bicycle", "motorcycle", "bus", "train", "truck", "dog", "cat"]
	}

	return { video: videoPlayer, audio: audioPlayer, models: startModel, tracking: t };
}