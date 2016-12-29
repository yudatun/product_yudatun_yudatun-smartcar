/*
 * Copyright (C) 2016 The Yudatun Open Source Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */

#include <sysexits.h>

#include <base/bind.h>
#include <base/command_line.h>
#include <binderwrapper/binder_wrapper.h>
#include <brillo/binder_watcher.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>

#include "action.h"
#include "binder_constants.h"
#include "binder_utils.h"
#include "configs.h"
#include "yudatun/product/smartcar/ISmartCarService.h"

#include "MQTTClient.h"

using android::String16;

using smartcard::binder_utils::ToString;
using yudatun::product::smartcar::ISmartCarService;

class Daemon final : public brillo::Daemon {
 public:
    Daemon(smartcar::Configs configs)
       : configs_{std::move(configs)} {}

 protected:
    int OnInit() override;

 private:
    void MQTTSubscribe(void);
    int  OnMQTTServiceConnected(void *context, char *topicName, int topicLen, MQTTClient_message *message);
    void OnMQTTServiceLost(void *context, char *cause);

    void ConnectToSmartCarService();
    void OnSmartCarServiceDisconnected();

    void CreateSmartCarComponentsIfNeeded();

    void UpdateDeviceState();

    smartcar::Configs configs_;

    // Device state variables.
    std::string status_{"idle"};

    // Smart Car Service interface.
    android::sp<ISmartCarService> smartcar_service_;

    // Current action
    std::unique_ptr<Action> action_;

    brillo::BinderWatcher binder_watcher_;

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

    MQTTSubcribe();

    ConnectToSmartCarService();

    LOG(INFO) << "Waiting for commands...";
    return EX_OK;
}

void Daemon::MQTTSubscribe(void) {
    MQTTClient client;

}

int Daemon::OnMQTTServiceConntected(
    void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    LOG(INFO) << "Message arrived";

    return 1;
}

void Daemon::OnMQTTServiceLost(void *context, char *cause) {
    LOG(INFO) << "Connection lost";
    LOG(INFO) << "    cause: " << cause;
}

void Daemon::CreateSmartCarComponentsIfNeeded() {
    if (smartcar_components_added_ || !smartcar_service_.get())
        return;

    smartcar_components_added_ = true;
}

void Daemon::OnSmartCarServiceDisconnected() {
    LOG(INFO) << "Daemon::OnSmartCarServiceDisconnected";

    action_.reset();
    smartcar_service_ = nullptr;
    ConnectToSmartCarService();
}

namespace {
const char kDefaultConfigFilePath[] = "/etc/smartcar/config.json";
}

int main(int argc, char *argv[]) {
    DEFINE_bool(log_to_stderr, false, "log trace messages to stderr as well");
    DEFINE_string(config_path, "",
                  "Path to file containing config information");

    DEFINE_string(client_id, "SmartCarSubscriber", "containing client id");
    DEFINE_string(topic, "Smartcar", "containing topic name");
    DEFINE_string(host, "localhost", "containing broker address");
    DEFINE_string(port, "1183", "containing broker port");
    DEFINE_int(qos, 0, "containing qos");

    brillo::FlagHelper::Init(argc, argv, "MQTT protocol example daemon");
    brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader);

    smartcar::Configs configs;

    configs.client_id_ = FLAGS_client_id;
    configs.topic_     = FLAGS_topic;
    configs.host_      = FLAGS_host;
    configs.port_      = FLAGS_port;
    configs.qos_       = FLAGS_qos;

    configs.connect_options_ = MQTTClient_connectOptions_initializer;

    base::FilePath default_file_path{kDefaultConfigFilePath};
    if (!FLAGS_config_path.empty()) {
        // In tests, we'll override the board specific and default
        // configurations with a test specific configuration
        smartcar::LoadConfigsFromFile(base::FilePath{FLAGS_config_path}, &options);
    }

    Daemon daemon{options};
    return daemon.Run();
}
