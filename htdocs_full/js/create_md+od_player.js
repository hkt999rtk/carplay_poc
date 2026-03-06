// deal with UI
syncUI = (videoPlayer) => {
	let checkedCount = 0;
	for (let i=0; i<objectName.length; i++) {
		let id = "#cb_" + objectName[i];
		let flag = localStorage.getItem(id);
		if (flag != null && flag == "checked") {
		$(id).prop("checked", true);
			checkedCount++;
		} else {
			$(id).prop("checked", false);
		}
		$(id).change(function() {
			if ($(id).prop("checked")) {
				let list = videoPlayer.filterClasses;
				localStorage.setItem(id, "checked");
				videoPlayer.osdLayers[0].addClass(objectName[i]);
			} else {
				localStorage.setItem(id, "unchecked");
				videoPlayer.osdLayers[0].removeClass(objectName[i]);
			}
		});
	}
	if (checkedCount == 0) { // at least, we need person to be checked
		$("#cb_person").prop("checked", true);
		localStorage.setItem("#cb_person", "checked");
	}
}

// for ai_player CPU utilization callback
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

$("#object_detection").click(function() {
	if (this.checked) {
		videoCmdLink.startModel(["yolo4t"]);
	} else {
		videoCmdLink.stopModel(["yolo4t", "retinaface", "mobilenetface", "yamnet", "object_tracking", "palm_detection", "hand_landmark"]);
	}
});


setupObjectDetection = () => {
	videoCmdLink.setObjectDetection(aiParam.confidence, aiParam.nmsThreshold);
	aiSaveParam();
}

$("#od_confidence").change(function() {
	console.log("confidence: " + this.value)
	aiParam.confidence = parseInt(this.value);
	$("#label_confidence").text("Confidence: " + aiParam.confidence);
	setupObjectDetection();
});

$("#od_nms_threshold").change(function() {
	console.log("nms: " + this.value)
	aiParam.nmsThreshold = parseInt(this.value);
	$("#label_nms_threshold").text("NMS: " + aiParam.nmsThreshold);
	setupObjectDetection();
});

setupMotionDetection = () => {
	// set motion detection
	videoCmdLink.setMotionDetection($("#motion_detection").is(':checked'), aiParam.sensitivityBase, aiParam.sensitivityLum, aiParam.sensitivity);
	aiSaveParam();
}

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

	// update OD UI
	$("#label_confidence").text("Confidence: " + aiParam.confidence);
	$("#label_nms_threshold").text("NMS: " + aiParam.nmsThreshold);
	$("#od_confidence").val(aiParam.confidence);
	$("#od_nms_threshold").val(aiParam.nmsThreshold);
	
	// update ISP FPS
	$("#label_isp_fps").text("FPS: " + aiParam.ispFps);

    setupMotionDetection();
	setupObjectDetection();
	setupIspFps();
	videoCmdLink.setFilterClass([]);
}

setupIspFps = () => {
	// setup isp fps
	videoCmdLink.setIspFps(aiParam.ispFps);
	aiSaveParam();
}

// called when motion occurs
let timeoutId = 0;
let lightOnTime = 0;
let personCount = 0;
let personOnTime = 0;

startDetection= () => {
	$("#light").attr("src", "img/light_on.png"); // turn on the light after detecting motion
	videoCmdLink.setFilterClass(["person"]); // set person class, enable AI
	lightOnTime = new Date().getTime();
	personOnTime = 0;
	personCount = 0;
	timeoutId = setInterval(() => {
		checkEndDetection();
	}, 1000);
}

checkEndDetection = () => {
	let nowTime = new Date().getTime();
	if (lightOnTime != 0) {
		if (personOnTime != 0) {
			// person is detected, check the latest time
			if (nowTime - personOnTime > 1000) { // turn off the light after 1000ms of no person
				endDetection();
			} else {
				// there is still person, continue
			}
		} else {
			// no person is detected
			if (nowTime - lightOnTime > 1000) { // turn off the light after 1000ms of no person
				endDetection();
			}
		}
	}
}

endDetection = () => {
	videoCmdLink.setFilterClass([]); // clear all classes
	$("#person_logo").attr("src", "img/no_person.png");
	$("#light").attr("src", "img/light_off.png"); // turn off the light
	timeoutId = 0;
	lightOnTime = 0;
	personOnTime = 0;
	clearInterval(timeoutId); // clear timer
}

aiMdCallback = () => {
	if (lightOnTime == 0) {
		startDetection();
	} else {
		// continue to detect person
	}
}

aiOdCallback = (od) => {
	// object detection callback
	/*
	let objArray = od.result.detection;
	for (let i=0; i<objArray.length; i++) {
		let obj = objArray[i];
		// console.log(obj.class, obj.score);
	}
	*/

	if (od.result.detection.length > 0) {
		personCount++;
		let nowTime = new Date().getTime();
		if (personOnTime == 0) { // if no person is detected
			if (personCount > 3 && (nowTime - lightOnTime) < 600) { // if person is detected for 3 within 600ms
				$("#person_logo").attr("src", "img/person.png"); // show person logo
				personOnTime = nowTime; // set person on time
			}
		} else {
			// person flag is already now, just record the time
			personOnTime = nowTime;
		}
	}
}

// create default video / audio player
createPlayer = () => {
	let name = "output_canvas";
	let videoPlayer = new AIVideo({ video: [name], osd: [{ canvas_name: name, accept_type:"object_detection", filter_classes: ["person"] }] });
	let audioPlayer = null;
	let startModel = ["yolo4t"];
	syncUI(videoPlayer);
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