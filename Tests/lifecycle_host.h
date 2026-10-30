#pragma once
#include <string>
namespace LifecycleHost {
void Configure(const std::string& root, bool reject_preferences = false);
int LiveAudioDevices();
}
