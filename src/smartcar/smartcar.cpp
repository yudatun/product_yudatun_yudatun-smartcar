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

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sysexits.h>

#include <base/bind.h>
#include <base/command_line.h>
#include <binderwrapper/binder_wrapper.h>
#include <brillo/binder_watcher.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>
#include <libweaved/service.h>
#include <cutils/fs.h>
#include <cutils/sockets.h>

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
const int  kDefaultSmartCarPort = 8888;
const int  kBufferMax = 1024;
}  // anonymous namespace

using smartcard::binder_utils::ToString;
using yudatun::product::smartcar::ISmartCarService;

class Daemon final : public brillo::Daemon {
 public:
    Daemon() = default;

 protected:
    int OnInit() override;

 private:
    int Readx(int s, void *_buf, int count);
    int Writex(int s, const void *_buf, int count);

    static void* ServerSocketThread(void* data);
    void ServerSocketThreadLoop();
    void LocalInit();

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

    pthread_t server_socket_thread_;

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

    LocalInit();

    weave_service_subscription_ = weaved::Service::Connect(
        brillo::MessageLoop::current(),
        base::Bind(&Daemon::OnWeaveServiceConnected,
                   weak_ptr_factory_.GetWeakPtr()));

    ConnectToSmartCarService();

    LOG(INFO) << "Waiting for commands...";
    return EX_OK;
}

int Daemon::Readx(int s, void *_buf, int count) {
    char *buf = (char *) _buf;
    int n = 0, r;
    if (count < 0) return -1;
    while (n < count) {
        r = read(s, buf + n, count - n);
        if (r < 0) {
            if (errno == EINTR) continue;
            LOG(ERROR) << "read error: " << strerror(errno);
            return -1;
        }
        if (r == 0) {
            LOG(ERROR) << "eof";
            return -1; /* EOF */
        }
        n += r;
    }
    return 0;
}

int Daemon::Writex(int s, const void *_buf, int count)
{
    const char *buf = (const char *) _buf;
    int n = 0, r;
    if (count < 0) return -1;
    while (n < count) {
        r = write(s, buf + n, count - n);
        if (r < 0) {
            if (errno == EINTR) continue;
            LOG(ERROR) << "Write error: " << strerror(errno);
            return -1;
        }
        n += r;
    }
    return 0;
}

void Daemon::LocalInit() {
    pthread_create(&server_socket_thread_, nullptr, ServerSocketThread, this);
}

void* Daemon::ServerSocketThread(void* data) {
    reinterpret_cast<Daemon*>(data)->ServerSocketThreadLoop();
    return nullptr;
}

void Daemon::ServerSocketThreadLoop() {
    int serverfd, fd;
    sockaddr_storage ss;
    sockaddr *addrp = reinterpret_cast<sockaddr*>(&ss);
    socklen_t alen;
    char buf[kBufferMax];

    serverfd = -1;
    for (;;) {
        if (serverfd < 0) {
            serverfd = socket_inaddr_any_server(kDefaultSmartCarPort, SOCK_STREAM);
            if (serverfd < 0) {
                LOG(ERROR) << "Cannot bind socket yet: " << strerror(errno);
                usleep(1000 * 1000); // 1s
                continue;
            }
            fcntl(serverfd, F_SETFD, FD_CLOEXEC);
        }

        alen = sizeof (ss);
        fd = TEMP_FAILURE_RETRY(accept(serverfd, addrp, &alen));
        if (fd < 0) {
            LOG(ERROR) << "Accept failed: " << strerror(errno);
            continue;
        }
        fcntl(fd, F_SETFD, FD_CLOEXEC);

        for (;;) {
            LOG(INFO) << "New connection on fd: " << fd;

            unsigned short count;
            if (Readx(fd, &count, sizeof (count))) {
                LOG(ERROR) << "Failed to read size";
                break;
            }
            if ((count < 1) || (count >= kBufferMax)) {
                LOG(ERROR) << "Invalid size " << count;
                break;
            }
            if (Readx(fd, buf, count)) {
                LOG(ERROR) << "Failed to read command";
                break;
            }
            buf[count] = 0;
            StartAction(std::string(buf), base::TimeDelta::FromSecondsD(0));
        }
        LOG(INFO) << "Closing connection";
        close(fd);
    }
    LOG(INFO) << "Server Socket Thread exiting";
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
