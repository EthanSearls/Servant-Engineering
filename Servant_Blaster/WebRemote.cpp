#include "WebRemote.h"
#include <WiFi.h>


///////////////////////////////////////////////
///               SERVER HTML               ///
///////////////////////////////////////////////


const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>S3 Remote Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: Helvetica, sans-serif; background: #222; color: #eee; margin: 0; padding: 20px; text-align: center; }
        .container { position:relative; margin:auto; width:100%; display: flex; flex-direction: column; gap:10px; max-width: 600px; }
        
        /* IR Status Box from original */
        .rx-box { 
            background: #333; border: 1px solid #555; padding: 10px; 
            margin: 15px auto; border-radius: 8px; font-family: monospace; color: #00ff00; width: 90%;
        }

        /* Buttons & Grid */
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin: 20px auto; width: 100%; }
        button { 
            background: #444; border: 2px solid #555; color: white; padding: 15px; 
            border-radius: 12px; font-size: 16px; cursor: pointer; transition: 0.2s;
        }
        button:active { background: #e74c3c; transform: scale(0.95); }

        /* Table Styles */
        table { border-spacing: 0; border-collapse: collapse; width: 100%; margin-top: 10px; background: #333; }
        td, th { border: 1px solid #555; padding: 12px; }
        th { background: #444; }
        
        #advanced { display: none; background: #2a2a2a; padding: 15px; border-radius: 8px; margin-top: 10px; }
        #advanced-toggle { cursor: pointer; color: #aaa; margin-top: 20px; padding: 10px; border: 1px dashed #555; }
        
        .status-text { color: #aaa; font-size: 0.8rem; margin-top: 5px; }
    </style>
</head>
<body onload="ini()">
    <div class="container">
        <h1>S3-Zero Hub</h1>
        
        <div class="rx-box" id="rx-display">Waiting for IR Signal...</div>

        <div class="grid">
            <button onclick="send('power')">POWER</button>
            <button onclick="send('source')">SOURCE</button>
            <button onclick="send('volup')">VOL +</button>
            <button onclick="send('voldown')">VOL -</button>
        </div>
        <div id="tx-status" class="status-text"></div>

        <div id="simpleControls">
            <h3>Channel Presets</h3>
            <table id="channel-table">
                <tr id="head">
                    <th>Preset</th>
                    <th>Channel</th>
                    <th>Actions</th>
                </tr>
            </table>
        </div>

        <div id="advanced-toggle" onclick="advToggle()">Advanced Settings ▾</div>
        <div id="advanced">
            <div class="text-line">Device: ESP32 S3 Remote</div>
            <div class="text-line">Firmware: 1.0.2</div>
        </div>
    </div>

    <script>
        // --- Original Logic ---
        function send(cmd) {
            document.getElementById('tx-status').innerText = "Sending " + cmd + "...";
            fetch('/send?cmd=' + cmd)
                .then(res => res.text())
                .then(text => document.getElementById('tx-status').innerText = text);
        }

        function updateRx() {
            fetch('/status').then(r => r.text()).then(d => {
                document.getElementById('rx-display').innerHTML = d;
            });
        }
        setInterval(updateRx, 2000);

        // --- Your New Logic ---
        let channels = [12, 0, 47, 10, 0];

        function ini() {
            document.getElementById("channel-table").addEventListener("click", (event) => {
                if(event.target.matches("button")){
                    let idParts = event.target.id.split("-");
                    let index = idParts[0];
                    let type = idParts[1];
                    
                    if(type == "edit") { channelEdit(index); }
                    else if(type == "del") { channelClear(index); }
                }
            });
            fillChannelTable();
        }

        function channelEdit(index) {
            let newChannel = prompt("Enter a channel number:", "");
            if (newChannel !== null && !isNaN(newChannel) && newChannel !== "") {
                channels[index-1] = Number(newChannel);
                refreshTable();
                // Optional: send to ESP32 here
                send("setchan-" + index + "-" + newChannel);
            }
        }

        function channelClear(index) {
            channels[index-1] = 0;
            refreshTable();
            send("clearchan-" + index);
        }

        function refreshTable() {
            const table = document.getElementById("channel-table");
            // Clear all rows except header
            while(table.rows.length > 1) { table.deleteRow(1); }
            fillChannelTable();
        }

        function fillChannelTable() {
            let table = document.getElementById("channel-table");
            channels.forEach((chnl, i) => {
                let row = table.insertRow(-1);
                let cell1 = row.insertCell(0);
                let cell2 = row.insertCell(1);
                let cell3 = row.insertCell(2);

                cell1.textContent = i + 1;
                cell2.textContent = (chnl !== 0) ? chnl : "Empty";
                
                cell3.innerHTML = `<button id="${i+1}-edit" style="padding:5px 10px; margin-right:5px;">Edit</button>` +
                                 `<button id="${i+1}-del" style="padding:5px 10px; background:#666;">Clear</button>`;
            });
        }

        function advToggle() {
            let element = document.getElementById("advanced");
            element.style.display = (element.style.display === 'none' || element.style.display === '') ? 'block' : 'none';
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

    // Captive portal, catch-all for connections
    dnsServer.start(53, "*", WiFi.softAPIP()); 
    // --------------------------------

    server.on("/", std::bind(&WebRemote::handleRoot, this));
    server.on("/send", std::bind(&WebRemote::handleSend, this));
    server.on("/status", std::bind(&WebRemote::handleStatus, this));
    
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

// If they try to navigate reroute to site
void WebRemote::handleNotFound() {
    server.sendHeader("Location", "/", true); // Redirect to our IP
    server.send(302, "text/plain", "");       // HTTP 302 Found
}


// Display code received on blaster
void WebRemote::handleStatus() {
    String msg;
    if (lastCode == 0) {
        msg = "Waiting for signal...";
    } else {
        msg = "Protocol: " + String(lastProtocol) + "<br>Code: 0x" + String(lastCode, HEX);
    }
    server.send(200, "text/html", msg);
}


// Send code selected on website to blaster
void WebRemote::handleSend() {
    String cmd = server.arg("cmd");
    Serial.println("Web CMD: " + cmd);

    if (onCommand) {
        onCommand(cmd);
    }
    server.send(200, "text/plain", "Sent: " + cmd);
}