// create default video / audio player
createPlayer = () => {
	let name = "output_canvas";
	let videoPlayer = new AIVideo({ video: [name], osd: [] });
	return { video: videoPlayer, audio: null };
}