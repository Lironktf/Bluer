/*
  BLE-Scanner

  (c) 2020 Christian.Lorenz@gromeck.de

  module to handle the HTTP stuff


  This file is part of BLE-Scanner.

  BLE-Scanner is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  BLE-Scanner is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with BLE-Scanner.  If not, see <https://www.gnu.org/licenses/>.

*/

#include <WebServer.h>
#include <Update.h>
#include "config.h"
#include "http.h"
#include "wifiHandler.h"
#include "ntp.h"
#include "bluetooth.h"
#include "watchdog.h"
#include "scandev.h"
#include "mqtt.h"

/*
   the web server object
*/
static WebServer _WebServer(80);

/*
   this is a callback helper to push data to the web client in chunks
*/
static void HttpSendHelper(const String& content)
{
  _WebServer.sendContent(content);
}

/*
  time of the last HTTP request
*/
static unsigned long _last_http_request = 0;

/*
   setup the webserver
*/
void HttpSetup(void)
{
  static String _html_header PROGMEM =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>"
    "<title>Laundry Scanner</title>"
    "<link href='/styles.css' rel='stylesheet' type='text/css'>"
    "</head>"
    "<body>"
    "<div class=content>"
    "<div class=header>"
    "<h3>Laundry Machine Scanner</h3>"
    "<h2>" + String(_config.device.name) + "</h2>"
    "</div>"
    ;
  static String _html_footer PROGMEM =
    "<div class=footer>"
    "<hr>"
    "<a href='https://laun-dryer.vercel.app' target='_blank' style='color:#aaa;'>Laundry Scanner " GIT_VERSION "</a>"
    "</div>"
    "</div>"
    "</body>"
    "</html>"
    ;

  LogMsg("HTTP: setting up HTTP server");

  /*
     get the current config as a duplicate
  */
  ConfigGet(0, sizeof(CONFIG_T), &_config);

  _WebServer.onNotFound( []() {
    _last_http_request = millis();
    if (_config.device.password[0] && !_WebServer.authenticate(HTTP_WEB_USER, _config.device.password))
      return _WebServer.requestAuthentication();

    _WebServer.send(200, "text/html",
                    _html_header +
                    "<form action='/machines' method='get'><button class='button greenbg'>Tracked Machines</button></form><p>"
                    "<form action='/info' method='get'><button>System Information</button></form><p>"
                    "<form action='/upgrade' method='get'><button>Firmware Upgrade</button></form><p>"
                    "<form action='/restart' method='get' onsubmit=\"return confirm('Are you sure to restart the device?');\"><button class='button redbg'>Restart</button></form><p>"
                    + _html_footer);
  });

  _WebServer.on("/styles.css", []() {
    _last_http_request = millis();
    _WebServer.send(200, "text/css",
                    "html, body { background:#ffffff; }"
                    "body { margin:1rem; padding:0; font-familiy:'sans-serif'; color:#202020; text-align:center; font-size:1rem; }"
                    "input { width:100%; font-size:1rem; box-sizing: border-box; -webkit-box-sizing: border-box; }"
                    "input[type=radio] { width:2rem; }"
                    "button { border: 0; border-radius: 0.3rem; background: #1881ba; color: #ffffff; line-height: 2.4rem; font-size: 1.2rem; width: 100%; -webkit-transition-duration: 0.5s; transition-duration: 0.5s; cursor: pointer; opacity:0.8; }"
                    "button:hover { opacity: 1.0; }"
                    ".header { text-align:center; }"
                    ".content { text-align:left; display:inline-block; color:#000000; min-width:340px; }"
                    ".msg { text-align:center; color:#be3731; font-weight:bold; padding:5rem 0; }"
                    ".devinfo { padding:0; margin:0; border-spacing:0; width: 100%; }"
                    ".devinfo tr th { background: #c0c0c0; font-weight:bold; }"
                    ".devinfo tr td { font-familiy:'monospace'; }"
                    ".devinfo tr td:first-child { font-weight:bold; }"
                    ".devinfo tr td, .devinfo tr th { padding:4px; }"
                    ".devinfo tr:nth-child(even) { background: #f0f0f0; }"
                    ".devinfo tr:nth-child(odd) {background: #ffffff; }"
                    ".btscanlist { padding:0; margin:0; border-spacing:0; width: 100%; }"
                    ".btscanlist tr th { background: #c0c0c0; font-weight:bold; }"
                    ".btscanlist tr td { font-familiy:'monospace'; }"
                    ".btscanlist tr td, .btscanlist tr th { padding:4px; }"
                    ".btscanlist tr:nth-child(even) { background: #f0f0f0; }"
                    ".btscanlist tr:nth-child(odd) {background: #ffffff; }"
                    ".footer { text-align:right; }"
                    ".greenbg { background: #348f4b; }"
                    ".redbg { background: #a12828; }"
                   );
  });

  _WebServer.on("/config", []() {
    _last_http_request = millis();
    if (_config.device.password[0] && !_WebServer.authenticate(HTTP_WEB_USER, _config.device.password))
      return _WebServer.requestAuthentication();

    // Configuration is hardcoded - show info page
    _WebServer.send(200, "text/html",
                    _html_header +
                    "<div class='msg'>"
                    "<p><b>Configuration is hardcoded</b></p>"
                    "<p>To change settings, edit the following in <code>config.h</code>:</p>"
                    "<ul style='text-align:left;'>"
                    "<li><b>WiFi:</b> WIFI_SSID, WIFI_PASSWORD</li>"
                    "<li><b>MQTT:</b> MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD</li>"
                    "<li><b>Scanning:</b> BT_SCAN_TIME, BT_PAUSE_TIME, BT_ABSENCE_CYCLES</li>"
                    "<li><b>Target:</b> TARGET_DEVICE_NAME, TARGET_MANUFACTURER_ID</li>"
                    "</ul>"
                    "<p>Then recompile and upload the firmware.</p>"
                    "</div>"
                    "<p><form action='/' method='get'><button>Main Menu</button></form><p>"
                    + _html_footer);
  });

  // Redirect old config sub-pages to main config page
  _WebServer.on("/config/device", []() {
    _WebServer.sendHeader("Location", "/config", true);
    _WebServer.send(302, "text/plain", "");
  });
  _WebServer.on("/config/wifi", []() {
    _WebServer.sendHeader("Location", "/config", true);
    _WebServer.send(302, "text/plain", "");
  });
  _WebServer.on("/config/ntp", []() {
    _WebServer.sendHeader("Location", "/config", true);
    _WebServer.send(302, "text/plain", "");
  });
  _WebServer.on("/config/mqtt", []() {
    _WebServer.sendHeader("Location", "/config", true);
    _WebServer.send(302, "text/plain", "");
  });
  _WebServer.on("/config/bluetooth", []() {
    _WebServer.sendHeader("Location", "/config", true);
    _WebServer.send(302, "text/plain", "");
  });
  _WebServer.on("/config/reset", []() {
    _WebServer.sendHeader("Location", "/config", true);
    _WebServer.send(302, "text/plain", "");
  });

  _WebServer.on("/info", []() {
    if (_config.device.password[0] && !_WebServer.authenticate(HTTP_WEB_USER, _config.device.password))
      return _WebServer.requestAuthentication();

    _last_http_request = millis();

    _WebServer.send(200, "text/html",
                    _html_header +
                    "<div class='info'>"
                    "<table class='devinfo'>"

                    "<tr><th colspan=2>Device</th></tr>"
                    "<tr>"
                    "<td>SW Version</td>"
                    "<td>" GIT_VERSION "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>SW Build Date</td>"
                    "<td>" __DATE__ " " __TIME__ "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Device Name</td>"
                    "<td>" + String(_config.device.name) + "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Machines Tracked</td>"
                    "<td>" + String(ScanDevGetCount()) + "</td>"
                    "</tr>"

                    "<tr><th colspan=2>WiFi</th></tr>"
                    "<tr>"
                    "<td>SSID</td>"
                    "<td>" + WifiGetSSID() + "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Channel</td>"
                    "<td>" + WifiGetChannel() + "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>RSSI</td>"
                    "<td>" + String(WIFI_RSSI_TO_QUALITY(WifiGetRSSI())) + " % (" + WifiGetRSSI() + " dBm)</td>"
                    "</tr>"
                    "<tr>"
                    "<td>MAC</td>"
                    "<td>" + WifiGetMacAddr() + "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>IP Address</td>"
                    "<td>" + WifiGetIpAddr() + "</td>"
                    "</tr>"

                    "<tr><th colspan=2>MQTT</th></tr>"
                    "<tr>"
                    "<td>Broker</td>"
                    "<td>" MQTT_BROKER ":" + String(MQTT_PORT) + "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Status</td>"
                    "<td>" + String(MqttGetStatusString()) + "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Topic Prefix</td>"
                    "<td>laundry/machines/</td>"
                    "</tr>"

                    "<tr><th colspan=2>Target Devices</th></tr>"
                    "<tr>"
                    "<td>Device Name</td>"
                    "<td>" TARGET_DEVICE_NAME "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Manufacturer ID</td>"
                    "<td>0x" + String(TARGET_MANUFACTURER_ID, HEX) + "</td>"
                    "</tr>"

                    "<tr><th colspan=2>Time</th></tr>"
                    "<tr>"
                    "<td>NTP Server</td>"
                    "<td>" + _config.ntp.server + "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Current Time</td>"
                    "<td>" + String(TimeToString(now())) + "</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Uptime</td>"
                    "<td>" + String(TimeToString(NtpUptime())) + "</td>"
                    "</tr>"

                    "<tr><th colspan=2>Bluetooth Scanning</th></tr>"
                    "<tr>"
                    "<td>Scan Duration</td>"
                    "<td>" + _config.bluetooth.scan_time + " s</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Pause Between Scans</td>"
                    "<td>" + _config.bluetooth.pause_time + " s</td>"
                    "</tr>"
                    "<tr>"
                    "<td>Absence Timeout Cycles</td>"
                    "<td>" + _config.bluetooth.absence_cycles + "</td>"
                    "</tr>"

                    "</table>"
                    "</div>"
                    "<p><form action='/' method='get'><button>Main Menu</button></form><p>"
                    + _html_footer);
  });

  _WebServer.on("/restart", []() {
    if (_config.device.password[0] && !_WebServer.authenticate(HTTP_WEB_USER, _config.device.password))
      return _WebServer.requestAuthentication();

    _last_http_request = millis();

    _WebServer.send(200, "text/html",
                    _html_header +
                    "<div class='msg'>"
                    "Device will restart now."
                    "</div>"
                    + _html_footer);

    /*
        trigger reboot
    */
    StateChange(STATE_WAIT_BEFORE_REBOOTING);
  });

  /*
     firmware upgrade -- form
  */
  _WebServer.on("/upgrade", HTTP_GET, []() {
    if (_config.device.password[0] && !_WebServer.authenticate(HTTP_WEB_USER, _config.device.password))
      return _WebServer.requestAuthentication();

    _last_http_request = millis();

    _WebServer.send(200, "text/html",
                    _html_header +

                    "<fieldset>"
                    "<legend>"
                    "<b>&nbsp;Upgrade by file upload&nbsp;</b>"
                    "</legend>"
                    "<form method='post' action='/upgrade' enctype='multipart/form-data'>"

                    "<p>"
                    "<b>Firmware File</b>"
                    "<br>"
                    "<input name='fwfile' type='file' placeholder='Firmware File'>"
                    "</p>"

                    "<button name='upgrade' type='submit' class='button greenbg'>Start upgrade</button>"
                    "</form>"
                    "</fieldset>"

                    "<p><form action='/' method='get'><button>Main Menu</button></form><p>"
                    + _html_footer);
  });

  /*
     firmware upgrade -- flash
  */
  _WebServer.on("/upgrade", HTTP_POST, []() {
    if (_config.device.password[0] && !_WebServer.authenticate(HTTP_WEB_USER, _config.device.password))
      return _WebServer.requestAuthentication();

    _last_http_request = millis();

    _WebServer.send(200, "text/html",
                    _html_header +
                    "<div class='msg'>"
                    "Upgrade " + ((Update.hasError()) ? "failed" : "succeeded") +
                    "<p>"
                    "Device will restart now."
                    "</div>"
                    + _html_footer);

    /*
        trigger reboot
    */
    StateChange(STATE_WAIT_BEFORE_REBOOTING);
  }, []() {
    if (_config.device.password[0] && !_WebServer.authenticate(HTTP_WEB_USER, _config.device.password))
      return _WebServer.requestAuthentication();

    _last_http_request = millis();

    /*
       this is the upload handler
    */
    HTTPUpload& upload = _WebServer.upload();

    if (upload.status == UPLOAD_FILE_START) {
      LogMsg("HTTP: Starting firmware upload: %s", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        LogMsg("HTTP: upgrade failure: %s", Update.errorString());
      }
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        LogMsg("HTTP: upload failure: %s", Update.errorString());
      }
    }
    else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        if (Update.isFinished()) {
          LogMsg("HTTP: upgrade success: %u bytes -- rebooting...", upload.totalSize);
        }
        else {
          LogMsg("HTTP: upgrade failure: %s", Update.errorString());
        }
      }
      else {
        LogMsg("HTTP: upgrade failure: %s", Update.errorString());
      }
    }
  });


  _WebServer.on("/machines", []() {
    if (_config.device.password[0] && !_WebServer.authenticate(HTTP_WEB_USER, _config.device.password))
      return _WebServer.requestAuthentication();

    _last_http_request = millis();

    _WebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _WebServer.send(200, "text/html");
    _WebServer.sendContent(
      _html_header +
      "<p><b>Scanning for:</b> " + String(TARGET_DEVICE_NAME) + " (0x" + String(TARGET_MANUFACTURER_ID, HEX) + ")</p>"
      "<p><b>MQTT Broker:</b> " + String(MQTT_BROKER) + ":" + String(MQTT_PORT) + 
      " (" + String(MqttGetStatusString()) + ")</p>"
      "<p><form action='/machines' method='get'><button class='button greenbg'>Refresh</button></form><p>");

    ScanDevListHTML(HttpSendHelper);

    _WebServer.sendContent(
      "<p><form action='/machines' method='get'><button class='button greenbg'>Refresh</button></form><p>"
      "<p><form action='/' method='get'><button>Main Menu</button></form><p>"
      + _html_footer);
  });

  // Keep old endpoint for compatibility
  _WebServer.on("/btlist", []() {
    _WebServer.sendHeader("Location", "/machines", true);
    _WebServer.send(302, "text/plain", "");
  });

  _WebServer.begin();
  _last_http_request = millis();
  LogMsg("HTTP: server started");
}

/*
**	handle incoming HTTP requests
*/
void HttpUpdate(void)
{
  _WebServer.handleClient();
}

/*
  return the time in seconds since the last HTTP request
*/
int HttpLastRequest(void)
{
  return (millis() - _last_http_request) / 1000;
}/**/
