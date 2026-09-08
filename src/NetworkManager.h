#pragma once

#include <Arduino.h>

void networkManagerBegin();
void networkManagerLoop();
void networkManagerStartBle();
void networkManagerStopBle();
void networkManagerStopWifi();
bool networkManagerConnectForSync();
bool networkManagerIsBleActive();
bool networkManagerTakeConfigUpdate();

bool networkManagerApplyBlePayload(const String& payload);
String networkManagerProvisioningStatus();
bool networkManagerGetLocation(float& latitude, float& longitude);
