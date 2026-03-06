function drawCircle(dispCtx, x, y, radius) {
	dispCtx.beginPath();
	dispCtx.arc(x, y, radius, 0, 2 * Math.PI, false);
	dispCtx.strokeStyle = 'yellow';
	dispCtx.lineWidth = 3;
	dispCtx.stroke();
}

function drawSquare(output_canvas, scale) {
	let dispCanvas = document.getElementById('disp_canvas');
	let dispCtx = dispCanvas.getContext('2d');
	let decoder = document.getElementById('decoder');
	let decoderCtx = decoder.getContext('2d');
	dispCtx.scale(-1, 1);
	drawCircle(decoderCtx, decoder.width / 2, decoder.height / 2, 1080/4); 
	dispCtx.drawImage(decoder, 420, 0, 1080, 1080, 0, 0, -1080, 1080);
	dispCtx.restore();
}

createPlayer = () => {
	let name = "decoder";
	// create left hands and right hands 
	let videoPlayer = new AIVideo({ video: [name], osd: [
		{ type: "left_hand_2d", canvas_name: "left_hand", accept_type:"hand_gesture"},
		{ type: "right_hand_2d", canvas_name:"right_hand", accept_type:"hand_gesture"}
	]});
	let startModel = ["palm_detection", "hand_landmark"];
	videoPlayer.onImageReady = drawSquare.bind(videoPlayer);
	return { video: videoPlayer, audio: null, models: startModel };
}

switchTo3D = () => {
	$("#switch_2d").show();
	$("#switch_3d").hide();
	if ($("#left_hand_3d").length) {
		$("#left_hand_3d").show();
	}
	$("#right_hand_3d").show();
	if ($("#left_hand").length) {
		$("#left_hand").hide();
	}
	$("#right_hand").hide();
}

switchTo2D = () => {
	$("#switch_2d").hide();
	$("#switch_3d").show();
	if ($("#left_hand_3d").length) {
		$("#left_hand_3d").hide();
	}
	$("#right_hand_3d").hide();
	if ($("#left_hand").length) {
		$("#left_hand").show();
	}
	$("#right_hand").show();
}

var nowDispMode = 0;
$("#switch_3d").click(function() {
	switchTo3D();
	nowDispMode = 1;
})

$("#switch_2d").click(function() {
	switchTo2D();
	nowDispMode = 0;
})

switchTo2D();
