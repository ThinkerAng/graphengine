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

#include "graph/passes/mark_graph_unknown_status_pass.h"
#include "graph/utils/node_utils.h"

namespace ge {
Status MarkGraphUnknownStatusPass::Run(ComputeGraphPtr graph) {
  GE_CHECK_NOTNULL(graph);
  bool is_unknown_shape = false;
  for (const auto &node : graph->GetDirectNode()) {
    GE_CHK_STATUS_RET(ge::NodeUtils::GetNodeUnknownShapeStatus(*node, is_unknown_shape),
                      "Get node[%s] shape status failed!", node->GetName().c_str());
    if (is_unknown_shape) {
      break;
    }
  }
  graph->SetGraphUnknownFlag(is_unknown_shape);
  GELOGD("mark graph [%s] unknown status success! value is %d", graph->GetName().c_str(), is_unknown_shape);
  return SUCCESS;
}
}  // namespace ge