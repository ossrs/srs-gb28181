//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_utility.hpp>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <arpa/inet.h>
#include <stdlib.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_protocol_io.hpp>

/**
 * resolve the vhost in query string
 * @pram vhost, update the vhost if query contains the vhost.
 * @param app, may contains the vhost in query string format:
 *   app?vhost=request_vhost
 *   app...vhost...request_vhost
 * @param param, the query, for example, ?vhost=xxx
 */
void srs_vhost_resolve(string& vhost, string& app, string& param)
{
    // get original param
    size_t pos = 0;
    if ((pos = app.find("?")) != std::string::npos) {
        param = app.substr(pos);
    }
    
    // filter tcUrl
    app = srs_string_replace(app, ",", "?");
    app = srs_string_replace(app, "...", "?");
    app = srs_string_replace(app, "&&", "?");
    app = srs_string_replace(app, "&", "?");
    app = srs_string_replace(app, "=", "?");
    
    if (srs_string_ends_with(app, "/_definst_")) {
        app = srs_erase_last_substr(app, "/_definst_");
    }
    
    if ((pos = app.find("?")) != std::string::npos) {
        std::string query = app.substr(pos + 1);
        app = app.substr(0, pos);
        
        if ((pos = query.find("vhost?")) != std::string::npos) {
            query = query.substr(pos + 6);
            if (!query.empty()) {
                vhost = query;
            }
        }
    }

    // vhost with params.
    if ((pos = vhost.find("?")) != std::string::npos) {
        vhost = vhost.substr(0, pos);
    }
    
    /* others */
}

void srs_discovery_tc_url(string tcUrl, string& schema, string& host, string& vhost, string& app, string& stream, int& port, string& param)
{
    size_t pos = std::string::npos;
    std::string url = tcUrl;
    
    if ((pos = url.find("://")) != std::string::npos) {
        schema = url.substr(0, pos);
        url = url.substr(schema.length() + 3);
        srs_info("discovery schema=%s", schema.c_str());
    }
    
    if ((pos = url.find("/")) != std::string::npos) {
        host = url.substr(0, pos);
        url = url.substr(host.length() + 1);
        srs_info("discovery host=%s", host.c_str());
    }
    
    port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    if (schema == "https") {
        port = SRS_DEFAULT_HTTPS_PORT;
    }

    if ((pos = host.find(":")) != std::string::npos) {
        srs_parse_hostport(host, host, port);
        srs_info("discovery host=%s, port=%d", host.c_str(), port);
    }
    
    if (url.empty()) {
        app = SRS_CONSTS_RTMP_DEFAULT_APP;
    } else {
        app = url;
    }
    
    vhost = host;
    srs_vhost_resolve(vhost, app, param);
    srs_vhost_resolve(vhost, stream, param);
    
    // Ignore when the param only contains the default vhost.
    if (param == "?vhost=" SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        param = "";
    }
}

void srs_parse_query_string(string q, map<string,string>& query)
{
    // query string flags.
    static vector<string> flags;
    if (flags.empty()) {
        flags.push_back("=");
        flags.push_back(",");
        flags.push_back("&&");
        flags.push_back("&");
        flags.push_back(";");
    }
    
    vector<string> kvs = srs_string_split(q, flags);
    for (int i = 0; i < (int)kvs.size(); i+=2) {
        string k = kvs.at(i);
        string v = (i < (int)kvs.size() - 1)? kvs.at(i+1):"";
        
        query[k] = v;
    }
}

void srs_random_generate(char* bytes, int size)
{
    for (int i = 0; i < size; i++) {
        // the common value in [0x0f, 0xf0]
        bytes[i] = 0x0f + (srs_random() % (256 - 0x0f - 0x0f));
    }
}

std::string srs_random_str(int len)
{
    static string random_table = "01234567890123456789012345678901234567890123456789abcdefghijklmnopqrstuvwxyz";

    string ret;
    ret.reserve(len);
    for (int i = 0; i < len; ++i) {
        ret.append(1, random_table[srs_random() % random_table.size()]);
    }

    return ret;
}

long srs_random()
{
    static bool _random_initialized = false;
    if (!_random_initialized) {
        _random_initialized = true;
        ::srandom((unsigned long)(srs_update_system_time() | (::getpid()<<13)));
    }

    return random();
}

string srs_generate_tc_url(string host, string vhost, string app, int port)
{
    string tcUrl = "rtmp://";
    
    if (vhost == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        tcUrl += host;
    } else {
        tcUrl += vhost;
    }
    
    if (port != SRS_CONSTS_RTMP_DEFAULT_PORT) {
        tcUrl += ":" + srs_int2str(port);
    }
    
    tcUrl += "/" + app;
    
    return tcUrl;
}

string srs_generate_stream_with_query(string host, string vhost, string stream, string param, bool with_vhost)
{
    string url = stream;
    string query = param;

    // If no vhost in param, try to append one.
    string guessVhost;
    if (query.find("vhost=") == string::npos) {
        if (vhost != SRS_CONSTS_RTMP_DEFAULT_VHOST) {
            guessVhost = vhost;
        } else if (!srs_is_ipv4(host)) {
            guessVhost = host;
        }
    }

    // Well, if vhost exists, always append in query string.
    if (!guessVhost.empty() && query.find("vhost=") == string::npos) {
        query += "&vhost=" + guessVhost;
    }

    // If not pass in query, remove it.
    if (!with_vhost) {
        size_t pos = query.find("&vhost=");
        if (pos == string::npos) {
            pos = query.find("vhost=");
        }

        size_t end = query.find("&", pos + 1);
        if (end == string::npos) {
            end = query.length();
        }

        if (pos != string::npos && end != string::npos && end > pos) {
            query = query.substr(0, pos) + query.substr(end);
        }
    }
    
    // Remove the start & when param is empty.
    query = srs_string_trim_start(query, "&");

    // Prefix query with ?.
    if (!query.empty() && !srs_string_starts_with(query, "?")) {
        url += "?";
    }
    
    // Append query to url.
    if (!query.empty()) {
        url += query;
    }
    
    return url;
}

template<typename T>
srs_error_t srs_do_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, T** ppmsg)
{
    srs_error_t err = srs_success;
    
    *ppmsg = NULL;
    T* msg = NULL;
    
    if (type == SrsFrameTypeAudio) {
        SrsMessageHeader header;
        header.initialize_audio(size, timestamp, stream_id);
        
        msg = new T();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else if (type == SrsFrameTypeVideo) {
        SrsMessageHeader header;
        header.initialize_video(size, timestamp, stream_id);
        
        msg = new T();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else if (type == SrsFrameTypeScript) {
        SrsMessageHeader header;
        header.initialize_amf0_script(size, stream_id);
        
        msg = new T();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else {
        return srs_error_new(ERROR_STREAM_CASTER_FLV_TAG, "unknown tag=%#x", (uint8_t)type);
    }
    
    *ppmsg = msg;
    
    return err;
}

srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, SrsSharedPtrMessage** ppmsg)
{
    srs_error_t err = srs_success;
    
    // only when failed, we must free the data.
    if ((err = srs_do_rtmp_create_msg(type, timestamp, data, size, stream_id, ppmsg)) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "create message");
    }
    
    return err;
}

srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, SrsCommonMessage** ppmsg)
{
    srs_error_t err = srs_success;
    
    // only when failed, we must free the data.
    if ((err = srs_do_rtmp_create_msg(type, timestamp, data, size, stream_id, ppmsg)) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "create message");
    }
    
    return err;
}

string srs_generate_stream_url(string vhost, string app, string stream)
{
    std::string url = "";
    
    if (SRS_CONSTS_RTMP_DEFAULT_VHOST != vhost){
        url += vhost;
    }
    url += "/";
    url += app;
    url += "/";
    url += stream;
    
    return url;
}

void srs_parse_rtmp_url(string url, string& tcUrl, string& stream)
{
    size_t pos;
    
    if ((pos = url.rfind("/")) != string::npos) {
        stream = url.substr(pos + 1);
        tcUrl = url.substr(0, pos);
    } else {
        tcUrl = url;
    }
}

string srs_generate_rtmp_url(string server, int port, string host, string vhost, string app, string stream, string param)
{
    string tcUrl = "rtmp://" + server + ":" + srs_int2str(port) + "/"  + app;
    string streamWithQuery = srs_generate_stream_with_query(host, vhost, stream, param);
    string url = tcUrl + "/" + streamWithQuery;
    return url;
}

srs_error_t srs_write_large_iovs(ISrsProtocolReadWriter* skt, iovec* iovs, int size, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;
    
    // the limits of writev iovs.
#ifndef _WIN32
    // for linux, generally it's 1024.
    static int limits = (int)sysconf(_SC_IOV_MAX);
#else
    static int limits = 1024;
#endif
    
    // send in a time.
    if (size <= limits) {
        if ((err = skt->writev(iovs, size, pnwrite)) != srs_success) {
            return srs_error_wrap(err, "writev");
        }
        return err;
    }
   
    // send in multiple times.
    int cur_iov = 0;
    ssize_t nwrite = 0;
    while (cur_iov < size) {
        int cur_count = srs_min(limits, size - cur_iov);
        if ((err = skt->writev(iovs + cur_iov, cur_count, &nwrite)) != srs_success) {
            return srs_error_wrap(err, "writev");
        }
        cur_iov += cur_count;
        if (pnwrite) {
            *pnwrite += nwrite;
        }
    }
    
    return err;
}

bool srs_is_ipv4(string domain)
{
    for (int i = 0; i < (int)domain.length(); i++) {
        char ch = domain.at(i);
        if (ch == '.') {
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            continue;
        }
        
        return false;
    }
    
    return true;
}

uint32_t srs_ipv4_to_num(string ip) {
    uint32_t addr = 0;
    if (inet_pton(AF_INET, ip.c_str(), &addr) <= 0) {
        return 0;
    }

    return ntohl(addr);
}

bool srs_ipv4_within_mask(string ip, string network, string mask) {
    uint32_t ip_addr = srs_ipv4_to_num(ip);
    uint32_t mask_addr = srs_ipv4_to_num(mask);
    uint32_t network_addr = srs_ipv4_to_num(network);

    return (ip_addr & mask_addr) == (network_addr & mask_addr);
}

static struct CIDR_VALUE {
    size_t length;
    std::string mask;
} CIDR_VALUES[32] = {
    { 1,  "128.0.0.0" },
    { 2,  "192.0.0.0" },
    { 3,  "224.0.0.0" },
    { 4,  "240.0.0.0" },
    { 5,  "248.0.0.0" },
    { 6,  "252.0.0.0" },
    { 7,  "254.0.0.0" },
    { 8,  "255.0.0.0" },
    { 9,  "255.128.0.0" },
    { 10, "255.192.0.0" },
    { 11, "255.224.0.0" },
    { 12, "255.240.0.0" },
    { 13, "255.248.0.0" },
    { 14, "255.252.0.0" },
    { 15, "255.254.0.0" },
    { 16, "255.255.0.0" },
    { 17, "255.255.128.0" },
    { 18, "255.255.192.0" },
    { 19, "255.255.224.0" },
    { 20, "255.255.240.0" },
    { 21, "255.255.248.0" },
    { 22, "255.255.252.0" },
    { 23, "255.255.254.0" },
    { 24, "255.255.255.0" },
    { 25, "255.255.255.128" },
    { 26, "255.255.255.192" },
    { 27, "255.255.255.224" },
    { 28, "255.255.255.240" },
    { 29, "255.255.255.248" },
    { 30, "255.255.255.252" },
    { 31, "255.255.255.254" },
    { 32, "255.255.255.255" },
};

string srs_get_cidr_mask(string network_address) {
    string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (srs_is_ipv4(network_address)) {
            return CIDR_VALUES[32-1].mask;
        }
        return "";
    }

    // Change here to include IPv6 support.
    string is_ipv4_address = network_address.substr(0, delimiter_position);
    if (!srs_is_ipv4(is_ipv4_address)) {
        return "";
    }

    size_t cidr_length_position = delimiter_position + delimiter.length();
    if (cidr_length_position >= network_address.length()) {
        return "";
    }

    string cidr_length = network_address.substr(cidr_length_position, network_address.length());
    if (cidr_length.length() <= 0) {
        return "";
    }

    size_t cidr_length_num = 31;
    try {
        cidr_length_num = atoi(cidr_length.c_str());
        if (cidr_length_num <= 0) {
            return "";
        }
    } catch (...) {
        return "";
    }

    return CIDR_VALUES[cidr_length_num-1].mask;
}

string srs_get_cidr_ipv4(string network_address) {
    string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (srs_is_ipv4(network_address)) {
            return network_address;
        }
        return "";
    }

    // Change here to include IPv6 support.
    string ipv4_address = network_address.substr(0, delimiter_position);
    if (!srs_is_ipv4(ipv4_address)) {
        return "";
    }

    size_t cidr_length_position = delimiter_position + delimiter.length();
    if (cidr_length_position >= network_address.length()) {
        return "";
    }

    string cidr_length = network_address.substr(cidr_length_position, network_address.length());
    if (cidr_length.length() <= 0) {
        return "";
    }

    try {
        size_t cidr_length_num = atoi(cidr_length.c_str());
        if (cidr_length_num <= 0) {
            return "";
        }
    } catch (...) {
        return "";
    }

    return ipv4_address;
}
