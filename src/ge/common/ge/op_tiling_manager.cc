/**
 * Copyright 2019-2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/ge/op_tiling_manager.h"
#include "framework/common/debug/log.h"
#include <string>

namespace {
const char *const kEnvName = "ASCEND_OPP_PATH";
const std::string kDefaultPath = "/usr/local/Ascend/opp";
const std::string kDefaultBuiltInTilingPath = "/op_impl/built-in/liboptiling.so";
const std::string kDefaultCustomTilingPath = "/op_impl/custom/liboptiling.so";
const uint8_t kPrefixIndex = 9;
}  // namespace

namespace ge {
void OpTilingManager::ClearHandles() noexcept {
  for (const auto &handle : handles_) {
    if (dlclose(handle.second) != 0) {
      GELOGE(FAILED, "Failed to close handle of %s: %s", handle.first.c_str(), dlerror());
    }
  }
  handles_.clear();
}

OpTilingManager::~OpTilingManager() { ClearHandles(); }

std::string OpTilingManager::GetPath() {
  const char *opp_path_env = std::getenv(kEnvName);
  std::string opp_path = kDefaultPath;
  if (opp_path_env != nullptr) {
    char resolved_path[PATH_MAX];
    if (realpath(opp_path_env, resolved_path) == NULL) {
      GELOGE(PARAM_INVALID, "Failed load tiling lib as env 'ASCEND_OPP_PATH'(%s) is invalid path.", opp_path_env);
      return std::string();
    }
    opp_path = resolved_path;
  }
  return opp_path;
}

void OpTilingManager::LoadSo() {
  std::string opp_path = GetPath();
  if (opp_path.empty()) {
    GELOGW("Skip load tiling lib.");
    return;
  }
  std::string built_in_tiling_lib = opp_path + kDefaultBuiltInTilingPath;
  std::string custom_tiling_lib = opp_path + kDefaultCustomTilingPath;
  std::string built_in_name = kDefaultBuiltInTilingPath.substr(kPrefixIndex);
  std::string custom_name = kDefaultCustomTilingPath.substr(kPrefixIndex);

  void *handle_bi = dlopen(built_in_tiling_lib.c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (handle_bi == nullptr) {
    GELOGW("Failed to dlopen %s!", dlerror());
  } else {
    handles_[built_in_name] = handle_bi;
  }

  void *handle_ct = dlopen(custom_tiling_lib.c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (handle_ct == nullptr) {
    GELOGW("Failed to dlopen %s!", dlerror());
  } else {
    handles_[custom_name] = handle_ct;
  }
}

}  // namespace ge
