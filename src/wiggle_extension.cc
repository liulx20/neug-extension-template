/** Copyright 2020 Alibaba Group Holding Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <glog/logging.h>
#include <string>

#include "ic1.h"
#include "ic10.h"
#include "ic11.h"
#include "ic12.h"
#include "ic13.h"
#include "ic14.h"
#include "ic2.h"
#include "ic3.h"
#include "ic4.h"
#include "ic5.h"
#include "ic6.h"
#include "ic7.h"
#include "ic8.h"
#include "ic9.h"
#include "is1.h"
#include "is2.h"
#include "is3.h"
#include "is4.h"
#include "is5.h"
#include "is6.h"
#include "is7.h"
#include "neug/compiler/extension/extension_api.h"
#include "neug/utils/exception/exception.h"
#include "wiggle_function.h"

namespace {

#define REGISTER_SCALAR_FUNC(Func)                       \
  neug::extension::ExtensionAPI::registerFunction<Func>( \
      neug::catalog::CatalogEntryType::SCALAR_FUNCTION_ENTRY)

#define REGISTER_TABLE_FUNC(Func)                        \
  neug::extension::ExtensionAPI::registerFunction<Func>( \
      neug::catalog::CatalogEntryType::TABLE_FUNCTION_ENTRY)

void register_ldbc_functions() {
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC1Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC2Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC3Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC4Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC5Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC6Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC7Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC8Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC9Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC10Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC11Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC12Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC13Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IC14Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IS1Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IS2Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IS3Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IS4Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IS5Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IS6Function);
  REGISTER_TABLE_FUNC(neug::extension::ldbc_ic::IS7Function);
}

}  // namespace

extern "C" {

void Init() {
  try {
    REGISTER_SCALAR_FUNC(neug::extension::wiggle::WiggleFunction);
    register_ldbc_functions();

    neug::extension::ExtensionAPI::registerExtension(
        neug::extension::ExtensionInfo{"wiggle",
                                       "LDBC SNB Interactive read queries "
                                       "(ic1-ic14, is1-is7) and wiggle()."});

    LOG(INFO) << "[wiggle extension] initialized";
  } catch (const std::exception& e) {
    THROW_EXCEPTION_WITH_FILE_LINE("[wiggle extension] registration failed: " +
                                   std::string(e.what()));
  } catch (...) {
    THROW_EXCEPTION_WITH_FILE_LINE(
        "[wiggle extension] registration failed: unknown exception");
  }
}

const char* Name() { return "WIGGLE"; }

}  // extern "C"
