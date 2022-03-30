//
// Copyright (c) 2013-2021 Lixin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_GB28181_HPP
#define SRS_APP_GB28181_HPP

#include <srs_core.hpp>

#include <arpa/inet.h>
#include <string>
#include <vector>
#include <queue>
#include <map>

#include <srs_app_st.hpp>
#include <srs_app_listener.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_app_log.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_gb28181_sip.hpp>
#include <srs_app_gb28181_jitter.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_source.hpp>
#include <srs_service_conn.hpp>

#define RTP_PORT_MODE_FIXED "fixed"
#define RTP_PORT_MODE_RANDOM "random"

#define PS_AUDIO_ID 0xc0
#define PS_AUDIO_ID_END 0xdf
#define PS_VIDEO_ID 0xe0
#define PS_VIDEO_ID_END 0xef

#define STREAM_TYPE_VIDEO_MPEG1     0x01
#define STREAM_TYPE_VIDEO_MPEG2     0x02
#define STREAM_TYPE_AUDIO_MPEG1     0x03
#define STREAM_TYPE_AUDIO_MPEG2     0x04
#define STREAM_TYPE_PRIVATE_SECTION 0x05
#define STREAM_TYPE_PRIVATE_DATA    0x06
#define STREAM_TYPE_AUDIO_AAC       0x0f
#define STREAM_TYPE_VIDEO_MPEG4     0x10
#define STREAM_TYPE_VIDEO_H264      0x1b
#define STREAM_TYPE_VIDEO_HEVC      0x24
#define STREAM_TYPE_VIDEO_CAVS      0x42
#define STREAM_TYPE_VIDEO_SAVC      0x80

#define STREAM_TYPE_AUDIO_AC3       0x81

#define STREAM_TYPE_AUDIO_G711      0x90
#define STREAM_TYPE_AUDIO_G711ULAW  0x91
#define STREAM_TYPE_AUDIO_G722_1    0x92
#define STREAM_TYPE_AUDIO_G723_1    0x93
#define STREAM_TYPE_AUDIO_G726      0x96
#define STREAM_TYPE_AUDIO_G729_1    0x99
#define STREAM_TYPE_AUDIO_SVAC      0x9b
#define STREAM_TYPE_AUDIO_PCM       0x9c

class SrsConfDirective;
class SrsRtspPacket;
class SrsRtmpClient;
class SrsRawH264Stream;
class SrsRawAacStream;
struct SrsRawAacStreamCodec;
class SrsSharedPtrMessage;
class SrsAudioFrame;
class SrsSimpleStream;
class SrsPithyPrint;
class SrsSimpleRtmpClient;
class SrsSipStack;
class SrsGb28181Manger;
class SrsRtpTimeJitter;
class SrsSipRequest;
class SrsGb28181RtmpMuxer;
class SrsGb28181Config;
class SrsGb28181PsRtpProcessor;
class SrsGb28181SipService;
class SrsGb28181StreamChannel;
class SrsGb28181SipSession;
class SrsRtpJitterBuffer;
class SrsServer;
class SrsLiveSource;
class SrsRequest;
class SrsResourceManager;
class SrsGb28181Conn;
class SrsGb28181Caster;

//ps rtp header packet parse

class SrsPsRtpPacket: public SrsRtspPacket
{
public:
    SrsPsRtpPacket();
    virtual ~SrsPsRtpPacket();
    bool isFirstPacket;
public:
    virtual srs_error_t decode(SrsBuffer* stream);
};

//randomly assigned ports receive gb28181 device streams
class SrsPsRtpListener: public ISrsUdpHandler
{
private:
    SrsUdpListener* listener;
    SrsGb28181PsRtpProcessor* rtp_processor;
    int _port;
public:
    SrsPsRtpListener(SrsGb28181Config* c, int p, std::string s);
    virtual ~SrsPsRtpListener();
public:
    virtual int port();
    virtual srs_error_t listen();
// Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
};

//multiplexing service, single port receiving all gb28181 device streams
class SrsGb28181RtpMuxService : public ISrsUdpHandler
{
private:
   SrsGb28181Config *config;
   SrsGb28181PsRtpProcessor *rtp_processor;
public:
    SrsGb28181RtpMuxService(SrsConfDirective* c);
    virtual ~SrsGb28181RtpMuxService();

    // Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
};


//process gb28181 RTP package, generate a completed PS stream data, 
//call the PS stream parser, parse the original video and audio
class SrsGb28181PsRtpProcessor: public ISrsUdpHandler
{
private:
    SrsPithyPrint* pprint;
    SrsGb28181Config* config;
    std::map<std::string, SrsPsRtpPacket*> cache_ps_rtp_packet;
    std::map<std::string, SrsPsRtpPacket*> pre_packet;
    std::string channel_id;
    bool auto_create_channel;
public:
    SrsGb28181PsRtpProcessor(SrsGb28181Config* c, std::string sid);
    virtual ~SrsGb28181PsRtpProcessor();
private:
    bool can_send_ps_av_packet();
    void dispose();
    void clear_pre_packet();
    SrsGb28181RtmpMuxer* fetch_rtmpmuxer(std::string channel_id, uint32_t ssrc);
    srs_error_t rtmpmuxer_enqueue_data(SrsGb28181RtmpMuxer *muxer, uint32_t ssrc, 
            int peer_port, std::string address_string, SrsPsRtpPacket *pkt);
// Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
    virtual srs_error_t on_tcp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
public:
    virtual srs_error_t on_rtp_packet_jitter(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
};


//ps stream processing parsing interface
class ISrsPsStreamHander
{
public:
    ISrsPsStreamHander();
    virtual ~ISrsPsStreamHander();
public:
    virtual srs_error_t on_rtp_video(SrsSimpleStream* stream, int64_t dts)=0;
    virtual srs_error_t on_rtp_audio(SrsSimpleStream* stream, int64_t dts, int type)=0;
};

//analysis of PS stream and 
//extraction of H264 raw data and audio data
//then process the flow through PS stream hander, 
//such as RTMP multiplexer, and composited into RTMP av stream
class SrsPsStreamDemixer
{
public:
    // gb28181 program stream struct define
    struct SrsPsPacketStartCode
    {
        uint8_t start_code[3];
        uint8_t stream_id[1];
    };

    struct SrsPsPacketHeader
    {
        SrsPsPacketStartCode start;// 4
        uint8_t info[9];
        uint8_t stuffing_length;
    };

    struct SrsPsPacketBBHeader
    {
        SrsPsPacketStartCode start;
        uint16_t    length;
    };

    struct SrsPsePacket
    {
        SrsPsPacketStartCode     start;
        uint16_t    length;
        uint8_t         info[2];
        uint8_t         stuffing_length;
    };

    struct SrsPsMapPacket
    {
        SrsPsPacketStartCode  start;
        uint16_t length;
    };

private:
    SrsFileWriter ps_fw;
    SrsFileWriter video_fw;
    SrsFileWriter audio_fw;
    SrsFileWriter unknow_fw;

    bool first_keyframe_flag;
    bool wait_first_keyframe;
    bool audio_enable;
    std::string channel_id;

    uint8_t video_es_id;
    uint8_t video_es_type;
    uint8_t audio_es_id;
    uint8_t audio_es_type;
    int audio_check_aac_try_count;
    
    SrsRawAacStream *aac;

    ISrsPsStreamHander *hander;
public:
    SrsPsStreamDemixer(ISrsPsStreamHander *h, std::string sid, bool a, bool k);
    virtual ~SrsPsStreamDemixer();
private:
    bool can_send_ps_av_packet();
public:
    int64_t parse_ps_timestamp(const uint8_t* p);
    std::string get_ps_map_type_str(uint8_t);
    virtual srs_error_t on_ps_stream(char* ps_data, int ps_size, uint32_t timestamp, uint32_t ssrc);
};


//RTMP multiplexer, which processes the raw H264 / AAC, 
//then publish it to RTMP server
class SrsGb28181RtmpMuxer : public ISrsCoroutineHandler, 
     public ISrsConnection, public ISrsPsStreamHander
{
private:
    SrsPithyPrint* pprint;
    SrsGb28181StreamChannel *channel;
    int stream_idle_timeout;
    srs_utime_t recv_rtp_stream_time;
    srs_utime_t send_rtmp_stream_time;
private:
    std::string channel_id;
    std::string _rtmp_url;
    std::string video_ssrc;
    std::string audio_ssrc;

    SrsGb28181Manger* gb28181_manger;
    SrsCoroutine* trd;
    SrsPsStreamDemixer* ps_demixer;
    srs_cond_t wait_ps_queue;

    SrsSimpleRtmpClient* sdk;
    SrsRtpTimeJitter* vjitter;
    SrsRtpTimeJitter* ajitter;

    SrsRawH264Stream* avc;
    std::string h264_sps;
    std::string h264_pps;

    SrsRawAacStream* aac;
    std::string aac_specific_config;

    SrsRequest* req;
    SrsLiveSource* source;
    SrsServer* server;

    SrsRtpJitterBuffer *jitter_buffer;
    SrsRtpJitterBuffer *jitter_buffer_audio;

    char *ps_buffer;
    char *ps_buffer_audio;

    int ps_buflen;
    int ps_buflen_auido;

    uint32_t ps_rtp_video_ts;

    bool source_publish; 

public:
    std::queue<SrsPsRtpPacket*> ps_queue;

public:
    SrsGb28181RtmpMuxer(SrsGb28181Manger* m, std::string id, bool a, bool k);
    virtual ~SrsGb28181RtmpMuxer();

public:
    virtual srs_error_t serve();
    virtual void stop();
    srs_error_t initialize(SrsServer* s, SrsRequest* r);
   
    virtual std::string get_channel_id();
    virtual void ps_packet_enqueue(SrsPsRtpPacket *pkt);
    virtual void copy_channel(SrsGb28181StreamChannel *s);
    virtual void set_channel_peer_ip(std::string ip);
    virtual void set_channel_peer_port(int port);
    virtual int channel_peer_port();
    virtual std::string channel_peer_ip();
    virtual void set_rtmp_url(std::string url);
    virtual std::string rtmp_url();
    virtual SrsGb28181StreamChannel get_channel();
    srs_utime_t get_recv_stream_time();

    void insert_jitterbuffer(SrsPsRtpPacket *pkt);

private:
    virtual srs_error_t do_cycle();
    virtual void destroy();

// Interface ISrsOneCycleThreadHandler
public:
    virtual srs_error_t cycle();
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId& get_id();
    virtual std::string desc();
public:
    virtual srs_error_t on_rtp_video(SrsSimpleStream* stream, int64_t dts);
    virtual srs_error_t on_rtp_audio(SrsSimpleStream* stream, int64_t dts, int type);
private:
    
    srs_error_t replace_startcode_with_nalulen(char *video_data, int &size,  uint32_t pts, uint32_t dts);
    srs_error_t write_h264_ipb_frame2(char *frame, int frame_size, uint32_t pts, uint32_t dts);
    virtual srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts, bool b = true);
    virtual srs_error_t write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts);
    virtual srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char* data, int size);
    virtual srs_error_t rtmp_write_packet_by_source(char type, uint32_t timestamp, char* data, int size);
private:
    // Connect to RTMP server.
    virtual srs_error_t connect();
    // Close the connection to RTMP server.
    virtual void close();
public:
    virtual void rtmp_close();
};

//system parameter configuration of gb28181 module,
//read file from configuration file to generate
class SrsGb28181Config
{
public:
    std::string host;
    srs_utime_t rtp_idle_timeout;
    bool audio_enable;
    bool wait_keyframe;
    std::string output;
    int rtp_port_min;
    int rtp_port_max;
    int rtp_mux_port;
	bool rtp_mux_tcp_enable;
    bool auto_create_channel;

    //sip config
    int  sip_port;
    std::string sip_serial;
    std::string sip_realm;
    bool sip_enable;
    srs_utime_t sip_ack_timeout;
    srs_utime_t sip_keepalive_timeout;
    bool sip_auto_play;
    bool sip_invite_port_fixed;
    srs_utime_t sip_query_catalog_interval;
 
public:
    SrsGb28181Config(SrsConfDirective* c);
    virtual ~SrsGb28181Config();
};

class SrsGb28181StreamChannel
{
private:
    std::string channel_id;
    std::string port_mode;
    std::string app;
    std::string stream;
    std::string rtmp_url;
    std::string flv_url;
    std::string hls_url;
    std::string webrtc_url;
    
    std::string ip;
    int rtp_port;
    int rtmp_port;
    uint32_t ssrc;
    srs_utime_t recv_time;
    std::string recv_time_str;
    
    //send rtp stream client local port
    int rtp_peer_port;
    //send rtp stream client local ip
    std::string rtp_peer_ip;

public:
    SrsGb28181StreamChannel();
    virtual ~SrsGb28181StreamChannel();

    std::string get_channel_id() const { return channel_id; }
    std::string get_port_mode() const { return port_mode; }
    std::string get_app() const { return app; }
    std::string get_stream() const { return stream; }
    std::string get_ip() const { return ip; }
    int get_rtp_port() const { return rtp_port; }
    int get_rtmp_port() const { return rtmp_port; }
    uint32_t get_ssrc() const { return ssrc; }
    uint32_t get_rtp_peer_port() const { return rtp_peer_port; }
    std::string get_rtp_peer_ip() const { return rtp_peer_ip; }
    std::string get_rtmp_url() const { return rtmp_url; }
    std::string get_flv_url() const { return flv_url; }
    std::string get_hls_url() const { return hls_url; }
    std::string get_webrtc_url() const { return webrtc_url; }
    srs_utime_t get_recv_time() const { return recv_time; }
    std::string get_recv_time_str() const { return recv_time_str; }

    void set_channel_id(const std::string &i) { channel_id = i; }
    void set_port_mode(const std::string &p) { port_mode = p; }
    void set_app(const std::string &a) { app = a; }
    void set_stream(const std::string &s) { stream = s; }
    void set_ip(const std::string &i) { ip = i; }
    void set_rtp_port( const int &p) { rtp_port = p; }
    void set_rtmp_port( const int &p) { rtmp_port = p; }
    void set_ssrc( const int &s) { ssrc = s;}
    void set_rtp_peer_ip( const std::string &p) { rtp_peer_ip = p; }
    void set_rtp_peer_port( const int &s) { rtp_peer_port = s;}
    void set_rtmp_url( const std::string &u) { rtmp_url = u; }
    void set_flv_url( const std::string &u) { flv_url = u; }
    void set_hls_url( const std::string &u) { hls_url = u; }
    void set_webrtc_url( const std::string &u) { webrtc_url = u; }
    void set_recv_time( const srs_utime_t &u) { recv_time = u; }
    void set_recv_time_str( const std::string &u) { recv_time_str = u; }

    void copy(const SrsGb28181StreamChannel *s);
    void dumps(SrsJsonObject* obj);

};

// Global singleton instance.
extern SrsGb28181Manger* _srs_gb28181;

//gb28181 module management, management of all RTMP multiplexers,
//random assignment of RTP listeners, and external control interfaces
class SrsGb28181Manger
{
private:
    SrsGb28181Config *config;
    // The key: port, value: whether used.
    std::map<int, bool> used_ports;
    std::map<uint32_t, SrsPsRtpListener*> rtp_pool;
    std::map<uint32_t, SrsGb28181RtmpMuxer*> rtmpmuxers_ssrc;
    std::map<std::string, SrsGb28181RtmpMuxer*> rtmpmuxers;
    SrsResourceManager* manager;
    SrsGb28181SipService* sip_service;
    SrsServer* server;
public:
    SrsGb28181Manger(SrsServer* s, SrsConfDirective* c);
    virtual ~SrsGb28181Manger();

public:
    srs_error_t fetch_or_create_rtmpmuxer(std::string id, SrsRequest *req, SrsGb28181RtmpMuxer** gb28181);
    SrsGb28181RtmpMuxer* fetch_rtmpmuxer(std::string id);
    SrsGb28181RtmpMuxer* fetch_rtmpmuxer_by_ssrc(uint32_t ssrc);
    void update_rtmpmuxer_to_newssrc_by_id(std::string id, uint32_t ssrc);
    void rtmpmuxer_map_by_ssrc(SrsGb28181RtmpMuxer*muxer, uint32_t ssrc);
    void rtmpmuxer_unmap_by_ssrc(uint32_t ssrc);
    uint32_t generate_ssrc(std::string id);
    uint32_t hash_code(std::string str);

    void set_sip_service(SrsGb28181SipService *s) { sip_service = s; }
    SrsGb28181SipService* get_sip_service() { return sip_service; }
    SrsGb28181Config* get_gb28181_config_ptr() { return config;}

public:
    //stream channel api
    srs_error_t create_stream_channel(SrsGb28181StreamChannel *channel);
    srs_error_t delete_stream_channel(std::string id);
    srs_error_t query_stream_channel(std::string id, SrsJsonArray* arr);
    //sip api
    srs_error_t notify_sip_invite(std::string id, std::string ip, int port, uint32_t ssrc, std::string chid);
    srs_error_t notify_sip_bye(std::string id, std::string chid);
    srs_error_t notify_sip_unregister(std::string id);
    srs_error_t notify_sip_query_catalog(std::string id);
    srs_error_t notify_sip_ptz(std::string id, std::string chid, std::string cmd, uint8_t speed, int priority);
    srs_error_t query_sip_session(std::string id, SrsJsonArray* arr);
    srs_error_t query_device_list(std::string id, SrsJsonArray* arr);

private:
    void destroy();

public:
    // Alloc a rtp port from local ports pool.
    // @param pport output the rtp port.
    void alloc_port(int* pport);
    // Free the alloced rtp port.
    void free_port(int lpmin, int lpmax);
    srs_error_t initialize();

    SrsGb28181Config get_gb28181_config();
    srs_error_t start_ps_rtp_listen(std::string id, int port);
    void stop_rtp_listen(std::string id);

public:
    void remove(SrsGb28181RtmpMuxer* conn);
    void remove_sip_session(SrsGb28181SipSession* sess);
};

// The gb28181 tcp connection serve the fd.
class SrsGb28181Conn : public ISrsCoroutineHandler, public ISrsConnection
{
private:
	char* mbuffer;
	srs_netfd_t stfd;
	SrsStSocket* skt;
	SrsRtspStack* rtsp;
	SrsGb28181Caster* caster;
	SrsCoroutine* trd;
	SrsGb28181PsRtpProcessor *processor;
public:
	SrsGb28181Conn(SrsGb28181Caster* c, srs_netfd_t fd, SrsGb28181PsRtpProcessor *rtp_processor);
	virtual ~SrsGb28181Conn();
public:
	virtual srs_error_t serve();
	virtual std::string remote_ip();
private:
	virtual srs_error_t do_cycle();
	// Interface ISrsOneCycleThreadHandler
public:
	virtual srs_error_t cycle();
    virtual std::string desc();
    virtual const SrsContextId& get_id();
};

// The caster for gb28181.
class SrsGb28181Caster : public ISrsTcpHandler
{
private:
	std::string output;
	SrsGb28181Config *config;
	SrsGb28181PsRtpProcessor *rtp_processor;
private:
	std::vector<SrsGb28181Conn*> clients;
	SrsResourceManager* manager;
public:
	SrsGb28181Caster(SrsConfDirective* c);
	virtual ~SrsGb28181Caster();
public:
	virtual srs_error_t initialize();
	// Interface ISrsTcpHandler
public:
	virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
	// internal methods.
public:
	virtual void remove(SrsGb28181Conn* conn);
};

#endif

