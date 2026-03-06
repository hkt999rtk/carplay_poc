// create audio player only
createPlayer = () => {
	let videoPlayer = null;
    let audioPlayer = new AIAudio({canvas_name: "sound_canvas"});
	let startModel = ["yamnet"];

	return { video: videoPlayer, audio: audioPlayer, models: startModel };
}