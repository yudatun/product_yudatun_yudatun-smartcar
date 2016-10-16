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

#include <string>

#include <base/bind.h>
#include <base/format_macros.h>
#include <base/macros.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/streams/file_stream.h>
#include <brillo/streams/stream_utils.h>

#include "binder_constants.h"
#include "wheels.h"

namespace smartcard {

Wheels::Wheels() {
    const std::initializer_list<const char*> kLogicalWheelss = {
        "left_front",
        "right_front",
        "left_after",
        "right_after",
    };

    for (const char* wheel_name : kLogicalWheelss) {
        wheel_names_.push_back(wheel_name);
    }

    const std::initializer_list<int> kWheelsGpioPins = {
        kLeftFrontWheelPin,
        kRightFrontWheelPin,
        kLeftAfterWheelPin,
        kRightAfterWheelPin,
    };

    for (int pin : kWheelsGpioPins) {
        wheel_pins_.push_back(pin);
        ExportGpio(pin);
        WriteGpio(pin, "direction", "out");
        WriteGpio(pin, "value", "0");
        wheel_status_.push_back(false);
    }
}

std::vector<std::string> Wheels::GetWheelNames() const {
    return wheel_names_;
}

std::vector<int> Wheels::GetWheelPins() const {
    return wheel_pins_;
}

std::vector<bool> Wheels::GetWheelStatus() const {
    return wheel_status_;
}

size_t Wheels::GetWheelCount() const {
    return wheel_names_.size();
}

bool Wheels::IsWheelOn(int pin) const {
    std::string value;
    if (!ReadGpio(pin, "value", value)) {
        return false;
    }
    int v = 0;
    if (!base::StringToInt(value, &v)) {
        return false;
    }
    return v > 0;
}

void Wheels::SetWheelStatus(int pin, bool on) {
    std::string value = on ? "1" : "0";
    if (!WriteGpio(pin, "value", value)) {
        return;
    }
    for (size_t i = 0; i < GetWheelCount(); ++i) {
        if (pin == wheel_pins_[i]) {
            wheel_status_[i] = on;
            break;
        }
    }
}

void Wheels::SetAllWheels(bool on) {
    for (size_t i = 0; i < GetWheelCount(); ++i) {
        SetWheelStatus(wheel_pins_[i], on);
    }
}

// Private Functions
bool Wheels::ExportGpio(int pin) const {
    brillo::StreamPtr stream = GetGpioExportStream(true);
    if (!stream) {
        return false;
    }
    std::string value = base::StringPrintf("%d", pin);
    stream->WriteAllBlocking(value.data(), value.size(), nullptr);
    return true;
}

bool Wheels::WriteGpio(
    int pin, const std::string& type, const std::string& v) const {
    brillo::StreamPtr stream = GetGpioDataStream(pin, type, true);
    if (!stream) {
        return false;
    }
    stream->WriteAllBlocking(v.data(), v.size(), nullptr);
    return true;
}

bool Wheels::ReadGpio(int pin, const std::string& type, std::string& v) const {
    brillo::StreamPtr stream = GetGpioDataStream(pin, type, false);
    if (!stream) {
        return false;
    }

    char buffer[10];
    size_t size_read = 0;
    if (!stream->ReadNonBlocking(
            buffer, sizeof (buffer), &size_read, nullptr, nullptr)) {
        return false;
    }

    v.insert(0, buffer, size_read);
    base::TrimWhitespaceASCII(v, base::TrimPositions::TRIM_ALL, &v);

    return true;
}

// GPIO sysfs path
const char* const kGPIOSysfsPath = "/sys/class/gpio";

/*
 * Get a file stream for GPIO export
 * bool write: access mode with true for write
 * return: file stream pointer
 */
brillo::StreamPtr Wheels::GetGpioExportStream(bool write) const {
    std::string gpio_path;
    gpio_path = base::StringPrintf("%s/export", kGPIOSysfsPath);
    base::FilePath dev_path{gpio_path};
    auto access_mode = brillo::stream_utils::MakeAccessMode(!write, write);
    return brillo::FileStream::Open(
        dev_path, access_mode, brillo::FileStream::Disposition::OPEN_EXISTING,
        nullptr);
}

/*
 * Get a file stream for GPIO data
 * int gpio: GPIO pin number
 * std::string type: GPIO type e.g. direction, edge, value
 * bool write: access mode with true for write
 * return: file stream pointer
 */
brillo::StreamPtr Wheels::GetGpioDataStream(
    int pin, const std::string& type, bool write) const {
    std::string gpio_path;
    gpio_path = base::StringPrintf("%s/gpio%d/%s", kGPIOSysfsPath, pin, type.c_str());
    base::FilePath dev_path{gpio_path};
    auto access_mode = brillo::stream_utils::MakeAccessMode(!write, write);
    return brillo::FileStream::Open(
        dev_path, access_mode, brillo::FileStream::Disposition::OPEN_EXISTING,
        nullptr);
}

}  // namespace smartcard
