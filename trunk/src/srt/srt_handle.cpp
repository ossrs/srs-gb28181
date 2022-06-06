//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include "srt_handle.hpp"
#include "time_help.hpp"
#include "srt_log.hpp"

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_config.hpp>

#include <srt/udt.h>
#include <stdio.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <assert.h>
#include <list>

static bool MONITOR_STATICS_ENABLE = false;
static long long MONITOR_TIMEOUT = 5000;
const unsigned int DEF_DATA_SIZE = 188*7;
const long long CHECK_ALIVE_INTERVAL = 5*1000;
const long long CHECK_ALIVE_TIMEOUT = 5*1000;
static const int SRT_WRTIE_FAIL_MAX = 10;

long long srt_now_ms = 0;

srt_handle::srt_handle(int pollid):_handle_pollid(pollid)
    ,_last_timestamp(0)
    ,_last_check_alive_ts(0) {
}

srt_handle::~srt_handle() {

}

void srt_handle::debug_statics(SRTSOCKET srtsocket, const std::string& streamid) {
    SRT_TRACEBSTATS mon;
    srt_bstats(srtsocket, &mon, 1);
    std::ostringstream output;
    long long now_ul = now_ms();

    if (!MONITOR_STATICS_ENABLE) {
        return;
    }
    if (_last_timestamp == 0) {
        _last_timestamp = now_ul;
        return;
    }

    if ((now_ul - _last_timestamp) < MONITOR_TIMEOUT) {
        return;
    }
    _last_timestamp = now_ul;
    output << "======= SRT STATS: sid=" << streamid << std::endl;
    output << "PACKETS     SENT: " << std::setw(11) << mon.pktSent            << "  RECEIVED:   " << std::setw(11) << mon.pktRecv              << std::endl;
    output << "LOST PKT    SENT: " << std::setw(11) << mon.pktSndLoss         << "  RECEIVED:   " << std::setw(11) << mon.pktRcvLoss           << std::endl;
    output << "REXMIT      SENT: " << std::setw(11) << mon.pktRetrans         << "  RECEIVED:   " << std::setw(11) << mon.pktRcvRetrans        << std::endl;
    output << "DROP PKT    SENT: " << std::setw(11) << mon.pktSndDrop         << "  RECEIVED:   " << std::setw(11) << mon.pktRcvDrop           << std::endl;
    output << "RATE     SENDING: " << std::setw(11) << mon.mbpsSendRate       << "  RECEIVING:  " << std::setw(11) << mon.mbpsRecvRate         << std::endl;
    output << "BELATED RECEIVED: " << std::setw(11) << mon.pktRcvBelated      << "  AVG TIME:   " << std::setw(11) << mon.pktRcvAvgBelatedTime << std::endl;
    output << "REORDER DISTANCE: " << std::setw(11) << mon.pktReorderDistance << std::endl;
    output << "WINDOW      FLOW: " << std::setw(11) << mon.pktFlowWindow      << "  CONGESTION: " << std::setw(11) << mon.pktCongestionWindow  << "  FLIGHT: " << std::setw(11) << mon.pktFlightSize << std::endl;
    output << "LINK         RTT: " << std::setw(9)  << mon.msRTT            << "ms  BANDWIDTH:  " << std::setw(7)  << mon.mbpsBandwidth    << "Mb/s " << std::endl;
    output << "BUFFERLEFT:  SND: " << std::setw(11) << mon.byteAvailSndBuf    << "  RCV:        " << std::setw(11) << mon.byteAvailRcvBuf      << std::endl;

    srt_log_trace("\r\n%s", output.str().c_str());
    return;
}

void srt_handle::add_new_puller(SRT_CONN_PTR conn_ptr, std::string stream_id) {
    _conn_map.insert(std::make_pair(conn_ptr->get_conn(), conn_ptr));

    auto iter = _streamid_map.find(stream_id);
    if (iter == _streamid_map.end()) {
        std::unordered_map<SRTSOCKET, SRT_CONN_PTR> srtsocket_map;
        srtsocket_map.insert(std::make_pair(conn_ptr->get_conn(), conn_ptr));

        _streamid_map.insert(std::make_pair(stream_id, srtsocket_map));
        srt_log_trace("add new puller fd:%d, streamid:%s", conn_ptr->get_conn(), stream_id.c_str());
    } else {
        iter->second.insert(std::make_pair(conn_ptr->get_conn(), conn_ptr));
        srt_log_trace("add new puller fd:%d, streamid:%s, size:%d", 
            conn_ptr->get_conn(), stream_id.c_str(), iter->second.size());
    }

    return;
}

void srt_handle::close_pull_conn(SRTSOCKET srtsocket, std::string stream_id) {
    srt_log_trace("close_pull_conn read_fd=%d, streamid=%s", srtsocket, stream_id.c_str());
    srt_epoll_remove_usock(_handle_pollid, srtsocket);

    auto streamid_iter = _streamid_map.find(stream_id);
    if (streamid_iter != _streamid_map.end()) {
        if (streamid_iter->second.size() == 0) {
            _streamid_map.erase(stream_id);
        } else if (streamid_iter->second.size() == 1) {
            streamid_iter->second.erase(srtsocket);
            _streamid_map.erase(stream_id);
        } else {
            streamid_iter->second.erase(srtsocket);
        }
    } else {
        assert(0);
    }

    auto conn_iter = _conn_map.find(srtsocket);
    if (conn_iter != _conn_map.end()) {
        _conn_map.erase(conn_iter);
        return;
    } else {
        assert(0);
    }
    
    return;
}

SRT_CONN_PTR srt_handle::get_srt_conn(SRTSOCKET conn_srt_socket) {
    SRT_CONN_PTR ret_conn;

    auto iter = _conn_map.find(conn_srt_socket);
    if (iter == _conn_map.end()) {
        return ret_conn;
    }

    ret_conn = iter->second;

    return ret_conn;
}

void srt_handle::add_newconn(SRT_CONN_PTR conn_ptr, int events) {
    int val_i;
    int opt_len = sizeof(int);

    int64_t val_i64;
    int opt64_len = sizeof(int64_t);

    srt_getsockopt(conn_ptr->get_conn(), 0, SRTO_LATENCY, &val_i, &opt_len);
    srt_log_trace("srto SRTO_LATENCY=%d", val_i);

    srt_getsockopt(conn_ptr->get_conn(), 0, SRTO_PEERLATENCY, &val_i, &opt_len);
    srt_log_trace("srto SRTO_PEERLATENCY=%d", val_i);
    srt_getsockopt(conn_ptr->get_conn(), 0, SRTO_RCVLATENCY, &val_i, &opt_len);
    srt_log_trace("srto SRTO_RCVLATENCY=%d", val_i);

    srt_getsockopt(conn_ptr->get_conn(), 0, SRTO_SNDBUF, &val_i, &opt_len);
    srt_log_trace("srto SRTO_SNDBUF=%d", val_i);
    srt_getsockopt(conn_ptr->get_conn(), 0, SRTO_RCVBUF, &val_i, &opt_len);
    srt_log_trace("srto SRTO_RCVBUF=%d", val_i);
    srt_getsockopt(conn_ptr->get_conn(), 0, SRTO_MAXBW, &val_i64, &opt64_len);
    srt_log_trace("srto SRTO_MAXBW=%d", val_i64);
    srt_log_trace("srt mix_correct is %s.", _srs_config->get_srt_mix_correct() ? "enable" : "disable");
    srt_log_trace("srt h264 sei filter is %s.", _srs_config->get_srt_sei_filter() ? "enable" : "disable");

    if (conn_ptr->get_mode() == PULL_SRT_MODE) {
        add_new_puller(conn_ptr, conn_ptr->get_path());
    } else {
        if(add_new_pusher(conn_ptr) == false) {
            srt_log_trace("push connection is repeated and rejected, fd:%d, streamid:%s",
                conn_ptr->get_conn(), conn_ptr->get_streamid().c_str());
            conn_ptr->close();
            return;
        }
    }
    srt_log_trace("new conn added fd:%d, event:0x%08x", conn_ptr->get_conn(), events);
    int ret = srt_epoll_add_usock(_handle_pollid, conn_ptr->get_conn(), &events);
    if (ret < 0) {
        srt_log_error("srt handle run add epoll error:%d", ret);
        return;
    }

    return;
}

void srt_handle::handle_push_data(SRT_SOCKSTATUS status, const std::string& path, const std::string& subpath, SRTSOCKET conn_fd) {
    SRT_CONN_PTR srt_conn_ptr;
    unsigned char data[DEF_DATA_SIZE];
    int ret;
    srt_conn_ptr = get_srt_conn(conn_fd);

    if (!srt_conn_ptr) {
        srt_log_error("handle_push_data fd:%d fail to find srt connection.", conn_fd);
        return;
    }

    if (status != SRTS_CONNECTED) {
        srt_log_error("handle_push_data error status:%d fd:%d", status, conn_fd);
        close_push_conn(conn_fd);
        return;
    }

    ret = srt_conn_ptr->read(data, DEF_DATA_SIZE);
    if (ret <= 0) {
        srt_log_error("handle_push_data srt connect read error:%d, fd:%d", ret, conn_fd);
        close_push_conn(conn_fd);
        return;
    }

    srt_conn_ptr->update_timestamp(srt_now_ms);

    srt2rtmp::get_instance()->insert_data_message(data, ret, subpath);
    {
        std::unique_lock<std::mutex> locker(srt2rtmp::_srt_error_mutex);
        if (srt2rtmp::_srt_error_map.count(subpath) == 1) {
            int err_code = srt2rtmp::_srt_error_map[subpath];
            if (err_code != ERROR_SUCCESS) {
                close_push_conn(conn_fd);
                srt_log_error("handle_push_data srt to rtmp error:%d, fd:%d", err_code,conn_fd);
                //todo: reset to next use, maybe update by srt2rtmp::cycle again
                srt2rtmp::_srt_error_map[subpath] = ERROR_SUCCESS;
                return;
            }
        }
    }
    
    //send data to subscriber(players)
    //streamid, play map<SRTSOCKET, SRT_CONN_PTR>
    auto streamid_iter = _streamid_map.find(path);
    if (streamid_iter == _streamid_map.end()) {//no puler
        srt_log_info("receive data size(%d) from pusher(%d) but no puller", ret, conn_fd);
        return;
    }
    srt_log_info("receive data size(%d) from pusher(%d) to pullers, count:%d", 
        ret, conn_fd, streamid_iter->second.size());

    std::vector<SRTSOCKET> remove_vec;
    for (auto puller_iter = streamid_iter->second.begin();
        puller_iter != streamid_iter->second.end();
        puller_iter++) {
        auto player_conn = puller_iter->second;
        if (!player_conn) {
            srt_log_error("handle_push_data get srt connect error from fd:%d", puller_iter->first);
            continue;
        }
        int write_ret = player_conn->write(data, ret);
        srt_log_info("send data size(%d) to puller fd:%d", write_ret, puller_iter->first);
        if (write_ret > 0) {
            puller_iter->second->update_timestamp(srt_now_ms);
        } else {
            if (player_conn->get_write_fail_count() > SRT_WRTIE_FAIL_MAX) {
                remove_vec.push_back(puller_iter->first);
            }
        }
    }

    for (auto item : remove_vec) {
        streamid_iter->second.erase(item);
        if (streamid_iter->second.empty()) {
            _streamid_map.erase(streamid_iter);
        }
    }

    return;
}

void srt_handle::check_alive() {
    long long diff_t;
    std::list<SRT_CONN_PTR> conn_list;

    if (_last_check_alive_ts == 0) {
        _last_check_alive_ts = srt_now_ms;
        return;
    }
    diff_t = srt_now_ms - _last_check_alive_ts;
    if (diff_t < CHECK_ALIVE_INTERVAL) {
        return;
    }

    for (auto conn_iter = _conn_map.begin();
        conn_iter != _conn_map.end();
        conn_iter++)
    {
        long long timeout = srt_now_ms - conn_iter->second->get_last_ts();
        if (timeout > CHECK_ALIVE_TIMEOUT) {
            conn_list.push_back(conn_iter->second);
        }
    }

    for (auto del_iter = conn_list.begin();
        del_iter != conn_list.end();
        del_iter++)
    {
        SRT_CONN_PTR conn_ptr = *del_iter;
        if (conn_ptr->get_mode() == PUSH_SRT_MODE) {
            srt_log_warn("check alive close pull connection fd:%d, streamid:%s",
                conn_ptr->get_conn(), conn_ptr->get_subpath().c_str());
            close_push_conn(conn_ptr->get_conn());
        } else if (conn_ptr->get_mode() == PULL_SRT_MODE) {
            srt_log_warn("check alive close pull connection fd:%d, streamid:%s",
                conn_ptr->get_conn(), conn_ptr->get_path().c_str());
            close_pull_conn(conn_ptr->get_conn(), conn_ptr->get_path());
        } else {
            srt_log_error("check_alive get unkown srt mode:%d, fd:%d", 
                conn_ptr->get_mode(), conn_ptr->get_conn());
            assert(0);
        }
    }
}

void srt_handle::close_push_conn(SRTSOCKET srtsocket) {
    auto iter = _conn_map.find(srtsocket);

    if (iter != _conn_map.end()) {
        SRT_CONN_PTR conn_ptr = iter->second;
        auto push_iter = _push_conn_map.find(conn_ptr->get_path());
        if (push_iter != _push_conn_map.end()) {
            _push_conn_map.erase(push_iter);
        }
        _conn_map.erase(iter);
        srt2rtmp::get_instance()->insert_ctrl_message(SRT_MSG_CLOSE_TYPE, conn_ptr->get_subpath());
        conn_ptr->close();
    }

    srt_epoll_remove_usock(_handle_pollid, srtsocket);
    
    return;
}

bool srt_handle::add_new_pusher(SRT_CONN_PTR conn_ptr) {
    auto push_iter = _push_conn_map.find(conn_ptr->get_path());
    if (push_iter != _push_conn_map.end()) {
        return false;
    }
    _push_conn_map.insert(std::make_pair(conn_ptr->get_path(), conn_ptr));
    _conn_map.insert(std::make_pair(conn_ptr->get_conn(), conn_ptr));
    srt_log_trace("srt_handle add new pusher streamid:%s, subpath:%s, sid:%s",
        conn_ptr->get_streamid().c_str(), conn_ptr->get_subpath().c_str(), conn_ptr->get_path().c_str());
    return true;
}

void srt_handle::handle_pull_data(SRT_SOCKSTATUS status, const std::string& subpath, SRTSOCKET conn_fd) {
    srt_log_info("handle_pull_data status:%d, subpath:%s, fd:%d",
        status, subpath.c_str(), conn_fd);
    auto conn_ptr = get_srt_conn(conn_fd);
    if (!conn_ptr) {
        srt_log_error("handle_pull_data fail to find fd(%d)", conn_fd);
        assert(0);
        return;
    }
    conn_ptr->update_timestamp(srt_now_ms);
}

void srt_handle::handle_srt_socket(SRT_SOCKSTATUS status, SRTSOCKET conn_fd)
{
    auto conn_ptr = get_srt_conn(conn_fd);
    if (!conn_ptr) {
        if (status != SRTS_CLOSED) {
            srt_log_error("handle_srt_socket find srt connection error, fd:%d, status:%d", 
                conn_fd, status);
        }
        return;
    }

    std::string path = conn_ptr->get_path();
    std::string subpath = conn_ptr->get_subpath();

    int mode = conn_ptr->get_mode();    
    if (mode == PUSH_SRT_MODE) {
        switch (status)
        {
            case SRTS_CONNECTED:
            {
                handle_push_data(status, path, subpath, conn_fd);
                break;
            }
            case SRTS_BROKEN:
            {
                srt_log_warn("srt push disconnected event fd:%d, streamid:%s",
                    conn_fd, conn_ptr->get_streamid().c_str());
                close_push_conn(conn_fd);
                break;
            }
            default:
                srt_log_error("push mode unkown status:%d, fd:%d", status, conn_fd);
                break;
        }
    } else if (mode ==  PULL_SRT_MODE) {
        switch (status)
        {
        case SRTS_CONNECTED:
        {
            handle_pull_data(status, subpath, conn_fd);
            break;
        }
        case SRTS_BROKEN:
        {
            srt_log_warn("srt pull disconnected fd:%d, streamid:%s",
                conn_fd, conn_ptr->get_streamid().c_str());
            close_pull_conn(conn_fd, path);
            break;
        }
        default:
            srt_log_error("pull mode unkown status:%d, fd:%d", status, conn_fd);
            break;
        }
    } else {
        assert(0);
    }
    return;
}
