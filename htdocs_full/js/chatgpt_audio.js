// Inject secrets at runtime instead of committing them into source control.
const runtimeConfig = window.chatAudioConfig || {};
const pollyConfig = runtimeConfig.polly || {};
const hasPollyConfig = Boolean(pollyConfig.accessKeyId && pollyConfig.secretAccessKey);

if (hasPollyConfig) {
    AWS.config.update({
        region: pollyConfig.region || 'ap-northeast-1',
        accessKeyId: pollyConfig.accessKeyId,
        secretAccessKey: pollyConfig.secretAccessKey,
    });
}

let text2speechWorking = false;
async function text2Speech(text) {
    text2speechWorking = true;
    endRecognition();
    if (text == undefined) {
        console.log("error: no text to speech (1)");
        return;
    }
    if (text.trim().length === 0) {
        console.log("error: no text to speech (2)");
        return;
    }
    if (!hasPollyConfig) {
        console.warn("AWS Polly is not configured. Set window.chatAudioConfig.polly to enable text-to-speech.");
        text2speechWorking = false;
        return;
    }

    const polly = new AWS.Polly();
    const params = {
        OutputFormat: 'mp3',
        Text: text,
        VoiceId: 'Zhiyu',
        TextType: 'text',
    };

    try {
        const data = await polly.synthesizeSpeech(params).promise();
        const audioUrl = URL.createObjectURL(new Blob([data.AudioStream], { type: 'audio/mpeg' }));
        playAudio(audioUrl);
    } catch (error) {
        console.error('Error synthesizing speech:', error);
    }
    text2speechWorking = false;
}

let isAlreadyExecuted = false;
let isExecuting = false;
let audio = null;
let queue = [];
let remainQueue = [];
function checkNext() {
  if (audio != null) {
    return;
  }

  if (queue.length > 0) {
    const nextAudioUrl = queue.shift();
    // console.log('continue play next audio:', nextAudioUrl)
    audio = new Audio(nextAudioUrl);
    audio.onended = function(event) {
      // console.log('audio end !!');
      audio = null;
      checkNext();
    };
    audio.onerror = function(event) {
      console.error('audio error !!', event);
      audio = null;
      checkNext();
    };
    audio.play();
    audio.onerror = function(event) {
      // console.log("--------> catch audio error: ", event);
      audio = null;
    }
  } else {
    audio = null;
  }
}

function playAudio(audioUrl) {
  if (isExecuting) {
    remainQueue.push(audioUrl);
  } else {
    queue.push(audioUrl);
  }
  checkNext();
}

// OpenAI stuffs
const API_KEY = runtimeConfig.openaiApiKey || "";
const API_URL = runtimeConfig.openaiApiUrl || "https://api.openai.com/v1/chat/completions";

const chatBox = document.getElementById("chat-box");
const inputText = document.getElementById("input-text");

let history = [];
let splitResponse = [];
let playedResponse = -1;

function mycb() { }

let activated = false;
let nameCount = 0;
let sectionPrompt = "";
let firstFaceTime = new Date().getTime();
uiNameCallback = function(name) {
    if (activated) {
        return; // do nothing
    }

    let now = new Date().getTime();
    if (now - firstFaceTime > 2500) {
      // first face already very old (more than 2.5s), drop the old counter
      firstFaceTime = now;
      nameCount = 0;
    }
    console.log("now - firstFaceTime:", now - firstFaceTime, "activated:", activated);

    nameCount++;
    if (nameCount < 7) {
        return; // do nothing
    }

    for (let i=0; i<numberOfLamps; i++) {
      videoCmdLink.lightRGB(i, 255, 230, 120, 0.7)
    }

    // adjust the UI and models
    /*
    videoCmdLink.stopModel()
    videoCmdLink.stopModel(["yolo4t", "retinaface", "mobilenetface", "yamnet", "object_tracking", "palm_detection", "hand_landmark"]);
    videoCmdLink.startModel(["palm_detection", "hand_landmark"]);
    */

    /*
    let btnNewReg = document.getElementById("btNewReg");
    let btnShowReg = document.getElementById("btShowReg");
    let btnClearFaces = document.getElementById("btClearFaces");
    btnNewReg.style.display = "none";
    btnShowReg.style.display = "none";
    btnClearFaces.style.display = "none";
    */

    //
    history = [];
    splitResponse = [];
    playedResponse = -1;
    chatBox.innerHTML = "";
    inputText.innerHTML = "";
    activated = true;
    console.log('uiNameCallback:', name);
    if (name == "Guest" || name.trim() == "") {
      sessionPrompt = '歡迎主人回家，我是您的管家，今天心情如何?'
      document.getElementById("title").innerHTML = "<h3>主人回家</h3>";
    } else {
      sessionPrompt = '歡迎主人' + name + '回家，我是您的管家，您今天心情如何?'
      document.getElementById("title").innerHTML = "<h3>主人 (" + name + ") 回家</h3>";
    }
    displayMessage(sessionPrompt, "gpt");
    text2Speech(sessionPrompt);
    let response = { role: "assistant", content: sessionPrompt };
    history.push(response);
    audioInterval = setInterval(() => checkAudio(), 500); // not stopping voice audio check
}

let leftGesture = new LeftHandGesture({callback: mycb}); // TODO: dirty code mycb, fix it
let rightGesture = new RightHandGesture({callback: mycb}); // TODO: dirty code mycb, fix it
uiGptCallback = function(od) {
  if (od.class == "palm") {
    let result = "";
    if (od.handedness == "right") {
      result = rightGesture.update(od.landmark, "right", od.angle);
    } else {
      result = leftGesture.update(od.landmark, "left", od.angle);
    }
    if (result == "fist") {
      console.log("gesture fist detected, goback to main");
      window.location.href = 'main';
    }
  }
}

document.addEventListener('keydown', function(event) {
  switch (event.key) {
    case "PageUp":
    case "PageDown":
      console.log("go back to home");
      window.location.href = 'main';
      break;
    case "Tab":
      break;
  }
});

document.addEventListener('keypress', function(event) {
  if (event.key == '&') {
    console.log("simulate uiNameCallback");
    uiNameCallback("Guest");
  } else if (event.key == ')') {
    console.log("go back to home");
    window.location.href = 'main';
  }
});

function buildFetchConfig(data) {
    return {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
            "Authorization": `Bearer ${API_KEY}`,
        },
        body: JSON.stringify(data),
    };
}

function myHslToRgb(h, s, l) {
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

function myRgbToHsl(r, g, b) {
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

const lightInterval = 20;
let musicIsPlaying = false;
function setHslIndex(i, h, s, l) {
  rgb = myHslToRgb(h, s, l);
  let r = rgb[0];
  let g = rgb[1];
  let b = rgb[2];
  console.log("----> RUNNING setRGBIndex, id=", i, "r=", r, "g=", g, "b=", b);
  videoCmdLink.lightRGB(i, r, g, b, 0.7)
  let next = i+1;
  let next_h = h + lightInterval;

  if (next < numberOfLamps) {
    setTimeout(() => setHslIndex(next, next_h, s, l), 100);
  }
}

// API for GPT
let isLightStop = false;
function lightStop() {
  console.log("-----------------> lightStop called !!");
  if (isLightStop == true) {
    // lightStop is already called
    return;
  }
  stopMusic();
  setRGB(0, 0, 0);
  isExecuting = false;
  isAlreadyExecuted = true;
  isLightStop = true;
}

var homeStatus = {
  lightColor: [0, 0, 0],
  humidity: 15,
  temperature: 40,
  cameraOn: false,
  windowOpen: false,
  security: false,
  musicPlaying: false,
  shopping: [],
}

function genHomeStatus() {
  let msg = "";
  msg = "<p>房間溫度: " + homeStatus.temperature + " 度</p>";
  msg += "<p>房間濕度: " + homeStatus.humidity + " %</p>";
  msg += "<p>攝影機狀態: " + (homeStatus.cameraOn ? "開啟" : "關閉") + "</p>";
  msg += "<p>窗戶狀態: " + (homeStatus.windowOpen ? "開啟" : "關閉") + "</p>";
  msg += "<p>保全系統狀態: " + (homeStatus.security ? "開啟" : "關閉") + "</p>";
  msg += "<p>音響狀態: " + (homeStatus.musicPlaying ? "播放中" : "關閉") + "</p>";
  msg += "<p>購物清單: " + homeStatus.shopping + "</p>";
  home_status.innerHTML = msg;
 
  let rgbColor = homeStatus.lightColor.join(", ");
  home_status.style.backgroundColor = "rgb(" + rgbColor+ ")";

}

// API for pheripherals
function setRGB(r, g, b) {
  if (isLightStop) {
    r = 0; g = 0; b = 0;
  }
  let hsl = myRgbToHsl(r, g, b);

  setHslIndex(0, hsl[0] - lightInterval * 2, hsl[1], hsl[2]);
  homeStatus.lightColor = [r, g, b];
  genHomeStatus();
}

function setRoomHumidity(h) {
  console.log("setRoomHumidity:", h);
  genHomeStatus();
}

function setTemperature(t) {
  console.log("setTemperature:", t);
  genHomeStatus();
}

function cameraOn() {
  console.log("cameraOn");
  genHomeStatus();
}

function cameraOff() {
  console.log("cameraOff");
  genHomeStatus();
}

function cameraIsOn() {
  console.log("cameraIsOn");
  return homeStatus.cameraOn
}

function windowOpen() {
  console.log("windowOpen");
  genHomeStatus();
}

function windowClosed() {
  console.log("windowClosed");
  genHomeStatus();
}

function securityIsArmed() {
  console.log("securityIsArmed");
  genHomeStatus();
}

function securityArmed() {
  console.log("securityArmed");
  genHomeStatus();
}

function securityDisarmed() {
  console.log("securityDisarmed");
  genHomeStatus();
}

function goShoppingRealtekStore(items) {
  console.log("goShoppingRealtekStore:", items);
  genHomeStatus();
}

let goodNight = false;
function gptGoodNight() {
  stopMusic(); // stop original music
  goodNight = true;
  playMusic(); // play good night music
}

function gptExit() {
  setTimeout(() => {
    window.location.href = 'main';
  }, 2000);
}

const PLAY_ON_DEVICE = false;
let musicPlayer = null;
function playMusic() {
  if (musicIsPlaying) {
    return;
  }
  if (PLAY_ON_DEVICE) {
    videoCmdLink.nextTrack();
    musicIsPlaying = true;
  } else {
    if (musicPlayer != null) {
      musicPlayer.play();
    } else {
      // create music player
      let musicIdx = Math.floor(Math.random() * 3);
      let addr = window.location.protocol + "//"+ window.location.hostname + "/play_music?file=" + musicIdx;
      if (goodNight) {
        addr = window.location.protocol + "//"+ window.location.hostname + "/play_music?file=3"; // good night song
      }
      console.log("------------------------------------ BROWSER START PLAY MUSIC:", addr);
      musicPlayer = new Audio(addr);
      if (musicPlayer != null) {
        musicPlayer.onerror = function() {
          musicPlayer = null; // clear
          musicIsPlaying = false;
          console.log("Music player error occur");
        }
        musicPlayer.play();
        musicIsPlaying = true;
      } else {
        musicIsPlaying = false;
      }
    }
  }
  console.log("start playing music");
  setTimeout(stopMusic, 12000);
}

function stopMusic() {
  if (!musicIsPlaying) {
    return;
  }
  if (PLAY_ON_DEVICE) {
    videoCmdLink.pause();
    musicIsPlaying = false;
  } else {
    console.log("------------------------------------ BROWSER STOP PLAY MUSIC");
    if (musicPlayer != null) {
      musicPlayer.pause();
      musicPlayer = null;
    }
    musicIsPlaying = false;
  }
  console.log("stop playing music");
}

function extractCodeSections(markdownString) {
    const codeSectionRegex = /```javascript[\s\S]*?```/g;
    const codeSections = markdownString.match(codeSectionRegex) || [];

    return codeSections.map((section) => section.replace(/```javascript|```/g, "").trim());
}

function removeCodeSections(markdownString) {
    const codeSectionRegex = /```[\s\S]*?(?:```|$)/g;

    return markdownString.replace(codeSectionRegex, "");
}

function splitStringByPunctuation(inputString) {
    const punctuationMarks = [',', '，', '.', '。', ';', '；', ':', '：', '!', '！', '?', '？'];
    const replacedString = inputString.replace(new RegExp(`[${punctuationMarks.join('')}]+`, 'g'), '#');
    const splittedArray = replacedString.split('#');
    const filteredArray = splittedArray.filter((str) => str !== '');

    return filteredArray;
}

let systemPrompt = "";
switch (envDemo.language) {
  case langEnUS:
    systemPrompt = "You will play the role of a household butler, controlling the colors of the lights at home and programming different light shows based on the master's mood. The key points of your role are as follows:" +
      "1. If you don't know my mood, create a warm and low color temperature light show." +
      "2. You will use the JavaScript language to control the lights at home, knowing that you can directly use the setRGB(r, g, b) function in JavaScript to set the color of the lights." +
      "3. When setting the lights, in your JavaScript program, use 'setRGB(r, g, b)' directly to control the color of the lights. You can use other web JavaScript functions as well." +
      "4. If you want to create a light dance, the entire light dance should be controlled within 10 seconds." +
      "5. You are the butler, responsible for responding with warm care and adjusting the JavaScript code for the lights, but you don't need to explain how the code works to the master." + 
      "6. You need to ensure that the program will end within 10 seconds and call the JavaScript function lightStop() when it ends." +
      "7. Each response should ask the master what other services they need until the master no longer needs any." +
      "8. Please reply using Traditional Chinese, not Simplified Chinese." +
      "9. Each sentence of your response should not exceed 10 words." +
      "10. You don't need to say phrases like 'The following is the code', just present the code directly." +
      "11. If the master wants to go to sleep, create a very gentle goodnight light show, and at the beginning of the goodnight light show program, call the gptGoodNight() function." +
      "12. If the master no longer needs your services or asks you to rest, please end the response with a JavaScript code snippet that only calls the gptExit() function.";
    break;

  case langZhTW:
  default:
    systemPrompt = "你扮演一個家中管家的角色。扮演的要點如下:\n" + 
      ". 如果你不知道我的心情，就編一個低色溫的溫馨燈光秀。\n" +
      ". 你會寫 javascript 的語言來控制家中的燈光，房間溫度與濕度，窗戶的開或關，設定保全系統與開關 Camera。\n" + 
      ". 你可以在 Javascript 內直接使用 setRGB(r, g, b) 函數來設定燈光的顏色與亮度\n" +
      ". 你可以使用 setRoomHumidity(h) 來設定或是房間濕度，濕度介於 0 到 100 之間。\n" +
      ". 你可以使用 getRoomHumidity() 來得知目前房間的濕度。\n" +
      ". 你可以使用 setTemperature(t) 來設定房間溫度，溫度介於 10 到 40 之間。\n" +
      ". 你可以使用 getTemperature() 來得知目前房間的溫度。\n" +
      ". 你可以使用 cameraOn() 來開攝影機，使用 cameraOff() 來關攝影機。\n" +
      ". 你可以只用 cameraIsOn() 來得知攝影機是否開啟。\n" + 
      ". 你可以使用 windowOpen() 來開窗戶，使用 windowClose() 來關窗戶，使用 windowIsOpen() 來得知窗戶是不是開啟。\n" +
      ". 你可以使用 securityArmed() 來啟動保全系統，使用 securityDisarmed() 來關閉保全系統。\n" +
      ". 你可以使用 securityIsArmed() 來得知保全系統是否啟動。\n" +
      ". 你可以使用 goShoppingRealtekStore(items) 來訂購商品，items 是一個陣列，例如 [\"蘋果\", \"香蕉\", \"橘子\"]。\n" + 
      ". 你可以使用 playMusic() 來播放音樂，使用 stopMusic() 來停止音樂。\n"
      ". 如果你要編燈光舞蹈 Javascript 的時候，整個燈光舞蹈的時間控制在 10 秒內，燈光舞的內容竟量用迴圈和顏色彩度變與明亮度換來根據心情呈現多樣性。\n" +
      ". 你要確定燈光秀 Javascript 會在 10 內結束，同時在結束的時候會呼叫 Javascript 的 lightStop() 函數\n" +
      ". 每次回覆都需要再詢問主人還需要什麼服務，直到主人不需要為止。\n" +
      ". 回覆請使用繁體中文，不要使用簡體中文。\n" +
      ". 你回答的每一個句子，不要超過 20 個字。\n" +
      ". 不需要告訴主人\"以下是程式碼\" 這一類的意思，就直接呈現程式碼就好。\n" +
      // ". 如果主人想睡覺了，就編一個非常柔和的晚安燈光秀，在晚安燈光秀程式的最前面，呼叫 gptGoodNight() 函數。\n" +
      // ". 如果主人要離家的時候，要開啟 Camera，並且打開保全系統，主人回家的時候，記得要關閉 Camera，並且關閉保全系統。\n" +
      // ". 如果主人不需要你的服務了，或者請你休息，在回覆最後請寫一段 Javascript 而且只呼叫 gptExit() 函數。\n" +
      ". 你回覆主人的時候，如果是 Javascript，你應該把 Javascript 合併在一起，不要分開回覆。\n" + 
      "請開始認真扮演一個管家的角色, 以管家的口吻跟我對話"
    break;
}

let inferenceGPT4 = false;
let checkInterval = null;;
function checkSpeechEnd() {
  if (audio == null && queue.length == 0 && !inferenceGPT4) {
    if (isAlreadyExecuted && remainQueue.length > 0) {
      // move remainQueue to queue
      queue = remainQueue;
      remainQueue = [];
      console.log("move remainQueue to queue");
      checkNext(); // play all remaining audio queue
      clearInterval(checkInterval);
      checkInterval = null;
    }

    if (isAlreadyExecuted == false && isExecuting == false) { // no code is executing
      if (checkInterval != null) {
        clearInterval(checkInterval);
        checkInterval = null;
      }
    }
  }
}

let checkExeInterval = null;
function executeAfterAudioEnd(dynamicScriptCode) {
  if (audio == null && queue.length == 0) {
    console.log("RUN javascript code:", dynamicScriptCode[0]);

    goodNight = false;
    eval(dynamicScriptCode[0]); // the code
    //playMusic();
    clearInterval(checkExeInterval);
    checkExeInterval = null;
    setTimeout(() => lightStop(), 12000);
  }
}

function newVoiceSession() {
  splitResponse = [];
  playedResponse = -1;
}

async function pushGPT4() {
  if (inferenceGPT4 == true) {
    return; // already running
  }
  if (!API_KEY) {
    displayMessage("OpenAI API key is not configured.", "gpt");
    return;
  }
  isAlreadyExecuted = false;
  isExecuting = false;
  inferenceGPT4 = true;
  endRecognition();
  newVoiceSession();
  const userMessage = inputText.value.trim();
  inputText.value = "";
  displayMessage(userMessage, "user");
  let q = {};
  q.role = "user";
  q.content = userMessage;
  history.push(q);

  my_messages = history;
  let prompt = { role: "system", content: systemPrompt };
  my_messages.push(prompt);
  const data = {
    //model: "gpt-3.5-turbo",
    //model: "gpt-4-0314",
    model: "gpt-4-0613",
    messages: history,
    temperature: 1.0,
    stream: true,
  };

  const fetchConfig = buildFetchConfig(data);
  //console.log(fetchConfig.body);

  displayMessage("...", "gpt");
  const response = await fetch(API_URL, fetchConfig);
  const reader = response.body.getReader();
  let responseText = "";
  const processStream = async () => {
    try {
      while (true) {
        const { done, value } = await reader.read();

        if (done) {
          break;
        }

        const inputString = new TextDecoder().decode(value);
        const parts = inputString.split('data:').slice(1);
        const jsonArray = [];

        parts.forEach((part) => {
          console.log("part:", part);
          jsonArray.push(JSON.parse(part));
        });

        /*
        example message:
          id: "chatcmpl-6wjCfqoSnIjjqUjmdvwKB7VH6Rmp5",
          object: "chat.completion.chunk",
          created: 1679454805,
          model: "gpt-4-0314",
          choices: [{ delta: { content: " for" }, index: 0, finish_reason: null }],
        */
        for (let i=0; i<parts.length; i++) {
          if (jsonArray[i].choices[0].delta.content) {
            responseText += jsonArray[i].choices[0].delta.content;
          }
        }
        chatBox.lastChild.remove();

        let resp = responseText;
        displayMessage(resp, "gpt");

        // check if there is code or not
        dynamicScriptCode = extractCodeSections(responseText);
        if (dynamicScriptCode.length > 0 && !isAlreadyExecuted && !isExecuting) {
          isExecuting = true;
          isLightStop = false;
          checkExeInterval = setInterval(() => executeAfterAudioEnd(dynamicScriptCode), 200);
        }

        splitResponse = splitStringByPunctuation(removeCodeSections(responseText))
        if (playedResponse < splitResponse.length - 2) {
          // output the latest response
          playedResponse++;
          text2Speech(splitResponse[playedResponse]);
        }
      }
    } catch (error) {
      let resp = {};
      resp.role = "assistant";
      resp.content = responseText;
      history.push(resp);
      splitResponse = splitStringByPunctuation(removeCodeSections(responseText))
      //console.log("splitResponse:", splitResponse)
      //console.log("playedResponse:", playedResponse)
      while (playedResponse < splitResponse.length) {
        // output the latest response
        playedResponse++;
        text2Speech(splitResponse[playedResponse]);
      }

      checkInterval = setInterval(() => checkSpeechEnd(), 200);
    } finally {
      inferenceGPT4 = false;
      reader.releaseLock();
    }
  };
  processStream();
}

inputText.addEventListener("keypress", async (e) => {
  if (e.key === "Enter" && inputText.value.trim() !== "") {
    pushGPT4();
  }
});

function displayMessage(message, sender) {
    const messageDiv = document.createElement("div");
    console.log("className:", `message ${sender}`);
    messageDiv.className = `message ${sender}`;
    if (sender == "gpt") {
      const converter = new showdown.Converter();

      let html = converter.makeHtml(message);

      const parser = new DOMParser();
      const doc = parser.parseFromString(html, "text/html");
      doc.querySelectorAll("pre > code").forEach((block) => {
        Prism.highlightElement(block);
      });

      //console.log("doc.body.innerHTML:", doc.body.innerHTML);
      let m = "<table><tr><td style='padding: 5px; vertical-align: top;'><img src='img/chatgpt-logo.jpg' align='left' width=48 height=48></td><td>" + 
        doc.body.innerHTML + "</td></tr></table>";

      messageDiv.innerHTML = m;
    } else {
      messageDiv.innerHTML = message
    }
    chatBox.appendChild(messageDiv);
    chatBox.scrollTop = chatBox.scrollHeight;
}

// speech recognition relative code
const startButton = document.getElementById("start");
const stopButton = document.getElementById("stop");
startButton.style.display = "block";
stopButton.style.display = "none";

// Check if the browser supports SpeechRecognition
const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
const recognition = new SpeechRecognition();
recognition.continuous = true;
recognition.interimResults = true;

switch (envDemo.language) {
  case langEnUS:
      recognition.lang = "en-US";
      break;

    case langZhTW:
    default:
      recognition.lang = "zh-TW";
      break;
}

recognition.onresult = function(event) {
    let transcript = "";

    for (let i = event.resultIndex; i < event.results.length; i++) {
        if (event.results[i].isFinal) {
            transcript += event.results[i][0].transcript;
        }
    }
    if (transcript.trim() != "") {
      document.getElementById("input-text").value = transcript;
      pushGPT4();
    }
};

recognition.onnomatch = function(event) {
    console.log("voice no match, restart the recognition");
    recognition.stop(); 
    setTimeout(() => { recognition.start() }, 200);
};

recognition.onerror = function(event) {
    console.log("voice error:", event.error);
    recognition.stop();
    setTimeout(() => { recognition.start() }, 200);
}

let flagStartRecognition = false;
function startRecognition() {
    if (flagStartRecognition) {
      return;
    }
    console.log('recognition.start()');
    if (typeof recognition !== 'undefined') {
      recognition.start();
      flagStartRecognition = true;
    }
    startButton.style.display = "none";
    startButton.disabled = true;
    stopButton.style.display = "block";
    stopButton.disabled = false;
}

let audioInterval = null;
function checkAudio() {
  if (audio == null && queue.length == 0 && !inferenceGPT4) {
    // console.log("START audio recognition:", audio, queue.length, inferenceGPT4)
    startRecognition();
  } else {
    // console.log("END audio recognition:", audio, queue.length, inferenceGPT4)
    endRecognition();
  }
}

function endRecognition() {
  if (flagStartRecognition == false) {
    return;
  }
  console.log('recognition.end()');
  if (typeof recognition != "undefined") {
    recognition.stop();
    flagStartRecognition = false;
  }
  startButton.style.display = "block";
  startButton.disabled = false;
  stopButton.style.display = "none";
  stopButton.disabled = true;
}

// default visibility
startButton.style.display = "block";
stopButton.style.display = "none";

/*
startButton.onclick = function(event) {
  startRecognition();
}

stopButton.onclick = function(event) {
  endRecognition();
}
*/

genHomeStatus();
