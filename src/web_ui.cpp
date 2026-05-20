#include "web_ui.h"

#include <ETH.h>
#include <WebServer.h>

#include "config.h"
#include "uart_link.h"

namespace {
WebServer server(80);
bool webServerStarted = false;

bool ethernetReady() {
  if (!ETH.linkUp()) {
    return false;
  }

  const IPAddress ip = ETH.localIP();
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

String htmlPage() {
  String html;
  html.reserve(5000);
  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Rotation WT32</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#10161c;color:#e8eef3;margin:0;padding:20px;}";
  html += ".card{max-width:760px;margin:0 auto;background:#17212b;border-radius:16px;padding:20px;box-shadow:0 10px 30px rgba(0,0,0,.25);}";
  html += "h1{margin-top:0;font-size:28px;}h2{font-size:18px;margin:18px 0 10px;}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;}";
  html += ".item{background:#223140;border-radius:12px;padding:12px;}";
  html += ".label{font-size:12px;color:#9cb0c3;text-transform:uppercase;}";
  html += ".value{font-size:24px;margin-top:4px;}";
  html += "button{background:#2c8cff;color:#fff;border:0;border-radius:10px;padding:12px 16px;font-size:16px;cursor:pointer;}";
  html += "button.secondary{background:#44586d;}input{width:100%;padding:12px;font-size:18px;border-radius:10px;border:1px solid #44586d;background:#0f1720;color:#fff;box-sizing:border-box;}";
  html += ".row{display:flex;gap:10px;flex-wrap:wrap;}.row>*{flex:1 1 180px;}";
  html += ".log{margin-top:12px;padding:12px;background:#0f1720;border-radius:10px;color:#9cb0c3;min-height:24px;}";
  html += "</style></head><body><div class='card'>";
  html += "<h1>RotationKon WT32</h1>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Device</div><div class='value' id='deviceId'>-</div></div>";
  html += "<div class='item'><div class='label'>IP</div><div class='value' id='ip'>-</div></div>";
  html += "<div class='item'><div class='label'>State</div><div class='value' id='state'>-</div></div>";
  html += "<div class='item'><div class='label'>Command</div><div class='value' id='command'>-</div></div>";
  html += "<div class='item'><div class='label'>Angle</div><div class='value' id='angle'>-</div></div>";
  html += "<div class='item'><div class='label'>Target</div><div class='value' id='target'>-</div></div>";
  html += "<div class='item'><div class='label'>Executing</div><div class='value' id='executing'>-</div></div>";
  html += "<div class='item'><div class='label'>Errors</div><div class='value' id='errors'>-</div></div>";
  html += "</div>";
  html += "<h2>Goto azimuth</h2>";
  html += "<div class='row'><input id='gotoAngle' type='number' min='0' max='359' value='0'><button onclick='sendGoto()'>Send Goto</button></div>";
  html += "<h2>Actions</h2>";
  html += "<div class='row'><button class='secondary' onclick=\"sendAction('ping')\">Ping</button><button class='secondary' onclick=\"sendAction('angle')\">Request Angle</button><button class='secondary' onclick='refreshStatus()'>Refresh UI</button></div>";
  html += "<div class='log' id='log'>Ready</div>";
  html += "<script>";
  html += "function setLog(t){document.getElementById('log').textContent=t;}";
  html += "async function refreshStatus(){const r=await fetch('/api/status');const d=await r.json();";
  html += "document.getElementById('deviceId').textContent=d.deviceId;";
  html += "document.getElementById('ip').textContent=d.ip;";
  html += "document.getElementById('state').textContent=d.state;";
  html += "document.getElementById('command').textContent=d.command;";
  html += "document.getElementById('angle').textContent=d.angle;";
  html += "document.getElementById('target').textContent=d.target;";
  html += "document.getElementById('executing').textContent=d.executing?'YES':'NO';";
  html += "document.getElementById('errors').textContent=d.errors;";
  html += "}";
  html += "async function sendGoto(){const v=document.getElementById('gotoAngle').value;const r=await fetch('/api/goto?angle='+encodeURIComponent(v),{method:'POST'});const d=await r.json();setLog(JSON.stringify(d));setTimeout(refreshStatus,300);}";
  html += "async function sendAction(a){const r=await fetch('/api/action?action='+encodeURIComponent(a),{method:'POST'});const d=await r.json();setLog(JSON.stringify(d));setTimeout(refreshStatus,300);}";
  html += "refreshStatus();setInterval(refreshStatus," + String(WEB_STATUS_REFRESH_MS) + ");";
  html += "</script></div></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", htmlPage());
}

void handleStatus() {
  String body = "{";
  body += "\"deviceId\":\"";
  body += DEVICE_ID;
  body += "\",\"ip\":\"";
  body += ETH.localIP().toString();
  body += "\",\"state\":\"";
  body += uartGetRemoteStateString();
  body += "\",\"command\":\"";
  body += uartGetCommandStateString();
  body += "\",\"angle\":";
  body += String(uartGetRemoteAngle());
  body += ",\"target\":";
  body += String(uartGetRemoteTarget());
  body += ",\"executing\":";
  body += uartIsRemoteExecuting() ? "true" : "false";
  body += ",\"errors\":";
  body += String(uartGetRemoteErrors());
  body += "}";
  server.send(200, "application/json", body);
}

void handleGoto() {
  if (!server.hasArg("angle")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing angle\"}");
    return;
  }

  const int angle = server.arg("angle").toInt();
  if (angle < 0 || angle > 359) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Angle out of range\"}");
    return;
  }

  uartSendGoto(angle);
  server.send(200, "application/json", "{\"ok\":true,\"action\":\"goto\"}");
}

void handleAction() {
  const String action = server.arg("action");

  if (action == "ping") {
    uartSendPing();
    server.send(200, "application/json", "{\"ok\":true,\"action\":\"ping\"}");
    return;
  }

  if (action == "angle") {
    uartRequestAngle();
    server.send(200, "application/json", "{\"ok\":true,\"action\":\"angle\"}");
    return;
  }

  server.send(400, "application/json", "{\"ok\":false,\"error\":\"Unknown action\"}");
}

void startWebServerIfNeeded() {
  if (webServerStarted || !ethernetReady()) {
    return;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/goto", HTTP_POST, handleGoto);
  server.on("/api/action", HTTP_POST, handleAction);
  server.begin();
  webServerStarted = true;

  Serial.print("[WEB] ready http://");
  Serial.println(ETH.localIP());
}
}

void webUiInit() {
  webServerStarted = false;
}

void webUiUpdate() {
  startWebServerIfNeeded();
  server.handleClient();
}
