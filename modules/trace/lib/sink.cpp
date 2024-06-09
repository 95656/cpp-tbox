/*
 *     .============.
 *    //  M A K E  / \
 *   //  C++ DEV  /   \
 *  //  E A S Y  /  \/ \
 * ++ ----------.  \/\  .
 *  \\     \     \ /\  /
 *   \\     \     \   /
 *    \\     \     \ /
 *     -============'
 *
 * Copyright (c) 2024 Hevake and contributors, all rights reserved.
 *
 * This file is part of cpp-tbox (https://github.com/cpp-main/cpp-tbox)
 * Use of this source code is governed by MIT license that can be found
 * in the LICENSE file in the root of the source tree. All contributing
 * project authors may be found in the CONTRIBUTORS.md file in the root
 * of the source tree.
 */
#include "sink.h"

#include <unistd.h>
#include <fcntl.h>

#include <cstring>
#include <vector>
#include <sstream>

#include <sys/syscall.h>
#include <tbox/base/defines.h>
#include <tbox/base/log.h>
#include <tbox/util/fs.h>
#include <tbox/util/string.h>
#include <tbox/util/scalable_integer.h>

namespace tbox {
namespace trace {

void CommitRecordFunc(const char *name, const char *module, uint32_t line,
                      uint64_t end_timepoint_us, uint64_t duration_us)
{
    Sink::GetInstance().commitRecord(name, module, line, end_timepoint_us, duration_us);
}

#define ENDLINE "\n"

namespace {
std::string GetLocalDateTimeStr()
{
    char timestamp[16]; //! 固定长度16B，"20220414_071023"
    time_t ts_sec = time(nullptr);
    struct tm tm;
    localtime_r(&ts_sec, &tm);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
    return timestamp;
}
}

Sink& Sink::GetInstance()
{
    static Sink instance;
    return instance;
}

Sink::~Sink()
{
    disable();
}

bool Sink::setPathPrefix(const std::string &path_prefix)
{
    auto striped_path_prefix = util::string::Strip(path_prefix);

    if (striped_path_prefix.empty() || striped_path_prefix.back() == '/') {
        LogWarn("path_prefix '%s' is invalid.", path_prefix.c_str());
        return false;
    }

    dir_path_ = striped_path_prefix + '.' + GetLocalDateTimeStr() + '.' + std::to_string(::getpid());
    name_list_filename_ = dir_path_ + "/names.txt";
    module_list_filename_ = dir_path_ + "/modules.txt";
    thread_list_filename_ = dir_path_ + "/threads.txt";
    CHECK_CLOSE_RESET_FD(curr_record_fd_);

    return true;
}

void Sink::setFileSyncEnable(bool is_enable)
{
    is_file_sync_enabled_ = is_enable;
    CHECK_CLOSE_RESET_FD(curr_record_fd_);
}

bool Sink::enable()
{
    if (is_enabled_)
        return true;

    if (dir_path_.empty()) {
        LogWarn("path_prefix is empty, can't enable.");
        return false;
    }

    using namespace std::placeholders;

    util::AsyncPipe::Config config;
    async_pipe_.initialize(config);
    async_pipe_.setCallback(std::bind(&Sink::onBackendRecvData, this, _1, _2));
    is_enabled_ = true;

    return true;
}

void Sink::disable()
{
    if (is_enabled_) {
        is_enabled_ = false;
        async_pipe_.cleanup();
        CHECK_CLOSE_RESET_FD(curr_record_fd_);
        buffer_.hasReadAll();
    }
}

void Sink::commitRecord(const char *name, const char *module, uint32_t line, uint64_t end_timepoint_us, uint64_t duration_us)
{
    if (!is_enabled_)
        return;

    RecordHeader header = {
        .thread_id = ::syscall(SYS_gettid),
        .end_ts_us = end_timepoint_us,
        .duration_us = duration_us,
        .line = line,
        .name_size = ::strlen(name) + 1,
        .module_size = ::strlen(module) + 1
    };

    async_pipe_.appendLock();
    async_pipe_.appendLockless(&header, sizeof(header));
    async_pipe_.appendLockless(name, header.name_size);
    async_pipe_.appendLockless(module, header.module_size);
    async_pipe_.appendUnlock();
}

void Sink::onBackendRecvData(const void *data, size_t size)
{
    buffer_.append(data, size);

    while (buffer_.readableSize() > sizeof(RecordHeader)) {
        RecordHeader header;
        ::memcpy(&header, buffer_.readableBegin(), sizeof(header));
        //! Q: 为什么不直接引用，像这样 auto *phead = static_cast<const RecordHeader*>(buffer_.readableBegin())
        //     而是要先copy到header再使用？
        //! A: 因为直接指针引用会有对齐的问题。

        //! 如果长度不够，就跳过，等下一次
        if ((header.name_size + header.module_size + sizeof(header)) > buffer_.readableSize())
            break;

        buffer_.hasRead(sizeof(header));

        const char *name = reinterpret_cast<const char*>(buffer_.readableBegin());
        const char *module = name + header.name_size;
        onBackendRecvRecord(header, name, module);

        buffer_.hasRead(header.name_size + header.module_size);
    }
}

void Sink::onBackendRecvRecord(const RecordHeader &record, const char *name, const char *module)
{
    if (!checkAndCreateRecordFile())
        return;

    auto thread_index = allocThreadIndex(record.thread_id);
    auto name_index = allocNameIndex(name, record.line);
    auto module_index = allocModuleIndex(module);
    auto time_diff = record.end_ts_us - last_timepoint_us_;

    constexpr size_t kBufferSize = 40;
    uint8_t buffer[kBufferSize];
    size_t  data_size = 0;

    data_size += util::DumpScalableInteger(time_diff, (buffer + data_size), (kBufferSize - data_size));
    data_size += util::DumpScalableInteger(record.duration_us, (buffer + data_size), (kBufferSize - data_size));
    data_size += util::DumpScalableInteger(thread_index, (buffer + data_size), (kBufferSize - data_size));
    data_size += util::DumpScalableInteger(name_index, (buffer + data_size), (kBufferSize - data_size));
    data_size += util::DumpScalableInteger(module_index, (buffer + data_size), (kBufferSize - data_size));

    auto wsize = ::write(curr_record_fd_, buffer, data_size);
    if (wsize != static_cast<ssize_t>(data_size)) {
        LogErrno(errno, "write record file '%s' fail", curr_record_filename_.c_str());
        return;
    }

    last_timepoint_us_ = record.end_ts_us;
    total_write_size_ += wsize;

    if (total_write_size_ >= record_file_max_size_)
        CHECK_CLOSE_RESET_FD(curr_record_fd_);
}

bool Sink::checkAndCreateRecordFile()
{
    if (curr_record_fd_ >= 0) {
        if (util::fs::IsFileExist(curr_record_filename_))
            return true;
        CHECK_CLOSE_RESET_FD(curr_record_fd_);
    }

    std::string records_path = dir_path_ + "/records";

    //!检查并创建路径
    if (!util::fs::MakeDirectory(records_path, false)) {
        LogErrno(errno, "create directory '%s' fail", records_path);
        return false;
    }

    std::string datatime_str = GetLocalDateTimeStr();
    std::string new_record_filename;
    int postfix = 0;
    do {
        new_record_filename = records_path + '/' + datatime_str + ".bin";
        if (postfix != 0) {
            new_record_filename += '.';
            new_record_filename += std::to_string(postfix);
        }
        ++postfix;
    } while (util::fs::IsFileExist(new_record_filename));    //! 避免在同一秒多次创建日志文件，都指向同一日志名
    curr_record_filename_ = std::move(new_record_filename);

    int flags = O_CREAT | O_WRONLY | O_APPEND;
    if (is_file_sync_enabled_)
        flags |= O_DSYNC;

    curr_record_fd_ = ::open(curr_record_filename_.c_str(), flags, S_IRUSR | S_IWUSR);
    if (curr_record_fd_ < 0) {
        LogErrno(errno, "open record file '%s' fail", curr_record_filename_.c_str());
        return false;
    }

    total_write_size_ = 0;
    last_timepoint_us_ = 0;
    return true;
}

Sink::Index Sink::allocNameIndex(const std::string &name, uint32_t line)
{
    //! 如果文件不存在了，则重写所有的名称列表
    if (!util::fs::IsFileExist(name_list_filename_)) {
        std::vector<std::string> name_vec;
        name_vec.resize(name_to_index_map_.size());
        for (auto &item : name_to_index_map_)
            name_vec[item.second] = item.first;

        std::ostringstream oss;
        for (auto &content: name_vec)
            oss << content << ENDLINE;

        util::fs::WriteStringToTextFile(name_list_filename_, oss.str(), is_file_sync_enabled_);
    }

    std::string content = name + " at L" + std::to_string(line);
    auto iter = name_to_index_map_.find(content);
    if (iter != name_to_index_map_.end())
        return iter->second;

    auto new_index = next_name_index_++;
    name_to_index_map_[content] = new_index;

    util::fs::AppendStringToTextFile(name_list_filename_, content + ENDLINE, is_file_sync_enabled_);
    return new_index;
}

Sink::Index Sink::allocModuleIndex(const std::string &module)
{
    //! 如果文件不存在了，则重写所有的名称列表
    if (!util::fs::IsFileExist(module_list_filename_)) {
        std::vector<std::string> module_vec;
        module_vec.resize(module_to_index_map_.size());
        for (auto &item : module_to_index_map_)
            module_vec[item.second] = item.first;

        std::ostringstream oss;
        for (auto &module : module_vec)
            oss << module << ENDLINE;

        util::fs::WriteStringToTextFile(module_list_filename_, oss.str(), is_file_sync_enabled_);
    }

    auto iter = module_to_index_map_.find(module);
    if (iter != module_to_index_map_.end())
        return iter->second;

    auto new_index = next_module_index_++;
    module_to_index_map_[module] = new_index;

    util::fs::AppendStringToTextFile(module_list_filename_, module + ENDLINE, is_file_sync_enabled_);
    return new_index;
}

Sink::Index Sink::allocThreadIndex(long thread_id)
{
    //! 如果文件不存在了，则重写所有的线程列表
    if (!util::fs::IsFileExist(thread_list_filename_)) {
        std::vector<int> thread_vec;
        thread_vec.resize(thread_to_index_map_.size());
        for (auto &item : thread_to_index_map_)
            thread_vec[item.second] = item.first;

        std::ostringstream oss;
        for (auto thread_id : thread_vec)
            oss << thread_id << ENDLINE;

        util::fs::WriteStringToTextFile(thread_list_filename_, oss.str(), is_file_sync_enabled_);
    }

    auto iter = thread_to_index_map_.find(thread_id);
    if (iter != thread_to_index_map_.end())
        return iter->second;

    auto new_index = next_thread_index_++;
    thread_to_index_map_[thread_id] = new_index;

    util::fs::AppendStringToTextFile(thread_list_filename_, std::to_string(thread_id) + ENDLINE, is_file_sync_enabled_);
    return new_index;
}

}
}
