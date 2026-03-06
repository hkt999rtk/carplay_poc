// create default video / audio player
createPlayer = () => {
	let name = "output_canvas";
	let videoPlayer = new AIVideo({ video: [name], osd: [{ canvas_name: name }] });
	let startModel = [];

	return { video: videoPlayer, audio: null, models: startModel };
}
