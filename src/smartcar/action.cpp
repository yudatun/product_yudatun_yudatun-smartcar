/*
 * Copyright 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <base/bind.h>
#include <base/message_loop/message_loop.h>

#include "action.h"
#include "action_forward.h"

using yudatun::product::smartcar::ISmartCarService;

Action::Action(
    android::sp<ISmartCarService> smartcar_service,
    const base::TimeDelta& duration)
    : smartcar_service_{smartcar_service}, duration_{duration} {
}

Action::~Action() {
    SetAllWheels(false);
}

void Action::Start() {
    DoAction();
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&Action::Start, weak_ptr_factory_.GetWeakPtr()),
        duration_);
}

void Action::Stop() {
    weak_ptr_factory_.InvalidateWeakPtrs();
}

bool Action::GetWheel(int pin) const {
    bool on = false;
    smartcar_service_->getWheelStatus(pin, &on);
    return on;
}

void Action::SetWheel(int pin, bool on) {
    smartcar_service_->setWheelStatus(pin, on);
}

void Action::SetAllWheels(bool on) {
    smartcar_service_->setAllWheels(on);
}

std::unique_ptr<Action> Action::Create(
    android::sp<ISmartCarService> smartcar_service,
    const std::string& type,
    const base::TimeDelta& duration) {
    std::unique_ptr<Action> action;

    LOG(INFO) << "Action: {" << type << ", " << duration << "}";

    if (type == "forward") {
        action.reset(new ActionForward{smartcar_service, duration});
    } else if (type == "back") {
    }

    return action;
}
