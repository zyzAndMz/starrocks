// This file is licensed under the Elastic License 2.0. Copyright 2021-present, StarRocks Inc.

#include "fs/fs_hdfs.h"

#include <fmt/format.h>
#include <hdfs/hdfs.h>

#include <atomic>

#include "gutil/strings/substitute.h"
#include "runtime/file_result_writer.h"
#include "runtime/hdfs/hdfs_fs_cache.h"
#include "udf/java/utils.h"
#include "util/hdfs_util.h"

using namespace fmt::literals;

namespace starrocks {

// ==================================  HdfsInputStream  ==========================================

// TODO: move this class to directory 'be/srcio/'
// class for remote read hdfs file
// Now this is not thread-safe.
class HdfsInputStream : public io::SeekableInputStream {
public:
    HdfsInputStream(hdfsFS fs, hdfsFile file, const std::string& file_name)
            : _fs(fs), _file(file), _file_name(file_name), _offset(0), _file_size(0) {}

    ~HdfsInputStream() override;

    StatusOr<int64_t> read(void* data, int64_t size) override;
    StatusOr<int64_t> get_size() override;
    StatusOr<int64_t> position() override { return _offset; }
    StatusOr<std::unique_ptr<io::NumericStatistics>> get_numeric_statistics() override;
    Status seek(int64_t offset) override;
    void set_size(int64_t size) override;

private:
    hdfsFS _fs;
    hdfsFile _file;
    std::string _file_name;
    int64_t _offset;
    int64_t _file_size;
};

HdfsInputStream::~HdfsInputStream() {
    auto ret = call_hdfs_scan_function_in_pthread([this]() {
        int r = hdfsCloseFile(this->_fs, this->_file);
        if (r == 0) {
            return Status::OK();
        } else {
            return Status::IOError("close error, file: {}"_format(_file_name));
        }
    });
    Status st = ret->get_future().get();
    PLOG_IF(ERROR, !st.ok()) << "close " << _file_name << " failed";
}

StatusOr<int64_t> HdfsInputStream::read(void* data, int64_t size) {
    if (UNLIKELY(size > std::numeric_limits<tSize>::max())) {
        size = std::numeric_limits<tSize>::max();
    }
    tSize r = hdfsPread(_fs, _file, _offset, data, static_cast<tSize>(size));
    if (r == -1) {
        return Status::IOError(fmt::format("fail to hdfsPread {}: {}", _file_name, get_hdfs_err_msg()));
    }
    _offset += r;
    return r;
}

Status HdfsInputStream::seek(int64_t offset) {
    if (offset < 0) return Status::InvalidArgument(fmt::format("Invalid offset {}", offset));
    _offset = offset;
    return Status::OK();
}

StatusOr<int64_t> HdfsInputStream::get_size() {
    if (_file_size == 0) {
        auto ret = call_hdfs_scan_function_in_pthread([this] {
            auto info = hdfsGetPathInfo(_fs, _file_name.c_str());
            if (UNLIKELY(info == nullptr)) {
                return Status::InternalError(fmt::format("hdfsGetPathInfo failed, file={}", _file_name));
            }
            this->_file_size = info->mSize;
            hdfsFreeFileInfo(info, 1);
            return Status::OK();
        });
        Status st = ret->get_future().get();
        if (!st.ok()) return st;
    }
    return _file_size;
}

void HdfsInputStream::set_size(int64_t value) {
    _file_size = value;
}

StatusOr<std::unique_ptr<io::NumericStatistics>> HdfsInputStream::get_numeric_statistics() {
    auto statistics = std::make_unique<io::NumericStatistics>();
    io::NumericStatistics* stats = statistics.get();
    auto ret = call_hdfs_scan_function_in_pthread([this, stats] {
        struct hdfsReadStatistics* hdfs_statistics = nullptr;
        auto r = hdfsFileGetReadStatistics(_file, &hdfs_statistics);
        if (r != 0) return Status::InternalError(fmt::format("hdfsFileGetReadStatistics failed: {}", r));
        stats->reserve(4);
        stats->append("TotalBytesRead", hdfs_statistics->totalBytesRead);
        stats->append("TotalLocalBytesRead", hdfs_statistics->totalLocalBytesRead);
        stats->append("TotalShortCircuitBytesRead", hdfs_statistics->totalShortCircuitBytesRead);
        stats->append("TotalZeroCopyBytesRead", hdfs_statistics->totalZeroCopyBytesRead);
        hdfsFileFreeReadStatistics(hdfs_statistics);
        return Status::OK();
    });
    Status st = ret->get_future().get();
    if (!st.ok()) return st;
    return std::move(statistics);
}

class HDFSWritableFile : public WritableFile {
public:
    HDFSWritableFile(hdfsFS fs, hdfsFile file, const std::string& path, size_t offset)
            : _fs(fs), _file(file), _path(path), _offset(offset), _closed(false) {}

    ~HDFSWritableFile() override { (void)HDFSWritableFile::close(); }

    Status append(const Slice& data) override;

    Status appendv(const Slice* data, size_t cnt) override;

    Status close() override;

    Status pre_allocate(uint64_t size) override { return Status::NotSupported("HDFS file pre_allocate"); }

    Status flush(FlushMode mode) override {
        int status = hdfsHFlush(_fs, _file);
        return status == 0 ? Status::OK()
                           : Status::InternalError(strings::Substitute("HDFS file flush error $0", _path));
    }

    Status sync() override {
        int status = hdfsHSync(_fs, _file);
        return status == 0 ? Status::OK()
                           : Status::InternalError(strings::Substitute("HDFS file sync error $0", _path));
    }

    uint64_t size() const override { return _offset; }

    const std::string& filename() const override { return _path; }

private:
    hdfsFS _fs;
    hdfsFile _file;
    std::string _path;
    size_t _offset;
    bool _closed;
};

Status HDFSWritableFile::append(const Slice& data) {
    tSize r = hdfsWrite(_fs, _file, data.data, data.size);
    if (r != data.size) {
        auto error_msg =
                "Fail to append {}, expect written size: {}, actual written size {} "_format(_path, data.size, r);
        LOG(WARNING) << error_msg;
        return Status::IOError(error_msg);
    }
    _offset += data.size;
    return Status::OK();
}

Status HDFSWritableFile::appendv(const Slice* data, size_t cnt) {
    for (size_t i = 0; i < cnt; i++) {
        RETURN_IF_ERROR(append(data[i]));
    }
    return Status::OK();
}

Status HDFSWritableFile::close() {
    if (_closed) {
        return Status::OK();
    }
    auto ret = call_hdfs_scan_function_in_pthread([this]() {
        // If we open a file and close it immediately here (before this file is flushed to the disk),
        // hdfs cannot find the file and will cause BE crash.
        // To avoid this, before closing the file, we need to call file sync.
        int r = hdfsHSync(_fs, _file);
        if (r != 0) {
            return Status::IOError("sync error, file: {}"_format(_path));
        }

        r = hdfsCloseFile(_fs, _file);
        if (r == 0) {
            return Status::OK();
        } else {
            return Status::IOError("close error, file: {}"_format(_path));
        }
    });
    Status st = ret->get_future().get();
    PLOG_IF(ERROR, !st.ok()) << "close " << _path << " failed";
    _closed = true;
    return st;
}

class HdfsFileSystem : public FileSystem {
public:
    HdfsFileSystem(const FSOptions& options) : _options(options) {}
    ~HdfsFileSystem() override = default;

    HdfsFileSystem(const HdfsFileSystem&) = delete;
    void operator=(const HdfsFileSystem&) = delete;
    HdfsFileSystem(HdfsFileSystem&&) = delete;
    void operator=(HdfsFileSystem&&) = delete;

    Type type() const override { return HDFS; }

    StatusOr<std::unique_ptr<RandomAccessFile>> new_random_access_file(const std::string& path) override;

    StatusOr<std::unique_ptr<RandomAccessFile>> new_random_access_file(const RandomAccessFileOptions& opts,
                                                                       const std::string& path) override;

    StatusOr<std::unique_ptr<SequentialFile>> new_sequential_file(const std::string& path) override;

    StatusOr<std::unique_ptr<WritableFile>> new_writable_file(const std::string& path) override;

    StatusOr<std::unique_ptr<WritableFile>> new_writable_file(const WritableFileOptions& opts,
                                                              const std::string& path) override;

    Status path_exists(const std::string& path) override;

    Status get_children(const std::string& dir, std::vector<std::string>* file) override {
        return Status::NotSupported("HdfsFileSystem::get_children");
    }

    Status list_path(const std::string& dir, std::vector<FileStatus>* result) override;

    Status iterate_dir(const std::string& dir, const std::function<bool(std::string_view)>& cb) override {
        return Status::NotSupported("HdfsFileSystem::iterate_dir");
    }

    Status delete_file(const std::string& path) override { return Status::NotSupported("HdfsFileSystem::delete_file"); }

    Status create_dir(const std::string& dirname) override {
        return Status::NotSupported("HdfsFileSystem::create_dir");
    }

    Status create_dir_if_missing(const std::string& dirname, bool* created) override {
        return Status::NotSupported("HdfsFileSystem::create_dir_if_missing");
    }

    Status create_dir_recursive(const std::string& dirname) override {
        return Status::NotSupported("HdfsFileSystem::create_dir_recursive");
    }

    Status delete_dir(const std::string& dirname) override {
        return Status::NotSupported("HdfsFileSystem::delete_dir");
    }

    Status delete_dir_recursive(const std::string& dirname) override {
        return Status::NotSupported("HdfsFileSystem::delete_dir_recursive");
    }

    Status sync_dir(const std::string& dirname) override { return Status::NotSupported("HdfsFileSystem::sync_dir"); }

    StatusOr<bool> is_directory(const std::string& path) override {
        return Status::NotSupported("HdfsFileSystem::is_directory");
    }

    Status canonicalize(const std::string& path, std::string* file) override {
        return Status::NotSupported("HdfsFileSystem::canonicalize");
    }

    StatusOr<uint64_t> get_file_size(const std::string& path) override {
        return Status::NotSupported("HdfsFileSystem::get_file_size");
    }

    StatusOr<uint64_t> get_file_modified_time(const std::string& path) override {
        return Status::NotSupported("HdfsFileSystem::get_file_modified_time");
    }

    Status rename_file(const std::string& src, const std::string& target) override;

    Status link_file(const std::string& old_path, const std::string& new_path) override {
        return Status::NotSupported("HdfsFileSystem::link_file");
    }

private:
    Status _path_exists(hdfsFS fs, const std::string& path);

    FSOptions _options;
};

Status HdfsFileSystem::path_exists(const std::string& path) {
    std::string namenode;
    RETURN_IF_ERROR(get_namenode_from_path(path, &namenode));
    HdfsFsHandle handle;
    RETURN_IF_ERROR(HdfsFsCache::instance()->get_connection(namenode, &handle, _options));
    if (handle.type != HdfsFsHandle::Type::HDFS) {
        return Status::InvalidArgument("invalid hdfs path, path={}"_format(path));
    }
    return _path_exists(handle.hdfs_fs, path);
}

Status HdfsFileSystem::list_path(const std::string& dir, std::vector<FileStatus>* result) {
    std::string namenode;
    RETURN_IF_ERROR(get_namenode_from_path(dir, &namenode));
    HdfsFsHandle handle;
    RETURN_IF_ERROR(HdfsFsCache::instance()->get_connection(namenode, &handle, _options));
    if (handle.type != HdfsFsHandle::Type::HDFS) {
        return Status::InvalidArgument("invalid hdfs path {}"_format(dir));
    }
    Status status = _path_exists(handle.hdfs_fs, dir);
    if (!status.ok()) {
        return status;
    }

    hdfsFileInfo* fileinfo;
    int numEntries;
    fileinfo = hdfsListDirectory(handle.hdfs_fs, dir.data(), &numEntries);
    if (fileinfo == nullptr) {
        return Status::InvalidArgument("hdfs list directory error {}"_format(dir));
    }
    for (int i = 0; i < numEntries && fileinfo; ++i) {
        // obj_key.data() + uri.key().size(), obj_key.size() - uri.key().size()
        int32_t dir_size;
        if (dir[dir.size() - 1] == '/') {
            dir_size = dir.size();
        } else {
            dir_size = dir.size() + 1;
        }
        std::string_view name(fileinfo[i].mName + dir_size);
        bool is_dir = fileinfo[i].mKind == tObjectKind::kObjectKindDirectory;
        int64_t file_size = fileinfo[i].mSize;
        result->emplace_back(std::move(name), is_dir, file_size);
    }
    if (fileinfo) {
        hdfsFreeFileInfo(fileinfo, numEntries);
    }
    return Status::OK();
}

Status HdfsFileSystem::_path_exists(hdfsFS fs, const std::string& path) {
    int status = hdfsExists(fs, path.data());
    return status == 0 ? Status::OK() : Status::NotFound(path);
}

StatusOr<std::unique_ptr<WritableFile>> HdfsFileSystem::new_writable_file(const std::string& path) {
    return HdfsFileSystem::new_writable_file(WritableFileOptions(), path);
}

StatusOr<std::unique_ptr<WritableFile>> HdfsFileSystem::new_writable_file(const WritableFileOptions& opts,
                                                                          const std::string& path) {
    std::string namenode;
    RETURN_IF_ERROR(get_namenode_from_path(path, &namenode));
    HdfsFsHandle handle;
    RETURN_IF_ERROR(HdfsFsCache::instance()->get_connection(namenode, &handle, _options));
    if (handle.type != HdfsFsHandle::Type::HDFS) {
        return Status::InvalidArgument("invalid hdfs path, path="_format(path));
    }

    int flags = O_WRONLY;
    if (opts.mode == FileSystem::CREATE_OR_OPEN_WITH_TRUNCATE) {
        if (auto st = _path_exists(handle.hdfs_fs, path); st.ok()) {
            return Status::NotSupported("Cannot truncate a file by hdfs writer, path="_format(path));
        }
    } else if (opts.mode == MUST_CREATE) {
        if (auto st = _path_exists(handle.hdfs_fs, path); st.ok()) {
            return Status::AlreadyExist(path);
        }
    } else if (opts.mode == MUST_EXIST) {
        return Status::NotSupported("Open with MUST_EXIST not supported by hdfs writer");
    } else if (opts.mode == CREATE_OR_OPEN) {
        return Status::NotSupported("Open with CREATE_OR_OPEN not supported by hdfs writer");
    } else {
        auto msg = strings::Substitute("Unsupported open mode $0", opts.mode);
        return Status::NotSupported(msg);
    }

    flags |= O_CREAT;

    int hdfs_write_buffer_size = 0;
    // pass zero to hdfsOpenFile will use the default hdfs_write_buffer_size
    if (_options.result_file_options != nullptr) {
        hdfs_write_buffer_size = _options.result_file_options->write_buffer_size_kb;
    }
    if (_options.export_sink != nullptr && _options.export_sink->__isset.hdfs_write_buffer_size_kb) {
        hdfs_write_buffer_size = _options.export_sink->hdfs_write_buffer_size_kb;
    }
    if (_options.upload != nullptr && _options.upload->__isset.hdfs_write_buffer_size_kb) {
        hdfs_write_buffer_size = _options.upload->__isset.hdfs_write_buffer_size_kb;
    }

    hdfsFile file = hdfsOpenFile(handle.hdfs_fs, path.c_str(), flags, hdfs_write_buffer_size, 0, 0);
    if (file == nullptr) {
        return Status::InternalError(fmt::format("hdfsOpenFile failed, file={}", path));
    }
    return std::make_unique<HDFSWritableFile>(handle.hdfs_fs, file, path, 0);
}

StatusOr<std::unique_ptr<SequentialFile>> HdfsFileSystem::new_sequential_file(const std::string& path) {
    std::string namenode;
    RETURN_IF_ERROR(get_namenode_from_path(path, &namenode));
    HdfsFsHandle handle;
    RETURN_IF_ERROR(HdfsFsCache::instance()->get_connection(namenode, &handle, _options));
    if (handle.type != HdfsFsHandle::Type::HDFS) {
        return Status::InvalidArgument("invalid hdfs path, path={}"_format(path));
    }
    // pass zero to hdfsOpenFile will use the default hdfs_read_buffer_size
    int hdfs_read_buffer_size = 0;
    if (_options.scan_range_params != nullptr && _options.scan_range_params->__isset.hdfs_read_buffer_size_kb) {
        hdfs_read_buffer_size = _options.scan_range_params->hdfs_read_buffer_size_kb;
    }
    if (_options.download != nullptr && _options.download->__isset.hdfs_read_buffer_size_kb) {
        hdfs_read_buffer_size = _options.download->hdfs_read_buffer_size_kb;
    }
    hdfsFile file = hdfsOpenFile(handle.hdfs_fs, path.c_str(), O_RDONLY, hdfs_read_buffer_size, 0, 0);
    if (file == nullptr) {
        return Status::InternalError("hdfsOpenFile failed, path={}"_format(path));
    }
    auto stream = std::make_shared<HdfsInputStream>(handle.hdfs_fs, file, path);
    return std::make_unique<SequentialFile>(std::move(stream), path);
}

StatusOr<std::unique_ptr<RandomAccessFile>> HdfsFileSystem::new_random_access_file(const std::string& path) {
    return HdfsFileSystem::new_random_access_file(RandomAccessFileOptions(), path);
}

StatusOr<std::unique_ptr<RandomAccessFile>> HdfsFileSystem::new_random_access_file(const RandomAccessFileOptions& opts,
                                                                                   const std::string& path) {
    std::string namenode;
    RETURN_IF_ERROR(get_namenode_from_path(path, &namenode));
    HdfsFsHandle handle;
    RETURN_IF_ERROR(HdfsFsCache::instance()->get_connection(namenode, &handle, _options));
    if (handle.type != HdfsFsHandle::Type::HDFS) {
        return Status::InvalidArgument("invalid hdfs path, path={}"_format(path));
    }
    // pass zero to hdfsOpenFile will use the default hdfs_read_buffer_size
    int hdfs_read_buffer_size = 0;
    if (_options.scan_range_params != nullptr && _options.scan_range_params->__isset.hdfs_read_buffer_size_kb) {
        hdfs_read_buffer_size = _options.scan_range_params->hdfs_read_buffer_size_kb;
    }
    if (_options.download != nullptr && _options.download->__isset.hdfs_read_buffer_size_kb) {
        hdfs_read_buffer_size = _options.download->hdfs_read_buffer_size_kb;
    }
    hdfsFile file = hdfsOpenFile(handle.hdfs_fs, path.c_str(), O_RDONLY, hdfs_read_buffer_size, 0, 0);
    if (file == nullptr) {
        return Status::InternalError("hdfsOpenFile failed, path={}"_format(path));
    }
    auto stream = std::make_shared<HdfsInputStream>(handle.hdfs_fs, file, path);
    return std::make_unique<RandomAccessFile>(std::move(stream), path);
}

Status HdfsFileSystem::rename_file(const std::string& src, const std::string& target) {
    std::string namenode;
    RETURN_IF_ERROR(get_namenode_from_path(src, &namenode));
    HdfsFsHandle handle;
    RETURN_IF_ERROR(HdfsFsCache::instance()->get_connection(namenode, &handle, _options));
    if (handle.type != HdfsFsHandle::Type::HDFS) {
        return Status::InvalidArgument("invalid hdfs path {}"_format(src));
    }
    int ret = hdfsRename(handle.hdfs_fs, src.data(), target.data());
    if (ret != 0) {
        return Status::InvalidArgument("rename file from {} to {} error"_format(src, target));
    }
    return Status::OK();
}

std::unique_ptr<FileSystem> new_fs_hdfs(const FSOptions& options) {
    return std::make_unique<HdfsFileSystem>(options);
}

} // namespace starrocks
