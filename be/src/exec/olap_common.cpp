// This file is made available under Elastic License 2.0.
// This file is based on code available under the Apache license here:
//   https://github.com/apache/incubator-doris/blob/master/be/src/exec/olap_common.cpp

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "exec/olap_common.h"

#include <string>
#include <utility>
#include <vector>

#include "exec/olap_utils.h"
#include "runtime/large_int_value.h"

namespace starrocks {

template <>
std::string cast_to_string(__int128 value) {
    std::stringstream ss;
    ss << value;
    return ss.str();
}

template <>
void ColumnValueRange<StringValue>::convert_to_fixed_value() {}

template <>
void ColumnValueRange<Slice>::convert_to_fixed_value() {}

template <>
void ColumnValueRange<DecimalValue>::convert_to_fixed_value() {}

template <>
void ColumnValueRange<DecimalV2Value>::convert_to_fixed_value() {}

template <>
void ColumnValueRange<__int128>::convert_to_fixed_value() {}

template <>
void ColumnValueRange<bool>::convert_to_fixed_value() {}

Status OlapScanKeys::get_key_range(std::vector<std::unique_ptr<OlapScanRange>>* key_range) {
    key_range->clear();

    for (int i = 0; i < _begin_scan_keys.size(); ++i) {
        std::unique_ptr<OlapScanRange> range(new OlapScanRange());
        range->begin_scan_range = _begin_scan_keys[i];
        range->end_scan_range = _end_scan_keys[i];
        range->begin_include = _begin_include;
        range->end_include = _end_include;
        key_range->emplace_back(std::move(range));
    }

    return Status::OK();
}

} // namespace starrocks

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
