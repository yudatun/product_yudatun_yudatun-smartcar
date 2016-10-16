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

#include <sysexits.h>

#include <base/bind.h>
#include <base/command_line.h>
#include <base/macros.h>
#include <binderwrapper/binder_wrapper.h>
#include <brillo/binder_watcher.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>

#include "binder_constants.h"
#include "yudatun/product/smartcar/BnSmartCarService.h"
#include "wheels.h"

using android::String16;

namespace smartcard {

// SmartCarService
class SmartCarService : public yudatun::product::smartcar::BnSmartCarService {
  public:
    android::binder::Status getAllWheelNames(
        std::vector<String16>* wheels) override {
        std::vector<std::string> wheelNames = wheels_.GetWheelNames();
        for (const std::string& name : wheelNames) {
            wheels->push_back(String16{name.c_str()});
        }
        return android::binder::Status::ok();
    }

    android::binder::Status getAllWheelPins(
        std::vector<int>* wheels) override {
        *wheels = wheels_.GetWheelPins();
        return android::binder::Status::ok();
    }

    android::binder::Status getAllWheelStatus(
        std::vector<bool>* wheels) override {
        *wheels = wheels_.GetWheelStatus();
        return android::binder::Status::ok();
    }

    android::binder::Status getWheelCount(int32_t* count) override {
        *count = wheels_.GetWheelCount();
        return android::binder::Status::ok();
    }

    android::binder::Status setWheelStatus(int pin, bool on) override {
        wheels_.SetWheelStatus(pin, on);
        return android::binder::Status::ok();
    }

    android::binder::Status getWheelStatus(int pin, bool *on) override {
        *on = wheels_.IsWheelOn(pin);
        return android::binder::Status::ok();
    }

    android::binder::Status setAllWheels(bool on) override {
        wheels_.SetAllWheels(on);
        return android::binder::Status::ok();
    }

  private:
    Wheels wheels_;
};

class SmartCarDaemon final : public brillo::Daemon {
 public:
    SmartCarDaemon() = default;

 protected:
    int OnInit() override;

 private:
    brillo::BinderWatcher binder_watcher_;
    android::sp<SmartCarService> smartcar_service_;

    DISALLOW_COPY_AND_ASSIGN(SmartCarDaemon);
};

// SmartCarDaemon
int SmartCarDaemon::OnInit() {
    android::BinderWrapper::Create();
    if (!binder_watcher_.Init())
        return EX_OSERR;

    smartcar_service_ = new SmartCarService();
    android::BinderWrapper::Get()->RegisterService(
        smartcard::kBinderServiceName,
        smartcar_service_);
    return brillo::Daemon::OnInit();
}

}  // namespace smartcard


int main(int argc, char *argv[]) {
    base::CommandLine::Init(argc, argv);
    brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader);
    smartcard::SmartCarDaemon daemon;
    return daemon.Run();
}
