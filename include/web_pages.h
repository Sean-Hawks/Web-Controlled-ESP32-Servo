#ifndef WEB_PAGES_H
#define WEB_PAGES_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta charset="UTF-8">
    <title>ESP32 數位孿生控制台</title>
    <style>
        :root { --primary: #8AB4F8; --bg: #202124; --card: #292a2d; --text: #e8eaed; --danger: #F28B82; }
        body { background: var(--bg); color: var(--text); font-family: 'Segoe UI', sans-serif; display: flex; flex-direction: column; align-items: center; margin: 0; padding: 20px; }
        .container { background: var(--card); padding: 2rem; border-radius: 24px; box-shadow: 0 10px 30px rgba(0,0,0,0.5); width: 100%; max-width: 400px; text-align: center; box-sizing: border-box; }
        h1 { font-weight: 300; color: var(--primary); margin-bottom: 1rem; font-size: 1.5rem; }
        
        /* 虛擬馬達視覺化 (半圓形儀表) */
        .servo-wrapper { position: relative; width: 240px; height: 120px; margin: 20px auto; overflow: hidden; border-bottom: 2px solid rgba(255,255,255,0.2); }
        .servo-bg { width: 240px; height: 240px; border-radius: 50%; border: 2px dashed rgba(138, 180, 248, 0.3); position: absolute; top: 0; left: 0; box-sizing: border-box; }
        
        /* 馬達指針 */
        .servo-arm { 
            position: absolute; bottom: 0; left: 50%; width: 6px; height: 110px; 
            background: var(--primary); transform-origin: bottom center; 
            transform: rotate(90deg); /* 初始 0 度指向右邊 */
            transition: transform 0.05s linear; /* 改為極短的過渡，用來對接高更新率的 Websocket */
            box-shadow: 0 0 15px var(--primary); border-radius: 10px;
        }
        .servo-pivot { position: absolute; bottom: -12px; left: 50%; width: 24px; height: 24px; background: #fff; border-radius: 50%; transform: translateX(-50%); box-shadow: 0 0 10px #000; }

        /* 數值顯示 */
        .status-box { display: flex; justify-content: space-between; background: rgba(255,255,255,0.05); padding: 15px 20px; border-radius: 16px; margin-bottom: 20px; }
        .data-group { display: flex; flex-direction: column; align-items: center; }
        .value { font-size: 1.8rem; font-weight: bold; color: var(--primary); font-variant-numeric: tabular-nums; }
        .label { font-size: 0.7rem; opacity: 0.7; letter-spacing: 1px; margin-top: 5px; }

        /* 按鈕設計 */
        .btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
        .btn { border: none; padding: 15px; border-radius: 12px; font-size: 0.9rem; cursor: pointer; transition: 0.2s; background: #3c4043; color: white; font-weight: 600; letter-spacing: 1px; }
        .btn:active { transform: scale(0.95); }
        .btn-main { grid-column: span 2; background: var(--primary); color: #202124; box-shadow: 0 4px 15px rgba(138, 180, 248, 0.2); }
        .btn-stop { background: var(--danger); color: #202124; box-shadow: 0 4px 15px rgba(242, 139, 130, 0.2); }
        
        /* 滑桿設計 (BPM 調速) */
        .slider-box { background: rgba(255,255,255,0.05); padding: 15px; border-radius: 12px; margin-bottom: 20px; }
        .slider-header { display: flex; justify-content: space-between; margin-bottom: 10px; }
        input[type="range"] { -webkit-appearance: none; width: 100%; background: #3c4043; height: 8px; border-radius: 4px; outline: none; }
        input[type="range"]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 20px; height: 20px; border-radius: 50%; background: var(--primary); cursor: pointer; }
        
        #msg { margin-top:15px; font-size:0.8rem; opacity: 0.6; min-height: 1.2em; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Digital Twin Servo</h1>
        
        <div class="servo-wrapper">
            <div class="servo-bg"></div>
            <div class="servo-arm" id="servo-arm"></div>
            <div class="servo-pivot"></div>
        </div>

        <div class="status-box">
            <div class="data-group">
                <div class="value" id="angle">0&deg;</div>
                <div class="label">POSITION</div>
            </div>
            <div class="data-group">
                <div class="value" id="state" style="color: #fff;">STOP</div>
                <div class="label">STATUS</div>
            </div>
        </div>

        <div class="slider-box">
            <div class="slider-header">
                <div class="label">SPEED (BPM)</div>
                <div id="bpm-val" style="color: var(--primary); font-weight: bold;">60</div>
            </div>
            <input type="range" id="bpm-slider" min="10" max="180" value="60" oninput="updateBPMUI(this.value)" onchange="sendBPM(this.value)">
        </div>

        <div class="btn-grid">
            <button class="btn btn-main" onclick="sendCmd('/toggle')">START / PAUSE</button>
            <button class="btn" onclick="sendCmd('/changeDir')">REVERSE</button>
            <button class="btn btn-stop" onclick="sendCmd('/stop')">E-STOP</button>
        </div>

        <p id="msg">System Ready</p>
    </div>

    <script>
        function updateBPMUI(val) {
            document.getElementById('bpm-val').innerText = val;
        }

        function sendBPM(val) {
            fetch('/setBPM?value=' + val);
        }

        function sendCmd(path) {
            document.getElementById('msg').innerText = "Command sent: " + path;
            fetch(path).then(res => res.text()).then(data => {
                document.getElementById('msg').innerText = "ESP32 Reply: " + data;
            });
        }

        // =====================
        // 🔥 直播級 WebSocket 連線 🔥
        // =====================
        let gateway = `ws://${window.location.hostname}/ws`;
        let websocket;

        function initWebSocket() {
            websocket = new WebSocket(gateway);
            websocket.onmessage = onMessage;
            websocket.onclose = () => {
                document.getElementById('msg').innerText = "連線中斷，正在重連...";
                setTimeout(initWebSocket, 1000); // 斷線後 1 秒重試
            };
        }

        // 每當 ESP32 傳來最新的儀表狀態 (33 FPS)
        function onMessage(event) {
            let data = JSON.parse(event.data);
            
            // 1. 同步角度 (如絲般順滑)
            document.getElementById('angle').innerHTML = data.angle + "&deg;";
            let cssAngle = 90 - parseInt(data.angle);
            document.getElementById('servo-arm').style.transform = `rotate(${cssAngle}deg)`;
            
            // 2. 同步目前工作狀態 
            document.getElementById('state').innerText = data.state;
            document.getElementById('state').style.color = (data.state === "RUNNING") ? "#8AB4F8" : "#fff";

            // 3. 防止別人改 BPM 你的 UI 沒動 (如果不影響滑動體驗才蓋回去)
            let slider = document.getElementById('bpm-slider');
            if (Math.abs(slider.value - data.bpm) > 5) {
                slider.value = data.bpm;
                document.getElementById('bpm-val').innerText = data.bpm;
            }
        }

        // 網頁載入後，直接建立 WebSocket 長連線
        window.addEventListener('load', initWebSocket);
    </script>
</body>
</html>
)rawliteral";

#endif