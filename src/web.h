// Settings web page, WiFi setup page and captive portal
#pragma once

void webBegin();          // register routes and start the HTTP server (both modes)
void webStartPortal();    // answer every DNS query with our own IP (setup hotspot only)
void webLoop();           // call often
