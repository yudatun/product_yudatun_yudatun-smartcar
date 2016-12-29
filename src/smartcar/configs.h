/*
 * Copyright (C) 2016 The Yudatun Open Source Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */

#ifndef SRC_SMARTCAR_CONFIGS_H_
#define SRC_SMARTCAR_CONFIGS_H_

#include <string>

#include "MQTTClient.h"

namespace smartcar {

struct Configs final {
 public:
    std::string client_id_;
    std::string topic_;
    std::string host_;
    std::string port_;
    int         qos_;

    MQTTClient_connectOptions connect_options_;
};

}

#endif /* SRC_SMARTCAR_CONFIGS_H_ */
