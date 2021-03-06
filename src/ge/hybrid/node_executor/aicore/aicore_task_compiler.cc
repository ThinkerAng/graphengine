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

#include "aicore_task_compiler.h"
#include "framework/common/debug/log.h"
#include "graph/debug/ge_attr_define.h"

namespace ge {
namespace hybrid {
namespace {
uintptr_t kWeightBase = 0x10000000;
uintptr_t kMemBase = 0x20000000;
uint64_t kFakeSize = 0x10000000UL;
}  // namespace
std::mutex AiCoreTaskCompiler::mu_;

AiCoreTaskCompiler::AiCoreTaskCompiler(OpsKernelInfoStorePtr aic_kernel_store)
    : aic_kernel_store_(std::move(aic_kernel_store)) {}

Status AiCoreTaskCompiler::DoCompileOp(OpsKernelInfoStore &ops_store, const NodePtr &node) {
  GE_CHECK_NOTNULL(node);
  vector<NodePtr> node_vec;
  node_vec.emplace_back(node);
  GE_CHK_STATUS_RET(ops_store.CompileOpRun(node_vec), "Failed to execute CompileOp, node = %s",
                    node->GetName().c_str());
  GE_CHK_STATUS_RET(ops_store.CalcOpRunningParam(*node), "Failed to execute CalcOpRunningParam, node = %s",
                    node->GetName().c_str());
  return SUCCESS;
}

Status AiCoreTaskCompiler::CompileOp(const NodePtr &node, std::vector<domi::TaskDef> &tasks) const {
  GE_CHECK_NOTNULL(node);
  GELOGI("AiCoreTaskCompiler(%s) CompileOp Start.", node->GetName().c_str());
  GE_CHECK_NOTNULL(aic_kernel_store_);

  GE_CHK_STATUS_RET_NOLOG(DoCompileOp(*aic_kernel_store_, node));
  GELOGD("successfully compiled op: %s", node->GetName().c_str());

  auto op_desc = node->GetOpDesc();
  std::vector<int64_t> input_offsets(op_desc->GetInputsSize(), kMemBase);
  std::vector<int64_t> output_offsets(op_desc->GetOutputsSize(), kMemBase);
  op_desc->SetInputOffset(input_offsets);
  op_desc->SetOutputOffset(output_offsets);
  std::vector<int64_t> workspaces(op_desc->GetWorkspaceBytes().size(), kMemBase);
  op_desc->SetWorkspace(std::move(workspaces));
  GE_CHK_STATUS_RET_NOLOG(DoGenerateTask(*aic_kernel_store_, *node, tasks));
  GELOGD("successfully generated task: %s", node->GetName().c_str());
  GELOGI("AiCoreTaskCompiler(%s) CompileOp End.", node->GetName().c_str());
  return SUCCESS;
}

Status AiCoreTaskCompiler::DoGenerateTask(OpsKernelInfoStore &store, const Node &node,
                                          std::vector<domi::TaskDef> &tasks) {
  rtModel_t rt_model_ = nullptr;
  GE_CHK_RT_RET(rtModelCreate(&rt_model_, 0));
  rtStream_t stream = nullptr;
  GE_CHK_RT_EXEC(rtStreamCreate(&stream, 0), GE_CHK_RT(rtModelDestroy(rt_model_)); return RT_FAILED);
  GE_MAKE_GUARD_RTSTREAM(stream);
  GE_CHK_RT_EXEC(rtModelBindStream(rt_model_, stream, 0), GE_CHK_RT(rtModelDestroy(rt_model_)); return RT_FAILED);

  RunContext context;
  context.stream = stream;
  context.model = rt_model_;
  context.graphStreamList.emplace_back(stream);
  context.weightMemBase = reinterpret_cast<uint8_t *>(kWeightBase);
  context.dataMemBase = reinterpret_cast<uint8_t *>(kWeightBase);
  context.weightMemSize = kFakeSize;
  context.dataMemSize = kFakeSize;

  Status ret;
  {
    std::lock_guard<std::mutex> lk(mu_);
    ret = store.GenerateTask(node, context, tasks);
  }

  GE_CHK_STATUS(ret, "Failed to execute GenerateTask, node = %s", node.GetName().c_str());
  GE_CHK_RT(rtModelUnbindStream(rt_model_, stream));
  GE_CHK_RT(rtModelDestroy(rt_model_));
  return ret;
}
}  // namespace hybrid
}  // namespace ge
