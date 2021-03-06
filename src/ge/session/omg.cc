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

#include "omg/omg.h"
#include <fstream>
#include <iostream>
#include <memory>
#include "common/auth/file_saver.h"
#include "common/convert/pb2json.h"
#include "common/debug/log.h"
#include "common/debug/memory_dumper.h"
#include "common/ge/ge_util.h"
#include "common/helper/model_helper.h"
#include "common/model_parser/base.h"
#include "common/model_parser/graph_parser_util.h"
#include "common/model_saver.h"
#include "common/properties_manager.h"
#include "common/string_util.h"
#include "common/types.h"
#include "common/util.h"
#include "common/util/error_manager/error_manager.h"
#include "framework/common/debug/ge_log.h"
#include "framework/omg/parser/parser_inner_ctx.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "graph/compute_graph.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/optimize/common/params.h"
#include "graph/utils/type_utils.h"
#include "ir_build/atc_ir_common.h"
#include "omg/omg_inner_types.h"
#include "omg/parser/model_parser.h"
#include "omg/parser/parser_factory.h"
#include "omg/parser/weights_parser.h"
#include "parser/common/pre_checker.h"
#include "proto/ge_ir.pb.h"
#include "register/op_registry.h"

using nlohmann::json;
using ProcParam = struct PROC_PARAM;
using domi::ModelParserFactory;
using domi::WeightsParserFactory;
using std::ostringstream;

namespace google {
namespace protobuf {
namespace io {
class FileOutputStream;
}
}  // namespace protobuf
}  // namespace google
namespace ge {
namespace {
const std::string kGraphDefaultName = "domi_default";
const std::string kScopeIdAttr = "fusion_scope";
const char *const kOutputTypeSample = "correct sample is \"opname:index:dtype\"";
const char *const kOutputTypeSupport = "only support FP32, FP16, UINT8";
const char *const kOutputTypeError = "The multiple out nodes set in output_type must be found in out_nodes.";
}  // namespace

// When the model is converted to a JSON file, the following operator attributes in the blacklist will be ignored
const std::set<string> kOmBlackFields = {"output",      "data_offset", "data", "workspace", "workspace_bytes",
                                         "memory_size", "weight_size", "size", "bt",        "quantize_factor"};

static std::map<std::string, ge::DataType> output_type_str_to_datatype = {
  {"FP32", ge::DT_FLOAT}, {"FP16", ge::DT_FLOAT16}, {"UINT8", ge::DT_UINT8}};

static bool CheckInputTrueOrFalse(const std::string &s, const std::string &atc_param) {
  if ((s == "true") || (s == "false")) {
    return true;
  } else {
    ErrorManager::GetInstance().ATCReportErrMessage("E10005", {"parameter", "value"}, {atc_param, s});
    GELOGE(PARAM_INVALID, "Input parameter[--%s]'s value[%s] must be true or false.", atc_param.c_str(), s.c_str());
    return false;
  }
}

static void ParseAtcParms(const std::map<std::string, std::string> &atc_params, const std::string &key,
                          std::string &param) {
  auto iter = atc_params.find(key);
  if (iter != atc_params.end()) {
    param = iter->second;
  }
}

static Status CheckInputShapeNode(const ComputeGraphPtr &graph) {
  for (auto it : domi::GetContext().user_input_dims) {
    std::string node_name = it.first;
    ge::NodePtr node = graph->FindNode(node_name);
    if (node == nullptr) {
      ErrorManager::GetInstance().ATCReportErrMessage("E10016", {"parameter", "opname"}, {"input_shape", node_name});
      GELOGE(PARAM_INVALID, "Input parameter[--input_shape]'s opname[%s] is not exist in model", node_name.c_str());
      return PARAM_INVALID;
    }
    if (node->GetType() != DATA) {
      ErrorManager::GetInstance().ATCReportErrMessage("E10017", {"parameter", "opname"}, {"input_shape", node_name});
      GELOGE(PARAM_INVALID, "Input parameter[--input_shape]'s opname[%s] is not a input opname", node_name.c_str());
      return PARAM_INVALID;
    }
  }
  return SUCCESS;
}

void AddAttrsForInputNodes(const vector<string> &adjust_fp16_format_vec, const string &fp16_nodes_name, uint32_t index,
                           OpDescPtr &op_desc) {
  if (AttrUtils::SetBool(op_desc, "input_fp16", true) &&
      AttrUtils::SetStr(op_desc, ATTR_ATC_USER_DEFINE_DATATYPE, TypeUtils::DataTypeToSerialString(DT_FLOAT16))) {
    if ((index < adjust_fp16_format_vec.size()) && (adjust_fp16_format_vec[index] == "true")) {
      GELOGI("This node [%s] should be set NC1HWC0", fp16_nodes_name.c_str());
      if (!AttrUtils::SetBool(op_desc, "input_set_nc1hwc0", true)) {
        GELOGW("This node [%s] set NC1HWC0 failed", fp16_nodes_name.c_str());
      }
      if (!AttrUtils::SetStr(op_desc, ATTR_ATC_USER_DEFINE_FORMAT, TypeUtils::FormatToSerialString(FORMAT_NC1HWC0))) {
        GELOGW("This node [%s] set NC1HWC0 failed", fp16_nodes_name.c_str());
      }
    }
  }
}

static Status CheckInputFp16Nodes(const ComputeGraphPtr &graph, const string &input_fp16_nodes,
                                  const string &is_input_adjust_hw_layout) {
  GE_CHECK_NOTNULL(graph);
  vector<string> adjust_fp16_format_vec;
  if (!is_input_adjust_hw_layout.empty()) {
    adjust_fp16_format_vec = StringUtils::Split(is_input_adjust_hw_layout, ',');
    for (auto &s : adjust_fp16_format_vec) {
      StringUtils::Trim(s);
      if (!CheckInputTrueOrFalse(s, "is_input_adjust_hw_layout")) {
        GELOGE(PARAM_INVALID, "Invalid Param, is_input_adjust_hw_layout only support true/false: but is [%s]",
               is_input_adjust_hw_layout.c_str());
        return PARAM_INVALID;
      }
    }
  }
  if (input_fp16_nodes.empty()) {
    return SUCCESS;
  }
  GELOGI("The input_fp16_nodes is set %s", input_fp16_nodes.c_str());
  vector<string> input_fp16_nodes_vec = StringUtils::Split(input_fp16_nodes, ';');
  for (uint32_t i = 0; i < input_fp16_nodes_vec.size(); ++i) {
    ge::NodePtr node = graph->FindNode(input_fp16_nodes_vec[i]);
    if (node == nullptr) {
      ErrorManager::GetInstance().ATCReportErrMessage("E10016", {"parameter", "opname"},
                                                      {"input_fp16_nodes", input_fp16_nodes_vec[i]});
      GELOGE(PARAM_INVALID, "Input parameter[--input_fp16_nodes]'s opname[%s] is not exist in model",
             input_fp16_nodes_vec[i].c_str());
      return PARAM_INVALID;
    }
    auto op_desc = node->GetOpDesc();
    GE_CHECK_NOTNULL(op_desc);
    if (op_desc->GetType() != DATA) {
      ErrorManager::GetInstance().ATCReportErrMessage("E10017", {"parameter", "opname"},
                                                      {"input_fp16_nodes", input_fp16_nodes_vec[i]});
      GELOGE(PARAM_INVALID, "Input parameter[--input_fp16_nodes]'s opname[%s] is not a input opname",
             input_fp16_nodes_vec[i].c_str());
      return PARAM_INVALID;
    }
    AddAttrsForInputNodes(adjust_fp16_format_vec, input_fp16_nodes_vec[i], i, op_desc);
  }
  return SUCCESS;
}

static Status SetWeightCompressNodes(const ComputeGraphPtr &graph, const string &compress_weight_conf) {
  GE_CHECK_NOTNULL(graph);
  if (compress_weight_conf.empty()) {
    return SUCCESS;
  }
  std::string real_path = RealPath(compress_weight_conf.c_str());
  if (real_path.empty()) {
    GELOGE(PARAM_INVALID, "Can not get real path for %s.", compress_weight_conf.c_str());
    return PARAM_INVALID;
  }
  std::ifstream ifs(real_path);
  if (!ifs.is_open()) {
    GELOGE(domi::FAILED, "Open file %s failed", compress_weight_conf.c_str());
    return domi::FAILED;
  }

  std::string compress_nodes;
  ifs >> compress_nodes;
  ifs.close();
  GELOGI("Compress weight of nodes: %s", compress_nodes.c_str());

  vector<string> compress_node_vec = StringUtils::Split(compress_nodes, ';');
  for (size_t i = 0; i < compress_node_vec.size(); ++i) {
    ge::NodePtr node = graph->FindNode(compress_node_vec[i]);
    if (node == nullptr) {
      GELOGW("node %s is not in graph", compress_node_vec[i].c_str());
      continue;
    }
    auto op_desc = node->GetOpDesc();
    GE_CHECK_NOTNULL(op_desc);
    if (!ge::AttrUtils::SetBool(op_desc, ge::ATTR_NAME_COMPRESS_WEIGHT, true)) {
      GELOGE(domi::FAILED, "node %s SetBool failed.", compress_node_vec[i].c_str());
      return domi::FAILED;
    }
  }
  return SUCCESS;
}

void FindParserSo(const string &path, vector<string> &file_list, string &caffe_parser_path) {
  // path, Change to absolute path
  string real_path = RealPath(path.c_str());
  if (real_path.empty()) {  // plugin path does not exist
    return;
  }

  struct dirent *dent(nullptr);
  DIR *dir = opendir(real_path.c_str());

  if (nullptr == dir) {  //  plugin path does not exist
    GELOGW("Open directory %s failed.", path.c_str());
    return;
  }

  while ((dent = readdir(dir)) != nullptr) {
    if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0) continue;
    string name = dent->d_name;
    string full_name = real_path + "/" + name;
    const string so_suff = ".so";
    const string caffe_parser_so_suff = "lib_caffe_parser.so";
    const string aicpu_so_suff = "_aicpu.so";
    const string aicpu_host_so_suff = "_online.so";
    if (name.size() >= so_suff.size() && name.compare(name.size() - so_suff.size(), so_suff.size(), so_suff) == 0) {
      if (full_name.size() >= caffe_parser_so_suff.size() &&
          full_name.compare(full_name.size() - caffe_parser_so_suff.size(), caffe_parser_so_suff.size(),
                            caffe_parser_so_suff) == 0) {
        caffe_parser_path = full_name;
      } else if ((full_name.size() >= aicpu_so_suff.size() &&
                  full_name.compare(full_name.size() - aicpu_so_suff.size(), aicpu_so_suff.size(), aicpu_so_suff) ==
                    0) ||
                 (full_name.size() >= aicpu_host_so_suff.size() &&
                  full_name.compare(full_name.size() - aicpu_host_so_suff.size(), aicpu_host_so_suff.size(),
                                    aicpu_host_so_suff) == 0)) {
        // aicpu so, Put the file path into the omgcontext and save into the model in the builder stage;
        domi::GetContext().aicpu_op_run_paths.push_back(full_name);
      } else {  // save parser so path into file_list vector
        file_list.push_back(full_name);
      }
      continue;
    }

    FindParserSo(full_name, file_list, caffe_parser_path);
  }
  closedir(dir);
  return;
}

Status CheckCustomAiCpuOpLib() {
  std::vector<std::string> vec_op_type;
  domi::OpRegistry::Instance()->GetOpTypeByImplyType(vec_op_type, domi::ImplyType::CUSTOM);
  for (uint32_t i = 0; i < vec_op_type.size(); i++) {
    bool aicpu_so_exist = false;
    std::string ai_cpu_so_name = "lib" + vec_op_type[i] + "_aicpu.so";
    for (uint32_t j = 0; j < domi::GetContext().aicpu_op_run_paths.size(); j++) {
      string bin_file_path = domi::GetContext().aicpu_op_run_paths[j];
      if (bin_file_path.size() >= ai_cpu_so_name.size() &&
          bin_file_path.compare(bin_file_path.size() - ai_cpu_so_name.size(), ai_cpu_so_name.size(), ai_cpu_so_name) ==
            0) {
        aicpu_so_exist = true;
        break;
      }
    }
    if (!aicpu_so_exist) {
      GELOGE(domi::FAILED, "cant find aicpu run so(%s), please check the plugin path!", ai_cpu_so_name.c_str());
      return domi::FAILED;
    }
  }
  return domi::SUCCESS;
}

Status SetOutFormatAndDataTypeAttr(ge::OpDescPtr op_desc, const ge::Format format, const ge::DataType data_type) {
  if (op_desc == nullptr) {
    GELOGE(domi::FAILED, "Input op desc invalid.");
    return domi::FAILED;
  }
  (void)ge::AttrUtils::SetInt(op_desc, ATTR_NAME_NET_OUTPUT_FORMAT, format);
  (void)ge::AttrUtils::SetInt(op_desc, ATTR_NAME_NET_OUTPUT_DATATYPE, data_type);
  return domi::SUCCESS;
}

void GetOutputNodesNameAndIndex(std::vector<std::pair<ge::NodePtr, int32_t>> &output_nodes_info,
                                std::vector<std::string> &output_nodes_name) {
  output_nodes_name.clear();
  if (domi::GetContext().out_top_names.empty()) {
    // tf process, no top name.
    for (const auto output_node_info : output_nodes_info) {
      std::string node_name = output_node_info.first->GetName();
      int32_t index = output_node_info.second;
      output_nodes_name.push_back(node_name + ":" + std::to_string(index));
    }
    return;
  }
  // caffe process, need add top name after node_name:index
  for (size_t i = 0; i < output_nodes_info.size(); ++i) {
    std::string node_name = output_nodes_info[i].first->GetName();
    int32_t index = output_nodes_info[i].second;
    if (i < domi::GetContext().out_top_names.size()) {
      output_nodes_name.push_back(node_name + ":" + std::to_string(index) + ":" + domi::GetContext().out_top_names[i]);
    } else {
      GELOGW("Get top name of node [%s] fail.", node_name.c_str());
      output_nodes_name.push_back(node_name + ":" + std::to_string(index));
    }
  }
}

///
/// @ingroup domi_common
/// @brief Initialize omgcontext based on command line input
/// @param [in] input_shape Input shape string to be parsed
/// @return SUCCESS: parse successfully; PARAM_INVALID：parse failed
///
Status InitDomiOmgContext(const string &input_shape, const string &input_format, const string &net_format,
                          bool is_dynamic_input) {
  // Clear omgcontext data first
  domi::GetContext().input_dims.clear();
  domi::GetContext().user_input_dims.clear();
  domi::GetContext().is_dynamic_input = is_dynamic_input;

  // the default value is ND
  domi::GetContext().format = DOMI_TENSOR_ND;
  if (!input_format.empty()) {
    auto iter = ge::input_format_str_to_geformat.find(input_format);
    if (iter != ge::input_format_str_to_geformat.end()) {
      domi::GetContext().format = iter->second;
    } else {
      GELOGE(PARAM_INVALID, "Input format %s not support , expect ND/NCHW/NHWC/CHWN/NC1HWC0/NHWC1C0.",
             input_format.c_str());
      return PARAM_INVALID;
    }
  }

  // Input is empty, do not process
  if (input_shape.empty()) {
    return SUCCESS;
  }

  // Analyze the input shape paramete
  unordered_map<string, vector<int64_t>> &shape_map = domi::GetContext().input_dims;

  if (!ge::ParseInputShape(input_shape, domi::GetContext().input_dims, domi::GetContext().user_input_dims,
                           is_dynamic_input) ||
      shape_map.empty()) {
    GELOGE(PARAM_INVALID, "Failed to parse input shape: %s", input_shape.c_str());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

/// @ingroup domi_common
///  @brief Judge whether the op_Name_Map parameter matches the network
///  @param [in] graph Input network graph
///  @return SUCCESS: Input parameters are correct; PARAM_INVALID: Input parameters are incorrect
///
static Status CheckOpNameMap(const ComputeGraphPtr &graph, const std::string &op_conf) {
  GE_CHECK_NOTNULL(graph);
  unordered_map<string, string> graphNodeTypes;
  for (const NodePtr &node : graph->GetAllNodes()) {
    auto op_desc = node->GetOpDesc();
    if (op_desc == nullptr) {
      GELOGE(PARAM_INVALID, "Invalid parameter for opDesc.");
      return PARAM_INVALID;
    }
    graphNodeTypes[op_desc->GetType()] = "";
  }
  std::map<std::string, std::string> &propertiesMap = domi::GetContext().op_conf_map;
  GE_RT_PARAM_INVALID_WITH_LOG_IF_TRUE(propertiesMap.empty(), "op_name_map file is empty, please check file!");
  for (auto iter = propertiesMap.begin(); iter != propertiesMap.end(); iter++) {
    GE_IF_BOOL_EXEC(graphNodeTypes.find(iter->second) == graphNodeTypes.end(),
                    ErrorManager::GetInstance().ATCReportErrMessage(
                      "E10003", {"parameter", "value", "reason"},
                      {"op_name_map", op_conf, "type[" + iter->second + "] is not found in model"});
                    GELOGE(PARAM_INVALID, "Invalid parameter for op_name_map."); return PARAM_INVALID;);
  }
  return SUCCESS;
}

FMK_FUNC_HOST_VISIBILITY Status ParseGraph(ge::Graph &graph, const std::map<string, string> &atc_params,
                                           const char *model_file, const char *weights_file, domi::FrameworkType type,
                                           const char *op_conf, const char *target, RunMode run_mode,
                                           bool is_dynamic_input) {
  GE_CHECK_NOTNULL(model_file);
  GE_CHECK_NOTNULL(weights_file);
  domi::GetContext().type = type;
  domi::GetContext().run_mode = run_mode;
  // Prevent data residue in multiple calls
  PreChecker::Instance().Clear();

  Params::Instance()->SetTarget(target);

  // Create an empty computegraph
  std::string om_name;
  ParseAtcParms(atc_params, "output", om_name);
  ModelHelper model_helper;
  string graph_name = "";
  Status name_ret = model_helper.GetBaseNameFromFileName(om_name, graph_name);
  if (name_ret != SUCCESS) {
    graph_name = kGraphDefaultName + "_" + CurrentTimeInStr();
  }
  ComputeGraphPtr compute_graph = MakeShared<ComputeGraph>(graph_name);
  GE_CHECK_NOTNULL(compute_graph);
  graph = GraphUtils::CreateGraphFromComputeGraph(compute_graph);

  // initialize omgContext
  std::string input_shape;
  ParseAtcParms(atc_params, "input_shape", input_shape);
  std::string input_format;
  ParseAtcParms(atc_params, "input_format", input_format);
  GE_RETURN_WITH_LOG_IF_ERROR(InitDomiOmgContext(input_shape, input_format, "", is_dynamic_input),
                              "ATC Generate call InitDomiOmgContext ret fail");

  std::string is_output_adjust_hw_layout;
  ParseAtcParms(atc_params, "is_output_adjust_hw_layout", is_output_adjust_hw_layout);
  GE_RETURN_WITH_LOG_IF_ERROR(ParseOutputFp16NodesFormat(is_output_adjust_hw_layout), "Parse is_output_fp16 failed");

  std::string out_nodes;
  ParseAtcParms(atc_params, "out_nodes", out_nodes);
  GE_RETURN_WITH_LOG_IF_ERROR(ParseOutputNodes(out_nodes), "ATC Generate parse out nodes fail");

  std::string output_type;
  ParseAtcParms(atc_params, "output_type", output_type);

  // parse configuration item
  if (op_conf != nullptr && *op_conf != '\0') {
    // divided by ":"
    PropertiesManager::Instance().SetPropertyDelimiter(OP_CONF_DELIMITER);
    // Parsing the op_conf configuration item file
    GE_IF_BOOL_EXEC(!PropertiesManager::Instance().Init(op_conf),
                    ErrorManager::GetInstance().ATCReportErrMessage("E10003", {"parameter", "value", "reason"},
                                                                    {"op_name_map", op_conf, "file content error"});
                    GELOGE(FAILED, "op_name_map init failed!"); return FAILED);
    // Return map and put it into ATC global variable
    domi::GetContext().op_conf_map = PropertiesManager::Instance().GetPropertyMap();
  }

  // parse network model
  auto model_parser = ModelParserFactory::Instance()->CreateModelParser(type);
  GE_CHK_BOOL_RET_STATUS(model_parser != nullptr, FAILED, "ATC create model parser ret fail, type:%d.", type);

  UpdateParserCtxWithOmgCtx();
  Status ret = model_parser->Parse(model_file, graph);
  UpdateOmgCtxWithParserCtx();

  // Generate the report in case of pre inspection failure or only pre inspection mode
  if (PreChecker::Instance().HasError() || run_mode == ONLY_PRE_CHECK) {
    std::string check_report;
    ParseAtcParms(atc_params, "check_report", check_report);
    GE_RETURN_WITH_LOG_IF_ERROR(PreChecker::Instance().Save(check_report), "Generate pre-checking report failed.");
    GEEVENT("The pre-checking report has been saved to %s.", check_report.c_str());
  }

  GE_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "ATC model parse ret fail.");

  std::string input_fp16_nodes;
  ParseAtcParms(atc_params, "input_fp16_nodes", input_fp16_nodes);
  std::string is_input_adjust_hw_layout;
  ParseAtcParms(atc_params, "is_input_adjust_hw_layout", is_input_adjust_hw_layout);
  compute_graph = GraphUtils::GetComputeGraph(graph);
  GE_RETURN_IF_ERROR(CheckInputFp16Nodes(compute_graph, input_fp16_nodes, is_input_adjust_hw_layout));

  GE_RETURN_IF_ERROR(CheckInputShapeNode(compute_graph));

  std::string compress_weight_conf;
  ParseAtcParms(atc_params, "compress_weight_conf", compress_weight_conf);
  GE_RETURN_IF_ERROR(SetWeightCompressNodes(compute_graph, compress_weight_conf));

  // Verify the contents of the op_name_map
  if (op_conf != nullptr && *op_conf != '\0') {
    GE_RETURN_WITH_LOG_IF_ERROR(CheckOpNameMap(compute_graph, op_conf),
                                "op_name_map parameter is not fit with input net!");
  }

  // Print parse network structure
  compute_graph->Dump();

  // parse weight
  graph = GraphUtils::CreateGraphFromComputeGraph(compute_graph);
  auto weights_parser = WeightsParserFactory::Instance()->CreateWeightsParser(type);
  ret = weights_parser->Parse(weights_file, graph);

  // IN ONLY_PRE_CHECK mode, generate pre inspection report only.
  if (PreChecker::Instance().HasError() || run_mode == ONLY_PRE_CHECK) {
    std::string check_report;
    ParseAtcParms(atc_params, "check_report", check_report);
    GE_RETURN_WITH_LOG_IF_ERROR(PreChecker::Instance().Save(check_report), "Generate pre-checking report failed.");
    GEEVENT("The pre-checking report has been saved to %s.", check_report.c_str());
  }
  // Prevent data residue in multiple calls
  PreChecker::Instance().Clear();

  GE_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "ATC weights parse ret fail.");

  GELOGI("ATC parser success.");

  return SUCCESS;
}

void GetGroupName(ge::proto::ModelDef &model_def) {
  auto modelAttrMap = model_def.mutable_attr();
  auto fusionModelOpListIter = modelAttrMap->find(MODEL_ATTR_FUSION_MODEL_DEF);
  GE_IF_BOOL_EXEC(
    fusionModelOpListIter != modelAttrMap->end(), int fusionOpIndex = 0;
    for (int i = 0; i < model_def.graph_size(); i++) {
      auto graph = model_def.mutable_graph(i);
      for (int j = 0; j < graph->op_size(); j++) {
        int64_t scope_id = 0;
        auto bt = fusionModelOpListIter->second.list().bt(fusionOpIndex++);
        ge::proto::OpDef fusion_op_def;
        GE_CHK_BOOL_EXEC(bt.size() != 0, GELOGW("Invalid bt size"); return;);

        (void)(fusion_op_def.ParseFromArray(bt.data(), bt.size()));
        auto fusion_attr_map = fusion_op_def.mutable_attr();
        auto fusion_iter = fusion_attr_map->find(kScopeIdAttr);
        GE_IF_BOOL_EXEC(fusion_iter == fusion_attr_map->end(), continue;);

        scope_id = fusion_iter->second.i();
        ge::proto::OpDef *opdef = graph->mutable_op(j);
        auto attr_map = opdef->mutable_attr();

        int64_t stream_id = opdef->stream_id();

        uint16_t l1_id = (((uint64_t)scope_id & 0xFFFF0000)) >> 16;
        GE_IF_BOOL_EXEC(l1_id != 0, ostringstream groupName; groupName << "group_op_l1_" << l1_id << "_" << stream_id;
                        (*attr_map)["group_op_name"].set_s(groupName.str()); continue;);

        uint16_t ub_id = ((uint64_t)scope_id & 0xFFFF);
        GE_IF_BOOL_EXEC(ub_id != 0, ostringstream groupName; groupName << "group_op_ub_" << ub_id << "_" << stream_id;
                        (*attr_map)["group_op_name"].set_s(groupName.str()););
      }
    });
}

FMK_FUNC_HOST_VISIBILITY Status ConvertOmModelToJson(const char *model_file, const char *json_file) {
  GE_CHECK_NOTNULL(model_file);
  GE_CHECK_NOTNULL(json_file);
  ge::ModelData model;

  // Mode 2 does not need to verify the priority, and a default value of 0 is passed
  int32_t priority = 0;

  // Load model from file
  Status ret = ModelParserBase::LoadFromFile(model_file, "", priority, model);
  if (ret != SUCCESS) {
    GELOGE(ret, "LoadFromFile failed.");
    return ret;
  }

  uint8_t *model_data = nullptr;
  uint32_t model_len = 0;

  // Parse the contents of the file to get the modeldef object
  ret = ModelParserBase::ParseModelContent(model, model_data, model_len);
  if (ret == SUCCESS) {
    OmFileLoadHelper omFileLoadHelper;
    ge::graphStatus status = omFileLoadHelper.Init(model_data, model_len);
    if (status != ge::GRAPH_SUCCESS) {
      GELOGE(ge::FAILED, "Om file init failed.");
      if (model.model_data != nullptr) {
        delete[](char *) model.model_data;
        model.model_data = nullptr;
      }
      return status;
    }

    ModelPartition ir_part;
    status = omFileLoadHelper.GetModelPartition(MODEL_DEF, ir_part);
    if (status != ge::GRAPH_SUCCESS) {
      GELOGE(ge::FAILED, "Get model part failed.");
      if (model.model_data != nullptr) {
        delete[](char *) model.model_data;
        model.model_data = nullptr;
      }
      return status;
    }

    ge::proto::ModelDef model_def;

    // De serialization
    bool flag = ReadProtoFromArray(ir_part.data, ir_part.size, &model_def);
    if (flag) {
      GetGroupName(model_def);

      json j;
      Pb2Json::Message2Json(model_def, kOmBlackFields, j, true);

      ret = ModelSaver::SaveJsonToFile(json_file, j);
    } else {
      ret = INTERNAL_ERROR;
      GELOGE(ret, "ReadProtoFromArray failed.");
    }
  } else {
    GELOGE(PARAM_INVALID, "ParseModelContent failed because of invalid om file. Please check --om param.");
  }

  if (model.model_data != nullptr) {
    delete[](char *) model.model_data;
    model.model_data = nullptr;
  }

  return ret;
}

FMK_FUNC_HOST_VISIBILITY Status ConvertPbtxtToJson(const char *model_file, const char *json_file) {
  ge::ModelData model;

  // Mode 2 does not need to verify the priority, and a default value of 0 is passed
  int32_t priority = 0;

  // Load model from file
  Status ret = ModelParserBase::LoadFromFile(model_file, "", priority, model);
  auto free_model_data = [](void **ptr) -> void {
    if (ptr != nullptr && *ptr != nullptr) {
      delete[] reinterpret_cast<char *>(*ptr);
      *ptr = nullptr;
    }
  };
  if (ret != SUCCESS) {
    free_model_data(&model.model_data);
    GELOGE(ret, "LoadFromFile failed.");
    return ret;
  }

  ge::proto::ModelDef model_def;
  bool flag = google::protobuf::TextFormat::ParseFromString(reinterpret_cast<char *>(model.model_data), &model_def);
  if (!flag) {
    free_model_data(&model.model_data);
    GELOGE(FAILED, "ParseFromString fail.");
    return FAILED;
  }

  GetGroupName(model_def);
  json j;
  Pb2Json::Message2Json(model_def, kOmBlackFields, j, true);
  ret = ModelSaver::SaveJsonToFile(json_file, j);
  if (ret != SUCCESS) {
    free_model_data(&model.model_data);
    GELOGE(ret, "Save json to file fail.");
    return ret;
  }

  free_model_data(&model.model_data);

  return SUCCESS;
}

FMK_FUNC_HOST_VISIBILITY Status ConvertFwkModelToJson(const domi::FrameworkType framework, const char *model_file,
                                                      const char *json_file) {
  if (framework == domi::CAFFE || framework == domi::TENSORFLOW || framework == domi::ONNX) {
    auto model_parser = ModelParserFactory::Instance()->CreateModelParser(framework);
    GE_CHK_BOOL_RET_STATUS(model_parser != nullptr, FAILED, "ATC create model parser ret fail, framework:%d.",
                           framework);
    return model_parser->ToJson(model_file, json_file);
  }

  ErrorManager::GetInstance().ATCReportErrMessage(
    "E10001", {"parameter", "value", "reason"},
    {"--framework", std::to_string(framework), "only support 0(Caffe) 3(TensorFlow)"});
  GELOGE(PARAM_INVALID, "Input parameter[--framework] is mandatory and it's value must be: 0(Caffe) 3(TensorFlow).");
  return PARAM_INVALID;
}

FMK_FUNC_HOST_VISIBILITY Status DumpInfershapeJson(const ge::Graph &graph, const char *json_file) {
  // Create buffer
  GELOGI("Enter to dump infershape json schedule.");
  ge::Model model("", "");
  model.SetGraph(graph);
  Buffer buffer;
  model.Save(buffer, true);

  ge::proto::ModelDef ge_proto;
  if (buffer.GetData() != nullptr) {
    std::string str(reinterpret_cast<const char *>(buffer.GetData()), buffer.GetSize());
    if (!ge_proto.ParseFromString(str)) {
      GELOGE(GRAPH_FAILED, "parse from string failed.");
      return FAILED;
    }

    nlohmann::json j;
    Pb2Json::Message2Json(ge_proto, std::set<string>(), j);

    ModelSaver::SaveJsonToFile(json_file, j);
  }
  return SUCCESS;
}

void UpdateOmgCtxWithParserCtx() {
  domi::GetContext().format = GetParserContext().format;
  domi::GetContext().input_dims = GetParserContext().input_dims;
  return;
}

void UpdateParserCtxWithOmgCtx() {
  GetParserContext().format = domi::GetContext().format;
  GetParserContext().input_dims = domi::GetContext().input_dims;
  GetParserContext().run_mode = domi::GetContext().run_mode;
  return;
}
}  // namespace ge
