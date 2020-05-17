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

#include "if_label_maker.h"

#include "common/util.h"
#include "common/ge_inner_error_codes.h"
#include "framework/common/types.h"
#include "framework/common/op/ge_op_utils.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/utils/graph_utils.h"

using domi::_IF;
using domi::IF;
using domi::STATELESSIF;

namespace ge {
constexpr uint8_t kIfPredIndex = 0;
constexpr uint8_t kThenBranchIndex = 0;
constexpr uint8_t kElseBranchIndex = 1;

// All ---> Node ---> If ---> Node --->
//                    |
//                    V
//  { Data ---> Node ---> Node ---> NetOutput }
//
// All ---> Node ---> If ---> Node --->
//             \              /
//            { Node ---> Node }

/**
 * @ingroup ge
 * @brief Make label node to functional call.
 * @param [in/out] label_index: serial id for whole graph.
 * @return: 0 for success / others for fail
 */
Status IfOpLabelMaker::Run(uint32_t &label_index) {
  GE_CHECK_NOTNULL(parent_node_);
  GE_CHECK_NOTNULL(parent_graph_);

  OpDescPtr if_desc = parent_node_->GetOpDesc();
  GE_CHECK_NOTNULL(if_desc);

  const std::string then_branch_name = if_desc->GetSubgraphInstanceName(kThenBranchIndex);
  const std::string else_branch_name = if_desc->GetSubgraphInstanceName(kElseBranchIndex);
  if (then_branch_name.empty() || else_branch_name.empty()) {
    GELOGE(INTERNAL_ERROR, "Node: %s has invalid subgraph, then branch: %s, else branch: %s.",
           if_desc->GetName().c_str(), then_branch_name.c_str(), else_branch_name.c_str());
    return FAILED;
  }

  ComputeGraphPtr then_sub_graph = parent_graph_->GetSubgraph(then_branch_name);
  ComputeGraphPtr else_sub_graph = parent_graph_->GetSubgraph(else_branch_name);
  GE_CHECK_NOTNULL(then_sub_graph);
  GE_CHECK_NOTNULL(else_sub_graph);

  const uint32_t then_enter_index = label_index++;
  const uint32_t else_enter_index = label_index++;
  const uint32_t else_leave_index = label_index++;
  const std::string then_enter_name = parent_node_->GetName() + "/LabelSwitch";    // rtLabelSwitchByIndex
  const std::string then_label_name = parent_node_->GetName() + "/ThenLabelSet";   // rtLabelSet(0)
  const std::string then_leave_name = parent_node_->GetName() + "/LabelGoto";      // rtLabelGoto
  const std::string else_enter_name = parent_node_->GetName() + "/ElseLabelSet";   // rtLabelSet(1)
  const std::string else_leave_name = parent_node_->GetName() + "/LeaveLabelSet";  // rtLabelSet

  if (AddLabelSetEnter(then_sub_graph, then_label_name, then_enter_index) == nullptr) {
    GELOGE(INTERNAL_ERROR, "Subgraph: %s add label set failed.", then_sub_graph->GetName().c_str());
    return FAILED;
  }

  if (AddLabelGotoLeave(then_sub_graph, then_leave_name, else_leave_index) == nullptr) {
    GELOGE(INTERNAL_ERROR, "Subgraph: %s add label goto failed.", then_sub_graph->GetName().c_str());
    return FAILED;
  }

  if (AddLabelSetEnter(else_sub_graph, else_enter_name, else_enter_index) == nullptr) {
    GELOGE(INTERNAL_ERROR, "Subgraph: %s add label set failed.", else_sub_graph->GetName().c_str());
    return FAILED;
  }
  if (AddLabelSetLeave(else_sub_graph, else_leave_name, else_leave_index) == nullptr) {
    GELOGE(INTERNAL_ERROR, "Subgraph: %s add label set failed.", else_sub_graph->GetName().c_str());
    return FAILED;
  }

  // false ==> 0 ==> switch_labels[0] ==> else_enter_index
  // true  ==> 1 ==> switch_labels[1] ==> then_enter_index
  const std::vector<uint32_t> switch_labels = {else_enter_index, then_enter_index};

  GeTensorDesc pred_desc = if_desc->GetInputDesc(kIfPredIndex);
  GeTensorDesc cond_desc(GeShape(pred_desc.GetShape().GetDims()), pred_desc.GetFormat(), DT_UINT32);
  NodePtr switch_node = AddLabelSwitchEnter(then_sub_graph, then_enter_name, cond_desc, switch_labels);
  if (switch_node == nullptr) {
    GELOGE(INTERNAL_ERROR, "Subgraph: %s add label switch failed.", then_sub_graph->GetName().c_str());
    return FAILED;
  }

  uint32_t parent_index = 0;  // If cond input is first.
  const std::string data_name = parent_node_->GetName() + "/SwitchIndexData";
  if (AddLabelSwitchIndex(then_sub_graph, data_name, cond_desc, switch_node, parent_index) == nullptr) {
    GELOGE(INTERNAL_ERROR, "Subgraph: %s add switch input failed.", then_sub_graph->GetName().c_str());
    return FAILED;
  }

  GELOGI("Node: %s assign label success.", if_desc->GetName().c_str());
  return SUCCESS;
}

REGISTER_LABEL_MAKER(IF, IfOpLabelMaker);
REGISTER_LABEL_MAKER(_IF, IfOpLabelMaker);
REGISTER_LABEL_MAKER(STATELESSIF, IfOpLabelMaker);
}  // namespace ge
