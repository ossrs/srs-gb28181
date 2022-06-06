//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_UTILITY_HPP
#define SRS_PROTOCOL_UTILITY_HPP

#include <srs_core.hpp>

#ifndef _WIN32
#include <sys/uio.h>
#endif

#include <string>
#include <vector>
#include <map>
#include <sstream>

#include <srs_kernel_consts.hpp>

class SrsMessageHeader;
class SrsSharedPtrMessage;
class SrsCommonMessage;
class ISrsProtocolReadWriter;

/**
 * parse the tcUrl, output the schema, host, vhost, app and port.
 * @param tcUrl, the input tcUrl, for example,
 *       rtmp://192.168.1.10:19350/live?vhost=vhost.ossrs.net
 * @param schema, for example, rtmp
 * @param host, for example, 192.168.1.10
 * @param vhost, for example, vhost.ossrs.net.
 *       vhost default to host, when user not set vhost in query of app.
 * @param app, for example, live
 * @param port, for example, 19350
 *       default to 1935 if not specified.
 * param param, for example, vhost=vhost.ossrs.net
 * @remark The param stream is input and output param, that is:
 *       input: tcUrl+stream
 *       output: schema, host, vhost, app, stream, port, param
 */
extern void srs_discovery_tc_url(std::string tcUrl, std::string& schema, std::string& host, std::string& vhost, std::string& app,
    std::string& stream, int& port, std::string& param);

// parse query string to map(k,v).
// must format as key=value&...&keyN=valueN
extern void srs_parse_query_string(std::string q, std::map<std::string, std::string>& query);

// Generate ramdom data for handshake.
extern void srs_random_generate(char* bytes, int size);

// Generate random string [0-9a-z] in size of len bytes.
extern std::string srs_random_str(int len);

// Generate random value, use srandom(now_us) to init seed if not initialized.
extern long srs_random();

/**
 * generate the tcUrl without param.
 * @remark Use host as tcUrl.vhost if vhost is default vhost.
 */
extern std::string srs_generate_tc_url(std::string host, std::string vhost, std::string app, int port);

/**
 * Generate the stream with param.
 * @remark Append vhost in query string if not default vhost.
 */
extern std::string srs_generate_stream_with_query(std::string host, std::string vhost, std::string stream, std::string param, bool with_vhost = true);

/**
 * create shared ptr message from bytes.
 * @param data the packet bytes. user should never free it.
 * @param ppmsg output the shared ptr message. user should free it.
 */
extern srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, SrsSharedPtrMessage** ppmsg);
extern srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, SrsCommonMessage** ppmsg);

// get the stream identify, vhost/app/stream.
extern std::string srs_generate_stream_url(std::string vhost, std::string app, std::string stream);

// parse the rtmp url to tcUrl/stream,
// for example, rtmp://v.ossrs.net/live/livestream to
//      tcUrl: rtmp://v.ossrs.net/live
//      stream: livestream
extern void srs_parse_rtmp_url(std::string url, std::string& tcUrl, std::string& stream);

// Genereate the rtmp url, for instance, rtmp://server:port/app/stream?param
// @remark We always put vhost in param, in the query of url.
extern std::string srs_generate_rtmp_url(std::string server, int port, std::string host, std::string vhost, std::string app, std::string stream, std::string param);

// write large numbers of iovs.
extern srs_error_t srs_write_large_iovs(ISrsProtocolReadWriter* skt, iovec* iovs, int size, ssize_t* pnwrite = NULL);

// join string in vector with indicated separator
template <typename T>
std::string srs_join_vector_string(std::vector<T>& vs, std::string separator)
{
    std::stringstream ss;

    for (int i = 0; i < (int)vs.size(); i++) {
        ss << vs.at(i);
        if (i != (int)vs.size() - 1) {
            ss << separator;
        }
    }

    return ss.str();
}

// Whether domain is an IPv4 address.
extern bool srs_is_ipv4(std::string domain);

// Convert an IPv4 from string to uint32_t.
extern uint32_t srs_ipv4_to_num(std::string ip);

// Whether the IPv4 is in an IP mask.
extern bool srs_ipv4_within_mask(std::string ip, std::string network, std::string mask);

// Get the CIDR (Classless Inter-Domain Routing) mask for a network address.
extern std::string srs_get_cidr_mask(std::string network_address);

// Get the CIDR (Classless Inter-Domain Routing) IPv4 for a network address.
extern std::string srs_get_cidr_ipv4(std::string network_address);

#endif

