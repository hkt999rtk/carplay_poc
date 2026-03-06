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

let envDemo = {};
let languageSupport = ['zh-TW', 'en-US'];
const langEnUS = 0;
const langZhTW = 1;

let loadEnvironment = function() {
  json = getCookie("env");
  if (json != null) {
    envDemo = JSON.parse(json);
  } else {
    // default value
    envDemo.mainState = -1;
  }
  //envDemo.language = langEnUS; // force language
  envDemo.language = langZhTW; // force language
  console.log("load environment:", envDemo);
}

let saveEnvironment = function() {
  console.log("save environment:", envDemo);
  setCookie("env", JSON.stringify(envDemo), 365);
}

let nextLanguage = function() {
  envDemo.language = (envDemo.language + 1) % languageSupport.length;
  saveEnvironment();
}

let showLanguage = function(id) {
  //document.getElementById(id).innerHTML = ""; // do nothing
  /*
  switch (envDemo.language) {
    case langEnUS:
      document.getElementById(id).innerHTML = "English";
      break;
    case langZhTW:
      document.getElementById(id).innerHTML = "中文";
      break;
    default:
      document.getElementById(id).innerHTML = envDemo.language;
      break;
  }
  */
}

loadEnvironment();
