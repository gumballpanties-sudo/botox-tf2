#pragma once
#include "../../globals/globals.h"
#include <string>
#include <vector>

void SaveConfig(const char* path = CONFIG_PATH);
void LoadConfig(const char* path = CONFIG_PATH);

// Named cfgs in CONFIG_DIR. Name = file name with or without ".cfg".
std::vector<std::string> ListConfigs();   // sorted, with ".cfg"
void SaveConfigNamed(const char* name);
void LoadConfigNamed(const char* name);
bool DeleteConfigNamed(const char* name);   // settings tab "remove"
