var mycenter = [{cx: 1920/2, cy: 1080/2}, {cx: 1920/2, cy: 1080/2}];

createSnapShot = (srcName, dstName) => {
  let canvas = document.getElementById(dstName);
  let ctx = canvas.getContext("2d");
  let src = document.getElementById(srcName);
  ctx.drawImage(src, 0, 0);
}

$("#snapshot").click(() => {
  let center = videoCmdLink.getUnionCenter(); // TODO: handle scale here
  if ($("#smartBitrate:checked").val() != undefined) {
    mycenter[0] = center; // smart bitrate
    createSnapShot("output_canvas", "snapshot_with_sb");
    $("#message").html( "Snapshot with smart bitrate" );
    setTimeout(() => {
      $("#message").html("");
    }, 2000)
  } else {
    mycenter[1] = center; // no smart bitrate
    createSnapShot("output_canvas", "snapshot_no_sb");
    $("#message").html( "Snapshot without smart bitrate");
    setTimeout(() => {
      $("#message").html("");
    }, 2000)
  }
})

$("#oneshot").click(() => {
  if ($("#smartBitrate:checked").val() != undefined) {
    let center = videoCmdLink.getUnionCenter(); // TODO: handle scale here
    mycenter[0] = center; // smart bitrate
    createSnapShot("output_canvas", "snapshot_with_sb");
    $("#message").html( "Snapshot with smart bitrate" );
    videoCmdLink.smartBitrateOff();
    setTimeout(() => {
      let center = videoCmdLink.getUnionCenter(); // TODO: handle scale here
      mycenter[1] = center; // no smart bitrate
      createSnapShot("output_canvas", "snapshot_no_sb");

      // enable smart bitrate again
      let value = 2000000;
      videoCmdLink.smartBitrateOn({max_qp: 48, min_qp: 12, target_bitrate: value});
      toggleCompare();
    }, 1500)
  } else {
    alert("Please turn on the smart bitrate");
  }
})

updateCompare = () => {
  let src1 = document.getElementById("snapshot_no_sb");
  let src2 = document.getElementById("snapshot_with_sb");
  let output = document.getElementById("compare_canvas");
  let ctx = output.getContext("2d");
  let halfWidth = 1920/2;

  // draw smart bitrate
  let left = mycenter[0].cx - halfWidth/2;
  if (left < 0) left = 0;
  if (left > halfWidth) left = halfWidth;
  ctx.drawImage(src2, left, 0, halfWidth, 1080, 0, 0, halfWidth, 1080);
  ctx.font = "20px Arial";
  ctx.fillStyle = 'red';
  ctx.fillText("SmartBitrate ON", 10, 10+80);

  // draw no smart bitrate
  left = mycenter[1].cx - halfWidth/2;
  if (left < 0) left = 0;
  if (left > halfWidth) left = halfWidth;
  ctx.drawImage(src1, left, 0, halfWidth, 1080, halfWidth, 0, halfWidth, 1080);
}

let compare_flag = false;
toggleCompare = () => {
  if (compare_flag) {
    $('#compare_canvas').hide();
    $('#output_canvas').show();
    $('#snapshot').hide();
    $('#oneshot').show();
    $('#compare').text("Compare");
    $('#compare').hide();
    compare_flag = false;
  } else {
    updateCompare();
    $('#compare_canvas').show();
    $('#output_canvas').hide();
    $('#snapshot').hide();
    $('#oneshot').hide();
    $('#compare').text("Back to Live");
    $('#compare').show();
    compare_flag = true;
  }
}

$("#compare").click(() => {
  toggleCompare();
})

$(document).ready(function() {
  $('#snapshot_no_sb').hide();
  $('#snapshot_with_sb').hide();
  $('#compare_canvas').hide();
  $('#oneshot').show();
  $('#snapshot').hide();
  $('#compare').hide();
});
