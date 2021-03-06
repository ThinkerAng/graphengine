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

#ifndef GE_COMMON_PROPERTIES_MANAGER_H_
#define GE_COMMON_PROPERTIES_MANAGER_H_

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "graph/op_desc.h"

namespace ge {
// Configuration property management
static const char *SYSMODE __attribute__((unused)) = "FMK_SYSMODE";
static const char *USE_FUSION __attribute__((unused)) = "FMK_USE_FUSION";
static const char *TIMESTAT_ENABLE __attribute__((unused)) = "DAVINCI_TIMESTAT_ENABLE";
static const char *ANNDROID_DEBUG __attribute__((unused)) = "ANNDROID_DEBUG";

class DumpProperties {
 public:
  DumpProperties() = default;
  ~DumpProperties() = default;
  DumpProperties(const DumpProperties &dump);
  DumpProperties &operator=(const DumpProperties &dump);

  void InitByOptions();

  void AddPropertyValue(const std::string &model, const std::set<std::string> &layers);
  void DeletePropertyValue(const std::string &model);

  std::set<std::string> GetAllDumpModel() const;
  std::set<std::string> GetPropertyValue(const std::string &model) const;
  bool IsLayerNeedDump(const std::string &model, const std::string &om_name, const std::string &op_name) const;

  void SetDumpPath(const std::string &path);
  std::string GetDumpPath() const;

  void SetDumpStep(const std::string &step);
  std::string GetDumpStep() const;

  void SetDumpMode(const std::string &mode);
  std::string GetDumpMode() const;

  bool IsOpDebugOpen() const { return is_op_debug_; }
  uint32_t GetOpDebugMode() const { return op_debug_mode_; }

 private:
  void CopyFrom(const DumpProperties &other);
  void SetDumpDebugOptions();

  string enable_dump_;
  string enable_dump_debug_;

  std::string dump_path_;
  std::string dump_step_;
  std::string dump_mode_;
  std::map<std::string, std::set<std::string>> model_dump_properties_map_;

  bool is_op_debug_ = false;
  uint32_t op_debug_mode_ = 0;
};

class PropertiesManager {
 public:
  // Singleton
  static PropertiesManager &Instance();

  /**
   *  @ingroup domi_ome
   *  @brief Initialize configuration parameters, which must be invoked in main.
   *  @param [in] file_path Property profile path
   *  @return true success
   *  @return false fail
   *  @author
   */
  bool Init(const std::string &file_path);

  /**
   *  @ingroup domi_ome
   *  @brief Get configuration parameter value
   *  @param [in] key Configuration parameter name
   *  @return Configuration parameter value. If the parameter name does not exist, return null
   *  @author
   */
  std::string GetPropertyValue(const std::string &key);

  /**
   *  @ingroup domi_ome
   *  @brief Set configuration parameters
   *  @param [in] key Configuration parameter name
   *  @param [out] key Configuration parameter value
   *  @author
   */
  void SetPropertyValue(const std::string &key, const std::string &value);

  /**
   *  @ingroup domi_ome
   *  @brief Return configuration parameters
   *  @return properties_map_
   *  @author
   */
  std::map<std::string, std::string> GetPropertyMap();

  /**
   *  @ingroup domi_ome
   *  @brief Adapt key value pair form, set different separators
   *  @param [in] delimiter
   *  @author
   */
  void SetPropertyDelimiter(const std::string &de);

  DumpProperties &GetDumpProperties(uint64_t session_id);
  void RemoveDumpProperties(uint64_t session_id);

 private:
  // Private construct, destructor
  PropertiesManager();
  ~PropertiesManager();

  // Get file content
  bool LoadFileContent(const std::string &file_path);

  // Parsing a single line file
  bool ParseLine(const std::string &line);

  // Remove space before and after string
  std::string Trim(const std::string &str);

  bool is_inited_;

  // Configuration item separator, default is "="
  std::string delimiter;

  std::map<std::string, std::string> properties_map_;
  std::mutex mutex_;

  std::map<uint64_t, DumpProperties> dump_properties_map_;
};
}  // namespace ge

#endif  // GE_COMMON_PROPERTIES_MANAGER_H_
