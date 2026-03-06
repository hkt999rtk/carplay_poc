
$(function() {
  updateMenu();
  updateFooter();
  updateText(document.getElementById("web_ver"), "/web_ver", "Web Build: ");

  var addr = "ws://" + window.location.hostname + ":8081";
  ws = new WebSocket(addr);
  ws.onopen = function() {
    this.send(JSON.stringify({cmd:"get_version"}));
  }
  ws.onmessage = function(evt) {
    console.log("onmessage: " + evt.data)
    var obj = JSON.parse(evt.data);
    var vs = "";
    var value = obj.result.value;
    for (var i=0; i<value.length; i++) {
        vs = vs + value[i];
    }
    $("#fw_ver").html("Firmware Build: " + vs);
    ws.close();
  }
})
