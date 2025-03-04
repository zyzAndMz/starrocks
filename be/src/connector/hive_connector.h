// This file is licensed under the Elastic License 2.0. Copyright 2021-present, StarRocks Inc.

#pragma once

#include "column/vectorized_fwd.h"
#include "connector/connector.h"
#include "exec/vectorized/hdfs_scanner.h"

namespace starrocks {

namespace connector {

class HiveConnector final : public Connector {
public:
    ~HiveConnector() override = default;

    DataSourceProviderPtr create_data_source_provider(vectorized::ConnectorScanNode* scan_node,
                                                      const TPlanNode& plan_node) const override;

    ConnectorType connector_type() const override { return ConnectorType::HIVE; }
};

class HiveDataSource;
class HiveDataSourceProvider;

class HiveDataSourceProvider final : public DataSourceProvider {
public:
    ~HiveDataSourceProvider() override = default;
    friend class HiveDataSource;
    HiveDataSourceProvider(vectorized::ConnectorScanNode* scan_node, const TPlanNode& plan_node);
    DataSourcePtr create_data_source(const TScanRange& scan_range) override;

protected:
    vectorized::ConnectorScanNode* _scan_node;
    const THdfsScanNode _hdfs_scan_node;
};

class HiveDataSource final : public DataSource {
public:
    ~HiveDataSource() override = default;

    HiveDataSource(const HiveDataSourceProvider* provider, const TScanRange& scan_range);
    Status open(RuntimeState* state) override;
    void close(RuntimeState* state) override;
    Status get_next(RuntimeState* state, vectorized::ChunkPtr* chunk) override;

    int64_t raw_rows_read() const override;
    int64_t num_rows_read() const override;
    int64_t num_bytes_read() const override;
    int64_t cpu_time_spent() const override;

private:
    const HiveDataSourceProvider* _provider;
    const THdfsScanRange _scan_range;

    // ============= init func =============
    Status _init_conjunct_ctxs(RuntimeState* state);
    Status _decompose_conjunct_ctxs(RuntimeState* state);
    void _init_tuples_and_slots(RuntimeState* state);
    void _init_counter(RuntimeState* state);

    Status _init_partition_values();
    Status _init_scanner(RuntimeState* state);

    // =====================================
    ObjectPool _pool;
    RuntimeState* _runtime_state = nullptr;
    vectorized::HdfsScanner* _scanner = nullptr;

    // ============ conjuncts =================
    std::vector<ExprContext*> _min_max_conjunct_ctxs;

    // complex conjuncts, such as contains multi slot, are evaled in scanner.
    std::vector<ExprContext*> _scanner_conjunct_ctxs;
    // conjuncts that contains only one slot.
    // 1. conjuncts that column is not exist in file, are used to filter file in file reader.
    // 2. conjuncts that column is materialized, are evaled in group reader.
    std::unordered_map<SlotId, std::vector<ExprContext*>> _conjunct_ctxs_by_slot;

    // partition conjuncts of each partition slot.
    std::vector<ExprContext*> _partition_conjunct_ctxs;
    std::vector<ExprContext*> _partition_values;
    bool _has_partition_conjuncts = false;
    bool _filter_by_eval_partition_conjuncts = false;
    bool _no_data = false;

    int _min_max_tuple_id = 0;
    TupleDescriptor* _min_max_tuple_desc = nullptr;

    // materialized columns.
    std::vector<SlotDescriptor*> _materialize_slots;
    std::vector<int> _materialize_index_in_chunk;

    // partition columns.
    std::vector<SlotDescriptor*> _partition_slots;

    // partition column index in `tuple_desc`
    std::vector<int> _partition_index_in_chunk;
    // partition index in hdfs partition columns
    std::vector<int> _partition_index_in_hdfs_partition_columns;
    bool _has_partition_columns = false;

    std::vector<std::string> _hive_column_names;
    bool _case_sensitive = false;
    const HiveTableDescriptor* _hive_table = nullptr;

    // ======================================
    // The following are profile metrics
    vectorized::HdfsScanProfile _profile;
};

} // namespace connector
} // namespace starrocks
