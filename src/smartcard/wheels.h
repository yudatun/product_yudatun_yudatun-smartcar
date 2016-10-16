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

#ifndef SRC_SMARTCARD_WHEELS_H_
#define SRC_SMARTCARD_WHEELS_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <brillo/streams/file_stream.h>

namespace smartcard {

class Wheels final {
 public:
    Wheels();

    std::vector<std::string> GetWheelNames() const;
    std::vector<int> GetWheelPins() const;
    std::vector<bool> GetWheelStatus() const;

    size_t GetWheelCount() const;

    bool IsWheelOn(int pin) const;
    void SetWheelStatus(int pin, bool on);
    void SetAllWheels(bool on);

 private:
    bool ExportGpio(int pin) const;
    bool WriteGpio(int pin, const std::string& type, const std::string& v) const;
    bool ReadGpio(int pin, const std::string& type, std::string& v) const;

    brillo::StreamPtr GetGpioExportStream(bool write) const;
    brillo::StreamPtr GetGpioDataStream(
        int pin, const std::string& type, bool write) const;

    std::vector<std::string> wheel_names_;
    std::vector<int> wheel_pins_;
    std::vector<bool> wheel_status_;

    DISALLOW_COPY_AND_ASSIGN(Wheels);
};

}  // namespace smartcard

#endif  // SRC_SMARTCARD_WHEELS_H_
