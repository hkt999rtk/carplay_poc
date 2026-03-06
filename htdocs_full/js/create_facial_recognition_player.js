// create facial recognition player
createPlayer = () => {
	let name = "output_canvas";
	let videoPlayer = new AIVideo({ video: [name], osd: [{ canvas_name: name, accept_type:"facial_recognition", type: "face"}]});
	let audioPlayer = null;
	let startModel = ["retinaface", "mobilenetface"];

	return { video: videoPlayer, audio: audioPlayer, models: startModel};
}