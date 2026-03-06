// create default video / audio player
createPlayer = () => {
	let name = "output_canvas";
	let videoPlayer = new AIVideo({ video: [name], osd: [{ canvas_name: name, accept_type:"object_detection"}] });
	let audioPlayer = null;
	let startModel = ["classify"];

	return { video: videoPlayer, audio: audioPlayer, models: startModel }; // no tracking
}
