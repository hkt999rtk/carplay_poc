// Hue (0-360)
// Saturation (0-1)
// Lightness (0-1)
const numberOfLamps = 5;
function hslToRgb(h, s, l) {
	function hueToRgb(p, q, t) {
	  if (t < 0) t += 1;
	  if (t > 1) t -= 1;
	  if (t < 1 / 6) return p + (q - p) * 6 * t;
	  if (t < 1 / 2) return q;
	  if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
	  return p;
	}
  
	if (s === 0) {
	  l = (Math.round(l * 255));
	  return [l, l, l];
	}
  
	let q = l < 0.5 ? l * (1 + s) : l + s - l * s;
	let p = 2 * l - q;
	h = h / 360;
  
	let r = Math.round(hueToRgb(p, q, h + 1 / 3) * 255);
	let g = Math.round(hueToRgb(p, q, h) * 255);
	let b = Math.round(hueToRgb(p, q, h - 1 / 3) * 255);
  
	return [r, g, b];
}

function rgbToHsl(r, g, b) {
	r /= 255;
	g /= 255;
	b /= 255;
  
	let max = Math.max(r, g, b);
	let min = Math.min(r, g, b);
	let h, s, l;
  
	// Calculate hue
	if (max === min) {
	  h = 0; // Achromatic (gray)
	} else if (max === r) {
	  h = ((g - b) / (max - min) + 6) % 6;
	} else if (max === g) {
	  h = (b - r) / (max - min) + 2;
	} else {
	  h = (r - g) / (max - min) + 4;
	}
	h *= 60;
  
	// Calculate lightness
	l = (max + min) / 2;
  
	// Calculate saturation
	if (max === min) {
	  s = 0; // Achromatic (gray)
	} else if (l <= 0.5) {
	  s = (max - min) / (max + min);
	} else {
	  s = (max - min) / (2 - max - min);
	}
  
	return [h, s, l];
}

// music player
let musicPlayer = null;

// draw square image for hand gesture
let dispCanavs = document.getElementById('disp_canvas');
let dispCtx = dispCanavs.getContext('2d');
let decoder = document.getElementById('decoder');
function drawSquare(output_canvas, scale)
{
	//dispCtx.scale(-1, 1); // flip horizontal
	//dispCtx.drawImage(decoder, 420, 0, 1080, 1080, 0, 0, -1080, 1080);
	dispCtx.drawImage(decoder, 210, 0, 1500, 1080, 0, 0, 1500, 1080);
	//dispCtx.restore();
 
	const centerX = dispCtx.canvas.width / 2;
	const centerY = dispCtx.canvas.height / 2;
	const radius = dispCtx.canvas.height * 90 / 360;

	dispCtx.strokeStyle = 'SeaGreen';
	dispCtx.lineWidth = 6;
	dispCtx.beginPath();
	dispCtx.arc(centerX, centerY, radius, 0, 2 * Math.PI);
	dispCtx.stroke();
}


// NOTE: when frame is updated, the gestureCallback will be called once
let rightGesture = ""
let rightAngle = 0
let rightStableCount = 0
let rightStableCandidate = ""
const repeatCount = 0
function gestureCallbackRight(hand, gesture, angle) {
	if (hand != "right")
		return;

	if (rightAngle != angle) {
		if (rightGesture != gesture) {
			if (rightStableCandidate != gesture) {
				rightStableCandidate = gesture;
				rightStableCount = 0;
			} else {
				// candidate is same as gesture
				rightStableCount++;
			}
		} else {
			// gesture is same as previous
			rightStableCount += (repeatCount + 3); // keep stable...
		}
		if (rightStableCount > repeatCount) { // this input is already stable
			rightGesture = gesture
			rightStableCandidate = ""
			gestureIn(hand, gesture)
		}
		rightAngle = angle;
	}
}

createPlayer = () => {
	let name = "decoder";
	let videoPlayer = new AIVideo(
		{
			video: [name], 
			osd: [
				{ type: "right_hand_2d", canvas_name: "right_hand_2d", accept_type: "hand_gesture", callback: gestureCallbackRight },
				{ type: "right_knob", canvas_name: "right_hand_3d", accept_type: "hand_gesture"},
			]
		}
	);
	let startModel = ["palm_detection", "hand_landmark"];
	videoPlayer.onImageReady = drawSquare.bind(videoPlayer);
	return { video: videoPlayer, audio: null, models: startModel};
}

switchTo3D = () => {
	$("#right_hand_3d").show();
	$("#right_hand").show();
}

// light dance !
const updateInterval = 100; // Update light colors every 100ms
let colorShiftSpeed = 16; // Higher value = faster color cycling

function turnOffAllLight() {
	for (let i = 0; i < numberOfLamps; i++) {
		videoCmdLink.lightRGB(i, 0, 0, 0, 0.1)
	}
}

let counter = 0;
function lightDance() {
	counter++;
	for (let i = 0; i < numberOfLamps; i++) {
		const hue = (counter * colorShiftSpeed + i * 360/numberOfLamps) % 360;
		const rgb = hslToRgb(hue, saturation, 0.5);
		videoCmdLink.lightRGB(i, rgb[0], rgb[1], rgb[2], 0.6);
	}
}

let lightDanceInterval = null;
function startLightDance() {
	if (!lightDanceInterval) {
		lightDanceInterval = setInterval(lightDance, updateInterval);
	}
}

function stopLightDance() {
	if (lightDanceInterval) {
		clearInterval(lightDanceInterval);
		lightDanceInterval = null;
		turnOffAllLight();
	}
}

let hue = 180
let saturation = 1
let lightness = 0.8
function initialLightColors() {
	const hueStep = 30 / numberOfLamps;
	for (let i = 0; i < numberOfLamps; i++) {
		let l_hue = hue + i * hueStep;
		const rgb = hslToRgb(l_hue, saturation, 0.5);
		if (lightness < 0.1) {
			videoCmdLink.lightRGB(i, 0, 0, 0, 0.1)
		} else {
			videoCmdLink.lightRGB(i, rgb[0]/2, rgb[1]/2, rgb[2]/2, lightness);
		}
	}
}

let musicIdx = 0;
function musicLoad() {
	let addr = "";
	if (window.location.hostname == "localhost") {
		addr = window.location.protocol + "//"+ window.location.hostname + ":" + window.location.port + "/music/music" + musicIdx + ".mp3";
	} else {
		addr = window.location.protocol + "//"+ window.location.hostname + "/play_music?file=" + musicIdx;
	}
	console.log("musicLoad: " + addr)
	musicPlayer = new Audio();
	if (musicPlayer != null) {
		musicPlayer.src = addr;
		musicPlayer.preload = "auto";
		musicPlayer.loop = true;
		musicPlayer.oncanplaythrough = function() {
			/* don't play */
		}
		musicPlayer.onerror = function() {
			musicPlayer = null;
		}
	}
}

function musicStart() {
	console.log("musicStart !");
	if (musicPlayer != null) {
		if (musicPlayer.paused) {
			musicPlayer.play();
		}
	} else {
		musicLoad();
	}
}

function musicStop() {
	console.log("musicStop !");
	if (musicPlayer != null) {
		if (!musicPlayer.paused) {
			musicPlayer.pause();
		}
	}
}

function musicNextTrack() {
	/*
	musicIdx = (musicIdx + 1) % 3;
	musicStop();
	musicLoad();
	*/
}

function musicPrevTrack() {
	/*
	musicIdx = (musicIdx + 2) % 3;
	musicStop();
	musicLoad();
	*/
}

const state_start = 0
const state_start_music_light = 1
var sceneState = -1;
const bodyElement = document.body;
function uiInit() {
	//videoCmdLink.pause() // stop audio player
	stopLightDance()
	turnOffAllLight()
}

function stateTransition(nextState) {
	if (sceneState == nextState)
		return; // no state change

	if (nextState == state_start) {
		if (sceneState >= 0) {
			// reload
			window.location.reload(true);
		}
	}

	sceneState = nextState;
	const canvas = document.getElementById("disp_canvas");
	const img = document.getElementById("begin_ok");

	setTimeout(() => {
		turnOffAllLight();
	}, 500);
	switch (sceneState) {
		case state_start:
			canvas.style.display = "none";
			img.style.display = "block";
			document.getElementById("right_hand_2d").style.display = "none";
			document.getElementById("right_hand_3d").style.display = "none";
			if (videoCmdLink == null || videoCmdLink.readyState != 1 ) {
				setTimeout(() => {
					if (videoCmdLink.readyState == 1 ) {
						uiInit()
					}
				}, 1000)
			} else {
				uiInit()
			}
			bodyElement.style.backgroundImage = 'url("img/homepage_bg.png"';
			break;

		case state_start_music_light:
			document.getElementById("right_hand_2d").style.display = "block";
			document.getElementById("right_hand_3d").style.display = "block";
			canvas.style.display = "block";
			img.style.display = "none";
			bodyElement.style.backgroundImage = 'url("img/inner_bg.png")';
			break;
	}
	console.log("save environment");
	envDemo.mainState = nextState;
	saveEnvironment();
}

let rejectGesture = false;
const menuElement = document.getElementById('menu_img');
function startGesture(hand, gesture) {
	if (rejectGesture) {
		return;
	}

	console.log("startGesture: " + hand + "-" + gesture);
	if (gesture == "ok" && rightAngle < 150 && rightAngle > -150) {
		rejectGesture = true;
		menuElement.src = 'img/home_ok_1.png';
		setTimeout(() => {
			menuElement.src = 'img/home_ok_2.png';
			setTimeout(() => {
				stateTransition(state_start_music_light);
				rejectGesture = false;
			}, 600);
		}, 300);
	} else if (gesture=="five") {
		rejectGesture = true;
		menuElement.src = 'img/home_five_1.png';
		setTimeout(() => {
			menuElement.src = 'img/home_five_2.png';
			setTimeout(() => {
				window.location.href = 'face_wakeup';
				rejectGesture = false;
			}, 600);
		}, 300);
	}
}

let prevAngleRight = 0;
function musicLightGesture(hand, gesture) {
	console.log("musicLightGesture: " + hand + "-" + gesture, "rightAngle: " + rightAngle)
	let angle = 0
	if (hand == "right") {
		angle = rightAngle;
		console.log("right angle: " + angle + " prev: " + prevAngleRight + " gesture: " + gesture);
		if (prevAngleRight == rightAngle) {
			return;
		}
	} 
	/*
	else if (hand == "left") {
		angle = leftAngle
		console.log("left angle: " + angle + " prev: " + prevAngleLeft + " gesture: " + gesture)
		if (prevAngleLeft == leftAngle) {
			return;
		}
	}
	*/
	if (angle > 480) {
		return // skip, for palm down noise
	}
	if (angle < -480) {
		return // skip, for palm down noise
	}
	if (angle > 400) {
		angle = 400;
	}
	if (angle < -400) {
		angle = -400;
	}

	switch (gesture) {
		case "two":
			if (lightDanceInterval == null) {
				/*
				if (envDemo.language == langEnUS) {
					swal("Start Light Dance", { buttons: false, timer: 2000})
				} else {
					swal("開啟燈舞", { buttons: false, timer: 2000})
				}
				*/
				musicStart();
				startLightDance();
			}
			break;

		case "five":
			stopLightDance();
			hue = (400-angle) / 800 * 360;
			lightness = 0.8;
			initialLightColors()
			musicStart();
			if (musicPlayer != null) {
				let volume = (400-angle)/800;
				if (volume < 0) volume = 0.0;
				if (volume > 1) volume = 1.0;
				musicPlayer.volume = 1-volume; // DIRTY, FIX IT
			}
			break

		case "fist":
			rightAngle += 1;
			stopLightDance();
			musicStop();
			turnOffAllLight();
			break
	}
}

function gestureIn(hand, gesture) {
	switch (sceneState) {
		case state_start:
			console.log("state_start: " + hand + "-" + gesture)
			if (videoCmdLink.audioPlaying) {
				videoCmdLink.pause();
			}
			startGesture(hand, gesture);
			break;
		case state_start_music_light:
			console.log("state_start_music_light: " + hand + "-" + gesture)
			musicLightGesture(hand, gesture);
			break;
	}
}

//$(function() {
document.addEventListener('DOMContentLoaded', function() {
	switchTo3D();
	musicLoad();
	stateTransition(state_start);
	// for guesture command simulation
	setTimeout(() => {
		console.log("HOOK");
		document.addEventListener('keydown', function(event) {
			switch (event.key) {
				case "PageUp":
					window.location.reload(true);
					//openFullscreen(document.documentElement);
					break;

				case "PageDown":
					window.location.reload(true);
					//openFullscreen(document.documentElement);
					break;

				case "Tab":
					break;
			}
		});

		document.addEventListener('keypress', function(event) {
			console.log("----------- keypress:", event.key);
			let gesture = "";
			let hand = "right";
			switch (event.key) {
				case '0':
					gesture = "fist"
					break;
				case '1':
					gesture = "one"
					break;
				case '2':
					gesture = "two"
					break;
				case '#':
					hand = "left"
				case '3':
					gesture = "ok"
					break
				case '4':
					gesture = "four"
					break;
				case '%':
					hand = "left"
				case '5':
					gesture = "five"
					break;
				case '6':
					gesture = "rock"
					break;
			}
			if (gesture != "") {
				/*
				if (hand=="left") {
					leftAngle = leftAngle + 2;
					if (leftAngle > 400) {
						leftAngle = -400;
					}
				}
				*/
				if (hand=="right") {
					rightAngle = rightAngle + 2
					if (rightAngle > 400) {
						rightAngle = -400;
					}
				}
				gestureIn(hand, gesture);
				console.log("keyboard gesture: " + hand + "-" + gesture);
			}
		})
	}, 1000);
})
