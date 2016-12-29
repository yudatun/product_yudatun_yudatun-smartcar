/*
 * Copyright (C) 2016 The Yudatun Open Source Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */

#include <string>

#include <base/files/file_util.h>

#include "configs.h"

namespace smartcar {

void LoadConfigsFromFile(const base::FilePath& json_file_path, Configs* configs) {
    std::string config_json;
    LOG(INFO) << "Loading server configuration from " << json_file_path.value();
    return base::ReadFileToString(json_file_path, &config_json) &&
           LoadConfigFromString(config_json, config, nullptr);
}

bool LoadConfigFromString(const std::string& config_json,
                          Configs* config,
                          brillo::ErrorPtr* error) {

}

} // namespace smartcar
