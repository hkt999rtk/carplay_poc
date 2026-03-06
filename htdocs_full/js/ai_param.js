function setCookie(name, value, days) {
  const expires = new Date();
  expires.setTime(expires.getTime() + (days * 24 * 60 * 60 * 1000));
  document.cookie = name + '=' + value + ';expires=' + expires.toUTCString() + ';path=/';
}

function getCookie(name) {
  const cookieArr = document.cookie.split(';');
  for (let i = 0; i < cookieArr.length; i++) {
    const cookiePair = cookieArr[i].split('=');
    const cookieName = cookiePair[0].trim();
    if (cookieName === name) {
      return cookiePair[1];
    }
  }
  return null;
}

let aiParam = {
  sensitivityBase: 3,
  sensitivityLum: 3,
  sensitivity: 50,
  confidence: 70,
  nmsThreshold: 30,
  ispFps: 20,
};

let aiLoadParam = function() {
  json = getCookie("ai-param");
  if (json != null) {
    aiParam = JSON.parse(json);
  }
  console.log("aiLoadParam:", aiParam);
}

let aiSaveParam = function() {
  console.log("aiSaveParam:", aiParam);
  setCookie("ai-param", JSON.stringify(aiParam), 365);
}

aiLoadParam();
