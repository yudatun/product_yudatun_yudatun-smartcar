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
#include <binderwrapper/binder_wrapper.h>
#include <brillo/binder_watcher.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>
#include <libweaved/service.h>

#include "action.h"
#include "binder_constants.h"
#include "binder_utils.h"
#include "yudatun/product/smartcar/ISmartCarService.h"

using android::String16;

namespace {
const char kSmartCarComponent[] = "smartcar";
const char kWheelComponentPrefix[] = "wheel";
const char kSmartCarTrait[] = "_smartcar";
const char kOnOffTrait[] = "onOff";
const char kWheelInfoTrait[] = "_wheelInfo";
}  // anonymous namespace

using smartcard::binder_utils::ToString;
using yudatun::product::smartcar::ISmartCarService;

class Daemon final : public brillo::Daemon {
 public:
    Daemon() = default;

 protected:
    int OnInit() override;

 private:
    void OnWeaveServiceConnected(const std::weak_ptr<weaved::Service>& service);
    void ConnectToSmartCarService();
    void OnSmartCarServiceDisconnected();
    void OnPairingInfoChanged(const weaved::Service::PairingInfo* pairing_info);
    void CreateSmartCarComponentsIfNeeded();

    void OnAction(std::unique_ptr<weaved::Command> command);
    void OnSetConfig(int pin, std::unique_ptr<weaved::Command> command);

    void UpdateDeviceState();

    void StartAction(const std::string& type, base::TimeDelta duration);
    void StopAction();

    // Device state variables.
    std::string status_{"idle"};

    std::weak_ptr<weaved::Service> weave_service_;

    // Smart Car Service interface.
    android::sp<ISmartCarService> smartcar_service_;

    // Current action
    std::unique_ptr<Action> action_;

    brillo::BinderWatcher binder_watcher_;
    std::unique_ptr<weaved::Service::Subscription> weave_service_subscription_;

    bool smartcar_components_added_{false};

    base::WeakPtrFactory<Daemon> weak_ptr_factory_{this};
    DISALLOW_COPY_AND_ASSIGN(Daemon);
};

int Daemon::OnInit() {
    int return_code = brillo::Daemon::OnInit();
    if (return_code != EX_OK)
        return return_code;

    android::BinderWrapper::Create();
    if (!binder_watcher_.Init())
        return EX_OSERR;

    weave_service_subscription_ = weaved::Service::Connect(
        brillo::MessageLoop::current(),
        base::Bind(&Daemon::OnWeaveServiceConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    ConnectToSmartCarService();

    LOG(INFO) << "Waiting for commands...";
    return EX_OK;
}

void Daemon::OnWeaveServiceConnected(
    const std::weak_ptr<weaved::Service>& service) {
    LOG(INFO) << "Daemon::OnWeaveServiceConnected";
    weave_service_ = service;
    auto weave_service = weave_service_.lock();
    if (!weave_service)
        return;

    weave_service->AddComponent(
        kSmartCarComponent, {kSmartCarTrait}, nullptr);
    weave_service->AddCommandHandler(
        kSmartCarComponent,
        kSmartCarTrait,
        "action",
        base::Bind(&Daemon::OnAction, weak_ptr_factory_.GetWeakPtr()));

    weave_service->SetPairingInfoListener(
        base::Bind(&Daemon::OnPairingInfoChanged,
                   weak_ptr_factory_.GetWeakPtr()));

    smartcar_components_added_ = false;
    CreateSmartCarComponentsIfNeeded();
    UpdateDeviceState();
}

void Daemon::ConnectToSmartCarService() {
    android::BinderWrapper* binder_wrapper = android::BinderWrapper::Get();
    auto binder = binder_wrapper->GetService(smartcard::kBinderServiceName);
    if (!binder.get()) {
        brillo::MessageLoop::current()->PostDelayedTask(
            base::Bind(&Daemon::ConnectToSmartCarService,
                       weak_ptr_factory_.GetWeakPtr()),
            base::TimeDelta::FromSeconds(1));
        return;
    }
    binder_wrapper->RegisterForDeathNotifications(
        binder,
        base::Bind(&Daemon::OnSmartCarServiceDisconnected,
                   weak_ptr_factory_.GetWeakPtr()));
    smartcar_service_ = android::interface_cast<ISmartCarService>(binder);
    CreateSmartCarComponentsIfNeeded();
    UpdateDeviceState();
}

void Daemon::CreateSmartCarComponentsIfNeeded() {
    if (smartcar_components_added_ || !smartcar_service_.get())
        return;

    auto weave_service = weave_service_.lock();
    if (!weave_service)
        return;

    std::vector<String16> wheelNames;
    std::vector<int> wheelPins;
    std::vector<bool> wheelStatus;

    if (!smartcar_service_->getAllWheelNames(&wheelNames).isOk())
        return;

    if (!smartcar_service_->getAllWheelPins(&wheelPins).isOk())
        return;

    if (!smartcar_service_->getAllWheelStatus(&wheelStatus).isOk())
        return;

    for (size_t index = 0; index < wheelNames.size(); ++index) {
        std::string wheelName = ToString(wheelNames[index]);
        std::string componentName =
                kWheelComponentPrefix + std::to_string(wheelPins[index]);
        if (weave_service->AddComponent(
                componentName, {kOnOffTrait, kWheelInfoTrait}, nullptr)) {
            weave_service->AddCommandHandler(
                componentName,
                kOnOffTrait,
                "setConfig",
                base::Bind(&Daemon::OnSetConfig,
                           weak_ptr_factory_.GetWeakPtr(),
                           wheelPins[index]));

            weave_service->SetStateProperty(
                componentName,
                kOnOffTrait,
                "state",
                *brillo::ToValue(wheelStatus[index] ? "on" : "off"),
                nullptr);

            weave_service->SetStateProperty(
                componentName,
                kWheelInfoTrait,
                "name",
                *brillo::ToValue(wheelName),
                nullptr);
        }
    }
    smartcar_components_added_ = true;
}

void Daemon::OnSmartCarServiceDisconnected() {
    action_.reset();
    smartcar_service_ = nullptr;
    ConnectToSmartCarService();
}

void Daemon::OnSetConfig(
    int pin, std::unique_ptr<weaved::Command> command) {
    if (!smartcar_service_.get()) {
        command->Abort("_system_error", "smartcar service unavailable", nullptr);
        return;
    }
    auto state = command->GetParameter<std::string>("state");
    bool on = (state == "on");
    android::binder::Status status = smartcar_service_->setWheelStatus(pin, on);
    if (!status.isOk()) {
        command->AbortWithCustomError(status, nullptr);
        return;
    }

    if (action_) {
        action_.reset();
        status_ = "idle";
        UpdateDeviceState();
    }

    auto weave_service = weave_service_.lock();
    if (weave_service) {
        std::string componentName =
            kWheelComponentPrefix + std::to_string(pin);
        weave_service->SetStateProperty(
            componentName,
            kOnOffTrait,
            "state",
            *brillo::ToValue(on ? "on" : "off"),
            nullptr);
    }
    command->Complete({}, nullptr);
}

void Daemon::OnAction(std::unique_ptr<weaved::Command> command) {
    if (!smartcar_service_.get()) {
        command->Abort("_system_error", "smartcar service unavailable", nullptr);
        return;
    }
    double duration = command->GetParameter<double>("duration");
    if (duration <= 0.0) {
        command->Abort("_invalid_parameter", "Invalid parameter value", nullptr);
        return;
    }
    std::string type = command->GetParameter<std::string>("type");
    StartAction(type, base::TimeDelta::FromSecondsD(duration));
    command->Complete({}, nullptr);
}

void Daemon::StartAction(const std::string& type, base::TimeDelta duration) {
    action_ = Action::Create(smartcar_service_, type, duration);
    if (action_) {
        status_ = "moving";
        action_->Start();
    } else {
        status_ = "idle";
    }
    UpdateDeviceState();
}

void Daemon::StopAction() {
    if (!action_) {
        return;
    }

    action_.reset();
    status_ = "idle";
    UpdateDeviceState();
}

void Daemon::OnPairingInfoChanged(
    const weaved::Service::PairingInfo* pairing_info) {
    LOG(INFO) << "Daemon::OnPairingInfoChanged: " << pairing_info;
    if (!pairing_info) {
        return StopAction();
    }
}

void Daemon::UpdateDeviceState() {
    auto weave_service = weave_service_.lock();
    if (!weave_service)
        return;

    weave_service->SetStateProperty(
        kSmartCarComponent,
        kSmartCarTrait,
        "status",
        *brillo::ToValue(status_),
        nullptr);
}

int main(int argc, char *argv[]) {
    base::CommandLine::Init(argc, argv);
    brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader);
    Daemon daemon;
    return daemon.Run();
}
