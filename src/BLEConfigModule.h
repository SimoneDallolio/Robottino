#pragma once

#include <Arduino.h>

void bleConfigBegin(const String& deviceName);
void bleConfigLoop();
void bleConfigStop();
bool bleConfigIsActive();
void bleConfigNotify(const String& message);
