// create hybrid player
createPlayer = () => {
	let videoPlayer = new AIVideo(
		{
			//video: ["obj_canvas", "face_canvas"],
			video: ["obj_canvas"],
			osd: [
				{ canvas_name: "obj_canvas", accept_type: "object_detection", filter_classes: ["person"] },
				//{ canvas_name: "face_canvas", accept_type: "facial_recognition", type: "face"},
				//{ canvas_name: "obj_canvas", type: "text", text: "Person Detection (YOLO4)", x: 20, y: 100, font: "90px Arial", color: "Lime"},
				//{ canvas_name: "face_canvas", type: "text", text: "Face Detection (RetinaNet)", x: 20, y: 100, font: "90px Arial", color: "Lime"},
			]
		}
	)
    let audioPlayer = new AIAudio({canvas_name: "sound_canvas", show_wave: true});
	//let startModel = ["yolo4t", "retinaface", "yamnet", "object_tracking"];
	let startModel = ["yolo4t", "yamnet"];
	let t = {
		kf_matrix: [
			1.0, 0.2, 0.1,
			0.0, 1.0, 0.2,
			0.0, 0.0, 0.1,
		],
		kf_noise: [0.1, 0.1],
		kf_maxDistThreshold: 80,
		kf_maxUnrefCount: 4,
		kf_maxTracking: 12,
		tracking_target: ["person", "car", "bicycle", "motorcycle", "bus", "train", "truck", "dog", "cat"] // TODO: think about this
	}

	return { video: videoPlayer, audio: audioPlayer, models: startModel, tracking: t};
}