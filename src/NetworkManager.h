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
String networkManagerDeviceName();

bool networkManagerApplyBlePayload(const String& payload);
String networkManagerLastConfigError();
String networkManagerProvisioningStatus();
bool networkManagerGetLocation(float& latitude, float& longitude);
