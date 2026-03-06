// CommandLink
// This is for settinig up the configuration of PRO2 AI

class ConfigLink extends WebSocket {
  constructor(url) {
    super(url);
    this.video2 = false;

    this.onopen = () => {
      console.log("config connection open");
      this.send(JSON.stringify({cmd: "get_config"}));
    };

    this.onclose = () => {
      console.log("config connection closed");
    };

    this.onmessage = (evt) => {
      if (!(evt.data instanceof ArrayBuffer)) {
        var obj = JSON.parse(evt.data);
        console.log(obj);
        switch (obj.type) {
            case "get_config":
              this.config = obj.result;
              this.updateToUI();
              break;
        }
      }
    }
  }

  updateFromUI() {
    let config = this.config;

    config.video[0].width = parseInt($("#v1_width").val());
    config.video[0].height = parseInt($("#v1_height").val());
    config.video[0].bitrate = parseInt($("#v1_bitrate").val());
    config.video[0].fps = parseInt($("#v1_fps").val());
    config.video[0].gop = parseInt($("#v1_gop").val());
    config.video[0].min_qp = parseInt($("#v1_minqp").val());
    config.video[0].max_qp = parseInt($("#v1_maxqp").val());
    config.video[0].delta_qp = parseInt($("#v1_deltaqp").val());

    if (this.video2) {
      config.video[1].width = parseInt($("#v2_width").val());
      config.video[1].height = parseInt($("#v2_height").val());
      config.video[1].bitrate = parseInt($("#v2_bitrate").val());
      config.video[1].fps = parseInt($("#v2_fps").val());
      config.video[1].gop = parseInt($("#v2_gop").val());
      config.video[1].min_qp = parseInt($("#v2_minqp").val());
      config.video[1].max_qp = parseInt($("#v2_maxqp").val());
      config.video[1].delta_qp = parseInt($("#v2_deltaqp").val());
    }
  }

  updateToUI() {
    let config = this.config;
    $("#v1_width").val(config.video[0].width);
    $("#v1_height").val(config.video[0].height);
    $("#v1_bitrate").val(config.video[0].bitrate);
    $("#v1_fps").val(config.video[0].fps);
    $("#v1_gop").val(config.video[0].gop);
    $("#v1_minqp").val(config.video[0].min_qp);
    $("#v1_maxqp").val(config.video[0].max_qp);
    $("#v1_deltaqp").val(config.video[0].delta_qp);
    if (config.video.length>1) {
      $("#v2_width").val(config.video[1].width);
      $("#v2_height").val(config.video[1].height);
      $("#v2_bitrate").val(config.video[1].bitrate);
      $("#v2_fps").val(config.video[1].fps);
      $("#v2_gop").val(config.video[1].gop);
      $("#v2_minqp").val(config.video[1].min_qp);
      $("#v2_maxqp").val(config.video[1].max_qp);
      $("#v2_deltaqp").val(config.video[1].delta_qp);
      $("#video2").show();
      $("#video2_title").html("Video 2 Setting");
      this.video2 = true;
    } else {
      $("#video2").hide();
      $("#video2_title").html("");
      this.video2 = false;
    }
  }

  setConfig = (conf) => {
    if (this.readyState == 1) {
      this.send(JSON.stringify({cmd: "set_config", param: conf}, null, '\t'));
    }
  }

  loadConfig = () => {
    this.send(JSON.stringify({cmd: "get_config"}));
    window.alert("Reload config success");
  }

  saveConfig = () => {
    if (this.readyState == 1) {
      this.updateFromUI();
      this.setConfig(this.config);
      window.alert("Save config success");
    }
  }

  deviceReboot = () => {
    if (this.readyState == 1) {
      this.send(JSON.stringify({cmd:"reboot"}));
    }
  }
  deviceResetDefault = () =>{
    if (this.readyState == 1) {
      this.send(JSON.stringify({cmd:"reset_default"}));
    }
  }
}

var configLink = new ConfigLink("ws://" + window.location.hostname + ":8081");

///////////////////////////
const total_tick = 8000;
const tick_interval = total_tick / 40;
const total_count = total_tick / tick_interval;
$("#device_reset").click(function() {
  let counter = 0;
  configLink.deviceReboot();
  $("#pdiv").show();
  setInterval( function() {
    counter++;
    if (counter < total_count) {
      let w = Math.floor(100*counter/total_count);
      $("#progressbar").attr("aria-valuenow",w); 
      $("#progressbar").attr("style","width:"+w+"%");
    } else {
      window.location = "/smart_bitrate"
    }
  }, tick_interval);
})

$("#save_config").click(function() {
  configLink.saveConfig();
})

$("#reload_config").click(function() {
    configLink.loadConfig();
})


$(function() {
  $("#pdiv").hide();
  updateMenu();
  updateFooter();
})
