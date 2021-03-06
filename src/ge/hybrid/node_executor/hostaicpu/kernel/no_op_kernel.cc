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

#include "hybrid/node_executor/hostaicpu/kernel/no_op_kernel.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/util.h"
#include "hybrid/node_executor/hostaicpu/kernel_factory.h"

namespace ge {
namespace hybrid {
namespace host_aicpu {
Status NoOpKernel::Compute(TaskContext& context) {
  GELOGI("NoOpKernel [%s, %s] no need to compute.", node_->GetName().c_str(), node_->GetType().c_str());
  return SUCCESS;
}

REGISTER_KERNEL_CREATOR(NoOp, NoOpKernel);
}  // namespace host_aicpu
}  // namespace hybrid
}  // namespace ge
