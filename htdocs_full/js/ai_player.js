colorArray = [
    "AliceBlue", "AntiqueWhite", "Aqua,", "Aquamarine,", "Azure,", "Beige,", "Bisque,",
    "Black,", "BlanchedAlmond", "Blue", "BlueViolet", "Brown", "BurlyWood", "CadetBlue",
    "Chartreuse", "Chocolate", "Coral", "CornflowerBlue", "Cornsilk", "Crimson", "Cyan",
    "DarkBlue", "DarkCyan", "DarkGoldenRod", "DarkGray", "DarkGrey", "DarkGreen", "DarkKhaki",
    "DarkMagenta", "DarkOliveGreen", "DarkOrange", "DarkOrchid", "DarkRed", "DarkSalmon", "DarkSeaGreen",
    "DarkSlateBlue", "DarkSlateGray", "DarkSlateGrey", "DarkTurquoise", "DarkViolet", "DeepPink", "DeepSkyBlue",
    "DimGray", "DimGrey", "DodgerBlue", "FireBrick", "FloralWhite", "ForestGreen", "Fuchsia",
    "Gainsboro", "GhostWhite", "Gold", "GoldenRod", "Gray", "Grey", "Green",
    "GreenYellow", "HoneyDew", "HotPink", "IndianRed", "Indigo", "Ivory", "Khaki",
    "Lavender", "LavenderBlush", "LawnGreen", "LemonChiffon", "LightBlue", "LightCoral", "LightCyan",
    "LightGoldenRodYellow", "LightGray", "LightGrey", "LightGreen", "LightPink", "LightSalmon", "LightSeaGreen",
    "LightSkyBlue", "LightSlateGray", "LightSlateGrey", "LightSteelBlue", "LightYellow", "Lime", "LimeGreen",
    "Linen", "Magenta", "Maroon", "MediumAquaMarine", "MediumBlue", "MediumOrchid", "MediumPurple",
    "MediumSeaGreen", "MediumSlateBlue", "MediumSpringGreen", "MediumTurquoise", "MediumVioletRed", "MidnightBlue", "MintCream",
    "MistyRose", "Moccasin", "NavajoWhite", "Navy", "OldLace", "Olive", "OliveDrab",
    "Orange", "OrangeRed", "Orchid", "PaleGoldenRod", "PaleGreen", "PaleTurquoise", "PaleVioletRed",
    "PapayaWhip", "PeachPuff", "Peru", "Pink", "Plum", "PowderBlue", "Purple",
    "RebeccaPurple", "Red", "RosyBrown", "RoyalBlue", "SaddleBrown", "Salmon", "SandyBrown",
    "SeaGreen", "SeaShell", "Sienna", "Silver", "SkyBlue", "SlateBlue", "SlateGray",
    "SlateGrey", "Snow", "SpringGreen", "SteelBlue", "Tan", "Teal", "Thistle",
    "Tomato", "Turquoise", "Violet", "Wheat", "White", "WhiteSmoke", "Yellow",
    "YellowGreen"
];

classToColor = (str) => {
    let hash = 9;
    for (let i = 0; i < str.length; i++) {
        const char = str.charCodeAt(i);
        hash = (hash << 5) - hash + char;
        hash &= hash; // Convert to 32bit integer
    }
    if (hash < 0) {
        hash = -hash;
    }
    return colorArray[hash%colorArray.length];
}

class OsdLayer {
	constructor(param) {
        this.acceptType = param.accept_type;
        this.canvas = document.getElementById(param.canvas_name);
        if (this.canvas == null) {
            console.log("-------> ERROR: not found ", param.canvas_name)
            console.log(param)
        }
        this.osd = { result: {status: "ok", detection: []} }
        this.md = null;
        this.osdArray = new Map();

        if (typeof param.filter_classes != "undefined") {
            this.filterClasses = param.filter_classes; // setup the filter class
        } else {
            this.filterClasses == null;
            //this.filterClasses = [ "person" ] ; // fallback to default
        }

        this.refreshTime = new Date().getTime();
        this.refreshTimeMD = this.refreshTime;
	}

    setFilterClass(classes) {
        this.filterClasses = classes;
    }

    filterDetectedClass(osd) {
        if (osd.result.status == "ok") {
            let detection = osd.result.detection;
            let newDetection = [];
            for (let i=0; i<detection.length; i++) {
                let od = detection[i];
                if (this.filterClassed == null) {
                    newDetection.push(od);
                } else if (this.filterClasses.includes(od.class)) {
                    newDetection.push(od);
                }
            }
            osd.result.detection = newDetection;
        }

        return osd;
    }

    addClass(name) {
        for (let i=0; i<this.filterClasses.length; i++) {
            if (this.filterClasses[i] == name) {
                return;
            }
        }
        this.filterClasses.push(name);
    }

    removeClass(name) {
        for (let i=0; i<this.filterClasses.length; i++) {
            if (this.filterClasses[i] == name) {
                this.filterClasses.splice(i, 1);
                return;
            }
        }
    }

	setOsd(osd) {
        if (this.acceptType=="all" || this.acceptType == osd.type) {
            this.osdArray.set(osd.type, this.filterDetectedClass(structuredClone(osd)));
            this.refreshTime = new Date().getTime();
            if (osd.type == "object_detection" && typeof aiOdCallback != "undefined") {
                aiOdCallback(osd);
            }
        }
	}

    setMd(md) {
        this.md = md;
        this.refreshTimeMD = new Date().getTime();
        if (typeof aiMdCallback != "undefined") {
            aiMdCallback(md);
        }
    }

    drawOsd(ctx, scale) {
        let nowTime = new Date().getTime();
        let elaspedMD = nowTime - this.refreshTimeMD;
        if (elaspedMD < 1000 && this.md != null) {
            ctx.fillStyle = "rgba(255, 0, 0, 0.3)";
            for (let i=0; i<this.md.value.length; i++) {
                let roi = this.md.value[i];
                let x0 = Math.floor(roi.xmin*1920);
                let y0 = Math.floor(roi.ymin*1080);
                let w0 = Math.floor(roi.xmax*1920) - x0;
                let h0 = Math.floor(roi.ymax*1080) - y0;
                ctx.fillRect(x0, y0, w0, h0);
            }
            ctx.stroke();
        }

        let elapsed = nowTime - this.refreshTime;
        if (elapsed < 1000) {
            for (const osd of this.osdArray.values()) {
                if (osd.result.status == "ok") {
                    let res = osd.result
                    let detection = res.detection;

                    // draw box
                    ctx.lineWidth = 2 * scale;
                    for (let i=0; i<detection.length; i++) {
                        ctx.beginPath();
                        let od = detection[i];
                        ctx.strokeStyle = classToColor(od.class);
                        let bw = od.maxx - od.minx + 1;
                        let bh = od.maxy - od.miny + 1;
                        ctx.rect(od.minx * scale, od.miny * scale, bw * scale, bh * scale);
                        ctx.stroke();
                    }

                    // draw class label
                    ctx.font = "50px Arial";
                    for (let i=0; i<detection.length; i++) {
                        let od = detection[i];
                        ctx.fillStyle = classToColor(od.class);
                        //let labelName = od.class + " " + od.score.toFixed(0) + "%";
                        ctx.fillText(od.class, od.minx * scale, (od.miny - 5) * scale);
                    }
                }
            }
        }
    }

    /* --> for image classification demo */
    /*
    drawOsd(ctx, scale) {
        ctx.font = "70px Arial";
        ctx.fillStyle = "red";
        let s = ""
        let max = -1000;
        let pick = -1;
        for (let i=0; i<this.imageClasses.length; i++) {
            if (this.imageScores[i] > max && this.imageScores[i] >= 0.45) {
                max = this.imageScores[i];
                pick = i;
            }
        }
        if (pick >=0) {
            s = s + this.imageClasses[pick];

            //for (let i=0; i<this.imageClasses.length; i++) {
            //    s = s + this.imageClasses[i] + ":";
            //    s = s + (this.imageScores[i] * 100).toFixed(0) + "% ";
            //}
            ctx.fillText(s, 30, 120);
            console.log(s)
        }
    }
    */
};

class DriverStatusOsdLayer extends OsdLayer {
    constructor(param) {
        super(param)
    }
    drawOsd(ctx, scale) {
        super.drawOsd(ctx, scale)

        // for DMS
        // TODO: move draw driver monitor to another OSD class
        /*
        if (typeof this.closed_eye_weight == "undefined") {
            this.closed_eye_weight = 0;
            this.open_mouth_weight = 0;
        }

        let nowTime = new Date().getTime();
        let elaspedMD = nowTime - this.refreshTimeMD;
        if (elaspedMD < 1000 && this.md != null) {
            ctx.fillStyle = "rgba(255, 0, 0, 0.3)";
            for (let i=0; i<this.md.value.length; i++) {
                let roi = this.md.value[i];
                let x0 = Math.floor(roi.xmin*1920);
                let y0 = Math.floor(roi.ymin*1080);
                let w0 = Math.floor(roi.xmax*1920) - x0;
                let h0 = Math.floor(roi.ymax*1080) - y0;
                ctx.fillRect(x0, y0, w0, h0);
            }
            ctx.stroke();
        }

        // Draw Driver Monitoring System
        let elapsed = nowTime - this.refreshTime;
        if (elapsed < 1000) {
            for (const osd of this.osdArray.values()) {
                if (osd.result.status == "ok") {
                    let res = osd.result
                    let detection = res.detection;

                    // draw box
                    ctx.lineWidth = 2 * scale;
                    for (let i=0; i<detection.length; i++) {
                        ctx.beginPath();
                        let od = detection[i];
                        ctx.strokeStyle = classToColor(od.class);
                        let bw = od.maxx - od.minx + 1;
                        let bh = od.maxy - od.miny + 1;
                        ctx.rect(od.minx * scale, od.miny * scale, bw * scale, bh * scale);
                        ctx.stroke();
                    }

                    // draw class label
                    ctx.font = "50px Arial";
                    for (let i=0; i<detection.length; i++) {
                        let od = detection[i];
                        ctx.fillStyle = classToColor(od.class);
                        //let labelName = od.class + " " + od.score.toFixed(0) + "%";
                        ctx.fillText(od.class, od.minx * scale, (od.miny - 5) * scale);

                        switch (od.class) {
                            case "closed_eye":
                                this.closed_eye_weight = this.closed_eye_weight * 0.9 + 0.1;
                                break;
                            case "open_eye":
                                this.closed_eye_weight = this.closed_eye_weight * 0.9 - 0.1;
                                break;
                            case "closed_mouth":
                                this.open_mouth_weight = this.open_mouth_weight * 0.9 - 0.1;
                                break;
                            case "open_mouth":
                                this.open_mouth_weight = this.open_mouth_weight * 0.9 + 0.1;
                                break;
                        }
                    }
                    ctx.font = "200px Arial";
                    //console.log("closed_eye_weight:" + this.closed_eye_weight + " open_mouth_weight:" + this.open_mouth_weight)
                    if ((this.closed_eye_weight > 0.8) || (this.open_mouth_weight > 0.45)) {
                        ctx.fillStyle = "red";
                        ctx.fillText("Drowsy Driving", 30, 230);
                    } else {
                        ctx.fillStyle = "green";
                        ctx.fillText("Safe Driving", 30, 230);
                    }
                }
            }
        }
        */
    }
}

class BitrateOsdLayer extends OsdLayer {
    constructor(param) {
        super(param);
    }
    setOsd() {}
    drawOsd(ctx, scale) {
        ctx.font = "50px Arial";
        ctx.fillStyle = "LawnGreen";

        let fps = videoCmdLink.fps.toFixed(1);
        let bps = (videoCmdLink.averageFrameSize * videoCmdLink.fps * 8 / 1000).toFixed(0);
        if (bps > 1000) {
            bps = (bps / 1000).toFixed(2) + " Mbps";
        } else {
            bps = bps + " Kbps";
        }
        ctx.fillText("FPS: " + fps, 10, 60);
        ctx.fillText("BPS: " + bps, 10, 120);
    }
}

class TextOsdLayer extends OsdLayer {
    constructor(param) {
        super(param);
        this.param = param;
    }
    setOsd() {}
    drawOsd(ctx, scale) {
        ctx.font = this.param.font;
        ctx.fillStyle = this.param.color;
        ctx.fillText(this.param.text, this.param.x, this.param.y);
    }
}

class FaceOsdLayer extends OsdLayer {
    constructor(param) {
        super(param);
    }

	getDistance(od) {
		let reference = Math.sqrt((od.maxx - od.minx) * (od.maxy - od.miny));
        let dist = 60 / reference; // 640x360
        dist = dist.toFixed(2);
        return dist;
	}

	setOsd(osd) {
        if (this.acceptType=="all" || this.acceptType == osd.type) { 
            // TODO: fix this to OSD array
            this.osd = structuredClone(osd);
            this.refreshTime = new Date().getTime();
        }
	}

    drawOsd(ctx, scale) {
        if (this.osd == null)
            return;

        let nowTime = new Date().getTime();
        let elapsed = nowTime - this.refreshTime;
        if (elapsed > 1000) {
            return;
        }

        if (this.osd.result.status == "ok") {
            let res = this.osd.result
            let detection = res.detection;

            // draw box
            ctx.lineWidth = 4;
            for (let i=0; i<detection.length; i++) {
                ctx.beginPath();
                let od = detection[i];
                ctx.strokeStyle = "lightskyblue"
                let bw = od.maxx - od.minx + 1;
                let bh = od.maxy - od.miny + 1;
                ctx.rect(od.minx * scale, od.miny * scale, bw * scale, bh * scale);
                ctx.stroke();
            }
            // draw text
            ctx.font = "50px Arial";
            for (let i=0; i<detection.length; i++) {
                let od = detection[i];
                ctx.fillStyle = classToColor(od.class);
                let labelName = od.class;
                if (labelName=="") {
                    labelName = "Stranger";
                }
                //labelName = labelName + " - distance:" + this.getDistance(od) + "m";
                ctx.fillText(labelName, od.minx * scale, (od.miny - 5) * scale);
                ctx.stroke();
            }
        }
    }
};

/*
#        8   12  16  20
#        |   |   |   |
#        7   11  15  19
#    4   |   |   |   |
#    |   6   10  14  18
#    3   |   |   |   |
#    |   5---9---13--17
#    2    \         /
#     \    \       /
#      1    \     /
#       \    \   /
#        ------0-
connections = [
    (0, 1), (1, 2), (2, 3), (3, 4),
    (5, 6), (6, 7), (7, 8),
    (9, 10), (10, 11), (11, 12),
    (13, 14), (14, 15), (15, 16),
    (17, 18), (18, 19), (19, 20),
    (0, 5), (5, 9), (9, 13), (13, 17), (0, 17), (2,5)
*/

const palmConn = [
    {f:0, t:1}, {f:1, t:2}, {f:2, t:3}, {f:3, t:4},
    {f:5, t:6}, {f:6, t:7}, {f:7, t:8},
    {f:9, t:10}, {f:10, t:11}, {f:11, t:12},
    {f:13, t:14}, {f:14, t:15}, {f:15, t:16},
    {f:17, t:18}, {f:18, t:19}, {f:19, t:20},
    {f:0, t:5}, {f:5, t:9}, {f:9, t:13}, {f:13, t:17}, {f:0, t:17}, {f:2, t:5}
]

function rotate(x, y, cosTheta, sinTheta, cx, cy) {
    let x1 = x - cx;
    let y1 = y - cy;
    let x2 = x1 * cosTheta - y1 * sinTheta;
    let y2 = x1 * sinTheta + y1 * cosTheta;
    return {x: x2 + cx, y: y2 + cy};
}

function rotate3d(x, y, z, cosTheta, sinTheta, cosThetaZ, sinThetaZ, cx, cy, cz) {
    let x1 = x - cx;
    let y1 = y - cy;
    let z1 = z - cz;
    let x2 = x1 * cosTheta - y1 * sinTheta;
    let y2 = x1 * sinTheta + y1 * cosTheta;
    let z2 = z1;
    let x3 = x2 * cosThetaZ - z2 * sinThetaZ;
    let y3 = y2;
    let z3 = y2 * sinThetaZ + z2 * cosThetaZ;

    // vanish point
    let vanishZ = 900;
    let x4 = x3 * vanishZ / (vanishZ + z3 + 112);
    let y4 = y3 * vanishZ / (vanishZ + z3 + 112);

    return {x: x4 + cx, y: y4 + cy, z: z3 + cz};
}

class _HandGesture {
    constructor(param) {
        this.palmType = "unknown"
        this.callback = param.callback;
    }

    isThumbNearFirstFinger(p1, p2) {
        let x2 = p1.x - p2.x
        let y2 = p2.y - p2.y
        let distance = Math.sqrt(x2*x2 + y2*y2);
        return distance < 0.1;
    }

    getValue(landmark, index) {
        let x = landmark[index*3];
        let y = landmark[index*3+1];
        return {x: x, y: y};
    }

    update(_landmark, hand, angle) {
        if (hand != this.palmType) {
            console.log("error udpate wrong hand (mismatch) hand:" + hand + " palmType:" + this.palmType)
            return ""
        }

        let landmark = structuredClone(_landmark)
        // normalization
        for (let i = 0; i < 21; i++) {
            if (hand=="left") { // DIRTY CODE
                landmark[i*3] = (224-landmark[i*3]) / 224; // mirror for right hand
            } else {
                landmark[i*3] = landmark[i*3] / 224; // left
            }
            landmark[i*3+1] = landmark[i*3+1] / 224;
            landmark[i*3+2] = landmark[i*3+2] / 224;
        }

        let thumbIsOpen = false;
        let firstFingerIsOpen = false;
        let secondFingerIsOpen = false;
        let thirdFingerIsOpen = false;
        let fourthFingerIsOpen = false;

        let pseudoFixKeyPoint = this.getValue(landmark, 2);
        if ((this.getValue(landmark, 3).x < pseudoFixKeyPoint.x) && (this.getValue(landmark, 4).x < pseudoFixKeyPoint.x)) {
            thumbIsOpen = true;
        }

        pseudoFixKeyPoint = this.getValue(landmark, 6);
        if ((this.getValue(landmark, 7).y < pseudoFixKeyPoint.y) && (this.getValue(landmark, 8).y < pseudoFixKeyPoint.y)) { 
            firstFingerIsOpen = true;
        }

        pseudoFixKeyPoint = this.getValue(landmark, 10);
        if ((this.getValue(landmark, 11).y < pseudoFixKeyPoint.y) && (this.getValue(landmark, 12).y < pseudoFixKeyPoint.y)) {
            secondFingerIsOpen = true;
        }

        pseudoFixKeyPoint = this.getValue(landmark, 14);
        if ((this.getValue(landmark, 15).y < pseudoFixKeyPoint.y) && (this.getValue(landmark, 16).y < pseudoFixKeyPoint.y)) {
            thirdFingerIsOpen = true;
        }

        pseudoFixKeyPoint = this.getValue(landmark, 18);
        if ((this.getValue(landmark, 19).y < pseudoFixKeyPoint.y) && (this.getValue(landmark, 20).y < pseudoFixKeyPoint.y)) {
            fourthFingerIsOpen = true;
        }

        let result = "";
        if (thumbIsOpen && !firstFingerIsOpen && !secondFingerIsOpen && !thirdFingerIsOpen && !fourthFingerIsOpen) {
            result = "fist";
            //result = "thumb up";
        } else if (thumbIsOpen && firstFingerIsOpen && secondFingerIsOpen && thirdFingerIsOpen && fourthFingerIsOpen) {
            result = "five";
        } else if (!thumbIsOpen && firstFingerIsOpen && secondFingerIsOpen && thirdFingerIsOpen && fourthFingerIsOpen) {
            result = "four";
        } else if (thumbIsOpen && firstFingerIsOpen && secondFingerIsOpen && !thirdFingerIsOpen && !fourthFingerIsOpen) {
            result = "three";
        } else if (thumbIsOpen && firstFingerIsOpen && !secondFingerIsOpen && !thirdFingerIsOpen && !fourthFingerIsOpen) {
            result = "seven";
        } else if (!thumbIsOpen && firstFingerIsOpen && !secondFingerIsOpen && !thirdFingerIsOpen && !fourthFingerIsOpen) {
            result = "one";
        } else if (!thumbIsOpen && firstFingerIsOpen && secondFingerIsOpen && !thirdFingerIsOpen && !fourthFingerIsOpen) {
            result = "two";
        } else if (!firstFingerIsOpen && secondFingerIsOpen && thirdFingerIsOpen && fourthFingerIsOpen && this.isThumbNearFirstFinger(this.getValue(landmark, 4), this.getValue(landmark, 8))) {
            result = "ok";
        } else if (!thumbIsOpen && firstFingerIsOpen && !secondFingerIsOpen && !thirdFingerIsOpen && fourthFingerIsOpen) {
            result = "rock";
        } else if (thumbIsOpen && firstFingerIsOpen && !secondFingerIsOpen && !thirdFingerIsOpen && fourthFingerIsOpen) {
            result = "spiderman"
        } else if (!thumbIsOpen && !firstFingerIsOpen && !secondFingerIsOpen && !thirdFingerIsOpen && !fourthFingerIsOpen) {
            result = "fist";
        }
        if (this.callback != null) {
            this.callback(hand, result, angle);
        }
        return result;
    }
}

class RightHandGesture extends _HandGesture {
    constructor(param) {
        super(param)
        this.palmType = "right"
    }
}

class LeftHandGesture extends _HandGesture {
    constructor(param) {
        super(param)
        this.palmType = "left"
    }
}

const palmTimeout = 1500; // 1.5s, if there no detection in 1.5s, then clear the palm shape
class _HandOsdLayer extends OsdLayer {
    constructor(param) {
        super(param);
        this.zAngle = 0;
        this.osd = null;
        this.refreshTime = new Date().getTime();
    }

	setOsd(osd) {
        if (this.acceptType == osd.type) {
            if (osd.result.detection.length > 0) {
                let od = osd.result.detection[0];
                if (od.handedness == this.palmType) {
                    this.refreshTime = new Date().getTime();
                    this.osd = this.alignment(structuredClone(osd));
                    this.palmShape = this.handGesture.update(od.landmark, this.palmType, od.angle);
                }
                // if (od.handedness == "notsure") {}
            }
        }
	}

    alignment(osd) {
        for (let i=0; i<osd.result.detection.length; i++) {
            let od = osd.result.detection[i];
            if (od.class == "palm") {
                if (od.landmark.length > 0) {
                    let cx = od.landmark[9*3];
                    let cy = od.landmark[9*3+1];
                    let cz = od.landmark[9*3+2];
                    for (let j=0; j<21; j++) {
                        od.landmark[j*3] -= cx;
                        od.landmark[j*3+1] -= cy;
                        od.landmark[j*3+2] -= cz;
                        od.landmark[j*3] += 112;
                        od.landmark[j*3+1] += 112;
                        od.landmark[j*3+2] += 112;
                    }
                }
            }
        }
        return osd;
    }

    drawSingleHand3D(od) {
        let hand = this.canvas;
        if (hand == null) {
            return;
        }
        let ctx = hand.getContext('2d');
        ctx.fillStyle = "#333333";
        ctx.fillRect(0, 0, hand.width, hand.height);

        let angle = od.angle / 256.0;
        let cosTheta = Math.cos(angle);
        let sinTheta = Math.sin(angle);
        let cosThetaZ = Math.cos(this.zAngle);
        let sinThetaZ = Math.sin(this.zAngle);
        let cx = od.landmark[9*3];
        let cy = od.landmark[9*3+1]
        let cz = od.landmark[9*3+2];

        // draw the connections
        for (let i=0; i<palmConn.length; i++) {
            let f = palmConn[i].f;
            let t = palmConn[i].t;
            let fp = rotate3d(od.landmark[f*3], od.landmark[f*3+1], od.landmark[f*3+2],
                cosTheta, sinTheta, cosThetaZ, sinThetaZ, cx, cy, cz);
            let fx = fp.x * hand.width/224;
            let fy = fp.y * hand.height/224;
            let tp = rotate3d(od.landmark[t*3], od.landmark[t*3+1], od.landmark[t*3+2],
                cosTheta, sinTheta, cosThetaZ, sinThetaZ, cx, cy, cz);
            let tx = tp.x * hand.width/224;
            let ty = tp.y * hand.height/224;
            ctx.beginPath();
            ctx.lineWidth = 2;
            ctx.strokeStyle = "lightskyblue"
            ctx.moveTo(hand.width-fx, fy);
            ctx.lineTo(hand.width-tx, ty);
            ctx.stroke();
        }

        // draw the key points
        ctx.font = "10px serif";
        for (let i=0; i<21; i++) {
            let rp = rotate3d(od.landmark[i*3], od.landmark[i*3+1], od.landmark[i*3+2],
                cosTheta, sinTheta, cosThetaZ, sinThetaZ, cx, cy, cz);
            let fx = rp.x * hand.width/224;
            let fy = rp.y * hand.height/224;
            ctx.beginPath();
            ctx.fillStyle = "red";
            ctx.arc(hand.width-fx, fy, 3, 0, 2 * Math.PI);
            ctx.fill();
            ctx.fillStyle = "yellow";
            ctx.fillText(i.toString(), hand.width-fx+3, fy);
        }
        this.drawPalmShape(ctx, "3D")
    }

    drawPalmShape(ctx, dim) {
        /*
        ctx.font="20px serif";
        ctx.fillStyle = "white";
        ctx.fillText(this.palmShape + " - " + dim, 10, 20);
        */
    }

    drawSingleHand2D(od) {
        // draw the square images
        // let hand = document.getElementById(name);
        let hand = this.canvas;
        let ctx = hand.getContext('2d');
        //ctx.fillStyle = 'rgba(255, 255, 255)'; //"#333333";
        ctx.fillStyle = "#000000";
        ctx.fillRect(0, 0, hand.width, hand.height);

        let angle = od.angle / 256.0;
        // angle = -angle; // TODO: dirty code
        let cosTheta = Math.cos(angle);
        let sinTheta = Math.sin(angle);

        /*
        // flip-x of od
        for (let i=0; i<21; i++) {
            od.landmark[i*3] = 224 - od.landmark[i*3];
        }
        */
        
        // draw the connections
        let cx = od.landmark[9*3];
        let cy = od.landmark[9*3+1];
        for (let i=0; i<palmConn.length; i++) {
            let f = palmConn[i].f;
            let t = palmConn[i].t;
            let fp = rotate(od.landmark[f*3], od.landmark[f*3+1], cosTheta, sinTheta, cx, cy, 0);
            let fx = fp.x * hand.width/224;
            let fy = fp.y * hand.height/224;
            let tp = rotate(od.landmark[t*3], od.landmark[t*3+1], cosTheta, sinTheta, cx, cy, 0);
            let tx = tp.x * hand.width/224;
            let ty = tp.y * hand.height/224;
            ctx.beginPath();
            ctx.lineWidth = 2;
            ctx.strokeStyle = "lightskyblue"
            ctx.moveTo(hand.width-fx, fy);
            ctx.lineTo(hand.width-tx, ty);
            ctx.stroke();
        }

        // draw the key points
        ctx.font = "10px serif";
        for (let i=0; i<21; i++) {
            let rp = rotate(od.landmark[i*3], od.landmark[i*3+1], cosTheta, sinTheta, cx, cy);
            let fx = rp.x * hand.width/224;
            let fy = rp.y * hand.height/224;
            ctx.beginPath();
            ctx.fillStyle = "red";
            ctx.arc(hand.width-fx, fy, 3, 0, 2 * Math.PI);
            ctx.fill();
            ctx.fillStyle = "#b8860b";
            ctx.fillText(i.toString(), hand.width-fx+3, fy);
        }

        this.drawPalmShape(ctx, "2D");
        /*
        // flip-x of od back
        for (let i=0; i<21; i++) {
            od.landmark[i*3] = 224 - od.landmark[i*3];
        }
        */
    }

    clearHand() {
        let hand = this.canvas
        if (hand == null) {
            return;
        }
        let ctx = hand.getContext('2d');
        //ctx.fillStyle = "#ffffff";
        ctx.fillStyle = "#000000";
        ctx.fillRect(0, 0, hand.width, hand.height);
    }

    drawOsd() {
        let now = new Date().getTime();
        if (now - this.refreshTime > palmTimeout) {
            this.clearHand()
            this.refreshTime = now
            this.osd = null
            return
        }
        this.drawHand()
    }
}

class RightHandOsdLayer2D extends _HandOsdLayer {
    constructor(param) {
        super(param)
        this.palmType = "right"
        this.handGesture = new RightHandGesture({callback:param.callback})
    }

    drawHand() {
        let osd = this.osd;
        if (osd == null) {
            return;
        }

        for (let i=0; i<osd.result.detection.length; i++) {
            let od = osd.result.detection[i];
            if ((od.class == "palm") && (od.handedness == this.palmType)) {
                this.drawSingleHand2D(od)
            }
        }
    }
}

class RightHandOsdLayer3D extends _HandOsdLayer {
    constructor(param) {
        super(param)
        this.palmType = "right"
        this.handGesture = new RightHandGesture({callback:param.callback})
    }

    drawHand() {
        let osd = this.osd;
        if (osd == null) {
            return;
        }

        for (let i=0; i<osd.result.detection.length; i++) {
            let od = osd.result.detection[i];
            if ((od.class == "palm") && (od.handedness == this.palmType)) {
                this.drawSingleHand3D(od)
            }
        }
        this.zAngle += 0.03;
        if (this.zAngle > 2 * Math.PI) {
            this.zAngle -= 2 * Math.PI;
        }
    }
}

class LeftHandOsdLayer2D extends _HandOsdLayer {
    constructor(param) {
        super(param)
        this.palmType = "left"
        this.handGesture = new LeftHandGesture({callback:param.callback}); // TODO: don't use this callback !!
    }

    drawHand() {
        let osd = this.osd;
        if (osd == null) {
            return;
        }

        for (let i=0; i<osd.result.detection.length; i++) {
            let od = osd.result.detection[i];
            if ((od.class == "palm") && (od.handedness == this.palmType)) {
                this.drawSingleHand2D(od)
            }
        }
    }
}

class LeftHandOsdLayer3D extends _HandOsdLayer {
    constructor(param) {
        super(param)
        this.palmType = "left"
        this.handGesture = new LeftHandGesture({callback:param.callback});
    }

    drawHand() {
        let osd = this.osd;
        if (osd == null) {
            return;
        }

        for (let i=0; i<osd.result.detection.length; i++) {
            let od = osd.result.detection[i];
            if ((od.class == "palm") && (od.handedness == this.palmType)) {
                this.drawSingleHand3D(od)
            }
        }
        this.zAngle += 0.03;
        if (this.zAngle > 2 * Math.PI) {
            this.zAngle -= 2 * Math.PI;
        }
    }
}

function drawKnob(context, pointerAngle, webColor, label) {
    pointerAngle = -pointerAngle;
    const centerX = context.canvas.width / 2;
    const centerY = context.canvas.height / 2;
    const radius = Math.min(centerX - 10, centerY - 10); 

    context.beginPath();
    context.arc(centerX, centerY, radius, 0, 2 * Math.PI);
    const gradient = context.createRadialGradient(centerX, centerY, radius * 0.5, centerX, centerY, radius);
    gradient.addColorStop(0, webColor);
    gradient.addColorStop(1, "black");
    context.fillStyle = gradient;
    context.fill();

    const numTicks = 10;
    const tickLength = radius * 0.1;
    const tickStart = radius * 0.9;
    const anglePerTick = (2 * Math.PI) / numTicks;

    context.lineWidth = 2;
    context.strokeStyle = "gray";
    context.fillStyle = "black";
    context.font = "12px Arial";
    context.textAlign = "center";
    context.textBaseline = "middle";
  
    for (let i = 0; i < numTicks; i++) {
        const angle = i * anglePerTick - Math.PI / 2;

        const startX = centerX + Math.cos(angle) * tickStart;
        const startY = centerY + Math.sin(angle) * tickStart;
        const endX = centerX + Math.cos(angle) * (tickStart + tickLength);
        const endY = centerY + Math.sin(angle) * (tickStart + tickLength);

        context.beginPath();
        context.moveTo(startX, startY);
        context.lineTo(endX, endY);
        context.stroke();
    }
  
    // 绘制指针
    const pointerLength = radius * 0.8; // 指针的长度
  
    context.save();
    context.translate(centerX, centerY);
    context.rotate(-pointerAngle);
 
    context.beginPath();
    context.moveTo(-3, -pointerLength)
    context.lineTo(3, -pointerLength)
    context.lineTo(3, -pointerLength + 30)
    context.lineTo(-3, -pointerLength + 30)
    context.closePath();
    context.fillStyle = "red";
    context.fill();
    context.restore();
 
    context.fillStyle = "yellow";
    context.font = "bold 24px Arial";
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillText(label, centerX, centerY);
}

class RightKnobOsdLayer extends _HandOsdLayer {
    constructor(param) {
        super(param);
        this.angle = 0;
        this.targetAngle = 0;
        this.palmType = "right"
        this.handGesture = new RightHandGesture({callback:param.callback})
    }

    clearHand() {}
    drawHand() { // color
        // check if the knob is timeout or not
        let now = new Date().getTime();
        if (now - this.refreshTime > palmTimeout) {
            this.refreshTime = now
            this.osd = null
        }
        let osd = this.osd;
        if (osd == null) {
            let hand = this.canvas;
            let ctx = hand.getContext('2d');
            let hue = Math.floor((-this.angle + 400) / 800 * 360)
            let rgb = localHslToRgb(hue, 0.2, 0.2);
            let webColor = "rgb(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ")";

            drawKnob(ctx, this.angle/256, webColor, hue.toString()+"°");
            return;
        }

        for (let i=0; i<osd.result.detection.length; i++) {
            let od = osd.result.detection[i];
            if ((od.class == "palm") && (od.handedness == this.palmType)) {
                let hand = this.canvas;
                let ctx = hand.getContext('2d');
                let hue = Math.floor((-this.angle + 400) / 800 * 360) % 360;
                let rgb = localHslToRgb(hue, 1.0, 0.8);
                let webColor = "rgb(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ")";
                drawKnob(ctx, this.angle/256, webColor, hue.toString()+"°");
                this.angle = this.angle * 0.7 + this.targetAngle * 0.3;
                this.targetAngle = od.angle
                break;
            }
        }
    }
    /*
    __drawHand() { // light
		let lightness = (-this.angle + 400) / 800
        if (lightness < 0) {
            lightness = 0;
        }
        if (lightness > 1) {
            lightness = 1;
        }
        // check if the knob is timeout or not
        let now = new Date().getTime();
        if (now - this.refreshTime > palmTimeout) {
            this.refreshTime = now
            this.osd = null
        }
        let osd = this.osd;
        if (osd == null) {
            let hand = this.canvas;
            let ctx = hand.getContext('2d');
            const degrade = 0.5
            let webColor = "rgb(" + Math.floor(degrade * 255 * lightness) + "," + Math.floor(degrade * 255 * lightness) + "," + Math.floor(degrade * 255 * lightness) + ")";
            drawKnob(ctx, this.angle/256, webColor, Math.floor(lightness * 100).toString());
            return;
        }
        for (let i=0; i<osd.result.detection.length; i++) {
            let od = osd.result.detection[i];
            if ((od.class == "palm") && (od.handedness == this.palmType)) {
                let hand = this.canvas;
                let ctx = hand.getContext('2d');
                let webColor = "rgb(" + Math.floor(255 * lightness) + "," + Math.floor(255 * lightness) + "," + Math.floor(255 * lightness) + ")";
                drawKnob(ctx, this.angle/256, webColor, Math.floor(lightness * 100).toString());
                this.angle = this.angle * 0.7 + this.targetAngle * 0.3;
                this.targetAngle = od.angle
                break;
            }
        }
    }
    */
}

function localHslToRgb(h, s, l) {
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

class LeftKnobOsdLayer extends _HandOsdLayer {
    constructor(param) {
        super(param);
        this.angle = 0;
        this.targetAngle = 0;
        this.palmType = "left"
        this.handGesture = new LeftHandGesture({callback:param.callback})
    }

    clearHand() {}
    drawHand() {
        // check if the knob is timeout or not
        let now = new Date().getTime();
        if (now - this.refreshTime > palmTimeout) {
            this.refreshTime = now
            this.osd = null
        }
        let osd = this.osd;
        if (osd == null) {
            let hand = this.canvas;
            let ctx = hand.getContext('2d');
            let hue = Math.floor((-this.angle + 400) / 800 * 360)
            let rgb = localHslToRgb(hue, 0.2, 0.3);
            let webColor = "rgb(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ")";

            drawKnob(ctx, this.angle/256, webColor, hue.toString()+"°");
            return;
        }

        for (let i=0; i<osd.result.detection.length; i++) {
            let od = osd.result.detection[i];
            if ((od.class == "palm") && (od.handedness == this.palmType)) {
                let hand = this.canvas;
                let ctx = hand.getContext('2d');
                let hue = Math.floor((-this.angle + 400) / 800 * 360)
                let rgb = localHslToRgb(hue, 1.0, 0.8);
                let webColor = "rgb(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ")";
                drawKnob(ctx, this.angle/256, webColor, hue.toString()+"°");
                this.angle = this.angle * 0.7 + this.targetAngle * 0.3;
                this.targetAngle = od.angle
                break;
            }
        }
    }
}

// initialize
class Video2 extends Player {
    constructor(param) {
        super();
        this.onRenderFrameComplete = (frame) => {
            for (let i=0; i<this.video.length; i++) {
                let output_canvas = document.getElementById(this.video[i]);
                let destCtx = output_canvas.getContext('2d');
                destCtx.imageSmoothingEnabled = true;
                destCtx.drawImage(frame.canvasObj.canvas, 0, 0, frame.width,
                    frame.width*output_canvas.height/output_canvas.width, 0, 0,
                    output_canvas.width, output_canvas.height);

                let scale = output_canvas.width / frame.width;
                if (typeof this.onImageReady == "function") {
                    this.onImageReady(output_canvas, scale);
                }
            }
        }
    }
}

class AIVideo extends Player
{
    constructor(param) {
        super();
        this.onRenderFrameComplete = (frame) => {
            for (let i=0; i<this.video.length; i++) {
                let output_canvas = document.getElementById(this.video[i]);
                let destCtx = output_canvas.getContext('2d');
                destCtx.imageSmoothingEnabled = true;
                destCtx.drawImage(frame.canvasObj.canvas, 0, 0, frame.width,
                    frame.width*output_canvas.height/output_canvas.width, 0, 0,
                    output_canvas.width, output_canvas.height);

                let scale = output_canvas.width / frame.width;
                if (typeof this.onImageReady == "function") {
                    this.onImageReady(output_canvas, scale);
                }
            }

            // draw OSD
            for (let i=0; i<this.osdLayers.length; i++) {
                let output_canvas = this.osdLayers[i].canvas;
                if (output_canvas == null) {
                    break;
                }
                let destCtx = output_canvas.getContext('2d');
                destCtx.imageSmoothingEnabled = true;
                let scale = output_canvas.width / frame.width;
                this.osdLayers[i].drawOsd(destCtx, scale);
            }
        }
        this.video = [];
        if (typeof(param.video) != "undefined") {
            for (let i=0; i<param.video.length; i++) {
                this.video.push(param.video[i]);
            }
        }
        this.slotInfo = [];
        for (let i=0; i<4; i++) {
            this.slotInfo[i] = { oid:-1, time: new Date().getTime() };
        }
        this.idMap = new Map();

        // construct OSD layers
        this.osdLayers = [];
        if (typeof(param.osd) != "undefined") {
            for (let i=0; i<param.osd.length; i++) {
                if (typeof(param.osd[i].type) == "undefined") {
                    this.osdLayers.push(new OsdLayer(param.osd[i]));
                } else {
                    console.log("Create OSD type:", param.osd[i].type);
                    switch (param.osd[i].type) {
                        case "face":
                            this.osdLayers.push(new FaceOsdLayer(param.osd[i]));
                            break;
                        case "bitrate":
                            this.osdLayers.push(new BitrateOsdLayer(param.osd[i]));
                            break;
                        case "text":
                            this.osdLayers.push(new TextOsdLayer(param.osd[i]));
                            break;
                        case "left_hand_2d":
                            this.osdLayers.push(new LeftHandOsdLayer2D(param.osd[i]));
                            break;
                        case "left_hand_3d":
                            this.osdLayers.push(new LeftHandOsdLayer3D(param.osd[i]));
                            break;
                        case "right_hand_2d":
                            this.osdLayers.push(new RightHandOsdLayer2D(param.osd[i]));
                            break;
                        case "right_hand_3d":
                            this.osdLayers.push(new RightHandOsdLayer3D(param.osd[i]));
                            break;
                        case "left_knob":
                            this.osdLayers.push(new LeftKnobOsdLayer(param.osd[i]));
                            break;
                        case "right_knob":
                            this.osdLayers.push(new RightKnobOsdLayer(param.osd[i]));
                            break;
                        default:
                            this.osdLayers.push(new OsdLayer(param.osd[i]));
                            break;
                    }
                }
            }
        }
        this.tracking = false;
    }
}

updateFaceTable = (resp) => {
    var genHTML = '<thead class="thead-dark"><tr><td>ID</td><td>Name</td><td>New Name</td></tr></thead>';
    genHTML += '<tbody>';
    for (let i=0; i<resp.length; i++) {
        genHTML += '<tr><td>' + resp[i].faceid + '</td><td>' + resp[i].name + '</td><td><input id="name_' + resp[i].faceid + '" type="text" maxlength="16"> ';
        genHTML += '<button id="rename_' + resp[i].faceid + '" type="button" class="btn btn-primary btn-sm"> Set</button> ';
        genHTML += '<button id="del_' + resp[i].faceid + '" type="button" class="btn btn-danger btn-sm"> Delete</button>';
        genHTML += '</td></tr>';
    }
    genHTML += '</tbody>';
    $("#newface_table").html(genHTML);
    for (var i=0; i<resp.length; i++) {
        var id = "#rename_" + resp[i].faceid;
        $(id).click((event) => {
            let faceid = event.currentTarget.id.substring(7);
            let textid = "#name_" + faceid;
            videoCmdLink.renameFace(faceid, $(textid).val());
            videoCmdLink.getRegFace((r) => {
                updateFaceTable(r);
            })
        });

        var id = "#del_" + resp[i].faceid;
        $(id).click((event) => {
            let faceid = event.currentTarget.id.substring(4);
            videoCmdLink.unregFace(faceid);
            videoCmdLink.getRegFace((r) => {
                updateFaceTable(r);
            })
        });
    }
}

addMargin = (rect, _marginW, _marginH) => {
    let margin_w = (rect.maxx - rect.minx + 1) * _marginW / 2;
    let margin_h = (rect.maxy - rect.miny + 1) * _marginH / 2;
    rect.minx -= margin_w;
    rect.miny -= margin_h;
    rect.maxx += margin_w;
    rect.maxy += margin_h;

    return rect;
}

getUnion = (detection, tracking) => {
    let detection_count = detection.length;
    let union = {minx:0, miny:0, maxx:0, maxy:0};
    if (tracking && detection_count > 0) {
        union.minx = detection[0].minx;
        union.maxx = detection[0].maxx;
        union.miny = detection[0].miny;
        union.maxy = detection[0].maxy;
        for (let i=1; i<detection_count; i++) {
            if (detection[i].minx < union.minx) {
                union.minx = detection[i].minx;
            }
            if (detection[i].maxx > union.maxx) {
                union.maxx = detection[i].maxx;
            }
            if (detection[i].miny < union.miny) {
                union.miny = detection[i].miny;
            }
            if (detection[i].maxy > union.maxy) {
                union.maxy = detection[i].maxy;
            }
        }
    } else {
        // full screen
        union.minx = 0
        union.maxx = 640
        union.miny = 0
        union.maxy = 360
    }

    return union;
}

rectScale = (rect, scale) => {
    rect.minx *= scale;
    rect.maxx *= scale;
    rect.miny *= scale;
    rect.maxy *= scale;

    return rect;
}

adjustRect = (union, output_canvas, tracking_canvas) => {
    // adjust the bounding box to the canvas size
    // if the object is outside of the canvas, then full screen it
    if (union.minx < 0) union.minx = 0;
    if (union.minx > output_canvas.width) union.minx = 0;
    if (union.miny < 0) union.miny = 0;
    if (union.miny > output_canvas.height) union.miny = 0;
    if (union.maxx >= output_canvas.width) union.maxx = output_canvas.width-1;
    if (union.maxx < 0) union.maxx = output_canvas.width-1;
    if (union.maxy >= output_canvas.height) union.maxy = output_canvas.height-1;
    if (union.maxy < 0) union.maxy = output_canvas.height-1;
    // keep aspect ratio as the tracking_canvas ratio
    let cx = (union.maxx + union.minx)/2;
    let cy = (union.maxy + union.miny)/2;
    let bw = union.maxx - union.minx + 1;
    let bh = union.maxy - union.miny + 1;
    // setup minimal width or height
    if (bw < 60) bw = 60;
    if (bh < 60) bh = 60;
    if (bw / bh > tracking_canvas.width/tracking_canvas.height) {
        bh = bw * tracking_canvas.height / tracking_canvas.width;
        if (bh > tracking_canvas.height) {
            bh = tracking_canvas.height;
            bw = bh * tracking_canvas.width/tracking_canvas.height;
        }
    } else {
        bw = bh * tracking_canvas.width / tracking_canvas.height;
        if (bw > tracking_canvas.width) {
            bw = tracking_canvas.width;
            bh = bw * tracking_canvas.height / tracking_canvas.width;
        }
    }
    // process max, in width or height
    if (bw > output_canvas.width) {
        bw = output_canvas.width;
        bh = bw * tracking_canvas.height / tracking_canvas.width;
    } else if (bh > output_canvas.height) {
        bh = output_canvas.height-1;
        bw = bh * tracking_canvas.width/tracking_canvas.height;
    }
    // adjust the result to the video canvas size
    let minx = cx - bw / 2;
    let maxx = cx + bw / 2;
    let miny = cy - bh / 2;
    let maxy = cy + bh / 2;
    // shift if necessary, but keep the size
    if (minx < 0) {
        maxx -= minx;
        minx = 0;
    }
    if (miny < 0) {
        maxy -= miny;
        miny = 0;
    }
    if (maxx > output_canvas.width) {
        minx -= (maxx - output_canvas.width);
        maxx = output_canvas.width - 1;
    }
    if (maxy > output_canvas.height) {
        miny -= (maxy - output_canvas.height);
        maxy = output_canvas.height - 1;
    }
    union.minx = minx;
    union.maxx = maxx;
    union.miny = miny;
    union.maxy = maxy;

    return union;
}

moveCamera = (curRect, minx, miny, maxx, maxy) => {
    /*
    let steps = 12; // steps to target
    let accel = 0.12;
    */
    let steps = 8; // faster
    let accel = 0.18; // faster
    let cx = (minx + maxx)/2;
    let cy = (miny + maxy)/2;
    let dx = curRect._cx - cx;
    let dy = curRect._cy - cy;
    if (dx!=0 || dy !=0) {
        let distance = Math.sqrt(dx*dx+dy*dy);
        let linear_speed = distance / steps;
        curRect._speed = curRect._speed + accel;
        if (curRect._speed > linear_speed) {
            curRect._speed = linear_speed;
        }
        // adjust steps count for controlling camera speed
        steps = steps * steps / curRect._speed;
        curRect._cx = (curRect._cx * (steps-1) + cx) / steps;
        curRect._cy = (curRect._cy * (steps-1) + cy) / steps;
        curRect._width = (curRect._width * (steps-1) + (maxx - minx)) / steps;
        curRect._height = (curRect._height * (steps-1) + (maxy - miny)) / steps;
    }
}

drawZoomInfo = (output_canvas, destCtx, scale) => {
    destCtx.font = "50px Arial";
    destCtx.fillStyle = "rgba(125,255,125,255)";

    let zoom = output_canvas.height / destCtx._height;
    zoom = zoom.toFixed(2);
    destCtx.fillText("ZOOM: " + zoom + "x", 10*scale, 20*scale);

    let left = 10 * scale;
    let top = 25 * scale;

    destCtx.globalAlpha = 0.7;
    destCtx.fillStyle = "#000000";
    destCtx.beginPath();
    destCtx.fillRect(left, top, output_canvas.width/8, output_canvas.height/8);
    destCtx.fill();

    destCtx.globalAlpha = 1.0;
    destCtx.strokeStyle = "yellow";
    destCtx.lineWidth = 1 * scale;
    destCtx.beginPath();
    destCtx.rect(left + (destCtx._cx - destCtx._width/2)/8, top + (destCtx._cy - destCtx._height/2)/8, destCtx._width/8, destCtx._height/8);
    destCtx.stroke();
}

class AIAudio {
    constructor(param) {
        this.output_canvas = document.getElementById(param.canvas_name);
        this.destCtx = this.output_canvas.getContext('2d');
        this.detectionText = [];
        this.rollingText = 0;
        this.audioData = [];
        this.drawTime = new Date().getTime();
        this.showWave = true;
        if (typeof(param.show_wave) == "boolean") {
            this.showWave = param.show_wave;
        }
    }

    setupData(audioData) {
        // process the audio data int a fixed size array
        const audioBlockSize = 24;
        let stepSize = audioData.length / audioBlockSize;
        if (audioData.length > audioBlockSize) {
            let newAudio = new Int16Array(audioBlockSize);
            let fidx = 0.0;
            for (let i = 0; i < audioBlockSize; i++) {
                newAudio[i] = audioData[Math.floor(fidx)];
                fidx += stepSize;
            }
            audioData = newAudio;
        }

        this.audioData.push(audioData)
        while (this.audioData.length > 8) {
            this.audioData.shift();
        }
        this.drawWave();
    }

    drawFrame() {
        let ctx = this.destCtx;
        let w = this.output_canvas.width;
        let h = this.output_canvas.height;
        ctx.clearRect(0, 0, w, h);
        ctx.beginPath();
        ctx.strokeStyle = "green";
        ctx.moveTo(0, h/2);
        ctx.lineTo(w, h/2);
        ctx.moveTo(0, h/4);
        ctx.lineTo(w, h/4);
        ctx.moveTo(0, 3*h/4);
        ctx.lineTo(w, 3*h/4);
        for (let ix=0; ix<w; ix+=w/10) {
            ctx.moveTo(ix, 0);
            ctx.lineTo(ix, h);
        }
        ctx.stroke();
    }

    drawWave() {
        let nowTime = new Date().getTime();
        let elapsed = nowTime - this.drawTime;
        if (elapsed < 66) { // bounded at 15fps
            return;
        }
        this.drawTime = nowTime;
        let w = this.output_canvas.width;
        let h = this.output_canvas.height;
        let ctx = this.destCtx;
        let divFactor = 24576;

        this.drawFrame();
        if (this.showWave) {
            ctx.beginPath();
            ctx.strokeStyle = "white";
            ctx.moveTo(0,h/2);
            let bw = w / this.audioData.length;
            for (let aidx = 0; aidx < this.audioData.length; aidx++) {
                let data = this.audioData[aidx];
                for (let ix=0; ix<data.length; ix++) {
                    let x = (aidx +ix/data.length) * bw;
                    let y = data[ix]/divFactor * h + h/2;
                    ctx.lineTo(x,y);
                }
            }
            ctx.stroke();
        }

        let detectionText = this.detectionText;
        ctx.fillStyle = "white";
        ctx.font = '100px Arial'
        let draw_y = 120 - this.rollingText*2;
        for (let i=detectionText.length-1; i>=0; i--) {
            let v = (i+1)/detectionText.length;
            ctx.fillStyle = "rgba(125,255,125,"+v+")";
            ctx.fillText(detectionText[i], 40, draw_y);
            draw_y += 100;
        }
        if (this.rollingText > 1) {
            this.rollingText = this.rollingText * 5/6;
        }
    }
}


// TODO: dirty code, fix this
let uiNameCallback = function(name) {
    // do nothing
}

class CommandLink extends WebSocket {
    constructor(url, p) {
        super(url)
        this.videoPlayer = null
        this.videoPlayer2 = null
        this.videoPlayer = p.video
        this.videoPlayer2 = p.video2
        this.audioPlayer = p.audio
        this.getModelStatusReady = null
        this.regFaceReady = null
        this.getRegFaceReady = null
        this.playingAudio = false
        this.playingVideo = false
        this.playingVideo2 = false
        this.firmwareVersion = "N/A"
        this.binaryType = 'arraybuffer' // setup the websocket binary type
        this.cryptoSession = new CryptoStream.StreamSession()
        this.gopAccu = 60
        this.fps = 20
        this.averageFrameSize = 0
        this.frameCount = 0
        this.icost = 0
        this.audioPlaying = false
        this.imageClasses = []
        this.onopen = () => {
            if (this.videoPlayer != null) {
                this.startVideo();
            }
            if (this.videoPlayer2 != null) {
                this.startVideo2();
            }
            if (this.audioPlayer != null) {
                this.startAudio();
            }

            this.getVersion();
            if (typeof p.tracking != "undefined") {
                this.setupTracking(p.tracking);
            }
            this.stopModel(["yolo4t", "retinaface", "mobilenetface", "yamnet", "object_tracking", "palm_detection", "hand_landmark"]);
            this.startModel(p.models);
            if (typeof aiStartCallback != "undefined") {
                aiStartCallback();
            }
            this.getModelMeta();
        }
        this.onclose = () => {
            console.log("connection closed");
        };
        this.onmessage = (evt) => {
            if (evt.data instanceof ArrayBuffer) {
                let decoded = null
                try {
                    decoded = this.cryptoSession.decryptBinary(evt.data)
                } catch (err) {
                    console.log("drop encrypted packet:", err.message)
                }
                if (decoded == null) {
                    return
                }
                if (decoded.streamId === CryptoStream.STREAM_AUDIO) {
                    if (this.audioPlayer != null) {
                        let audioData = new Int16Array(decoded.payload.buffer, decoded.payload.byteOffset, Math.floor(decoded.payload.byteLength / 2))
                        this.audioPlayer.setupData(audioData);
                    }
                } else if (decoded.streamId === CryptoStream.STREAM_VIDEO) {
                    if (this.videoPlayer != null) {
                        let nowTime = new Date();
                        let b = decoded.payload
                        this.videoPlayer.decode(b);
                        let nal_type = b[0] & 0x1f;
                        if (nal_type == 5) { // IDR
                            if (this.gopAccu == 0) {
                                this.gopAccu = 30;
                            }
                            this.icost = (b.byteLength - this.averageFrameSize) / this.gopAccu;
                            this.gopAccu = 0;
                            this.updateFrameRate(nowTime);
                        } else if (nal_type == 1) { // P
                            this.gopAccu++;
                            this.averageFrameSize = (this.averageFrameSize * 29 + (b.byteLength + this.icost)) / 30;
                            this.updateFrameRate(nowTime);
                        }
                    }
                }
            } else {
                let obj = JSON.parse(evt.data);
                if (this.cryptoSession.updateFromControl(obj)) {
                    return
                }
                switch (obj.type) {
                    case "classification":
                        if (obj.result.status == "ok") {
                            //console.log(obj.result.scores)
                            //console.log(this.imageClasses)
                            let osdLayers = this.videoPlayer.osdLayers;
                            for (let i=0; i < osdLayers.length; i++) {
                                osdLayers[i].imageClasses = this.imageClasses;
                                osdLayers[i].imageScores = obj.result.scores;
                            }
                        }
                        break;

                    case "get_model_meta":
                        this.imageClasses = obj.result.value;
                        console.log(this.imageClasses)
                        break;

                    case "get_model_status":
                        if (this.getModelStatusReady != null) {
                            this.getModelStatusReady(obj.result.value);
                            this.getModelStatusReady = null;
                        }
                        break;

                    case "reg_face":
                        if (this.regFaceReady != null) {
                            if (obj.result.status == "ok") {
                                this.regFaceReady(obj.result);
                                this.regFaceReady = null;
                                $("#newface-title").html("New Face Registration");
                            } else {
                                $("#newface-title").html("Face Registration Fail: "+ obj.result.status)
                                $("#newface_table").html("");
                            }
                        }
                        break;

                    case "get_reg_face":
                        if (this.getRegFaceReady != null) {
                            this.getRegFaceReady(obj.result.value);
                            this.getRegFaceReady = null;
                            $("#newface-title").html("Registered Faces");
                        }
                        break;

                    case "get_version":
                        let vs = "";
                        let value = obj.result.value;
                        for (let i=0; i<value.length; i++) {
                            vs = vs + value[i];
                        }
                        this.firmwareVersion = vs;
                        break;

                    case "audio_detection":
                        let res = obj.result;
                        let detection = res.detection;
                        const labelList = [
                            "Music",
                            "Speech",
                            "Baby laughter",
                            "Crying, sobbing",
                            "Baby cry, infant cry",
                            "Dog",
                            "Bark",
                            "Howl",
                            "Whimper (dog)",
                            "Cat",
                            "Meow",
                            "caterwaul",
                            "Emergency vehicle",
                            "Police car (siren)",
                            "Ambulance (siren)",
                            "Fire engine, fire truck (siren)",
                            "Doorbell",
                            "Ding-dong",
                            "Alarm clock",
                            "Siren",
                            "Smoke detector, smoke alarm",
                            "Fire alarm",
                        ]
                        if (typeof(detection) != "undefined") {
                            if (res.status == "ok") {
                                for (let i=0; i<detection.length; i++) {
                                    let od = detection[i];
                                    if (labelList.includes(od.class)) {
                                        let detectionText = this.audioPlayer.detectionText;
                                        if (detectionText.length >= 5) {
                                            detectionText.shift();
                                        }
                                        detectionText.push(od.class);
                                        this.audioPlayer.rollingText = 48;  
                                        break; // the class found, so exit
                                    }
                                }          
                            }
                        }
                        break;

                    case "system_info":
                        if (obj.result.status == "ok") {
                            if (typeof cpuUtilCallback != "undefined" && typeof obj.result.cpu != "undefined") {
                                cpuUtilCallback(obj.result.cpu);
                            }
                            if (typeof plumeraiCallback != "undefined" && typeof obj.result.plumerai != "undefined") {
                                plumeraiCallback(obj.result.plumerai);
                            }
                            if (typeof videoRecordCallback != "undefined" && typeof obj.result.mp4_rec_status != "undefined") {
                                videoRecordCallback(obj.result.mp4_rec_status);
                            }
                        }
                        break;

                    case "md_result":
                        let osdLayers = this.videoPlayer.osdLayers;
                        for (let i=0; i < osdLayers.length; i++) {
                            osdLayers[i].setMd(obj.result);
                        }
                        break;

                    case "object_detection":
                    case "facial_recognition":
                    case "hand_gesture":
                        if (obj.type == "facial_recognition") {
                            for (let i=0; i<obj.result.detection.length; i++) {
                                let od = obj.result.detection[i];
                                let bw = od.maxx - od.minx + 1;
                                let bh = od.maxy - od.miny + 1;
                                let name = od.class;
                                if (typeof uiNameCallback != "undefined" && bw > 120 && bh > 120) {
                                    uiNameCallback(name);
                                }
                            }
                        } else if (obj.type == "hand_gesture") {
                            for (let i=0; i<obj.result.detection.length; i++) {
                                let od = obj.result.detection[i];
                                /*
                                if (od.handedness == "right") {
                                    od.handedness = "left"; // dirty code for mirror
                                } else if (od.handedness == "left") {
                                    od.handedness = "right";
                                }
                                */
                            }
                        }

                        this.curDet = obj.result.detection;
                        if (this.videoPlayer != null) {
                            let osdLayers = this.videoPlayer.osdLayers;
                            for (let i=0; i < osdLayers.length; i++) {
                                osdLayers[i].setOsd(obj);
                            }
                        }
                        break;
                }
            }
        };
        this.onerror = () => {
            console.log("connection error");
        }
        this.modelStatus = null;
    }

    setFilterClass(classes) {
        if (this.videoPlayer != null) {
            let osdLayers = this.videoPlayer.osdLayers;
            for (let i=0; i < osdLayers.length; i++) {
                osdLayers[i].setFilterClass(classes);
            }
        }
    }

    getUnionCenter() {
        let minx = 1920;
        let maxx = 0;
        let miny = 1080;
        let maxy = 0;
        if (typeof this.curDet != "undefined") {
            for (let i=0; i<this.curDet.length; i++) {
                if (this.curDet[i].class == "person") {
                    let det = this.curDet[i];
                    if (det.minx < minx) {
                        minx = det.minx;
                    }
                    if (det.maxx > maxx) {
                        maxx = det.maxx;
                    }
                    if (det.miny < miny) {
                        miny = det.miny;
                    }
                    if (det.maxy > maxy) {
                        maxy = det.maxy;
                    }
                }
            }
        }
        //let scale = 3.0; // TODO: 1.0 is for 1080p, other resolution need to adjust the scale
        let scale = 1.0;
        return { cx: scale*(minx+maxx)/2, cy: scale*(miny+maxy)/2 };
    }

    updateFrameRate(nowTime) {
        const fpsLength = 30;
        if (typeof this.startDate == 'undefined') {
            this.startDate = nowTime;
            this.timeArray = [];
            this.timeArray.push(nowTime);
        } else {
            if (this.timeArray.length >= fpsLength) {
                let start = this.timeArray.shift();
                this.fps = fpsLength / ((nowTime - start) / 1000);
            }
            this.timeArray.push(nowTime);
        }
    }

    smartExposureOn(value) {
        let p = {
            cmd: "smart_exposure_on",
            param : {
                target: value,          // target AE for face
                person_target: 75,      // target AE for person
                tolerance: 15,          // tolerance of target AE
                min_target_ae: 30,
                max_target_ae: 33333,   // 1000000 / fps
                ae_step: 400,           // step of AE adjustment
                rgb_step: 1,            // step of average_y()
                x_pad: 112/3,           // pad size of face image (ROI)
                y_pad: 16,              // pad size of face image (ROI)
                time_wait: 1000
            }
        }
        this.send(JSON.stringify(p));
    }

    smartBitrateOn(rate) {
        let p = {
            cmd: "smart_bitrate_on",
            param: rate
        }
        this.send(JSON.stringify(p));
    }

    smartBitrateOff() {
        this.send(JSON.stringify({cmd: 'smart_bitrate_off'}));
    }


    roiCenterBitrateOn() {
        let p = {
            cmd: "roi_center_bitrate_on",
        }
        this.send(JSON.stringify(p));
    }

    roiCenterBitrateOff() {
        let p = {
            cmd: "roi_center_bitrate_off",
        }
        this.send(JSON.stringify(p));
    }

    smartExposureOff() {
        this.send(JSON.stringify({cmd: 'smart_exposure_off'}));
    }

    setupTracking(p) {
        let packet = { cmd: "setup_tracking" };
        if (typeof(p) == "undefined") {
            // default value
            packet.param = {
                    kf_matrix: [1.0, 1.0, 0.2, 0.0, 1.0, 1.0, 0.0, 0.0, 0.3],
                    kf_noise: [0.1, 0.1],
                    kf_maxDistThreshold: 80,
                    kf_maxUnrefCount: 4,
                    kf_maxTracking: 12,
                    tracking_target: ["person", "car", "bicycle", "motorcycle", "bus", "train", "truck", "dog", "cat"]
                }
        } else {
            packet.param = p;
        }
        this.send(JSON.stringify(packet));
    }

    startVideo() {
        this.send(JSON.stringify({cmd: "start_stream", param: { mirror:1 } }));
        this.playingVideo = true;
    }

    startVideo2() {
        this.send(JSON.stringify({cmd: "start_stream2"}));
        this.playingVideo2 = true;
    }

    stopVideo() {
        this.send(JSON.stringify({cmd: "stop_stream"}));
        this.playingVideo = false;
    }

    stopVideo2() {
        this.send(JSON.stringify({cmd: "stop_stream2"}));
        this.playingVideo2 = false;
    }

    getModelStatus(ready) {
        if (ready == undefined) {
            ready = null;
        }
        this.getModelStatusReady = ready;
        this.send(JSON.stringify({cmd: "get_model_status"}));
    }

    startModel(models) {
        this.send(JSON.stringify({cmd:"start_model", param: models}));
    }

    stopModel(models) {
        this.send(JSON.stringify({cmd:"stop_model", param: models}));
    }

    setMotionDetection(_enable, _tbase, _tlum, _obj_sensitivity) {
        this.send(JSON.stringify( {cmd : "md",
            param : { enable:_enable?1:0, Tbase: _tbase, Tlum: _tlum, obj_sensitivity: _obj_sensitivity } 
        }));
    }

    setObjectDetection(_confidence, _nms_threshold) {
        this.send(JSON.stringify({cmd: "object_detect", param: {confidence: _confidence, nms_thresh: _nms_threshold}}));
    }

    setIrCut(_on) {
        this.send(JSON.stringify({cmd: "ircut", param: { on:  _on}}));
    }

    setIspFps(_fps) {
        this.send(JSON.stringify({cmd: "isp_fps", param: { isp_fps: _fps}}));
    }

    regFace(ready) {
        if (ready == undefined) {
            ready = null;
        }
        this.regFaceReady = ready;
        this.send(JSON.stringify({cmd : "reg_face" }));
    }

    unregFace(faceid) {
        if (typeof faceid === 'string') {
            faceid = parseInt(faceid);
        }
        this.send(JSON.stringify({cmd:"unreg_face", param: {"faceid": faceid}}));
    }

    unregAllFace() {
        this.send(JSON.stringify({cmd: "unreg_allface"}));
    }

    getRegFace(ready) {
        if (ready == undefined) {
            ready = null;
        }
        this.getRegFaceReady = ready;
        this.send(JSON.stringify({cmd:"get_reg_face"}));
    }

    renameFace(faceid, newName) {
        if (typeof faceid === 'string') {
            faceid = parseInt(faceid);
        }
        this.send(JSON.stringify({cmd:"rename_face", param: {"faceid": faceid, "name": newName}}));
    }

    getVersion() {
        this.send(JSON.stringify({cmd:"get_version"}));
    }

    saveFace() {
        this.send(JSON.stringify({cmd:"save_reg_face"}));
    }

    startAudio() {
        this.send(JSON.stringify({cmd: "start_audio_stream"}));
        this.playingAudio = true;
    }

    stopAudio() {
        this.send(JSON.stringify({cmd: "stop_audio_stream"}));
        this.playingAudio = false;
    }

    enableTracking() {
        if (this.videoPlayer != null)
            this.videoPlayer.tracking = true;
    }

    disableTracking() {
        if (this.videoPlayer != null)
            this.videoPlayer.tracking = false;
    }

    // remote audio player command for show
    playAudioFile(filename) {
        let msg = {
            "type": "audio",
            "action": "play",
            "filename": filename
        }
        this.send(JSON.stringify(msg))
    }

    play() {
        let msg = {
            "type": "audio",
            "action": "play",
        }
        this.send(JSON.stringify(msg))
        this.audioPlaying = true
    }

    pause() {
        let msg = {
            "type": "audio",
            "action": "pause"
        }
        this.send(JSON.stringify(msg))
        this.audioPlaying = false
    }

    restart() {
        let msg = {
            "type": "audio",
            "action": "restart"
        }
        this.send(JSON.stringify(msg))
    }

    nextTrack() {
        let msg = {
            "type": "audio",
            "action": "next_track"
        }
        this.send(JSON.stringify(msg))
    }

    prevTrack() {
        let msg = {
            "type" : "audio",
            "action" : "prev_track"
        }
        this.send(JSON.stringify(msg))
    }

    lightRGB(id, r, g, b, dimming) {
        let d = Math.floor(dimming*100);
        if (d<10) {
            d = 10
        }
        if (d>100) {
            d = 100
        }
        if (r==0 && g==0 && b==0) {
            r = 1;
            g = 1;
            b = 1;
        }
        let msg = {
            "type" : "light",
            "action" : "rgb",
            "id": id,
            "r" : r,
            "g" : g,
            "b" : b,
            "dimming" : d
        }
        this.send(JSON.stringify(msg))
    }

    getModelMeta() {
        this.send(JSON.stringify({cmd:"get_model_meta", param: ["classify"]}));
    }

    startMp4Record(record_seconds) {
        // 获取当前日期并格式化为 YYYYMMDD_HHMMSS
        let now = new Date();
        let year = now.getFullYear();
        let month = String(now.getMonth() + 1).padStart(2, '0');
        let day = String(now.getDate()).padStart(2, '0');
        let hours = String(now.getHours()).padStart(2, '0');
        let minutes = String(now.getMinutes()).padStart(2, '0');
        let seconds = String(now.getSeconds()).padStart(2, '0');
        let formattedDate = `${year}${month}${day}_${hours}${minutes}${seconds}`;
    
        let msg = {
            "cmd": "start_mp4_record",
            "param": {
                "folder": "mp4_001",
                "filename": `${formattedDate}.mp4`,
                "record_seconds": record_seconds
            }
        }
        this.send(JSON.stringify(msg));
    }

    stopMp4Record() {
        this.send(JSON.stringify({cmd: "stop_mp4_record", param: {}}));
    }
} // CommandLink

var videoCmdLink = null;
document.addEventListener("DOMContentLoaded", function() {
    updateMenu();
    updateFooter();
    let addr = "ws://" + window.location.hostname + ":8081";
    let p = createPlayer();
    videoCmdLink = new CommandLink(addr, p);

    // setup the polling timer for auto re-connect
    window.setInterval(() => {
        if (videoCmdLink.readyState == 2 /* CLOSING */ || videoCmdLink.readyState == 3 /* CLOSED */ ) {
            console.log("reconnect !!");
            videoCmdLink = new CommandLink(addr, p);
        }
    }, 200);
});
