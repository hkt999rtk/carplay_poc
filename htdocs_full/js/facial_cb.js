const DO_NOTHING = 0;
const DO_MEMBER_FACE = 1;
const DO_NON_MEMBER_FACE = 2;
let currentMode = DO_NOTHING;
let activated = false;
let finishState = false;

let nameCount = 0;
let firstFaceTime = new Date().getTime();
const chatBoxFast = document.getElementById("chat-box-fast");
const chatBoxSlow = document.getElementById("chat-box-slow");

/*
getDateTimeString = function() {
  let currentDate = new Date();

  // Get the individual components of the date and time
  let year = currentDate.getFullYear();
  let month = (currentDate.getMonth() + 1).toString().padStart(2, '0'); // Months are zero-based, so add 1
  let day = currentDate.getDate().toString().padStart(2, '0');
  let hours = currentDate.getHours().toString().padStart(2, '0');
  let minutes = currentDate.getMinutes().toString().padStart(2, '0');
  let seconds = currentDate.getSeconds().toString().padStart(2, '0');
  let milliseconds = currentDate.getMilliseconds().toString().padStart(3, '0');
  // Format the components into a string
  return '<b>' + year + '-' + month + '-' + day + ' ' + hours + ':' + minutes + ':' + seconds + '.' + milliseconds + '</b><br>';
}
*/
getDateTimeString = function() {
  return "";
}

turnOffAllLight = function() {
	for (let i = 0; i < numberOfLamps; i++) {
		videoCmdLink.lightRGB(i, 0, 0, 0, 0.1)
	}
}

copyFace = function(imgElement) {
    // output_canvas
    let sourceCanvas = document.getElementById('output_canvas');

    let cropWidth = 480;
    let cropHeight = 480;

    let targetCanvas = document.createElement('canvas');
    targetCanvas.width = 120;
    targetCanvas.height = 120;
    let targetContext = targetCanvas.getContext('2d');

    let sourceX = 720; 
    let sourceY = 130;

    targetContext.drawImage(
      sourceCanvas,
      sourceX, sourceY, cropWidth, cropHeight, // source rectangle
      0, 0, 120, 120 // destination rectangle
    );
    imgElement.src = targetCanvas.toDataURL();
}

updateProgress = function(element, duration) {
  let startTime = new Date().getTime();
  let interval = setInterval(function() {
    // total interval is 13.4s, bar width is 15s
    let now = new Date().getTime();
    let percent = (now - startTime) / 150;
    if (now - startTime > duration) {
      clearInterval(interval);
      element.textContent = (duration/1000).toFixed(1) + " Sec";
      return
    }
    element.style.width = percent + '%';
    element.setAttribute('aria-valuenow', percent);
    element.textContent = ((now - startTime)/1000).toFixed(1) + " Sec";
  }, 100);
}

startSlowCase = function() {
  updateProgress(document.getElementById('non_member'), 13400);
  chatBoxSlow.innerHTML = "";

  let msg = getDateTimeString();
  msg += "<div class='gpt-progress'>Detected by PIR";
  displayMessage(chatBoxSlow, msg, "gpt");
  setTimeout(function() {
    let msg = getDateTimeString();
    msg += "<div class='gpt-progress'>"
    msg += "<p class='pbar'>Stranger</p>";
    msg += "<table width=100%><tr align=center>";
    msg += "<td width=25%><p class='ptime'>1.2s</p></td><td width=25%><img src='img/bcam.png'></td><td width=50%><img style='padding: 5px' id='face_slow'></td>";
    msg += "</tr></table></div>";
    displayMessage(chatBoxSlow, msg, "gpt");

    let imgElement = document.getElementById('face_slow');
    copyFace(imgElement);

    let recording = 0;
    let recordingTimer = setInterval(function() {
      let msg = getDateTimeString();
      recording++;
      if (recording > 12) {
        clearInterval(recordingTimer);
        msg += "<div class='gpt-progress' style='color:balck;'>"
        msg += "<p class='pbar'>Go to sleep</p>";
        msg += "<table width=100%><tr align=center>";
        msg += "<td><p class='ptime'>0.2s</p><td><img src='img/sleep.png'></td>";
        msg += "</tr></table></div>";
        displayMessage(chatBoxSlow, msg, "gpt");
        msg = "<div class='gpt-progress'><h1>Total <b>13.4 Sec.</bL</div>";
        activated = false;
        finishState = true;
        displayMessage(chatBoxSlow, msg, "gpt");
      } else {
        msg += "<div class='gpt-progress'>"
        msg += "<p class='pbar'>Recording to cloud</p>";
        msg += "<table width=100%><tr align=center>";
        msg += "<td width=25%><p class='ptime'>" + recording + "s</p></td><td width=25%><img src='img/bcam.png'></td><td width=25% style='color:black;'><img src='img/arrow-right.png'></td><td width=25%><img id='snap" + recording + "' src='img/cloud-storage.png'></td>";
        msg += "</tr></table></div>";
        if (recording == 1) {
          displayMessage(chatBoxSlow, msg, "gpt");
        } else {
          displayMessageOverlay(chatBoxSlow, msg, "gpt");
        }
      }
    }, 1000);
  }, 1200);
}

startFastCase = function (name) {
  updateProgress(document.getElementById('member'), 1500);
  chatBoxFast.innerHTML = "";

  let msg = getDateTimeString()
  msg += "<div class='gpt-progress' style='color:black;'>Detected by PIR";
  displayMessage(chatBoxFast, msg, "gpt");
  setTimeout(function() {
    let msg = getDateTimeString();
    msg += "<div class='gpt-progress'>"
    msg += "<p class='pbar'>Face: " + name + "</p>";
    msg += "<table width=100%><tr align=center>";
    msg += "<td width=25%><p class='ptime'>1.2s</p></td><td width=25%><img src='img/bcam.png'></td><td width=50%><img style='padding: 5px' id='face_fast'></td>";
    msg += "</tr></table></div>";
    displayMessage(chatBoxFast, msg, "gpt");

    let imgElement = document.getElementById('face_fast');
    copyFace(imgElement);

    setTimeout(function() {
      let msg = getDateTimeString();
      msg += "<div class='gpt-progress' style='color:balck;'>"
      msg += "<p class='pbar'>Send message</p>";
      msg += "<table width=100%><tr align=center>";
      msg += "<td width=25%><p class='ptime'>0.1s</td><td width=25%><img src='img/bcam.png'></td><td width=25% style='color:black;'>Message<img src='img/arrow-right.png'></td><td width=25%><img src='img/phone.png'></td>";
      msg += "</tr></table></div>";
      displayMessage(chatBoxFast, msg, "gpt");

      setTimeout(function() {
        let msg = getDateTimeString();
        msg += "<div class='gpt-progress' style='color:balck;'>"
        msg += "<p class='pbar'>Go to sleep</p>";
        msg += "<table width=100%><tr align=center>";
        msg += "<td width=25%><p class='ptime'>0.2s</p></td><td><img src='img/sleep.png'></td>";
        msg += "</tr></table></div>";

        displayMessage(chatBoxFast, msg, "gpt");
        msg = "<div class='gpt-progress'><h1>Total <b>1.5 Sec.</b></div>";
        displayMessage(chatBoxFast, msg, "gpt");
        activated = false;
        finishState = true;
      }, 200);
    }, 100);
  }, 1200);
}

uiNameCallback = function(name) {
  if (activated) {
      return; // do nothing
  }
  if (currentMode == DO_NOTHING) {
    return;
  }
  if (finishState == true) {
    return;
  }

  if (currentMode == DO_MEMBER_FACE && ((name == "") || (name == "Guest"))) {
    return; // filter out guest
  }

  if (currentMode == DO_NON_MEMBER_FACE && (name != "" && name != "Guest")) {
    return; // filter out member
  }

  let now = new Date().getTime();
  if (now - firstFaceTime > 2500) {
    // first face already very old (more than 2.5s), drop the old counter
    firstFaceTime = now;
    nameCount = 0;
  }

  nameCount++;
  if (nameCount < 7) {
      return; // do nothing
  }
  turnOffAllLight();
  
  let msg = getDateTimeString() + "<br>";
  if (name == "") name = "Stranger";
  if (name == "Guest") name = "Stranger";

  if (name == "Stranger") {
    activated = true;
    startSlowCase();
  } else {
    activated = true;
    startFastCase(name);
  }
}

document.addEventListener('keydown', function(event) {
  console.log("key:", event.key);
  switch (event.key) {
    case "PageUp":
      window.location.href = 'main';
      break;

    case "PageDown":
      window.location.href = 'main';
      break;

    case "e":
      if (activated) {
        return;
      }
      switch (currentMode) {
        case DO_NOTHING:
          currentMode = DO_MEMBER_FACE;
          chatBoxFast.innerHTML = "";
          displayMessage(chatBoxFast, "<b>Facial Recognition</b><hr>", "gpt");
          finishState = false;
          break;
        case DO_MEMBER_FACE:
          currentMode = DO_NON_MEMBER_FACE;
          chatBoxSlow.innerHTML = "";
          displayMessage(chatBoxSlow, "<b>Facial Recognition</b><hr>", "gpt");
          finishState = false;
          break;
        case DO_NON_MEMBER_FACE:
          chatBoxFast.innerHTML = "";
          chatBoxSlow.innerHTML = "";
          currentMode = DO_NOTHING;
          finishState = false;
          break;
      }
      break;
  }
});

document.addEventListener('keypress', function(event) {
  switch (event.key) {
    case '&':
      console.log("simulate uiNameCallback (Guest)");
      uiNameCallback("Guest");
      break;
    case '*':
      console.log("simulate uiNameCallback (Jimmy)");
      uiNameCallback("Jimmy");
      break;
  }
});

displayMessage = function(chatBox, message, sender) {
    const messageDiv = document.createElement("div");
    console.log("className:", `message ${sender}`);
    messageDiv.className = `message ${sender}`;
    if (sender == "gpt") {
      messageDiv.innerHTML = message;
    } else {
      messageDiv.innerHTML = message
    }
    chatBox.appendChild(messageDiv);
    chatBox.scrollTop = chatBox.scrollHeight;
}

displayMessageOverlay = function(chatBox, message, sender) {
  var lastDiv = chatBox.lastElementChild;
  chatBox.removeChild(lastDiv);
  displayMessage(chatBox, message, sender)
}

startDialog = function() {
  chatBoxFast.innerHTML = "";
  chatBoxSlow.innerHTML = "";

  setTimeout(function() {
    turnOffAllLight();
  }, 1000);
}

$(function() {
  startDialog();
});
