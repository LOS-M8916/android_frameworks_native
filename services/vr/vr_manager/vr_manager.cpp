/*
 * Copyright (C) 2017 The Android Open Source Project
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

#define LOG_TAG "VrManager"
#include <utils/Log.h>

#include <vr/vr_manager/vr_manager.h>
#include <stdint.h>
#include <sys/types.h>
#include <binder/Parcel.h>

namespace android {

// Must be kept in sync with interface defined in IVrStateCallbacks.aidl.

class BpVrStateCallbacks : public BpInterface<IVrStateCallbacks> {
 public:
  explicit BpVrStateCallbacks(const sp<IBinder>& impl)
      : BpInterface<IVrStateCallbacks>(impl) {}

  void onVrStateChanged(bool enabled) {
    Parcel data, reply;
    data.writeInterfaceToken(IVrStateCallbacks::getInterfaceDescriptor());
    data.writeBool(enabled);
    remote()->transact(ON_VR_STATE_CHANGED, data, &reply, IBinder::FLAG_ONEWAY);
  }
};

IMPLEMENT_META_INTERFACE(VrStateCallbacks, "android.service.vr.IVrStateCallbacks");

status_t BnVrStateCallbacks::onTransact(uint32_t code, const Parcel& data,
                                        Parcel* reply, uint32_t flags) {
  switch(code) {
    case ON_VR_STATE_CHANGED: {
      CHECK_INTERFACE(IVrStateCallbacks, data, reply);
      onVrStateChanged(data.readBool());
      return OK;
    }
  }
  return BBinder::onTransact(code, data, reply, flags);
}

// Must be kept in sync with interface defined in IVrManager.aidl.

class BpVrManager : public BpInterface<IVrManager> {
 public:
  explicit BpVrManager(const sp<IBinder>& impl)
      : BpInterface<IVrManager>(impl) {}

  void registerListener(const sp<IVrStateCallbacks>& cb) override {
    Parcel data;
    data.writeInterfaceToken(IVrManager::getInterfaceDescriptor());
    data.writeStrongBinder(IInterface::asBinder(cb));
    remote()->transact(REGISTER_LISTENER, data, NULL);
  }

  void unregisterListener(const sp<IVrStateCallbacks>& cb) override {
    Parcel data;
    data.writeInterfaceToken(IVrManager::getInterfaceDescriptor());
    data.writeStrongBinder(IInterface::asBinder(cb));
    remote()->transact(UNREGISTER_LISTENER, data, NULL);
  }

  bool getVrModeState() override {
    Parcel data, reply;
    data.writeInterfaceToken(IVrManager::getInterfaceDescriptor());
    remote()->transact(GET_VR_MODE_STATE, data, &reply);
    int32_t ret = reply.readExceptionCode();
    if (ret != 0) {
      return false;
    }
    return reply.readBool();
  }
};

IMPLEMENT_META_INTERFACE(VrManager, "android.service.vr.IVrManager");

class BpVrDisplayStateService : public BpInterface<IVrDisplayStateService> {
 public:
  explicit BpVrDisplayStateService(const sp<IBinder>& impl)
      : BpInterface<IVrDisplayStateService>(impl) {}

  void displayAvailable(bool available) {
    Parcel data, reply;
    data.writeInterfaceToken(IVrDisplayStateService::getInterfaceDescriptor());
    data.writeBool(available);
    remote()->transact(static_cast<uint32_t>(
                           VrDisplayStateTransaction::ON_DISPLAY_STATE_CHANGED),
                       data, &reply);
  }
};

status_t BnVrDisplayStateService::onTransact(uint32_t code, const Parcel& data,
                                             Parcel* reply, uint32_t flags) {
  switch (static_cast<VrDisplayStateTransaction>(code)) {
    case VrDisplayStateTransaction::ON_DISPLAY_STATE_CHANGED:
      CHECK_INTERFACE(IVrDisplayStateService, data, reply);
      displayAvailable(data.readBool());
      return OK;
  }
  return BBinder::onTransact(code, data, reply, flags);
}

IMPLEMENT_META_INTERFACE(VrDisplayStateService,
                         "android.service.vr.IVrDisplayStateService");

}  // namespace android
