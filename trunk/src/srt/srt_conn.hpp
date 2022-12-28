//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRT_CONN_H
#define SRT_CONN_H

#include <srs_core.hpp>

#include "stringex.hpp"
#include <srt/srt.h>
#include <thread>
#include <memory>
#include <string>
#include <vector>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_config.hpp>

#define ERR_SRT_MODE  0x00
#define PULL_SRT_MODE 0x01
#define PUSH_SRT_MODE 0x02

bool is_streamid_valid(const std::string& streamid);
bool get_key_value(const std::string& info, std::string& key, std::string& value);
bool get_streamid_info(const std::string& streamid, int& mode, std::string& vhost, std::string& url_subpash);

class srt_conn {
public:
    srt_conn(SRTSOCKET conn_fd, const std::string& streamid);
    ~srt_conn();

    void close();
    SRTSOCKET get_conn();
    int get_mode();
    std::string get_streamid();
    std::string get_path();
    std::string get_subpath();
    std::string get_vhost();
    int read(unsigned char* data, int len);
    int write(unsigned char* data, int len);

    void update_timestamp(long long now_ts);
    long long get_last_ts();
    int get_write_fail_count();

private:
    SRTSOCKET _conn_fd;
    std::string _streamid;
    std::string _url_path;
    std::string _url_subpath;
    std::string _vhost;
    int _mode;
    long long _update_timestamp;
    int write_fail_cnt_;
};

typedef std::shared_ptr<srt_conn> SRT_CONN_PTR;

#endif //SRT_CONN_H
