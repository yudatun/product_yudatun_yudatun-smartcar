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

#ifndef SRC_SMARTCAR_ACTION_H_
#define SRC_SMARTCAR_ACTION_H_

#include <vector>
#include <memory>

#include <base/time/time.h>
#include <base/memory/weak_ptr.h>

#include "yudatun/product/smartcar/ISmartCarService.h"

class Action {
 public:
    Action(android::sp<yudatun::product::smartcar::ISmartCarService> smartcar_service,
            const base::TimeDelta& duration);
    virtual ~Action();

    void Start();
    void Stop();

    static std::unique_ptr<Action> Create(
        android::sp<yudatun::product::smartcar::ISmartCarService> smartcar_service,
        const std::string& type,
        const base::TimeDelta& duration);

 protected:
    virtual void DoAction() = 0;

    bool GetWheel(int pin) const;
    void SetWheel(int pin, bool on);
    void SetAllWheels(bool on);

 private:
    android::sp<yudatun::product::smartcar::ISmartCarService> smartcar_service_;
    base::TimeDelta duration_;

    base::WeakPtrFactory<Action> weak_ptr_factory_{this};
    DISALLOW_COPY_AND_ASSIGN(Action);
};

#endif  // SRC_SMARTCAR_ACTION_H_
