#include "WebRemote.h"
#include <WiFi.h>


///////////////////////////////////////////////
///               SERVER HTML               ///
///////////////////////////////////////////////


const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Receiver Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { background: #121212; color: #e0e0e0; font-family: sans-serif; margin: 0; padding: 15px; }
        .body { max-width: 800px; margin: auto; display: flex; flex-direction: column; gap: 12px; }
        h1 { text-align: center; font-size: 1.5rem; margin-bottom: 5px; }
        
        /* Green Status Bar Fix */
        .rx-display { 
            background: #000; color: #00ff00; padding: 10px; font-family: monospace; 
            text-align: center; border: 1px solid #333; border-radius: 4px; min-height: 1.2em;
        }

        /* Controls Layout */
        .btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        .btn-wrapper { display: flex; background: #222; border: 1px solid #444; border-radius: 6px; overflow: hidden; }
        .main-btn { flex: 1; background: transparent; color: #fff; border: none; padding: 15px; cursor: pointer; text-align: center; }
        .learn-btn { background: #331a1a; color: #ff4444; border: none; border-left: 1px solid #444; width: 45px; cursor: pointer; font-size: 1.2rem; }
        
        /* Hex Code Labels */
        .code-info { display: block; font-size: 0.65rem; color: #777; margin-top: 4px; font-family: monospace; }

        /* Presets Table */
        table { width: 100%; border-collapse: collapse; background: #1a1a1a; }
        th { background: #252525; padding: 10px; font-size: 0.8rem; color: #888; text-transform: uppercase; }
        td { border: 1px solid #333; padding: 10px; text-align: center; }
        
        .digit-grid { display: grid; grid-template-columns: repeat(5, 1fr); gap: 5px; }
        .digit-btn { background: #222; border: 1px solid #444; color: white; padding: 10px 0; border-radius: 4px; cursor: pointer; }
        
        #advanced { display: none; padding: 10px; color: #555; font-size: 0.7rem; }
    </style>
</head>
<body onload="init()">
    <div class="body">
        <h1>ESP32 Receiver Control</h1>
        <div class="rx-display" id="rx-display">Waiting for IR...</div>

        <h3>User Controls</h3>
        <div class="btn-grid">
            <div class="btn-wrapper">
                <button class="main-btn" onclick="send('power')">POWER<span class="code-info" id="c-10">---</span></button>
                <button class="learn-btn" onclick="learn(10)">&#11044;</button>
            </div>
            <div class="btn-wrapper">
                <button class="main-btn" onclick="send('source')">SOURCE<span class="code-info" id="c-11">---</span></button>
                <button class="learn-btn" onclick="learn(11)">&#11044;</button>
            </div>
            <div class="btn-wrapper">
                <button class="main-btn" onclick="send('volup')">VOL +<span class="code-info" id="c-12">---</span></button>
                <button class="learn-btn" onclick="learn(12)">&#11044;</button>
            </div>
            <div class="btn-wrapper">
                <button class="main-btn" onclick="send('voldown')">VOL -<span class="code-info" id="c-13">---</span></button>
                <button class="learn-btn" onclick="learn(13)">&#11044;</button>
            </div>
        </div>

        <h3>Channel Presets</h3>
        <table>
            <thead><tr><th>#</th><th>CH</th><th>Actions</th></tr></thead>
            <tbody id="p-body"></tbody>
        </table>

        <h3>Digits</h3>
        <div class="digit-grid" id="d-grid"></div>

        <div onclick="document.getElementById('advanced').style.display='block'" style="cursor:pointer; color:#444; font-size:0.7rem;">Advanced v2.1</div>
        <div id="advanced">NVS Memory Storage Active | SSID: S3-Zero-Remote</div>
    </div>

    <script>
        function send(c) { fetch('/send?cmd=' + c); }
        
        function learn(i) { 
            document.getElementById('rx-display').innerText = "System: Recording index " + i + "... Press Remote Button";
            fetch('/send?cmd=learn-' + i); 
        }

        // Fetch memory from ESP32 and update labels
        function sync() {
            fetch('/getMemory').then(r => r.json()).then(data => {
                // Update 0-13 codes
                data.mem.forEach((hex, i) => {
                    let el = document.getElementById('c-' + i);
                    if (el) el.innerText = (hex !== "0") ? hex.toUpperCase() : "---";
                });
                
                // Update Channel Table (Only needed if the values actually changed to avoid flicker)
                // We re-render the table rows here
                let h = "";
                data.channels.forEach((ch, i) => {
                    h += `<tr><td>${i+1}</td><td>${ch||'--'}</td><td>
                        <button onclick="send('send-pre-${i+1}')">GO</button>
                        <button onclick="edit(${i+1})">Edit</button>
                    </td></tr>`;
                });
                // Only update DOM if it's different (prevents input lag if you were trying to click)
                let tbody = document.getElementById('p-body');
                if(tbody.innerHTML.length != h.length) tbody.innerHTML = h;
            }).catch(e => console.log("Sync error (ignore if flashing):", e));
        }

        function edit(i) {
            let v = prompt("Channel Number:");
            // We wait 500ms before syncing to ensure ESP32 saved it
            if(v) fetch(`/send?cmd=setchan-${i}-${v}`).then(() => setTimeout(sync, 500));
        }

        function init() {
            // 1. Create the digit buttons
            let g = document.getElementById('d-grid');
            for(let i=0; i<10; i++) {
                g.innerHTML += `<button class="digit-btn" onclick="learn(${i})">${i}<span class="code-info" id="c-${i}">---</span></button>`;
            }
            
            // 2. Initial Sync
            sync();

            // 3. The Loop (Runs every 2 seconds)
            setInterval(() => {
                // Update the Green Status Bar
                fetch('/status').then(r => r.text()).then(t => {
                    if(t.length > 5) document.getElementById('rx-display').innerHTML = t;
                });

                // Update the Buttons (The Fix)
                sync(); 
            }, 2000);
        }
    </script>
</body>
</html>
)rawliteral";
///////////////////////////////////////////////
///               Fun Stuff                 ///
///////////////////////////////////////////////

//Init
WebRemote::WebRemote(int port) : server(port) {}

void WebRemote::begin(const char* ssid, const char* password) {
    Serial.println("Starting Access Point...");
    WiFi.softAP(ssid, password);
    Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

    // Captive portal
    dnsServer.start(53, "*", WiFi.softAPIP()); 

    server.on("/", std::bind(&WebRemote::handleRoot, this));
    server.on("/send", std::bind(&WebRemote::handleSend, this));
    
    // Memory Provider Endpoint
    server.on("/getMemory", HTTP_GET, [this]() {
        if (_dataProvider != nullptr) {
            server.send(200, "application/json", _dataProvider());
        } else {
            server.send(500, "text/plain", "Data provider not set");
        }
    });

    
    server.on("/status", HTTP_GET, [this]() {
        
        String s = "Protocol: " + String(lastProtocol) + "<br>Code: 0x" + String(lastCode, HEX);
        server.send(200, "text/html", s);
    });

    server.onNotFound(std::bind(&WebRemote::handleNotFound, this));

    server.begin();
    Serial.println("Web Server & Captive Portal Started");
}

void WebRemote::setCallback(CommandCallback cb) {
    onCommand = cb;
}

void WebRemote::handle() {
    dnsServer.processNextRequest(); 
    server.handleClient();
}

void WebRemote::handleRoot() {
    server.send(200, "text/html", INDEX_HTML);
}

void WebRemote::handleNotFound() {
    server.sendHeader("Location", "/", true); 
    server.send(302, "text/plain", "");      
}

void WebRemote::handleStatus() {
    String msg;
    if (lastCode == 0) {
        msg = "Waiting for signal...";
    } else {
        msg = "Protocol: " + String(lastProtocol) + "<br>Code: 0x" + String(lastCode, HEX);
    }
    server.send(200, "text/html", msg);
}

void WebRemote::handleSend() {
    String cmd = server.arg("cmd");
    Serial.println("Web CMD: " + cmd);

    if (onCommand) {
        onCommand(cmd);
    }
    server.send(200, "text/plain", "Sent: " + cmd);
}