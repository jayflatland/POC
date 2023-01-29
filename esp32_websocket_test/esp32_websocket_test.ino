#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>

#include "index_html.hpp"
#include "powerhawk.hpp"

const char *ssid = "To The Oasis";
const char *password = "deadbeef";

bool ledState = 0;
const int ledPin = 2;

AsyncWebServer webserver(80);
AsyncWebSocket websocket_handler("/ws");
AsyncWebSocket powerhawk_websocket_handler("/powerhawk");

powerhawk::powerhawk_type powerhawk_monitor;

void notifyClients()
{
    std::string s = std::to_string(ledState);
    // websocket_handler.textAll(String(ledState));
    websocket_handler.textAll(s.data(), s.size());
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
    {
        data[len] = 0;
        if (strcmp((char *)data, "toggle") == 0)
        {
            ledState = !ledState;
            notifyClients();
        }
    }
}

void onEvent(AsyncWebSocket *websocket, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
    {
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        std::string s = std::to_string(ledState);
        websocket_handler.text(client->id(), s.data(), s.size());
    }
    break;
    case WS_EVT_DISCONNECT:
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;
    case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
        break;
    }
}

void on_event_powerhawk(AsyncWebSocket *websocket, AsyncWebSocketClient *client, AwsEventType type,
                        void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
    {
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        // std::string s = std::to_string(ledState);
        // websocket_handler.text(client->id(), s.data(), s.size());
    }
    break;
    case WS_EVT_DISCONNECT:
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;
    case WS_EVT_DATA:
        // handleWebSocketMessage(arg, data, len);
        break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
        break;
    }
}

void initWebSocket()
{
    websocket_handler.onEvent(onEvent);
    powerhawk_websocket_handler.onEvent(on_event_powerhawk);
    webserver.addHandler(&websocket_handler);
    webserver.addHandler(&powerhawk_websocket_handler);
}

// String processor(const String &var)
// {
//     Serial.println(var);
//     if (var == "STATE")
//     {
//         if (ledState)
//         {
//             return "ON";
//         }
//         else
//         {
//             return "OFF";
//         }
//     }
//     return String();
// }

void setup()
{
    // Serial port for debugging purposes
    Serial.begin(115200);

    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);

    // Trying static IP, is PITA
    //  IPAddress addr;
    //  addr.fromString("10.1.10.24");
    //  WiFi.config(addr);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.println("Connecting to WiFi..");
    }

    // Print ESP Local IP Address
    Serial.println(WiFi.localIP());

    initWebSocket();

    // Route for root / web page
    webserver.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->send_P(200, "text/html", index_html, nullptr); });

    // Start webserver
    webserver.begin();

    powerhawk_monitor.setup();
}

void loop()
{
    websocket_handler.cleanupClients();
    powerhawk_websocket_handler.cleanupClients();

    powerhawk_monitor.loop();
    if (powerhawk_monitor.report)
    {
        powerhawk_websocket_handler.textAll("data!!");
    }

    digitalWrite(ledPin, ledState);
}