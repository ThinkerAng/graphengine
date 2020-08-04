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

#ifndef GE_GRAPH_LOAD_NEW_MODEL_MANAGER_TASK_INFO_MEMCPY_ADDR_ASYNC_TASK_INFO_H_
#define GE_GRAPH_LOAD_NEW_MODEL_MANAGER_TASK_INFO_MEMCPY_ADDR_ASYNC_TASK_INFO_H_
#include "graph/load/new_model_manager/task_info/task_info.h"

namespace ge {
class MemcpyAddrAsyncTaskInfo : public TaskInfo {
 public:
  MemcpyAddrAsyncTaskInfo() : dst_(nullptr), dst_max_(0), src_(nullptr), args_(nullptr), count_(0), kind_(0) {}

  ~MemcpyAddrAsyncTaskInfo() override {
    src_ = nullptr;
    dst_ = nullptr;

    if (args_ != nullptr) {
      rtError_t ret = rtFree(args_);
      if (ret != RT_ERROR_NONE) {
        GELOGE(RT_FAILED, "Call rt api failed, ret: 0x%X", ret);
      }
    }

    args_ = nullptr;
  }

  Status Init(const domi::TaskDef &task_def, DavinciModel *davinci_model) override;

  Status Distribute() override;

 private:
  Status GetUpdateBaseAddr(DavinciModel *davinci_model, uint64_t update_addr, uint64_t &base_addr);

  void *dst_;
  uint64_t dst_max_;
  void *src_;
  void *args_;
  uint64_t count_;
  uint32_t kind_;
};
}  // namespace ge
#endif  // GE_GRAPH_LOAD_NEW_MODEL_MANAGER_TASK_INFO_MEMCPY_ADDR_ASYNC_TASK_INFO_H_
