//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_MPEGTS_UDP_HPP
#define SRS_APP_MPEGTS_UDP_HPP

#include <srs_core.hpp>

struct sockaddr;
#include <string>
#include <map>

class SrsBuffer;
class SrsTsContext;
class SrsConfDirective;
class SrsSimpleStream;
class SrsRtmpClient;
class SrsStSocket;
class SrsRequest;
class SrsRawH264Stream;
class SrsSharedPtrMessage;
class SrsRawAacStream;
struct SrsRawAacStreamCodec;
class SrsPithyPrint;
class SrsSimpleRtmpClient;

#include <srs_app_st.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_listener.hpp>

// The queue for mpegts over udp to send packets.
// For the aac in mpegts contains many flv packets in a pes packet,
// we must recalc the timestamp.
class SrsMpegtsQueue
{
private:
    // The key: dts, value: msg.
    std::map<int64_t, SrsSharedPtrMessage*> msgs;
    int nb_audios;
    int nb_videos;
public:
    SrsMpegtsQueue();
    virtual ~SrsMpegtsQueue();
public:
    virtual srs_error_t push(SrsSharedPtrMessage* msg);
    virtual SrsSharedPtrMessage* dequeue();
};

// The mpegts over udp stream caster.
class SrsMpegtsOverUdp : public ISrsTsHandler, public ISrsUdpHandler
{
private:
    SrsTsContext* context;
    SrsSimpleStream* buffer;
    std::string output;
private:
    SrsSimpleRtmpClient* sdk;
private:
    SrsRawH264Stream* avc;
    std::string h264_sps;
    bool h264_sps_changed;
    std::string h264_pps;
    bool h264_pps_changed;
    bool h264_sps_pps_sent;
private:
    SrsRawAacStream* aac;
    std::string aac_specific_config;
private:
    SrsMpegtsQueue* queue;
    SrsPithyPrint* pprint;
public:
    SrsMpegtsOverUdp(SrsConfDirective* c);
    virtual ~SrsMpegtsOverUdp();
// Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
private:
    virtual srs_error_t on_udp_bytes(std::string host, int port, char* buf, int nb_buf);
// Interface ISrsTsHandler
public:
    virtual srs_error_t on_ts_message(SrsTsMessage* msg);
private:
    virtual srs_error_t on_ts_video(SrsTsMessage* msg, SrsBuffer* avs);
    virtual srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts);
    virtual srs_error_t on_ts_audio(SrsTsMessage* msg, SrsBuffer* avs);
    virtual srs_error_t write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts);
private:
    virtual srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char* data, int size);
private:
    // Connect to RTMP server.
    virtual srs_error_t connect();
    // Close the connection to RTMP server.
    virtual void close();
};

#endif

