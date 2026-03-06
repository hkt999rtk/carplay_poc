// create smart exposure player

createPlayer = () => {
	let name = "output_canvas";
	let videoPlayer = new AIVideo({ video: [name], osd: [
		{canvas_name: name, accept_type:"all", type: "smart_bitrate"},
		{canvas_name: name, accpet_type:"all", type: "bitrate"}
	]});

	let audioPlayer = null;
	let startModel = ["yolo4t", "object_tracking"];

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
		tracking_target: ["person"]
	}
	return { video: videoPlayer, audio: audioPlayer, models: startModel, tracking: t };
}