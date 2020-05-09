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

#include "graph/label/label_maker.h"

#include "common/util.h"
#include "common/ge_inner_error_codes.h"
#include "framework/common/types.h"
#include "framework/common/op/ge_op_utils.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/utils/graph_utils.h"

namespace ge {
constexpr static int64_t kInvalidStreamId = -1;

/**
 * @ingroup ge
 * @brief Set stream id for head node.
 * @param [in] graph: graph for add node.
 * @param [in] op_desc: OpDesc for set logical stream id.
 * @return: void
 */
void LabelMaker::SetStreamIdEnter(const ComputeGraphPtr &graph, const OpDescPtr &op_desc) {
  int64_t stream_id = kInvalidStreamId;
  const auto &node_list = graph->GetDirectNode();
  for (size_t i = 0; i < node_list.size(); ++i) {
    const auto &node = node_list.at(i);
    GE_CHECK_NOTNULL_EXEC(node, continue);

    stream_id = node->GetOpDesc()->GetStreamId();
    if (stream_id != kInvalidStreamId) {
      break;
    }
  }

  GELOGI("SetStreamId: Node %s assign stream is %ld.", op_desc->GetName().c_str(), stream_id);
  op_desc->SetStreamId(stream_id);
}

/**
 * @ingroup ge
 * @brief Set stream id for tail node.
 * @param [in] graph: graph for add node.
 * @param [in] op_desc: OpDesc for set logical stream id.
 * @return: void
 */
void LabelMaker::SetStreamIdLeave(const ComputeGraphPtr &graph, const OpDescPtr &op_desc) {
  int64_t stream_id = kInvalidStreamId;
  const auto &node_list = graph->GetDirectNode();
  for (size_t i = node_list.size(); i > 0; --i) {
    const auto &node = node_list.at(i - 1);  // i from list size, need shift 1.
    GE_CHECK_NOTNULL_EXEC(node, continue);

    stream_id = node->GetOpDesc()->GetStreamId();
    if (stream_id != kInvalidStreamId) {
      break;
    }
  }

  GELOGI("SetStreamId: Node %s assign stream is %ld.", op_desc->GetName().c_str(), stream_id);
  op_desc->SetStreamId(stream_id);
}

/**
 * @ingroup ge
 * @brief Link Node to Graph head.
 * @param [in] graph: graph for add node.
 * @param [in] lb_node: Node for set link to head.
 * @return: SUCCESS / FAILED
 */
Status LabelMaker::AddCtrlLink2Data(const ComputeGraphPtr &graph, const NodePtr &node) {
  GE_CHECK_NOTNULL(graph);
  GE_CHECK_NOTNULL(node);

  std::set<NodePtr> linked_nodes;
  for (const NodePtr &n : graph->GetDirectNode()) {
    GE_CHECK_NOTNULL(n);
    if (n->GetType() != DATA) {
      continue;
    }

    // Link control edge to graph head.
    for (const NodePtr &out_node : n->GetOutAllNodes()) {
      if (linked_nodes.count(out_node) > 0) {
        continue;
      }

      (void)linked_nodes.insert(out_node);
      if (GraphUtils::AddEdge(node->GetOutControlAnchor(), out_node->GetInControlAnchor()) != SUCCESS) {
        GELOGE(INTERNAL_ERROR, "LabelSet: Add ctrl edge to %s failed.", node->GetName().c_str());
        return FAILED;
      }
    }
  }

  return SUCCESS;
}

/**
 * @ingroup ge
 * @brief Add LabelSet node at graph front.
 * @param [in] graph: graph for add node.
 * @param [in] name: label set node name.
 * @param [in] index: label id for set.
 * @return: NodePtr for success / nullptr for fail
 */
NodePtr LabelMaker::AddLabelSetEnter(const ComputeGraphPtr &graph, const std::string &name, uint32_t index) {
  GE_CHECK_NOTNULL_EXEC(graph, return nullptr);

  const auto &node_list = graph->GetDirectNode();
  auto it = node_list.begin();
  if (it == node_list.end()) {
    GELOGE(INTERNAL_ERROR, "LabelSet: Graph %s node is empty.", graph->GetName().c_str());
    return nullptr;
  }

  OpDescPtr op_desc = MakeShared<OpDesc>(name, LABELSET);
  GE_CHECK_NOTNULL_EXEC(op_desc, return nullptr);
  SetStreamIdEnter(graph, op_desc);

  GELOGI("LabelSet: Create node %s.", op_desc->GetName().c_str());
  (void)AttrUtils::SetInt(op_desc, ATTR_NAME_LABEL_SWITCH_INDEX, index);
  NodePtr label_set = graph->AddNodeFront(op_desc);
  GE_CHECK_NOTNULL_EXEC(label_set, return nullptr);

  // Link control edge to graph head.
  if (AddCtrlLink2Data(graph, label_set) != SUCCESS) {
    GELOGE(INTERNAL_ERROR, "LabelSet: Add ctrl edge to %s failed.", graph->GetName().c_str());
    return nullptr;
  }

  return label_set;
}

/**
 * @ingroup ge
 * @brief Add LabelSet node at graph back.
 * @param [in] graph: graph for add node.
 * @param [in] name: label set node name.
 * @param [in] index: label id for set.
 * @return: NodePtr for success / nullptr for fail
 */
NodePtr LabelMaker::AddLabelSetLeave(const ComputeGraphPtr &graph, const std::string &name, uint32_t index) {
  GE_CHECK_NOTNULL_EXEC(graph, return nullptr);

  const auto &node_list = graph->GetDirectNode();
  auto it = node_list.end();
  if (it == node_list.begin()) {
    GELOGE(INTERNAL_ERROR, "LabelSet: Graph %s node is empty.", graph->GetName().c_str());
    return nullptr;
  }
  --it;
  const NodePtr &node = *it;
  GE_CHECK_NOTNULL_EXEC(node, return nullptr);

  OpDescPtr op_desc = MakeShared<OpDesc>(name, LABELSET);
  GE_CHECK_NOTNULL_EXEC(op_desc, return nullptr);
  SetStreamIdLeave(graph, op_desc);

  GELOGI("LabelSet: Create node %s.", op_desc->GetName().c_str());
  (void)AttrUtils::SetInt(op_desc, ATTR_NAME_LABEL_SWITCH_INDEX, index);
  NodePtr label_set = graph->AddNode(op_desc);
  GE_CHECK_NOTNULL_EXEC(label_set, return nullptr);

  // Link control edge to graph tail.
  if (GraphUtils::AddEdge(node->GetOutControlAnchor(), label_set->GetInControlAnchor()) != SUCCESS) {
    GELOGE(INTERNAL_ERROR, "LabelSet: Add ctrl edge to %s failed.", node->GetName().c_str());
    return nullptr;
  }

  return label_set;
}

/**
 * @ingroup ge
 * @brief Add LabelGoto node at graph front.
 * @param [in] graph: graph for add node.
 * @param [in] name: label goto node name.
 * @param [in] index: label id for goto.
 * @return: NodePtr for success / nullptr for fail
 */
NodePtr LabelMaker::AddLabelGotoEnter(const ComputeGraphPtr &graph, const std::string &name, uint32_t index) {
  GE_CHECK_NOTNULL_EXEC(graph, return nullptr);

  const auto &node_list = graph->GetDirectNode();
  auto it = node_list.begin();
  if (it == node_list.end()) {
    GELOGE(INTERNAL_ERROR, "LabelGoto: Graph %s node is empty.", graph->GetName().c_str());
    return nullptr;
  }

  OpDescPtr op_desc = MakeShared<OpDesc>(name, LABELGOTOEX);
  GE_CHECK_NOTNULL_EXEC(op_desc, return nullptr);
  SetStreamIdEnter(graph, op_desc);

  GELOGI("LabelGoto: Create node %s.", op_desc->GetName().c_str());
  (void)AttrUtils::SetInt(op_desc, ATTR_NAME_LABEL_SWITCH_INDEX, index);
  NodePtr label_goto = graph->AddNodeFront(op_desc);
  if (label_goto == nullptr) {
    GELOGE(INTERNAL_ERROR, "LabelGoto: Add to graph %s failed.", graph->GetName().c_str());
    return nullptr;
  }

  return label_goto;
}

/**
 * @ingroup ge
 * @brief Add LabelGoto node at graph back.
 * @param [in] graph: graph for add node.
 * @param [in] name: label goto node name.
 * @param [in] index: label id for goto.
 * @return: NodePtr for success / nullptr for fail
 */
NodePtr LabelMaker::AddLabelGotoLeave(const ComputeGraphPtr &graph, const std::string &name, uint32_t index) {
  GE_CHECK_NOTNULL_EXEC(graph, return nullptr);

  const auto &node_list = graph->GetDirectNode();
  auto it = node_list.end();
  if (it == node_list.begin()) {
    GELOGE(INTERNAL_ERROR, "LabelGoto: Graph %s node is empty.", graph->GetName().c_str());
    return nullptr;
  }
  --it;
  const NodePtr &node = *it;
  GE_CHECK_NOTNULL_EXEC(node, return nullptr);

  OpDescPtr op_desc = MakeShared<OpDesc>(name, LABELGOTOEX);
  GE_CHECK_NOTNULL_EXEC(op_desc, return nullptr);
  SetStreamIdLeave(graph, op_desc);

  GELOGI("LabelGoto: Create node %s.", op_desc->GetName().c_str());
  (void)AttrUtils::SetInt(op_desc, ATTR_NAME_LABEL_SWITCH_INDEX, index);
  NodePtr label_goto = graph->AddNode(op_desc);
  GE_CHECK_NOTNULL_EXEC(label_goto, return nullptr);

  // Link control edge to graph tail.
  if (GraphUtils::AddEdge(node->GetOutControlAnchor(), label_goto->GetInControlAnchor()) != SUCCESS) {
    GELOGE(INTERNAL_ERROR, "LabelGoto: Add ctrl edge to %s failed.", node->GetName().c_str());
    return nullptr;
  }

  return label_goto;
}

/**
 * @ingroup ge
 * @brief Add LabelSwitch node at graph front.
 * @param [in] graph: graph for add node.
 * @param [in] name: label switch node name.
 * @param [in] desc: label index data desc.
 * @param [in] labels: label id for switch.
 * @return: NodePtr for success / nullptr for fail
 */
NodePtr LabelMaker::AddLabelSwitchEnter(const ComputeGraphPtr &graph, const std::string &name, const GeTensorDesc &desc,
                                        const std::vector<uint32_t> &labels) {
  GE_CHECK_NOTNULL_EXEC(graph, return nullptr);

  const auto &node_list = graph->GetDirectNode();
  auto it = node_list.begin();
  if (it == node_list.end()) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Graph %s node is empty.", graph->GetName().c_str());
    return nullptr;
  }

  OpDescPtr op_desc = MakeShared<OpDesc>(name, LABELSWITCHBYINDEX);
  GE_CHECK_NOTNULL_EXEC(op_desc, return nullptr);
  SetStreamIdEnter(graph, op_desc);

  GELOGI("LabelSwitchByIndex: Create node %s.", op_desc->GetName().c_str());
  if (op_desc->AddInputDesc(desc) != GRAPH_SUCCESS) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Add input desc failed.");
    return nullptr;
  }

  if (!AttrUtils::SetListInt(op_desc, ATTR_NAME_LABEL_SWITCH_LIST, labels)) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Add %s failed.", ATTR_NAME_LABEL_SWITCH_INDEX.c_str());
    return nullptr;
  }

  NodePtr label_switch = graph->AddNodeFront(op_desc);
  if (label_switch == nullptr) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Add to graph %s failed.", graph->GetName().c_str());
    return nullptr;
  }

  return label_switch;
}

/**
 * @ingroup ge
 * @brief Add LabelSwitch node at graph back.
 * @param [in] graph: graph for add node.
 * @param [in] name: label switch node name.
 * @param [in] desc: label index data desc.
 * @param [in] labels: label id for switch.
 * @return: NodePtr for success / nullptr for fail
 */
NodePtr LabelMaker::AddLabelSwitchLeave(const ComputeGraphPtr &graph, const std::string &name, const GeTensorDesc &desc,
                                        const std::vector<uint32_t> &labels) {
  GE_CHECK_NOTNULL_EXEC(graph, return nullptr);

  const auto &node_list = graph->GetDirectNode();
  auto it = node_list.end();
  if (it == node_list.begin()) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Graph %s node is empty.", graph->GetName().c_str());
    return nullptr;
  }
  --it;
  const NodePtr &node = *it;
  GE_CHECK_NOTNULL_EXEC(node, return nullptr);

  OpDescPtr op_desc = MakeShared<OpDesc>(name, LABELSWITCHBYINDEX);
  GE_CHECK_NOTNULL_EXEC(op_desc, return nullptr);
  SetStreamIdLeave(graph, op_desc);

  GELOGI("LabelSwitchByIndex: Create node %s.", op_desc->GetName().c_str());
  if (op_desc->AddInputDesc(desc) != GRAPH_SUCCESS) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Add input desc failed.");
    return nullptr;
  }

  if (!AttrUtils::SetListInt(op_desc, ATTR_NAME_LABEL_SWITCH_LIST, labels)) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Add %s failed.", ATTR_NAME_LABEL_SWITCH_INDEX.c_str());
    return nullptr;
  }

  NodePtr label_switch = graph->AddNode(op_desc);
  GE_CHECK_NOTNULL_EXEC(label_switch, return nullptr);

  // Link control edge to graph tail.
  if (GraphUtils::AddEdge(node->GetOutControlAnchor(), label_switch->GetInControlAnchor()) != SUCCESS) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Add ctrl edge to %s failed.", node->GetName().c_str());
    return nullptr;
  }

  return label_switch;
}

/**
 * @ingroup ge
 * @brief Add Data node at graph front for switch input.
 * @param [in] graph: graph for add node.
 * @param [in] name: label switch node name.
 * @param [in] desc: label index data desc.
 * @param [in] sw_node: switch node for add input.
 * @param [in] parent_index: index for parent node.
 * @return: NodePtr for success / nullptr for fail
 */
NodePtr LabelMaker::AddLabelSwitchIndex(const ComputeGraphPtr &graph, const std::string &name, const GeTensorDesc &desc,
                                        const NodePtr &sw_node, uint32_t parent_index) {
  GE_CHECK_NOTNULL_EXEC(graph, return nullptr);

  OpDescPtr op_desc = MakeShared<OpDesc>(name, DATA);
  GE_CHECK_NOTNULL_EXEC(op_desc, return nullptr);
  op_desc->SetStreamId(kInvalidStreamId);

  GELOGI("Data: Create node %s.", op_desc->GetName().c_str());
  if (op_desc->AddOutputDesc(desc) != GRAPH_SUCCESS) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Add data output desc failed.");
    return nullptr;
  }

  if (!AttrUtils::SetInt(op_desc, ATTR_NAME_PARENT_NODE_INDEX, parent_index)) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Add %s failed.", ATTR_NAME_PARENT_NODE_INDEX.c_str());
    return nullptr;
  }
  NodePtr op_data = graph->AddNodeFront(op_desc);
  GE_CHECK_NOTNULL_EXEC(op_data, return nullptr);
  GE_CHECK_NOTNULL_EXEC(graph->AddInputNode(op_data), return nullptr);  // take as input node for memory assign.

  // Link control edge to graph head.
  if (GraphUtils::AddEdge(op_data->GetOutDataAnchor(0), sw_node->GetInDataAnchor(0)) != SUCCESS) {
    GELOGE(INTERNAL_ERROR, "LabelSwitchByIndex: Add input edge to %s failed.", op_data->GetName().c_str());
    return nullptr;
  }

  return op_data;
}
}  // namespace ge
