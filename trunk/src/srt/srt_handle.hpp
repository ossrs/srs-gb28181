//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRT_HANDLE_H
#define SRT_HANDLE_H

#include <srs_core.hpp>

#include <srt/srt.h>

#include <thread>
#include <memory>
#include <unordered_map>
#include <queue>
#include <string.h>
#include <mutex>

#include "srt_conn.hpp"
#include "srt_to_rtmp.hpp"

class srt_handle {
public:
    srt_handle(int pollid);
    ~srt_handle();

    //add new srt connection into epoll event
    void add_newconn(SRT_CONN_PTR conn_ptr, int events);
    //handle recv/send srt socket
    void handle_srt_socket(SRT_SOCKSTATUS status, SRTSOCKET conn_fd);
    //check srt connection whether it's still alive.
    void check_alive();

private:
    //get srt conn object by srt socket
    SRT_CONN_PTR get_srt_conn(SRTSOCKET conn_srt_socket);

    void handle_push_data(SRT_SOCKSTATUS status, const std::string& path, const std::string& subpath, SRTSOCKET conn_fd);
    void handle_pull_data(SRT_SOCKSTATUS status, const std::string& subpath, SRTSOCKET conn_fd);

    //add new puller into puller list and conn_map
    void add_new_puller(SRT_CONN_PTR, std::string stream_id);
    //remove pull srt from play list
    void close_pull_conn(SRTSOCKET srtsocket, std::string stream_id);

    //add new pusher into pusher map: <socket fd, pusher conn ptr>
    bool add_new_pusher(SRT_CONN_PTR conn_ptr);
    //remove push connection and remove epoll
    void close_push_conn(SRTSOCKET srtsocket);

    //debug statics
    void debug_statics(SRTSOCKET srtsocket, const std::string& streamid);

private:
    int _handle_pollid;

    std::unordered_map<SRTSOCKET, SRT_CONN_PTR> _conn_map;//save all srt connection: pull or push

    //save push srt connection for prevent from repeat push connection
    std::unordered_map<std::string, SRT_CONN_PTR> _push_conn_map;//key:streamid, value:SRT_CONN_PTR
    //streamid, play map<SRTSOCKET, SRT_CONN_PTR>
    std::unordered_map<std::string, std::unordered_map<SRTSOCKET, SRT_CONN_PTR>> _streamid_map;

    long long _last_timestamp;
    long long _last_check_alive_ts;
};

#endif //SRT_HANDLE_H
