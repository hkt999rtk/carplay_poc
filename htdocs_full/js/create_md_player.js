function setProgressValue(element, newValue) {
    // Ensure newValue is within 0 and 100 range
    newValue = Math.max(0, Math.min(newValue, 100));

    // Update the text
    var progressValueDiv = element.querySelector('.progress-value');
    if (progressValueDiv) {
        progressValueDiv.innerText = newValue + '%';
    }

    // Update the circle
    var rightBar = element.querySelector('.progress-right .progress-bar');
    var leftBar = element.querySelector('.progress-left .progress-bar');

    if (newValue <= 50) {
        if (rightBar) {
            rightBar.style.transform = 'rotate(' + (newValue * 3.6) + 'deg)';
        }
        if (leftBar) {
            leftBar.style.transform = 'rotate(0deg)';
        }
    } else {
        if (rightBar) {
            rightBar.style.transform = 'rotate(180deg)'; // complete the right half
        }
        if (leftBar) {
            leftBar.style.transform = 'rotate(' + ((newValue - 50) * 3.6) + 'deg)';
        }
    }
}

cpuUtilCallback = function(cpu) {
    var blueProgressDiv = document.querySelector('.blue');
    setProgressValue(blueProgressDiv, cpu);  // Change to desired percentage
}

setupMotionDetection = () => {
	// set motion detection
	videoCmdLink.setMotionDetection($("#motion_detection").is(':checked'), aiParam.sensitivityBase, aiParam.sensitivityLum, aiParam.sensitivity);
    aiSaveParam();
}

setupIspFps = () => {
	// setup isp fps
	videoCmdLink.setIspFps(aiParam.ispFps);
	aiSaveParam();
}

// Tbase: 0 ~ 40, Tlum: 0 ~ 5, dynamic_thr 0/1, trigger_blocks:1~256
// param : { enable:1, Tbase: 2, Tlum: 3, dynamic_thr: 1, trigger_blocks: 3 } 
$("#motion_detection").click(function() {
	setupMotionDetection();
});

$("#md_base").change(function() {
	aiParam.sensitivityBase = parseInt(this.value);
	$("#label_base").text("Sensitivity Base: " + aiParam.sensitivityBase);
	setupMotionDetection();
});

$("#md_lum").change(function() {
	aiParam.sensitivityLum = parseInt(this.value);
	$("#label_lum").text("Sensitivity Lum: " + aiParam.sensitivityLum);
	setupMotionDetection();
});

$("#md_sensitivity").change(function() {
	aiParam.sensitivity = parseInt(this.value);
	$("#label_sensitivity").text("Sensitivity: " + aiParam.sensitivity);
	setupMotionDetection();
})

$("#isp_fps").change(function() {
	aiParam.ispFps = parseInt(this.value);
	$("#label_isp_fps").text("FPS: " + aiParam.ispFps);
	setupIspFps();
})

aiStartCallback =() => {
	// update MD UI
	$("#label_base").text("Sensitivity Base: " + aiParam.sensitivityBase);
	$("#label_lum").text("Sensitivity Lum: " + aiParam.sensitivityLum);
	$("#label_sensitivity").text("Sensitivity: " + aiParam.sensitivity);
	$("#label_isp_fps").text("FPS: " + aiParam.ispFps);
	$("#md_base").val(aiParam.sensitivityBase);
	$("#md_lum").val(aiParam.sensitivityLum);
	$("#md_sensitivity").val(aiParam.sensitivity);
	$("#isp_fps").val(aiParam.ispFps);

    setupMotionDetection();
	setupIspFps();
}

$("#video_streaming").click(function() {
	if (this.checked) {
		videoCmdLink.startVideo();
	} else {
		videoCmdLink.stopVideo();
	}
});

$("#ircut").click(function() {
	videoCmdLink.setIrCut(this.checked?1:0);
});

// create default video / audio player
createPlayer = () => {
	let name = "output_canvas";
	let videoPlayer = new AIVideo({ video: [name], osd: [{ canvas_name: name, accept_type:"object_detection", filter_classes: ["person"] }] });
	let audioPlayer = null;
	let startModel = [];
	//syncUI(videoPlayer);

	return { video: videoPlayer, audio: audioPlayer, models: startModel, tracking: null };
}
