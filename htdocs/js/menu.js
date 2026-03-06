//var features = ['Object Detection', 'Facial Recognition', 'Face Tracking', 'Sound Events', 'Hand Gesture', 'About'];
//var features = ['Object Detection', 'Single Tracking', 'Facial Recognition', 'Smart Exposure', 'Smart Bitrate', 'Sound Events', 'Hybrid', 'About'];
//var features = ['Hand Gesture', 'About'];
//var features = ['Main', 'Face Wakeup', 'About']
//var features = ['eNeural DMS', 'Sound Events', 'Object Detection', 'Face Tracking', 'Facial Recognition', 'Hand Gesture', 'Face Fusion']; 
//var features = ['eNeural DMS'];
//var features = ['Image Classification'];
var features =['Record Video'];

updateText = function(text, url, title) {
    var xhr = new XMLHttpRequest();
    xhr.open("GET", url);
    xhr.onload=function() {
        if (text != null) {
            text.innerHTML = title + xhr.responseText;
        }
    }
    xhr.send();
}

// Menu and Footer
updateMenu = function() {
    var menu = document.getElementById("menu");
    var ms = "";
    for (var i=0; i<features.length; i++) {
        s = features[i].toLowerCase();
        s = s.replace(" ", "_");
        var filename = window.location.href.substr(window.location.href.lastIndexOf("/")+1);
        if (filename == s) {
            ms += '<a class="nav-link fw-bold py-1 px-0 active" aria-current="page" href="' + s + '">' + features[i] + '</a>';
        } else {
            ms += '<a class="nav-link fw-bold py-1 px-0" href="' + s + '">' + features[i] + '</a>';
        }
    }
    menu.innerHTML = ms;
}

updateFooter = function() {
    var footer = document.getElementById("footer");
    footer.innerHTML = '<footer class="mt-auto text-white-50"> </footer>';
}
