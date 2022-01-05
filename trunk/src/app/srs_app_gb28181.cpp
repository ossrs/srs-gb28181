//
// Copyright (c) 2013-2021 Lixin
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_gb28181.hpp>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>

using namespace std;

#include <srs_protocol_json.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_rtsp_stack.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_raw_avc.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_format.hpp>
#include <srs_app_gb28181_stack.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_kernel_codec.hpp>

//#define W_PS_FILE
//#define W_VIDEO_FILE
//#define W_AUDIO_FILE
//#define W_UNKONW_FILE

SrsPsRtpPacket::SrsPsRtpPacket()
{ 
    isFirstPacket = false;
}

SrsPsRtpPacket::~SrsPsRtpPacket()
{
}

srs_error_t SrsPsRtpPacket::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // 12bytes header
    if (!stream->require(12)) {
        return srs_error_new(ERROR_RTP_HEADER_CORRUPT, "requires 12 only %d bytes", stream->left());
    }
    
    int8_t vv = stream->read_1bytes();
    version = (vv >> 6) & 0x03;
    padding = (vv >> 5) & 0x01;
    extension = (vv >> 4) & 0x01;
    csrc_count = vv & 0x0f;
    
    int8_t mv = stream->read_1bytes();
    marker = (mv >> 7) & 0x01;
    payload_type = mv & 0x7f;
    
    sequence_number = stream->read_2bytes();
    timestamp = stream->read_4bytes();
    ssrc = stream->read_4bytes();
    
    // TODO: FIXME: check sequence number.
    
    // video codec.
    if (payload_type == 96) {
        // ps stream playload atleast 1bytes content.
        if (!stream->require(1)) {
            return srs_error_new(ERROR_RTP_TYPE96_CORRUPT, "requires 1 only %d bytes", stream->left());
        }
        // append left bytes to payload.
        payload->append(stream->data() + stream->pos() , stream->size()-stream->pos());
    } else {
        return srs_error_new(ERROR_RTP_HEADER_CORRUPT, "unknown payload data");
    }
    return err;
}

//SrsPsRtpListener
SrsPsRtpListener::SrsPsRtpListener(SrsGb28181Config* c, int p, std::string s)
{
    rtp_processor = new SrsGb28181PsRtpProcessor(c, s);
    _port = p;
    // TODO: support listen at <[ip:]port>
    listener = new SrsUdpListener(this, srs_any_address_for_listener(), p);
}

SrsPsRtpListener::~SrsPsRtpListener()
{
    srs_freep(listener);
    srs_freep(rtp_processor);
}

int SrsPsRtpListener::port()
{
    return _port;
}

srs_error_t SrsPsRtpListener::listen()
{
    return listener->listen();
}

srs_error_t SrsPsRtpListener::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf){
    srs_error_t err = srs_success;
    if (rtp_processor){
        err = rtp_processor->on_udp_packet(from, fromlen, buf, nb_buf);
    }
    return err;
}

//SrsGb28181RtpMuxService 
SrsGb28181RtpMuxService::SrsGb28181RtpMuxService(SrsConfDirective* c)
{
    config = new SrsGb28181Config(c);
    rtp_processor = new SrsGb28181PsRtpProcessor(config,"");
}

SrsGb28181RtpMuxService::~SrsGb28181RtpMuxService()
{
    srs_freep(config);
    srs_freep(rtp_processor);
}

srs_error_t SrsGb28181RtpMuxService::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf){
    srs_error_t err = srs_success;
    if (rtp_processor){
        err = rtp_processor->on_udp_packet(from, fromlen, buf, nb_buf);
    }
    return err;
}

//SrsGb28181PsRtpProcessor
SrsGb28181PsRtpProcessor::SrsGb28181PsRtpProcessor(SrsGb28181Config* c, std::string id)
{
    config = c;
    pprint = SrsPithyPrint::create_caster();
    channel_id = id;
}

SrsGb28181PsRtpProcessor::~SrsGb28181PsRtpProcessor()
{
    dispose();
    srs_freep(pprint);
}

void SrsGb28181PsRtpProcessor::dispose()
{
    map<std::string, SrsPsRtpPacket*>::iterator it2;
    for (it2 = cache_ps_rtp_packet.begin(); it2 != cache_ps_rtp_packet.end(); ++it2) {
        srs_freep(it2->second);
    }
    cache_ps_rtp_packet.clear();

    clear_pre_packet();

    return;
}

void SrsGb28181PsRtpProcessor::clear_pre_packet()
{
    map<std::string, SrsPsRtpPacket*>::iterator it;
    for (it = pre_packet.begin(); it != pre_packet.end(); ++it) {
        srs_freep(it->second);
    }
    pre_packet.clear();
}

srs_error_t SrsGb28181PsRtpProcessor::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    return on_rtp_packet_jitter(from, fromlen, buf, nb_buf);
}

srs_error_t SrsGb28181PsRtpProcessor::on_tcp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    return on_udp_packet(from, fromlen, buf, nb_buf);
}

SrsGb28181RtmpMuxer* SrsGb28181PsRtpProcessor::fetch_rtmpmuxer(std::string channel_id, uint32_t ssrc)
{
    if(true){
        SrsGb28181RtmpMuxer* muxer = NULL;
        //First, search according to the channel_id. Otherwise, search according to the SSRC. 
        //Some channel_id are created by RTP pool, which are different ports. 
        //No channel_id are created by multiplexing ports, which are the same port
        if (!channel_id.empty()){
            muxer = _srs_gb28181->fetch_rtmpmuxer(channel_id);
        }else {
            muxer = _srs_gb28181->fetch_rtmpmuxer_by_ssrc(ssrc);
        }

        //auto crate channel
        if (!muxer && config->auto_create_channel){
            //auto create channel generated id
            std::stringstream ss, ss1;
            ss << "chid" << ssrc;
            std::string tmp_id = ss.str();

            SrsGb28181StreamChannel channel;
            channel.set_channel_id(tmp_id);
            // channel.set_port_mode(RTP_PORT_MODE_FIXED); 
            if (!config->sip_invite_port_fixed) {
                channel.set_port_mode(RTP_PORT_MODE_RANDOM);
            }else
            {
                channel.set_port_mode(RTP_PORT_MODE_FIXED);
            }
            channel.set_ssrc(ssrc);
            
            srs_error_t err2 = srs_success;
            if ((err2 = _srs_gb28181->create_stream_channel(&channel)) != srs_success){
                srs_warn("gb28181: RtpProcessor create stream channel error %s", srs_error_desc(err2).c_str());
                srs_error_reset(err2);
            };

            muxer = _srs_gb28181->fetch_rtmpmuxer(tmp_id);
        }
       
        return muxer;
    }//end if FoundFrame
}

srs_error_t SrsGb28181PsRtpProcessor::rtmpmuxer_enqueue_data(SrsGb28181RtmpMuxer *muxer, uint32_t ssrc, 
            int peer_port, std::string address_string, SrsPsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    if (!muxer)
        return err;
     
    if (muxer){
        //TODO: fixme: the same device uses the same SSRC to send with different local ports
        //record the first peer port
        muxer->set_channel_peer_port(peer_port);
        muxer->set_channel_peer_ip(address_string);
        //not the first peer port's non processing
        if (muxer->channel_peer_port() != peer_port){
            srs_warn("<- " SRS_CONSTS_LOG_GB28181_CASTER " gb28181: client_id %s, ssrc=%#x, first peer_port=%d cur peer_port=%d",
                muxer->get_channel_id().c_str(), ssrc, muxer->channel_peer_port(), peer_port);
        }else {
            //muxer->ps_packet_enqueue(pkt);
            muxer->insert_jitterbuffer(pkt);
        }//end if (muxer->channel_peer_port() != peer_port)
    }//end  if (muxer)

    return err;
}

srs_error_t SrsGb28181PsRtpProcessor::on_rtp_packet_jitter(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;

    pprint->elapse();
    
    char address_string[64];
    char port_string[16];
    if (getnameinfo(from, fromlen, 
                (char*)&address_string, sizeof(address_string),
                (char*)&port_string, sizeof(port_string),
                NI_NUMERICHOST|NI_NUMERICSERV)){
        // return srs_error_new(ERROR_SYSTEM_IP_INVALID, "bad address");
        srs_warn("gb28181 ps rtp: bad address");
        return srs_success;
    }
    
    int peer_port = atoi(port_string);

    if (true) {
        SrsBuffer stream(buf, nb_buf);
        SrsPsRtpPacket *pkt = new SrsPsRtpPacket();;
        
        if ((err = pkt->decode(&stream)) != srs_success) {
            srs_freep(pkt);
            // return srs_error_wrap(err, "ps rtp decode error");
            srs_warn("gb28181 ps rtp: decode error");
            srs_freep(err);
            return srs_success;
        }
        
        std::stringstream ss3;
        ss3 << pkt->ssrc << ":" << port_string;
        std::string jitter_key = ss3.str();
        
        pkt->completed = pkt->marker;
        
        
        if (pprint->can_print()) {
            srs_trace("<- " SRS_CONSTS_LOG_GB28181_CASTER " gb28181: client_id %s, peer(%s, %d) ps rtp packet %dB, age=%d, vt=%d/%u, sts=%u/%u/%#x, paylod=%dB",
                        channel_id.c_str(),  address_string, peer_port, nb_buf, pprint->age(), pkt->version, 
                        pkt->payload_type, pkt->sequence_number, pkt->timestamp, pkt->ssrc,
                        pkt->payload->length()
                        );
        }
      
        SrsGb28181RtmpMuxer *muxer = fetch_rtmpmuxer(channel_id,  pkt->ssrc);
        if (muxer){
            rtmpmuxer_enqueue_data(muxer, pkt->ssrc, peer_port, address_string, pkt);
        }
     
        SrsAutoFree(SrsPsRtpPacket, pkt);
    }

    return err;
}


//ISrsPsStreamHander ps stream raw video/audio hander interface
ISrsPsStreamHander::ISrsPsStreamHander()
{
}

ISrsPsStreamHander::~ISrsPsStreamHander()
{
}

//SrsPsStreamDemixer ps stream parse to h264/aac
SrsPsStreamDemixer::SrsPsStreamDemixer(ISrsPsStreamHander *h, std::string id, bool a, bool k)
{
    hander = h;
    audio_enable = a;
    wait_first_keyframe = k;
    channel_id = id;
    first_keyframe_flag = false;

    video_es_id = 0;
    video_es_type = 0;
    audio_es_id = 0;
    audio_es_type = 0;
    audio_check_aac_try_count = 0;

    aac = new SrsRawAacStream();
}

SrsPsStreamDemixer::~SrsPsStreamDemixer()
{
    srs_freep(aac);
}

bool SrsPsStreamDemixer::can_send_ps_av_packet(){
    if (!wait_first_keyframe)
        return true;
    
    if (first_keyframe_flag)
       return true;

    return false;
}

std::string SrsPsStreamDemixer::get_ps_map_type_str(uint8_t type)
{
    switch(type){
        case STREAM_TYPE_VIDEO_MPEG1:     //0x01
           return "mpeg1";
        case STREAM_TYPE_VIDEO_MPEG2://     0x02
            return "mpeg2";
        case STREAM_TYPE_AUDIO_MPEG1://     0x03
            return "mpeg1";
        case STREAM_TYPE_AUDIO_MPEG2://     0x04
            return "mpeg2";
        case STREAM_TYPE_PRIVATE_SECTION:// 0x05
            return "private_section";
        case STREAM_TYPE_PRIVATE_DATA://    0x06
            return "private_data";
        case STREAM_TYPE_AUDIO_AAC://       0x0f
            return "aac";
        case STREAM_TYPE_VIDEO_MPEG4://     0x10
            return "mpeg4";
        case STREAM_TYPE_VIDEO_H264://      0x1b
            return "h264";
        case STREAM_TYPE_VIDEO_HEVC://      0x24
            return "hevc";
        case STREAM_TYPE_VIDEO_CAVS://      0x42
            return "cavs";
        case STREAM_TYPE_VIDEO_SAVC://      0x80
            return "savc";

        case STREAM_TYPE_AUDIO_AC3://       0x81
            return "ac3";

        case STREAM_TYPE_AUDIO_G711://      0x90
            return "g711";
        case STREAM_TYPE_AUDIO_G711ULAW://    0x91
            return "g711ulaw";
        case STREAM_TYPE_AUDIO_G722_1://    0x92
            return "g722_1";
        case STREAM_TYPE_AUDIO_G723_1://    0x93
            return "g723_1";
        case STREAM_TYPE_AUDIO_G726://      0x96
            return "g726";
        case STREAM_TYPE_AUDIO_G729_1://    0x99
            return "g729_1";
        case STREAM_TYPE_AUDIO_SVAC://      0x9b
            return "svac";
        case STREAM_TYPE_AUDIO_PCM://       0x9c
            return "pcm";
        default:
            return "unknow";
    }
}

int64_t  SrsPsStreamDemixer::parse_ps_timestamp(const uint8_t* p)
{
	unsigned long b;
	//total 33 bits
	unsigned long val, val2, val3;

	//1st byte, 5、6、7 bit
	b = *p++;
	val = (b & 0x0e);

	//2 byte, all bit 
	b = (*(p++)) << 8;
    //3 bytes 1--7 bit
	b += *(p++);
	val2 = (b & 0xfffe) >> 1;
	
	//4 byte, all bit
	b = (*(p++)) << 8;
    //5 byte 1--7 bit
	b += *(p++);
	val3 = (b & 0xfffe) >> 1;

    //<32--val--30> <29----val2----15> <14----val3----0>
	val = (val << 29) | (val2 << 15) | val3;
	return val;
}

srs_error_t SrsPsStreamDemixer::on_ps_stream(char* ps_data, int ps_size, uint32_t timestamp, uint32_t ssrc)
{
    srs_error_t err = srs_success;
    int complete_len = 0;
    int incomplete_len = ps_size;
    char *next_ps_pack = ps_data;

    SrsSimpleStream video_stream;
    SrsSimpleStream audio_stream;
    uint64_t audio_pts = 0;
    uint64_t video_pts = 0;
    int pse_index = 0;

#ifdef W_PS_FILE           
        if (!ps_fw.is_open()) {
                std::string filename = "test_ps_" + channel_id + ".mpg";
                ps_fw.open(filename.c_str());
        }
        ps_fw.write(ps_data, ps_size, NULL);          
#endif

	while(incomplete_len > 0 
        && incomplete_len >= sizeof(SrsPsPacketStartCode))
    {
    	if (next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xBA)
		{
            //ps header 
            SrsPsPacketHeader *head = (SrsPsPacketHeader *)next_ps_pack;
            unsigned char pack_stuffing_length = head->stuffing_length & 0x07;
        
            next_ps_pack = next_ps_pack + sizeof(SrsPsPacketHeader) + pack_stuffing_length;
            complete_len = complete_len + sizeof(SrsPsPacketHeader) + pack_stuffing_length;
            incomplete_len = ps_size - complete_len;
        }
        else if(next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xBB)
        {
            //ps system header 
            SrsPsPacketBBHeader *bbhead=(SrsPsPacketBBHeader *)(next_ps_pack);
            int bbheaderlen = htons(bbhead->length);
            next_ps_pack = next_ps_pack + sizeof(SrsPsPacketBBHeader) + bbheaderlen;
            complete_len = complete_len + sizeof(SrsPsPacketBBHeader) + bbheaderlen;
            incomplete_len = ps_size - complete_len;

            first_keyframe_flag = true;
        }
        else if(next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xBC)
        {
            //program stream map 

		    SrsPsMapPacket* psmap_pack = (SrsPsMapPacket*)next_ps_pack;
            psmap_pack->length = htons(psmap_pack->length);
          
            next_ps_pack = next_ps_pack + psmap_pack->length + sizeof(SrsPsMapPacket);
            complete_len = complete_len + psmap_pack->length + sizeof(SrsPsMapPacket);
            incomplete_len = ps_size - complete_len;

            //parse ps map
            uint16_t psm_length=0, ps_info_length=0, es_map_length=0;
            char *p = (char*)psmap_pack + sizeof(SrsPsMapPacket);

            SrsBuffer buf(p, (int)psmap_pack->length);

            psm_length =(int)psmap_pack->length;
            buf.read_1bytes();
            buf.read_1bytes();

            ps_info_length = buf.read_2bytes();

            /* skip program_stream_info */
            buf.skip(ps_info_length);
            /*es_map_length = */buf.read_2bytes();
            /* Ignore es_map_length, trust psm_length */
            es_map_length = psm_length - ps_info_length - 10;
        
            // /* at least one es available? */
            while (es_map_length >= 4) {
                uint8_t type      = buf.read_1bytes();
                uint8_t es_id     = buf.read_1bytes();
                uint16_t es_info_length = buf.read_2bytes();
                std::string s_type = get_ps_map_type_str(type);

                /* remember mapping from stream id to stream type */
                if (es_id >= PS_AUDIO_ID && es_id <= PS_AUDIO_ID_END){
                    if (audio_es_type != type){
                        srs_trace("gb28181: ps map audio es_type=%s(%x), es_id=%0x, es_info_length=%d", 
                         s_type.c_str(), type, es_id, es_info_length);
                    }
                    
                    audio_es_id = es_id;
                    audio_es_type = type;
                }else if (es_id >= PS_VIDEO_ID && es_id <= PS_VIDEO_ID_END){
                    
                    if (video_es_type != type){
                        srs_trace("gb28181: ps map video es_type=%s(%x), es_id=%0x, es_info_length=%d", 
                         s_type.c_str(), type, es_id, es_info_length);
                    }

                    video_es_id = es_id;
                    video_es_type = type;
                }
           
                /* skip program_stream_info */
                if (es_info_length + 4 < es_map_length){ 
                    //check is es_info_length overflow es_map_length
                    buf.skip(es_info_length);
                }
                es_map_length -= 4 + es_info_length;
            }
    
        }
        else if(next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xE0)
        {
            //pse video stream
            SrsPsePacket* pse_pack = (SrsPsePacket*)next_ps_pack;

            unsigned char pts_dts_flags = (pse_pack->info[0] & 0xF0) >> 6;
            //in a frame of data, pts is obtained from the first PSE packet
            if (pse_index == 0 && pts_dts_flags > 0) {
				video_pts = parse_ps_timestamp((unsigned char*)next_ps_pack + 9);
                srs_info("gb28181: ps stream video ts=%u pkt_ts=%u", video_pts, timestamp);
			}
            pse_index +=1;

            int packlength = htons(pse_pack->length);
            int payloadlen = packlength - 2 - 1 - pse_pack->stuffing_length;
         
            next_ps_pack = next_ps_pack + 9 + pse_pack->stuffing_length;
            complete_len = complete_len + 9 + pse_pack->stuffing_length;

            video_stream.append(next_ps_pack, payloadlen);

#ifdef W_VIDEO_FILE            
            if (!video_fw.is_open()) {
                 std::string filename = "test_video_" + channel_id + ".h264";
                 video_fw.open(filename.c_str());
            }
            video_fw.write(next_ps_pack,  payloadlen, NULL);          
#endif

            next_ps_pack = next_ps_pack + payloadlen;
            complete_len = complete_len + payloadlen;
            incomplete_len = ps_size - complete_len;
        }
     	else if (next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xBD)
        {
            //private stream 

			SrsPsePacket* pse_pack = (SrsPsePacket*)next_ps_pack;
			
            int packlength = htons(pse_pack->length);
			int payload_len = packlength - 2 - 1 - pse_pack->stuffing_length;
            
			next_ps_pack = next_ps_pack + payload_len + 9 + pse_pack->stuffing_length;
            complete_len = complete_len + (payload_len + 9 + pse_pack->stuffing_length);
            incomplete_len = ps_size - complete_len;
		}
		else if (next_ps_pack
			&& next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x01
			&& next_ps_pack[3] == (char)0xC0)
        {
            //audio stream
            
            SrsPsePacket* pse_pack = (SrsPsePacket*)next_ps_pack;

		    unsigned char pts_dts_flags = (pse_pack->info[0] & 0xF0) >> 6;
			if (pts_dts_flags > 0 ) {
				audio_pts = parse_ps_timestamp((unsigned char*)next_ps_pack + 9);
                srs_info("gb28181: ps stream video ts=%u pkt_ts=%u", audio_pts, timestamp);
         	}

			int packlength = htons(pse_pack->length);
			int payload_len = packlength - 2 - 1 - pse_pack->stuffing_length;
            next_ps_pack = next_ps_pack + 9 + pse_pack->stuffing_length;

            //if ps map is not aac, but stream  many be aac adts , try update type, 
            //TODO: dahua audio ps map type always is 0x90(g711)

            uint8_t p1 = (uint8_t)(next_ps_pack[0]);
            uint8_t p2 = (uint8_t)(next_ps_pack[1]);
            //uint8_t p3 = (uint8_t)(next_ps_pack[2]);
            //uint8_t p4 = (uint8_t)(next_ps_pack[3]);

            if (audio_enable && audio_es_type != STREAM_TYPE_AUDIO_AAC &&
                (p1 & 0xFF) == 0xFF &&  (p2 & 0xF0) == 0xF0) {
                
                //try update aac type
                SrsBuffer avs(next_ps_pack, payload_len);
                char* frame = NULL;
                int frame_size = 0;
                SrsRawAacStreamCodec codec;

                srs_error_t err2 = srs_success;
                if ((err2 = aac->adts_demux(&avs, &frame, &frame_size, codec)) != srs_success) {
                    srs_info("gb28181: client_id %s, audio data not aac adts (%#x/%u) %02x %02x\n",
                             channel_id.c_str(), ssrc, timestamp, p1, p2);  
                    srs_error_reset(err);
                }else{
                    srs_warn("gb28181: client_id %s, ps map is not aac (%s) type, but stream many be aac adts, try update type",
                         channel_id.c_str(), get_ps_map_type_str(audio_es_type).c_str());
                    audio_es_type = STREAM_TYPE_AUDIO_AAC;
                }
            }
         
            audio_stream.append(next_ps_pack, payload_len);
            
#ifdef W_AUDIO_FILE            
            if (!audio_fw.is_open()) {
                 std::string filename = "test_audio_" + channel_id + ".aac";
                 audio_fw.open(filename.c_str());
            }
            audio_fw.write(next_ps_pack,  payload_len, NULL);          
#endif
            
			next_ps_pack = next_ps_pack + payload_len;
            complete_len = complete_len + (payload_len + 9 + pse_pack->stuffing_length);
            incomplete_len = ps_size - complete_len;

            if (hander && audio_enable && audio_stream.length() && can_send_ps_av_packet()) {
                if ((err = hander->on_rtp_audio(&audio_stream, audio_pts, audio_es_type)) != srs_success) {
                    return srs_error_wrap(err, "process ps audio packet");
                }
            }
		}
        else
        {

#ifdef W_UNKONW_FILE            
            if (!unknow_fw.is_open()) {
                 std::string filename = "test_unknow_" + channel_id + ".mpg";
                 unknow_fw.open(filename.c_str());
            }
            unknow_fw.write(next_ps_pack,  incomplete_len, NULL);          
#endif      
            //TODO: fixme unkonw ps data parse
            if (next_ps_pack
            && next_ps_pack[0] == (char)0x00
			&& next_ps_pack[1] == (char)0x00
			&& next_ps_pack[2] == (char)0x00
			&& next_ps_pack[3] == (char)0x01){
                //dahua's PS header may lose packets. It is sent by an RTP packet of Dahua's PS header
                //dahua rtp send format:
                //ts=1000 seq=1 mark=false payload= ps header
                //ts=1000 seq=2 mark=false payload= video
                //ts=1000 seq=3 mark=true payload= video
                //ts=1000 seq=4 mark=true payload= audio
                incomplete_len = ps_size - complete_len; 
                complete_len = complete_len + incomplete_len;
            }

            first_keyframe_flag = false;
            srs_trace("gb28181: client_id %s, unkonw ps data (%#x/%u) %02x %02x %02x %02x\n", 
                channel_id.c_str(), ssrc, timestamp,  
                next_ps_pack[0]&0xFF, next_ps_pack[1]&0xFF, next_ps_pack[2]&0xFF, next_ps_pack[3]&0xFF);
            break;
        }
    }

    if (complete_len != ps_size){
         srs_trace("gb28181: client_id %s decode ps packet error (%#x/%u)! ps_size=%d  complete=%d \n", 
                     channel_id.c_str(), ssrc, timestamp, ps_size, complete_len);
    }else if (hander && video_stream.length() && can_send_ps_av_packet() && video_es_type == STREAM_TYPE_VIDEO_H264) {
         if ((err = hander->on_rtp_video(&video_stream, video_pts)) != srs_success) {
            video_es_type = 0;
            return srs_error_wrap(err, "process ps video packet");
        }
    }
	else if (hander && video_stream.length() && can_send_ps_av_packet() && video_es_type == STREAM_TYPE_VIDEO_HEVC) {
		if ((err = hander->on_rtp_video_h265(&video_stream, video_pts)) != srs_success) {
			video_es_type = 0;
			return srs_error_wrap(err, "process hevc ps video packet");
		}
	}
  
    return err;
}

static std::string get_host_candidate_ips(SrsConfDirective* c)
{
    string candidate = _srs_config->get_stream_caster_gb28181_host(c);
    if (candidate == "*" || candidate == "0.0.0.0") {
        std::vector<SrsIPAddress*>& ips = srs_get_local_ips();
        int index = _srs_config->get_stats_network();
        SrsIPAddress* ip = ips.at(index);
        return ip->ip;
    } else {
        return candidate;
    }
}

//Gb28181 Config
SrsGb28181Config::SrsGb28181Config(SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    host = get_host_candidate_ips(c);
    output = _srs_config->get_stream_caster_output(c);
    rtp_mux_port = _srs_config->get_stream_caster_listen(c);
	rtp_mux_tcp_enable = _srs_config->get_stream_caster_tcp_enable(c);
    rtp_port_min = _srs_config->get_stream_caster_rtp_port_min(c);
    rtp_port_max = _srs_config->get_stream_caster_rtp_port_max(c);
    rtp_idle_timeout = _srs_config->get_stream_caster_gb28181_rtp_idle_timeout(c);

    wait_keyframe = _srs_config->get_stream_caster_gb28181_wait_keyframe(c);
    audio_enable = _srs_config->get_stream_caster_gb28181_audio_enable(c);
    auto_create_channel = _srs_config->get_stream_caster_gb28181_auto_create_channel(c);

    //sip config
    sip_enable = _srs_config->get_stream_caster_gb28181_sip_enable(c);
    sip_port = _srs_config->get_stream_caster_gb28181_sip_listen(c);
    sip_realm = _srs_config->get_stream_caster_gb28181_realm(c);
    sip_serial = _srs_config->get_stream_caster_gb28181_serial(c);
    sip_auto_play = _srs_config->get_stream_caster_gb28181_sip_auto_play(c);
    sip_ack_timeout = _srs_config->get_stream_caster_gb28181_ack_timeout(c);
    sip_keepalive_timeout = _srs_config->get_stream_caster_gb28181_keepalive_timeout(c);
    sip_invite_port_fixed = _srs_config->get_stream_caster_gb28181_sip_invite_port_fixed(c);
    sip_query_catalog_interval = _srs_config->get_stream_caster_gb28181_sip_query_catalog_interval(c);
}

SrsGb28181Config::~SrsGb28181Config()
{
   
}

//SrsGb28181RtmpMuxer gb28181 rtmp muxer, process ps stream to rtmp
SrsGb28181RtmpMuxer::SrsGb28181RtmpMuxer(SrsGb28181Manger* c, std::string id, bool a, bool k)
{
    channel_id = id;
    gb28181_manger = c;
    channel = new SrsGb28181StreamChannel();

    pprint = SrsPithyPrint::create_caster();
    trd = new SrsSTCoroutine("gb28181rtmpmuxer", this);
    //change stack size to 256K, fix crash when call FFMpeg
    ((SrsSTCoroutine*)trd)->set_stack_size(1 << 18);
    
    sdk = NULL;
    vjitter = new SrsRtpTimeJitter();
    ajitter = new SrsRtpTimeJitter();
    
    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();
	//hevc_ptr = std::make_shared<SrsRawHEVCStream>();
	hevc_ptr = new SrsRawHEVCStream();

    ps_demixer = new SrsPsStreamDemixer(this, id, a, k);
    wait_ps_queue = srs_cond_new();

    stream_idle_timeout = -1;
    recv_rtp_stream_time = 0;
    send_rtmp_stream_time = 0;

    _rtmp_url = "";

    h264_sps = "";
    h264_pps = "";
    aac_specific_config = "";

	h265_vps = "";
	h265_sps = "";
	h265_pps = "";
	hevc_vps_changed = false;
	hevc_sps_changed = false;
	hevc_pps_changed = false;
	hevc_sps_pps_sent = false;

    req = NULL;
    server = NULL;
    source = NULL;
    source_publish = true;

    jitter_buffer = new SrsRtpJitterBuffer(id);
    jitter_buffer_audio = new SrsRtpJitterBuffer(id);

    ps_buflen = 0;
    ps_buffer = NULL;

    ps_buflen_auido = 0;
    ps_buffer_audio = NULL;
}

SrsGb28181RtmpMuxer::~SrsGb28181RtmpMuxer()
{

    close();
    
    srs_cond_destroy(wait_ps_queue);
 
    srs_freep(jitter_buffer);
    srs_freep(jitter_buffer_audio);
    srs_freepa(ps_buffer);
    srs_freepa(ps_buffer_audio);

    srs_freep(channel);
    srs_freep(ps_demixer);
    srs_freep(trd);
    srs_freep(sdk);
    srs_freep(vjitter);
    srs_freep(ajitter);
    srs_freep(pprint);
    srs_freep(hevc_ptr);

    destroy();
}

srs_error_t SrsGb28181RtmpMuxer::serve()
{
    srs_error_t err = srs_success;
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "gb28181rtmpmuxer");
    }
    
    return err;
}

std::string SrsGb28181RtmpMuxer::remote_ip()
{
    return "";
}

const SrsContextId& SrsGb28181RtmpMuxer::get_id()
{
    return _srs_context->get_id();
}

std::string SrsGb28181RtmpMuxer::desc()
{
    return "GBConn";
}

std::string SrsGb28181RtmpMuxer::get_channel_id()
{
    return channel_id;
}

void SrsGb28181RtmpMuxer::copy_channel(SrsGb28181StreamChannel *s)
{
    channel->copy(s);
}

SrsGb28181StreamChannel SrsGb28181RtmpMuxer::get_channel()
{
   return *channel;
}

void SrsGb28181RtmpMuxer::set_channel_peer_ip(std::string ip)
{
    if (channel->get_rtp_peer_ip().empty()){
       channel->set_rtp_peer_ip(ip);
    }
}

void SrsGb28181RtmpMuxer::set_channel_peer_port(int port)
{
    if (channel->get_rtp_peer_port() == 0){
        channel->set_recv_time_str(srs_sip_get_utc_date());
        channel->set_recv_time(srs_get_system_time());
        channel->set_rtp_peer_port(port);
    }
}

int SrsGb28181RtmpMuxer::channel_peer_port()
{
    return channel->get_rtp_peer_port();
}

std::string SrsGb28181RtmpMuxer::channel_peer_ip()
{
    return channel->get_rtp_peer_ip();
}

void SrsGb28181RtmpMuxer::set_rtmp_url(std::string url)
{
    _rtmp_url = url;
}
std::string SrsGb28181RtmpMuxer::rtmp_url()
{
    return _rtmp_url;
}

srs_utime_t SrsGb28181RtmpMuxer::get_recv_stream_time()
{
    return recv_rtp_stream_time;
}


void SrsGb28181RtmpMuxer::destroy()
{
    while(!ps_queue.empty()){
        SrsPsRtpPacket* pkt =  ps_queue.front();
        ps_queue.pop();
        //must be free pkt
        srs_freep(pkt);
    }
}

srs_error_t SrsGb28181RtmpMuxer::initialize(SrsServer *s, SrsRequest* r)
{
    srs_error_t err = srs_success;

    if (!jitter_buffer) {
        jitter_buffer = new SrsRtpJitterBuffer(channel_id);
    }

    jitter_buffer->SetDecodeErrorMode(kSelectiveErrors);
    jitter_buffer->SetNackMode(kNack, -1, -1);
    jitter_buffer->SetNackSettings(250, 450, 0);

    if (!jitter_buffer_audio) {
        jitter_buffer_audio = new SrsRtpJitterBuffer(channel_id);
    }

    jitter_buffer_audio->SetDecodeErrorMode(kSelectiveErrors);
    jitter_buffer_audio->SetNackMode(kNack, -1, -1);
    jitter_buffer_audio->SetNackSettings(250, 450, 0);

    if (!source_publish) return err;

    req = r;
    server = s;
    
    if ((err = _srs_sources->fetch_or_create(req, (ISrsLiveSourceHandler*)server, &source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

 #ifdef SRS_RTC
    SrsRtcSource *rtc = NULL;
    bool rtc_server_enabled = _srs_config->get_rtc_server_enabled();
    bool rtc_enabled = _srs_config->get_rtc_enabled(req->vhost);
    if (rtc_server_enabled && rtc_enabled) {
        if ((err = _srs_rtc_sources->fetch_or_create(req, &rtc)) != srs_success) {
            return srs_error_wrap(err, "create source");
        }

        if (!rtc->can_publish()) {
            return srs_error_new(ERROR_RTC_SOURCE_BUSY, "gb28181 rtc stream %s busy", req->get_stream_url().c_str());
        }
    }
#endif

    // Check whether RTMP stream is busy.
    if (!source->can_publish(false)) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "gb28181 rtmp: stream %s is busy", req->get_stream_url().c_str());
    }

    // Bridge to RTC streaming.
#if defined(SRS_RTC) && defined(SRS_FFMPEG_FIT)
    if (rtc) {
        SrsRtcFromRtmpBridger *bridger = new SrsRtcFromRtmpBridger(rtc);
        if ((err = bridger->initialize(req)) != srs_success) {
            srs_freep(bridger);
            return srs_error_wrap(err, "bridger init");
        }

        source->set_bridger(bridger);
    }
#endif

    if ((err = source->on_publish()) != srs_success) {
        return srs_error_wrap(err, "on publish");
    }

    return err;
}


srs_error_t SrsGb28181RtmpMuxer::do_cycle()
{
    srs_error_t err = srs_success;
    recv_rtp_stream_time =  srs_get_system_time();
    send_rtmp_stream_time = srs_get_system_time();
    uint32_t cur_timestamp = 0;
    int buffer_size = 0;
    bool keyframe = false;
           
    //consume ps stream, and check status
    while (true) {

        pprint->elapse();

        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "gb28181 rtmp muxer cycle");
        }

        SrsGb28181Config config = gb28181_manger->get_gb28181_config();

        if (true) {
            if(jitter_buffer->FoundFrame(cur_timestamp)){
                jitter_buffer->GetFrame(&ps_buffer, ps_buflen, buffer_size, keyframe, cur_timestamp);
            
                if (buffer_size > 0){
                    if ((err = ps_demixer->on_ps_stream(ps_buffer, buffer_size, cur_timestamp, 0)) != srs_success){
                        srs_warn("gb28181: demix ps stream error:%s",  srs_error_desc(err).c_str());
                        srs_freep(err);
                    };
                }
            }

            if(jitter_buffer_audio->FoundFrame(cur_timestamp)){
                jitter_buffer_audio->GetFrame(&ps_buffer_audio, ps_buflen_auido, buffer_size, keyframe, cur_timestamp);
            
                if (buffer_size > 0){
                    if ((err = ps_demixer->on_ps_stream(ps_buffer_audio, buffer_size, cur_timestamp, 0)) != srs_success){
                        srs_warn("gb28181: demix ps stream error:%s",  srs_error_desc(err).c_str());
                        srs_freep(err);
                    };
                }
            }
        }

        if (pprint->can_print()) {
            srs_trace("gb28181: client id=%s,  ssrc=%#x, peer(%s, %d), rtmp muxer is alive",
                channel_id.c_str(),  channel->get_ssrc(), 
                channel->get_rtp_peer_ip().c_str(),
                channel->get_rtp_peer_port());
        }
        
        srs_utime_t now = srs_get_system_time();
        srs_utime_t duration = now - recv_rtp_stream_time;

        //if no RTP data is received within 2 seconds, 
        //the peer-port and peer-ip will be cleared and 
        //other port data will be received again
        if (duration > (2 * SRS_UTIME_SECONDS) && channel->get_rtp_peer_port() != 0){
            srs_warn("gb28181: client id=%s ssrc=%#x, peer(%s, %d), no rtp data %d in seconds, clean it, wait other port!", 
                channel_id.c_str(), channel->get_ssrc(), channel->get_rtp_peer_ip().c_str(),
                channel->get_rtp_peer_port(), duration/SRS_UTIME_SECONDS);
            channel->set_rtp_peer_port(0);
            channel->set_rtp_peer_ip("");
        }

       
        if (duration > config.rtp_idle_timeout){
            srs_trace("gb28181: client id=%s, stream idle timeout, stop!!!", channel_id.c_str());
            break;
        }

        //RTMP connection is about to timeout without receiving any data., 
        //waiting for the next time there is data automatically connected
        //it is related to the following two parameter settings of the rtmp server
        //the publish 1st packet timeout in srs_utime_t
        //publish_1stpkt_timeout  default 20000ms
        //the publish normal packet timeout in srs_utime_t
        //publish_normal_timeout  default 5000ms
        duration = now - send_rtmp_stream_time;
        bool will_timeout = duration > (5 * SRS_UTIME_SECONDS);
        if (will_timeout && sdk){
            srs_warn("gb28181: client id=%s RTMP connection is about to time out without receiving any data",
                channel_id.c_str());
            rtmp_close();
        }
        
        srs_usleep(30 * SRS_UTIME_MILLISECONDS);
    }
    
    return err;
}


void SrsGb28181RtmpMuxer::stop()
{
    if (trd){
        trd->interrupt();
	return;
    }
    //stop rtmp publish
    close();
}


void SrsGb28181RtmpMuxer::insert_jitterbuffer(SrsPsRtpPacket *pkt)
{
    if (!pkt){
        return;
    }
        
    recv_rtp_stream_time = srs_get_system_time();

    char *payload = pkt->payload->bytes();
    if (!payload) {
        return;
    }

    uint8_t p1 = (uint8_t)(payload[0]);
    uint8_t p2 = (uint8_t)(payload[1]);
    uint8_t p3 = (uint8_t)(payload[2]);
    uint8_t p4 = (uint8_t)(payload[3]);

   
    //check for rtp ps audio streaming
    bool av_same_ts = true;

    if (p1 == 0x00 && p2 == 0x00 && p3 == 0x01 && p4 == 0xC0 &&
        ps_rtp_video_ts != pkt->timestamp) {
        av_same_ts = false;
    }

    //if audio and video are the same clock, 
    //if both audio and video use jitter_buffer, 
    //otherwise audio uses jitter_buffer_audio, and video uses jitter_buffer
    if (av_same_ts){
        pkt->marker = false;
        jitter_buffer->InsertPacket(pkt->sequence_number, pkt->timestamp, pkt->marker, 
                pkt->payload->bytes(), pkt->payload->length(), NULL);
        ps_rtp_video_ts = pkt->timestamp;
    }else {
        jitter_buffer_audio->InsertPacket(pkt->sequence_number, pkt->timestamp, pkt->marker,
                pkt->payload->bytes(), pkt->payload->length(), NULL);
    }
 
    //srs_cond_signal(wait_ps_queue);
}

void SrsGb28181RtmpMuxer::ps_packet_enqueue(SrsPsRtpPacket *pkt)
{
    srs_assert(pkt);
   
    recv_rtp_stream_time = srs_get_system_time();

    //prevent consumers from being unable to process data 
    //and accumulating in the queue
    uint32_t size = ps_queue.size();
    if (size > 100){
        srs_warn("gb28181: rtmpmuxer too much queue data, need to clear!!!");
        while(!ps_queue.empty()) {
            SrsPsRtpPacket* pkt =  ps_queue.front();
            ps_queue.pop();
            srs_freep(pkt);
        }
    }
   
    ps_queue.push(pkt);
    //srs_cond_signal(wait_ps_queue);
}

srs_error_t SrsGb28181RtmpMuxer::cycle()
{
    // serve the rtmp muxer.
    srs_error_t err = do_cycle();
    
    gb28181_manger->stop_rtp_listen(channel_id);

    gb28181_manger->remove(this);
    srs_trace("gb28181: client id=%s rtmp muxer is remove", channel_id.c_str());
    
    if (err == srs_success) {
        srs_trace("client finished.");
    } else if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. code=%d", srs_error_code(err));
        srs_freep(err);
    }
   
    return err;
}

srs_error_t SrsGb28181RtmpMuxer::on_rtp_video(SrsSimpleStream *stream, int64_t fpts)
{
    srs_error_t err = srs_success;
   
    if (!source_publish){
        // ensure rtmp connected.
        if ((err = connect()) != srs_success) {
            //after the connection fails, need to clear flag 
            //and send the av header again next time
            h264_sps = "";
            h264_pps = "";
            aac_specific_config = "";
            return srs_error_wrap(err, "connect");
        }
    }
    
    if ((err = vjitter->correct(fpts)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(fpts / 90);
    uint32_t pts = (uint32_t)(fpts / 90);
    srs_info("gb28181rtmpmuxer: on_rtp_video dts=%u", dts);
    
    if (true) {
        char *data = stream->bytes();
        int length = stream->length();

        err = replace_startcode_with_nalulen(data, length, dts, pts);
    }

    return err;
}

srs_error_t SrsGb28181RtmpMuxer::on_rtp_video_h265(SrsSimpleStream *stream, int64_t fpts)
{
	srs_error_t err = srs_success;

	if (!source_publish) {
		// ensure rtmp connected.
		if ((err = connect()) != srs_success) {
			//after the connection fails, need to clear flag 
			//and send the av header again next time
			h265_vps = "";
			h265_sps = "";
			h265_pps = "";
			aac_specific_config = "";
			return srs_error_wrap(err, "connect");
		}
	}

	if ((err = vjitter->correct(fpts)) != srs_success) {
		return srs_error_wrap(err, "jitter");
	}

	// ts tbn to flv tbn.
	uint32_t dts = (uint32_t)(fpts / 90);
	uint32_t pts = (uint32_t)(fpts / 90);
	srs_info("gb28181rtmpmuxer: on_rtp_video_h265 dts=%u", dts);

	if (true) {
		char *data = stream->bytes();
		int length = stream->length();

		err = replace_startcode_with_nalulen_h265(data, length, dts, pts);
		
	}

	return err;
}

srs_error_t SrsGb28181RtmpMuxer::write_h264_ipb_frame2(char *frame, int frame_size, uint32_t pts, uint32_t dts)
{
    srs_error_t err = srs_success;

    if (!frame){
        return srs_error_new(ERROR_GB28181_H264_FRAME_FULL, "h264 frame null");
    }

    if (frame_size <= 0){
        return srs_error_new(ERROR_GB28181_H264_FRAMESIZE, "h264 frame size");
    }

    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    // ignore the nalu type sei(6) aud(9) 
    if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter ||
        nal_unit_type == SrsAvcNaluTypeSEI) {
        return err;
    }

    // for sps
    if (avc->is_sps(frame, frame_size)) {
        std::string sps;
        if ((err = avc->sps_demux(frame, frame_size, sps)) != srs_success) {
            return srs_error_wrap(err, "demux sps");
        }
        
        if (h264_sps == sps) {
            return err;
        }
        h264_sps = sps;

        if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write sps/pps");
        }
        return err;
    }

    // for pps
    if (avc->is_pps(frame, frame_size)) {
        std::string pps;
        if ((err = avc->pps_demux(frame, frame_size, pps)) != srs_success) {
            return srs_error_wrap(err, "demux pps");
        }
        
        if (h264_pps == pps) {
            return err;
        }
        h264_pps = pps;
        
        if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write sps/pps");
        }
        return err;
    }

    srs_info("gb28181: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
    if ((err = write_h264_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
        return srs_error_wrap(err, "write frame");
    }

    return err;
}

srs_error_t SrsGb28181RtmpMuxer::write_hevc_ipb_frame2(char *frame, int frame_size, uint32_t pts, uint32_t dts)
{
	srs_error_t err = srs_success;

	if (!frame) {
		return srs_error_new(ERROR_GB28181_H264_FRAME_FULL, "h265 frame null");
	}

	if (frame_size <= 0) {
		return srs_error_new(ERROR_GB28181_H264_FRAMESIZE, "h265 frame size");
	}

	SrsHevcNaluType nal_unit_type = HEVC_NALU_TYPE(frame[0]);
	
	if (nal_unit_type == NAL_UNIT_ACCESS_UNIT_DELIMITER) {
		return err;
	}
	// TODO: FIXME: Should cache this config, it's better not to get it for each video frame.
	if (_srs_config->get_srt_sei_filter()) {
		if (nal_unit_type == NAL_UNIT_SEI) {
			return err;
		}
	}

	// for vps
	if (hevc_ptr->is_vps(frame, frame_size)) {
		std::string vps;
		if ((err = hevc_ptr->vps_demux(frame, frame_size, vps)) != srs_success) {
			return srs_error_wrap(err, "hevc demux vps");
		}

		if (h265_vps == vps) {
			return err;
		}
		hevc_vps_changed = true;
		h265_vps = vps;
		
		//write hevc vps_sps_pps
		// if ((err = write_hevc_sps_pps(dts, pts)) != srs_success) {
		// 	return srs_error_wrap(err, "hevc write vps/sps/pps");
		// }
		return err;
	}

	// for sps
	if (hevc_ptr->is_sps(frame, frame_size)) {
		std::string sps;
		if ((err = hevc_ptr->sps_demux(frame, frame_size, sps)) != srs_success) {
			return srs_error_wrap(err, "demux hevc sps");
		}

		if (h265_sps == sps) {
			return err;
		}
		hevc_sps_changed = true;
		h265_sps = sps;

		// if ((err = write_hevc_sps_pps(dts, pts)) != srs_success) {
		// 	return srs_error_wrap(err, "write hevc sps/pps");
		// }
		return err;
	}

	// for pps
	if (hevc_ptr->is_pps(frame, frame_size)) {
		std::string pps;
		if ((err = hevc_ptr->pps_demux(frame, frame_size, pps)) != srs_success) {
			return srs_error_wrap(err, "demux hevc pps");
		}

		if (h265_pps == pps) {
			return err;
		}
		hevc_pps_changed = true;
		h265_pps = pps;

		// if ((err = write_hevc_sps_pps(dts, pts)) != srs_success) {
		// 	return srs_error_wrap(err, "write hevc sps/pps");
		// }
		return err;
	}

	if (hevc_sps_changed || hevc_pps_changed || hevc_vps_changed) {
		if ((err = write_hevc_sps_pps(dts, pts)) != srs_success) {
			return srs_error_wrap(err, "write hevc sps/pps");
		}
	}

	srs_info("gb28181: demux hevc ibp frame size=%d, dts=%d", frame_size, dts);
	if ((err = write_hevc_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
		return srs_error_wrap(err, "write frame");
	}

	return err;
}


 srs_error_t SrsGb28181RtmpMuxer::replace_startcode_with_nalulen(char *video_data, int &size, uint32_t pts, uint32_t dts)
 {
    srs_error_t err = srs_success;

    int index = 0;
    std::list<int> list_index;

    for(; index < size; index++){
        if (index > (size-4))
            break;
        if (video_data[index] == 0x00 && video_data[index+1] == 0x00 &&
             video_data[index+2] == 0x00 && video_data[index+3] == 0x01){
                 list_index.push_back(index);
             }
    }

    if (list_index.size() == 1){
        int cur_pos = list_index.front();
        list_index.pop_front();

        //0001xxxxxxxxxx
        //xxxx0001xxxxxxx
        uint32_t naluLen = size - cur_pos - 4;
        
        char *frame = video_data + cur_pos + 4;
        int frame_size = naluLen;

        err = write_h264_ipb_frame2(frame, frame_size, dts, pts);

    }else if (list_index.size() > 1){
        int pre_pos = list_index.front();
        list_index.pop_front();
        int first_pos = pre_pos;

        while(list_index.size() > 0){
            int cur_pos = list_index.front();
            list_index.pop_front();

            //pre=========cur======================
            //0001xxxxxxxx0001xxxxxxxx0001xxxxxxxxx
            //xxxxxxxxxxxx0001xxxxxxxx0001xxxxxxxxx
            uint32_t naluLen = cur_pos - pre_pos - 4;
            
            char *frame = video_data + pre_pos + 4;
            int frame_size = naluLen;

            pre_pos = cur_pos;
            err = write_h264_ipb_frame2(frame, frame_size, dts, pts);
        }
        
        //========================pre==========
        //0001xxxxxxxx0001xxxxxxxx0001xxxxxxxxx
        if (first_pos != pre_pos){

            uint32_t naluLen = size - pre_pos - 4;
            
            char *frame = video_data + pre_pos + 4;
            int frame_size = naluLen;

            err = write_h264_ipb_frame2(frame, frame_size, dts, pts);
        }
    }else{
        //xxxxxxxxxxxxxxxxxxx
        char *frame = video_data;
        int frame_size = size;
        err = write_h264_ipb_frame2(frame, frame_size, dts, pts);
    }

    return err;
}

 srs_error_t SrsGb28181RtmpMuxer::replace_startcode_with_nalulen_h265(char *video_data, int &size, uint32_t pts, uint32_t dts)
 {
	 srs_error_t err = srs_success;

	 int index = 0;
	 std::list<int> list_index;

	 for (; index < size; index++) {
		 if (index > (size - 4))
			 break;
		 if (video_data[index] == 0x00 && video_data[index + 1] == 0x00 &&
			 video_data[index + 2] == 0x00 && video_data[index + 3] == 0x01) {
			 list_index.push_back(index);
		 }
	 }

	 if (list_index.size() == 1) {
		 int cur_pos = list_index.front();
		 list_index.pop_front();

		 //0001xxxxxxxxxx
		 //xxxx0001xxxxxxx
		 uint32_t naluLen = size - cur_pos;
		 char *p = (char*)&naluLen;

		 video_data[cur_pos] = p[3];
		 video_data[cur_pos + 1] = p[2];
		 video_data[cur_pos + 2] = p[1];
		 video_data[cur_pos + 3] = p[0];

		 char *frame = video_data + cur_pos + 4;
		 int frame_size = naluLen;

		 err = write_hevc_ipb_frame2(frame, frame_size, dts, pts);

	 }
	 else if (list_index.size() > 1) {
		 int pre_pos = list_index.front();
		 list_index.pop_front();
		 int first_pos = pre_pos;

		 while (list_index.size() > 0) {
			 int cur_pos = list_index.front();
			 list_index.pop_front();

			 //pre=========cur======================
			 //0001xxxxxxxx0001xxxxxxxx0001xxxxxxxxx
			 //xxxxxxxxxxxx0001xxxxxxxx0001xxxxxxxxx
			 uint32_t naluLen = cur_pos - pre_pos - 4;
			 char *p = (char*)&naluLen;

			 video_data[pre_pos] = p[3];
			 video_data[pre_pos + 1] = p[2];
			 video_data[pre_pos + 2] = p[1];
			 video_data[pre_pos + 3] = p[0];

			 char *frame = video_data + pre_pos + 4;
			 int frame_size = naluLen;

			 pre_pos = cur_pos;
			 err = write_hevc_ipb_frame2(frame, frame_size, dts, pts);
		 }

		 //========================pre==========
		 //0001xxxxxxxx0001xxxxxxxx0001xxxxxxxxx
		 if (first_pos != pre_pos) {

			 uint32_t naluLen = size - pre_pos - 4;
			 char *p = (char*)&naluLen;

			 video_data[pre_pos] = p[3];
			 video_data[pre_pos + 1] = p[2];
			 video_data[pre_pos + 2] = p[1];
			 video_data[pre_pos + 3] = p[0];

			 char *frame = video_data + pre_pos + 4;
			 int frame_size = naluLen;

			 err = write_hevc_ipb_frame2(frame, frame_size, dts, pts);
		 }
	 }
	 else {
		 //xxxxxxxxxxxxxxxxxxx
		 char *frame = video_data;
		 int frame_size = size;
		 err = write_hevc_ipb_frame2(frame, frame_size, dts, pts);
	 }

    return err;
}

srs_error_t SrsGb28181RtmpMuxer::on_rtp_audio(SrsSimpleStream* stream, int64_t fdts, int type)
{
    srs_error_t err = srs_success;
    
    if (!source_publish){
        // ensure rtmp connected.
        if ((err = connect()) != srs_success) {
            //after the connection fails, need to clear flag 
            //and send the av header again next time
            h264_sps = "";
            h264_pps = "";
            aac_specific_config = "";
            return srs_error_wrap(err, "connect");
        }
    }
       
    if ((err = ajitter->correct(fdts)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
    uint32_t dts = (uint32_t)(fdts / 90);

    // send each frame.
    SrsBuffer  *avs = new SrsBuffer(stream->bytes(), stream->length());
    SrsAutoFree(SrsBuffer, avs);
    if (!avs->empty()) {
        if (type == STREAM_TYPE_AUDIO_AAC) {
            char* frame = NULL;
            int frame_size = 0;
            SrsRawAacStreamCodec codec;
            if ((err = aac->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
                return srs_error_wrap(err, "demux adts");
            }

            if (frame_size <= 0) {
                return err;
            }

            bool send_adts = false;
            static int srs_aac_srates[] = {
                96000, 88200, 64000, 48000,
                44100, 32000, 24000, 22050,
                16000, 12000, 11025,  8000,
                7350,     0,     0,    0
            };
            switch (srs_aac_srates[codec.sampling_frequency_index]) {
                case 11025:
                    codec.sound_rate = SrsAudioSampleRate11025;
                    break;
                case 22050:
                    codec.sound_rate = SrsAudioSampleRate22050;
                    break;
                case 44100:
                    codec.sound_rate = SrsAudioSampleRate44100;
                    break;
                default:
                    send_adts = true; //raw with adts
                    break;
            };

            std::string sh;
            if ((err = aac->mux_sequence_header(&codec, sh)) != srs_success) {
                return srs_error_wrap(err, "mux sequence header");
            }
            
            if (aac_specific_config != sh){
                std::string sh;
                if ((err = aac->mux_sequence_header(&codec, sh)) != srs_success) {
                    return srs_error_wrap(err, "mux sequence header");
                }
                aac_specific_config = sh;
                codec.aac_packet_type = 0;
                if ((err = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), &codec, dts)) != srs_success) {
                    return srs_error_wrap(err, "write raw audio frame");
                }
            }

            codec.aac_packet_type = 1;
            if  (send_adts) {  // audio raw data. with  adts header
                if ((err = write_audio_raw_frame(stream->bytes(), stream->length(), &codec, dts)) != srs_success) {
                        return srs_error_wrap(err, "write audio raw frame");
                    }
            }else {  // audio raw data. without  adts header
                if ((err = write_audio_raw_frame(frame, frame_size, &codec, dts)) != srs_success) {
                        return srs_error_wrap(err, "write audio raw frame");
                    }
            }
        }else if (type != 0) {
            SrsRawAacStreamCodec codec;
            codec.aac_packet_type = 0;

            if (type == STREAM_TYPE_AUDIO_G711){
                codec.sound_format = SrsAudioCodecIdReservedG711AlawLogarithmicPCM;
                codec.sound_rate = SrsAudioSampleRate5512;
                codec.sound_type = 0;  //MONO = 0, STEREO = 1
                codec.sound_size = 0;  //0=8K, 1=16K
            }else if(type == STREAM_TYPE_AUDIO_G711ULAW){
                codec.sound_format = SrsAudioCodecIdReservedG711MuLawLogarithmicPCM;
                codec.sound_rate = SrsAudioSampleRate5512;
                codec.sound_type = 0;
                codec.sound_size = 0;
            }else {
                return srs_error_wrap(err, "write audio raw frame, type=%d not suppered", type);
            }

            char* frame = stream->bytes();
            int frame_size = stream->length();
            if ((err = write_audio_raw_frame(frame, frame_size, &codec, dts)) != srs_success) {
                return srs_error_wrap(err, "write audio raw frame");
            }
        }
    }//end if (!avs->empty()) 
   
    return err;
}

srs_error_t SrsGb28181RtmpMuxer::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    if (h264_sps == "" ||  h264_pps == ""){
        return err;
    }

    // h264 raw to h264 packet.
    std::string sh;
    if ((err = avc->mux_sequence_header(h264_sps, h264_pps, dts, pts, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "write packet");
    }

    return err;
}

srs_error_t SrsGb28181RtmpMuxer::write_hevc_sps_pps(uint32_t dts, uint32_t pts) {
	srs_error_t err = srs_success;

	// TODO: FIMXE: there exists bug, see following comments.
	// when sps or pps changed, update the sequence header,
	// for the pps maybe not changed while sps changed.
	// so, we must check when each video ts message frame parsed.
	// if (!hevc_sps_changed || !hevc_pps_changed || !hevc_vps_changed) {
	if (!(hevc_sps_changed || hevc_pps_changed || hevc_vps_changed)) {
		return err;
	}

	// hevc raw to hevc packet.
	std::string sh;
	if ((err = hevc_ptr->mux_sequence_header(h265_sps, h265_pps, h265_vps, dts, pts, sh)) != srs_success) {
		return srs_error_wrap(err, "mux sequence header");
	}

	// hevc packet to flv packet.
	int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
	int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
	char* flv = NULL;
	int nb_flv = 0;
	if ((err = hevc_ptr->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
		return srs_error_wrap(err, "avc to flv");
	}

	//if (_srs_config->get_srt_mix_correct()) {
	//	_rtmp_queue.insert_rtmp_data((unsigned char*)flv, nb_flv, (int64_t)dts, SrsFrameTypeVideo);
	//	rtmp_write_work();
	//}
	//else {
		rtmp_write_packet(SrsFrameTypeVideo, dts, flv, nb_flv);
	//}

	// reset vps, sps and pps.
	hevc_vps_changed = false;
	hevc_sps_changed = false;
	hevc_pps_changed = false;

	hevc_sps_pps_sent = true;

	return err;
}

srs_error_t SrsGb28181RtmpMuxer::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts, bool writelen)
{
    srs_error_t err = srs_success;
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    
    // for IDR frame, the frame is keyframe.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }
    
    std::string ibp;

    if (writelen){
        if ((err = avc->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
            return srs_error_wrap(err, "mux ibp frame");
        }
    }else{
        ibp = string(frame, frame_size);
    }
    
    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

srs_error_t SrsGb28181RtmpMuxer::write_hevc_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts) {
	srs_error_t err = srs_success;

	// when vps, sps or pps not sent, ignore the packet.
	if (!hevc_sps_pps_sent) {
		return srs_error_new(ERROR_HEVC_DROP_BEFORE_SPS_PPS, "drop hevc vps/sps/pps");
	}

	SrsHevcNaluType nal_unit_type = HEVC_NALU_TYPE(frame[0]);
	// for IDR frame, the frame is keyframe.
	SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;

	if ((nal_unit_type >= NAL_UNIT_CODED_SLICE_BLA) && (nal_unit_type <= NAL_UNIT_RESERVED_23)) {
		frame_type = SrsVideoAvcFrameTypeKeyFrame;
	}

	std::string ibp;
	if ((err = hevc_ptr->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
		return srs_error_wrap(err, "mux frame");
	}

	int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
	char* flv = NULL;
	int nb_flv = 0;
	if ((err = hevc_ptr->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
		return srs_error_wrap(err, "mux avc to flv");
	}
	//if (_srs_config->get_srt_mix_correct()) {
	//	_rtmp_queue.insert_rtmp_data((unsigned char*)flv, nb_flv, (int64_t)dts, SrsFrameTypeVideo);
	//	rtmp_write_work();
	//}
	//else {
		rtmp_write_packet(SrsFrameTypeVideo, dts, flv, nb_flv);
	//}

	return err;
}

srs_error_t SrsGb28181RtmpMuxer::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t SrsGb28181RtmpMuxer::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if (source_publish){
        return rtmp_write_packet_by_source(type, timestamp, data, size);
    }
    
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    SrsSharedPtrMessage* msg = NULL;
    
    send_rtmp_stream_time = srs_get_system_time();

    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk->sid(), &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(msg);
    
    // send out encoded msg.
    if ((err = sdk->send_and_free_message(msg)) != srs_success) {
        close();
        return srs_error_wrap(err, "write message");
    }
    return err;
}

srs_error_t SrsGb28181RtmpMuxer::rtmp_write_packet_by_source(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    send_rtmp_stream_time = srs_get_system_time();

    //create a source that will process stream without the need for internal rtmpclient
    if (type == SrsFrameTypeAudio) {
        SrsMessageHeader header;
        header.message_type = RTMP_MSG_AudioMessage;
        // TODO: FIXME: Maybe the tbn is not 90k.
        header.timestamp = timestamp & 0x3fffffff;

        SrsCommonMessage* shared_video = new SrsCommonMessage();
        SrsAutoFree(SrsCommonMessage, shared_video);

        // TODO: FIXME: Check error.
        shared_video->create(&header, data, size);
        source->on_audio(shared_video);
    }else if(type == SrsFrameTypeVideo) {
        SrsMessageHeader header;
        header.message_type = RTMP_MSG_VideoMessage;
        // TODO: FIXME: Maybe the tbn is not 90k.
        header.timestamp = timestamp & 0x3fffffff;

        SrsCommonMessage* shared_video = new SrsCommonMessage();
        SrsAutoFree(SrsCommonMessage, shared_video);

        // TODO: FIXME: Check error.
        shared_video->create(&header, data, size);
        source->on_video(shared_video);
    }
    
    return err;
}

srs_error_t SrsGb28181RtmpMuxer::connect()
{
    srs_error_t err = srs_success;
    
    // Ignore when connected.
    if (sdk) {
        return err;
    }
    
    // generate rtmp url to connect to.
    std::string url = _rtmp_url;
   
    // connect host.
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
  
    srs_trace("gb28181: rtmp connect url=%s", url.c_str());

    if ((err = sdk->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }
    
    // publish.
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish %s failed", url.c_str());
    }
    
    return err;
}

void SrsGb28181RtmpMuxer::close()
{
    srs_freep(sdk);
  
    // cleared and sequence header will be sent again next time.
    // RTMP close may stop through API(rtmp_close)
    h264_sps = "";
    h264_pps = "";
    aac_specific_config = "";

    // BUGFIX: if don't unpublish, it will always be in the /api/v1/streams list
    //if (source_publish && !source){
    if (source_publish && source){
        source->on_unpublish();
    }
}

void SrsGb28181RtmpMuxer::rtmp_close(){
    close();
}

SrsGb28181StreamChannel::SrsGb28181StreamChannel(){
    channel_id = "";
    port_mode = "";
    app = "";
    stream = "";
    ip = "";
    rtp_port = 0;
    rtmp_port = 0;
    ssrc = 0;
    rtp_peer_port = 0;
    rtp_peer_ip = "";
    rtmp_url = "";
    flv_url = "";
    hls_url = "";
    webrtc_url = "";
    recv_time = 0;
    recv_time_str = "";
}

SrsGb28181StreamChannel::~SrsGb28181StreamChannel()
{
    
}

void SrsGb28181StreamChannel::copy(const SrsGb28181StreamChannel *s){
    channel_id = s->get_channel_id();
    port_mode = s->get_port_mode();
    app = s->get_app();
    stream = s->get_stream();

    ip = s->get_ip();
    rtp_port = s->get_rtp_port();
    rtmp_port = s->get_rtmp_port();
    ssrc = s->get_ssrc();

    rtp_peer_ip = s->get_rtp_peer_ip();
    rtp_peer_port = s->get_rtp_peer_port();

    rtmp_url = s->get_rtmp_url();
    flv_url = s->get_flv_url();
    hls_url = s->get_hls_url();
    webrtc_url = s->get_webrtc_url();
    
    recv_time_str = s->get_recv_time_str();
    recv_time = s->get_recv_time();

}

void SrsGb28181StreamChannel::dumps(SrsJsonObject* obj)
{
    obj->set("id", SrsJsonAny::str(channel_id.c_str()));
    obj->set("ip", SrsJsonAny::str(ip.c_str()));
    obj->set("rtmp_port", SrsJsonAny::integer(rtmp_port));
    obj->set("app", SrsJsonAny::str(app.c_str()));
    obj->set("stream", SrsJsonAny::str(stream.c_str()));
    obj->set("rtmp_url", SrsJsonAny::str(rtmp_url.c_str()));
    obj->set("flv_url", SrsJsonAny::str(flv_url.c_str()));
    obj->set("hls_url", SrsJsonAny::str(hls_url.c_str()));
    obj->set("webrtc_url", SrsJsonAny::str(webrtc_url.c_str()));
   
    obj->set("ssrc", SrsJsonAny::integer(ssrc));
    obj->set("rtp_port", SrsJsonAny::integer(rtp_port));
    obj->set("port_mode", SrsJsonAny::str(port_mode.c_str()));
    obj->set("rtp_peer_port", SrsJsonAny::integer(rtp_peer_port));
    obj->set("rtp_peer_ip", SrsJsonAny::str(rtp_peer_ip.c_str()));
    obj->set("recv_time", SrsJsonAny::integer(recv_time/SRS_UTIME_SECONDS));
    obj->set("recv_time_str", SrsJsonAny::str(recv_time_str.c_str()));
    
}


//Global Singleton instance, init in SrsServer
SrsGb28181Manger* _srs_gb28181 = NULL;

//SrsGb28181Manger
SrsGb28181Manger::SrsGb28181Manger(SrsServer *s, SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    server = s;
    config = new SrsGb28181Config(c);
    manager = new SrsResourceManager("GB28181");
}

SrsGb28181Manger::~SrsGb28181Manger()
{
    used_ports.clear();
    destroy();
  
    srs_freep(manager);
    srs_freep(config);
}

srs_error_t SrsGb28181Manger::initialize()
{
    srs_error_t err = srs_success;
    if ((err = manager->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }
  
    return err;
}

SrsGb28181Config SrsGb28181Manger::get_gb28181_config()
{
    return *config;
}

void SrsGb28181Manger::alloc_port(int* pport)
{
    // use a pair of port.
    for (int i = config->rtp_port_min; i < config->rtp_port_max - 1; i += 2) {
        if (!used_ports[i]) {
            used_ports[i] = true;
            used_ports[i + 1] = true;
            *pport = i;
            break;
        }
    }
    srs_info("gb28181: alloc port=%d-%d", *pport, *pport + 1);
}

void SrsGb28181Manger::free_port(int lpmin, int lpmax)
{
    for (int i = lpmin; i < lpmax; i++) {
        used_ports[i] = false;
    }
    srs_trace("gb28181: free rtp port=%d-%d", lpmin, lpmax);
}

uint32_t SrsGb28181Manger::hash_code(std::string str)
{  
    uint32_t h = 0;  
    int len = str.length();
    
    if (h == 0) {  
        int off = 0;  
        const char *val = str.c_str();
        
        for (int i = 0; i < len; i++) {  
            h = 31 * h + val[off++];  
        }  
    }  
    return h;  
} 

uint32_t SrsGb28181Manger::generate_ssrc(std::string id)
{
    srand(uint(time(0)));
    // TODO: SSRC rules can be customized,
    //gb28181 live ssrc max value 0999999999(3B9AC9FF)  
    //gb28181 vod ssrc max value 1999999999(773593FF)
    uint8_t  index = uint8_t(rand() % (0x0F - 0x01 + 1) + 0x01);
    uint32_t ssrc = ((0x2FFFF00) & (hash_code(id) << 8)) | index;
    //uint32_t ssrc = 0x00FFFFFF & (hash_code(id));
    srs_trace("gb28181: generate ssrc id=%s, ssrc=%u", id.c_str(), ssrc);
    return  ssrc;
}

srs_error_t SrsGb28181Manger::fetch_or_create_rtmpmuxer(std::string id,  SrsRequest *req,  SrsGb28181RtmpMuxer** gb28181)
{
    srs_error_t err = srs_success;

    SrsGb28181RtmpMuxer* muxer = NULL;
    if ((muxer = fetch_rtmpmuxer(id)) != NULL) {
        *gb28181 = muxer;
        return err;
    }
    
    muxer = new SrsGb28181RtmpMuxer(this, id, config->audio_enable, config->wait_keyframe);
    if ((err = muxer->initialize(server, req)) != srs_success) {
        return srs_error_wrap(err, "gb28181: rtmp muxer initialize %s", id.c_str());
    }

    if ((err = muxer->serve()) != srs_success) {
        return srs_error_wrap(err, "gb28181: rtmp muxer serve %s", id.c_str());
    }
    rtmpmuxers[id] = muxer;
    *gb28181 = muxer;
    
    return err;
}

SrsGb28181RtmpMuxer* SrsGb28181Manger::fetch_rtmpmuxer(std::string id)
{
    SrsGb28181RtmpMuxer* muxer = NULL;
    
    if (rtmpmuxers.find(id) == rtmpmuxers.end()) {
        return NULL;
    }
    
    muxer = rtmpmuxers[id];
    return muxer;
}

void SrsGb28181Manger::update_rtmpmuxer_to_newssrc_by_id(std::string id, uint32_t ssrc)
{
    SrsGb28181RtmpMuxer* muxer = NULL;

    if (rtmpmuxers.find(id) == rtmpmuxers.end()) {
        srs_warn("gb28181: at update_rtmpmuxer_to_newssrc_by_id() client_id not found. client_id=%s",id.c_str());
        return;
    }

    muxer = rtmpmuxers[id];
    
    SrsGb28181StreamChannel mc = muxer->get_channel();
    uint32_t old_ssrc = mc.get_ssrc();
    if (old_ssrc == ssrc) {
        return;
    } else {
        srs_trace("gb28181: update ssrc of muxer %s from %x to %x", id.c_str(), old_ssrc, ssrc);
    }
    rtmpmuxer_unmap_by_ssrc(old_ssrc);
    mc.set_ssrc(ssrc);
    muxer->copy_channel(&mc);
    rtmpmuxer_map_by_ssrc(muxer, ssrc);
}

SrsGb28181RtmpMuxer* SrsGb28181Manger::fetch_rtmpmuxer_by_ssrc(uint32_t ssrc)
{
    SrsGb28181RtmpMuxer* muxer = NULL;
    if (rtmpmuxers_ssrc.find(ssrc) == rtmpmuxers_ssrc.end()) {
        return NULL;
    }
    
    muxer = rtmpmuxers_ssrc[ssrc];
    return muxer;
}

void SrsGb28181Manger::rtmpmuxer_map_by_ssrc(SrsGb28181RtmpMuxer*muxer, uint32_t ssrc)
{
    if (rtmpmuxers_ssrc.find(ssrc) == rtmpmuxers_ssrc.end()) {
         rtmpmuxers_ssrc[ssrc] = muxer;
    }
}

void SrsGb28181Manger::rtmpmuxer_unmap_by_ssrc(uint32_t ssrc)
{
    std::map<uint32_t, SrsGb28181RtmpMuxer*>::iterator it = rtmpmuxers_ssrc.find(ssrc);
    if (it != rtmpmuxers_ssrc.end()) {
         rtmpmuxers_ssrc.erase(it);
    }
}

void SrsGb28181Manger::destroy()
{
    if (!config->rtp_mux_tcp_enable) {
        //destory ps rtp listen
        std::map<uint32_t, SrsPsRtpListener*>::iterator it;
        for (it = rtp_pool.begin(); it != rtp_pool.end(); ++it) {
            SrsPsRtpListener* listener = it->second;
            srs_freep(listener);
        }
        rtp_pool.clear();
    }

    //destory gb28181 muxer
    std::map<std::string, SrsGb28181RtmpMuxer*>::iterator it;
    for (it = rtmpmuxers.begin(); it != rtmpmuxers.end(); ++it) {
        SrsGb28181RtmpMuxer* muxer = it->second;
        SrsGb28181StreamChannel sess = muxer->get_channel();
        rtmpmuxer_unmap_by_ssrc(sess.get_ssrc());
        manager->remove(muxer);
    }
    rtmpmuxers.clear();
}

void SrsGb28181Manger::remove(SrsGb28181RtmpMuxer* muxer)
{
    std::string id = muxer->get_channel_id();
  
    map<std::string, SrsGb28181RtmpMuxer*>::iterator it = rtmpmuxers.find(id);
    if (it != rtmpmuxers.end()) {
        SrsGb28181RtmpMuxer* muxer = it->second;
        SrsGb28181StreamChannel sess = muxer->get_channel();
        rtmpmuxer_unmap_by_ssrc(sess.get_ssrc());
        rtmpmuxers.erase(it);
    }
    manager->remove(muxer);
}

void SrsGb28181Manger::remove_sip_session(SrsGb28181SipSession* sess)
{
    manager->remove(sess);
}

srs_error_t SrsGb28181Manger::start_ps_rtp_listen(std::string id, int port)
{
    srs_error_t err = srs_success;
    if (port == config->rtp_mux_port) {
        return srs_error_wrap(err, "start rtp listen port is mux port"); 
    }

    /* delete by xbpeng 20201222 should not check rtmpmuxers, becasue it always not find*/
    // map<std::string, SrsGb28181RtmpMuxer*>::iterator key = rtmpmuxers.find(id);
    // if (key == rtmpmuxers.end()){
    //     srs_warn("start rtp listen port rtmp muxer is null. id=%s,port=%d", id.c_str(),port); 
    //     return srs_error_wrap(err, "start rtp listen port rtmp muxer is null"); 
    // }

    if (!config->rtp_mux_tcp_enable) {
        if (rtp_pool.find(port) == rtp_pool.end())
        {
            SrsPsRtpListener* rtp = new SrsPsRtpListener(this->config, port, id);
            rtp_pool[port] = rtp;
            if ((err = rtp_pool[port]->listen()) != srs_success) {
                stop_rtp_listen(id);
                return srs_error_wrap(err, "rtp listen");
            }

            srs_trace("gb28181: start rtp ps stream over server-port=%d", port);
        }
    }

    return err;
}

void SrsGb28181Manger::stop_rtp_listen(std::string id)
{
    map<std::string, SrsGb28181RtmpMuxer*>::iterator it = rtmpmuxers.find(id);
    if (it == rtmpmuxers.end()){
       return; 
    }

    SrsGb28181RtmpMuxer* muxer = it->second;
    SrsGb28181StreamChannel sess = muxer->get_channel();

    int port = sess.get_rtp_port();
    if (port == config->rtp_mux_port) {
        return; 
    }

    if (!config->rtp_mux_tcp_enable) {
        map<uint32_t, SrsPsRtpListener*>::iterator it2 = rtp_pool.find(port);
        if (it2 != rtp_pool.end()) {
            srs_freep(it2->second);
            rtp_pool.erase(it2);
        }
    }
    free_port(port, port+1);
}

//api
srs_error_t SrsGb28181Manger::create_stream_channel(SrsGb28181StreamChannel *channel)
{
    srs_error_t err = srs_success;
    srs_assert(channel);

    std::string id = channel->get_channel_id();
    SrsGb28181RtmpMuxer *muxer = NULL;

    muxer = fetch_rtmpmuxer(id);
    if (muxer){
       SrsGb28181StreamChannel s = muxer->get_channel();
       channel->copy(&s);
       //return ERROR_GB28181_SESSION_IS_EXIST;
       return err;
    }

    //Start RTP listening port, receive gb28181 stream, 
    //fixed is mux port, 
    //random is random allocation port
    int rtp_port = 0;
    std::string port_mode = channel->get_port_mode();
    
    if (port_mode.empty()){
        port_mode = RTP_PORT_MODE_FIXED;
        channel->set_port_mode(port_mode);
    }
   
    if (port_mode == RTP_PORT_MODE_RANDOM){
        alloc_port(&rtp_port);
        if (rtp_port <= 0){
           return srs_error_new(ERROR_GB28181_RTP_PORT_FULL, "gb28181: rtp port full");
        }
     
        if ((err = start_ps_rtp_listen(id, rtp_port)) != srs_success){
            free_port(rtp_port, rtp_port + 1);
            return  err;
        }
    }
    else if(port_mode == RTP_PORT_MODE_FIXED) {
        rtp_port = config->rtp_mux_port;
    }
    else{
        return srs_error_new(ERROR_GB28181_PORT_MODE_INVALID, "gb28181: port mode invalid");
    }

    uint32_t ssrc = channel->get_ssrc();
    if (ssrc == 0){
        //auto generate SSRC according to the hash code, 
        //of the string value of the id
        ssrc = generate_ssrc(id);
    }

    //generate RTMP push stream address,
    //if the app and stream in the API are empty, 
    //RTMP URL is generated using the output template parameter
    std::string url = "";
    int rtmp_port;
    string app = channel->get_app();
    string stream = channel->get_stream();

    SrsRequest request;

    if (true) {
        string tcUrl, stream_name;

        //get template rtmp url configuration
        std::string output = config->output;
        srs_parse_rtmp_url(output, tcUrl, stream_name);
        
        string _schema, _host, _vhost, _param, _app, _stream;
        srs_discovery_tc_url(tcUrl, _schema, _host, _vhost, _app, _stream, rtmp_port, _param);

        //if the stream name is not parameterized, 
        //it needs to be parameterized to ensure that the stream name is different
        if (!srs_string_contains(stream_name, "[stream]") &&
            !srs_string_contains(stream_name, "[timestamp]") &&
            !srs_string_contains(stream_name, "[ssrc]")){
            stream_name = stream_name + "_[stream]";
        }

        if (app.empty()){
            app = _app;
        }

        if (stream.empty())
        {
            stream = stream_name;
        }

        url = srs_generate_rtmp_url(_host, rtmp_port, "", "", app, stream, "");
        url = srs_string_replace(url, "[app]", "live");
        url = srs_string_replace(url, "[stream]", id);
        std::stringstream ss;
        ss << ssrc;
        url = srs_string_replace(url, "[ssrc]", ss.str());
        url = srs_path_build_timestamp(url);
        
        //update channel app stream value
        srs_parse_rtmp_url(url, tcUrl, stream_name);
        srs_discovery_tc_url(tcUrl, _schema, _host, _vhost, _app, _stream, rtmp_port, _param);
        
        //generate the value returned to the api response
        channel->set_rtp_port(rtp_port);
        channel->set_ssrc(ssrc);

        channel->set_app(_app);
        channel->set_stream(stream_name);
        channel->set_rtmp_port(rtmp_port);
        channel->set_ip(config->host);
        std::string play_url = srs_generate_rtmp_url(config->host, rtmp_port, "", "", app, stream_name, "");
        
        std::string flv_url = srs_string_replace(play_url, "rtmp://", "http://");
        std::stringstream port;
        port << ":" << rtmp_port;
        flv_url = srs_string_replace(flv_url, port.str(), ":"+_srs_config->get_http_stream_listen());
        std::string hls_url = flv_url + ".m3u8";
        flv_url = flv_url + ".flv";
     
        std::string webrtc_url = srs_string_replace(play_url, "rtmp://", "webrtc://");
        webrtc_url = srs_string_replace(webrtc_url, port.str(), ":"+_srs_config->get_http_api_listen());

        channel->set_rtmp_url(play_url);
        channel->set_flv_url(flv_url);
        channel->set_hls_url(hls_url);
        channel->set_webrtc_url(webrtc_url);

        request.app = app;
        request.stream = stream_name;
        //request.vhost = config->host;
    }

    //create on rtmp muxer, gb28181 stream to rtmp
    if ((err = fetch_or_create_rtmpmuxer(id, &request, &muxer)) != srs_success){
        srs_warn("gb28181: create rtmp muxer error, %s", srs_error_desc(err).c_str());
        return err;
    }

    rtmpmuxer_map_by_ssrc(muxer, ssrc);
    muxer->set_rtmp_url(url);
    srs_trace("gb28181: create new stream channel id:%s rtmp url=%s", id.c_str(), url.c_str());

    muxer->copy_channel(channel);

    return err;
}

srs_error_t SrsGb28181Manger::delete_stream_channel(std::string id, std::string chid)
{
    srs_error_t err = srs_success;

    //notify the device to stop streaming 
    //if an internal sip service controlled channel
    notify_sip_bye(id, chid);

    string channel_id = id + "@" + chid;

    SrsGb28181RtmpMuxer *muxer = fetch_rtmpmuxer(channel_id);
    if (muxer){
        stop_rtp_listen(channel_id);
        muxer->stop();
       return err;
    }else {
       return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "stream channel is not exists");
    }
}


srs_error_t SrsGb28181Manger::query_stream_channel(std::string id, SrsJsonArray* arr)
{
    srs_error_t err = srs_success;

    if (!id.empty()){
        SrsGb28181RtmpMuxer *muxer = fetch_rtmpmuxer(id);
        if (!muxer){
            return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "stream channel not exists");
        }
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        muxer->get_channel().dumps(obj);
    }else {
        std::map<std::string, SrsGb28181RtmpMuxer*>::iterator it2;
        for (it2 = rtmpmuxers.begin(); it2 != rtmpmuxers.end(); ++it2) {
            SrsGb28181RtmpMuxer* muxer = it2->second;
            SrsJsonObject* obj = SrsJsonAny::object();
            arr->append(obj);
            muxer->get_channel().dumps(obj);
        }
    }

    return err;
}

srs_error_t SrsGb28181Manger::notify_sip_invite(std::string id, std::string ip, int port, uint32_t ssrc, std::string chid)
{
    srs_error_t err = srs_success;
    
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }
   
    //if RTMP Muxer does not exist, you need to create
    std::string key = id+"@"+chid;
    SrsGb28181RtmpMuxer *muxer = fetch_rtmpmuxer(key);
   
    if (!muxer){
        //if there is an invalid parameter, the channel will be created automatically
        if (ip.empty() || port == 0 || ssrc == 0){
             //channel not exist
            SrsGb28181StreamChannel channel;
            channel.set_channel_id(key);
            if (!this->config->sip_invite_port_fixed) {
                channel.set_port_mode(RTP_PORT_MODE_RANDOM);
            }else
            {
                channel.set_port_mode(RTP_PORT_MODE_FIXED);
            }
            err =  create_stream_channel(&channel);
            if (err != srs_success){
                return err;
            }

            ip = channel.get_ip();
            port = channel.get_rtp_port();
            ssrc = channel.get_ssrc();
        }
    }else {
        //channel exit, use channel config
        SrsGb28181StreamChannel channel = muxer->get_channel();
        ip = channel.get_ip();
        port = channel.get_rtp_port();
        ssrc = channel.get_ssrc();
    }

    SrsSipRequest req;
    req.sip_auth_id = id;
    return sip_service->send_invite(&req, ip, port, ssrc, chid);
}

srs_error_t SrsGb28181Manger::notify_sip_bye(std::string id, std::string chid)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }

    SrsSipRequest req;
    req.sip_auth_id = id;
    return sip_service->send_bye(&req, chid);
}

srs_error_t SrsGb28181Manger::notify_sip_ptz(std::string id, std::string chid, std::string cmd, 
            uint8_t speed,  int priority)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }

    SrsSipRequest req;
    req.sip_auth_id = id;
    return sip_service->send_ptz(&req, chid, cmd, speed, priority);
}

srs_error_t SrsGb28181Manger::notify_sip_unregister(std::string id)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }
    sip_service->remove_session(id);
    return srs_success;
    // useless, because
    //   sip session has been removed
    //   id is not channel id like id@chid
    //return delete_stream_channel(id);
}

srs_error_t SrsGb28181Manger::notify_sip_query_catalog(std::string id)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }

    SrsSipRequest req;
    req.sip_auth_id = id;
    SrsGb28181SipSession *sip_session = sip_service->fetch(req.sip_auth_id);
    if (sip_session) {
        sip_session->item_list.clear();
        sip_session->clear_device_list();
        srs_trace("notify_sip_query_catalog, clear sip session item and device list");
    }
    return sip_service->send_query_catalog(&req);
}

srs_error_t SrsGb28181Manger::query_sip_session(std::string id, SrsJsonArray* arr)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }
    
    return sip_service->query_sip_session(id, arr);
}

srs_error_t SrsGb28181Manger::query_device_list(std::string id, SrsJsonArray* arr)
{
    if (!sip_service){
        return srs_error_new(ERROR_GB28181_SIP_NOT_RUN, "sip not run");
    }

    return sip_service->query_device_list(id, arr);
}
#define SRS_RTSP_BUFFER 8192
#define RTP_TCP_HEADER 2
#define MAX_PACKAGE_SIZE 1024 * 10
SrsGb28181Conn::SrsGb28181Conn(SrsGb28181Caster* c, srs_netfd_t fd, SrsGb28181PsRtpProcessor *rtp_processor)
{
	caster = c;
	stfd = fd;
	skt = new SrsStSocket();
	rtsp = new SrsRtspStack(skt);
	trd = new SrsSTCoroutine("rtsp", this);
	mbuffer = (char*)malloc(SRS_RTSP_BUFFER);
	processor = rtp_processor;
}

SrsGb28181Conn::~SrsGb28181Conn()
{
	free(mbuffer);
	srs_close_stfd(stfd);

	srs_freep(trd);
	srs_freep(skt);
	srs_freep(rtsp);
}

srs_error_t SrsGb28181Conn::serve()
{
	srs_error_t err = srs_success;

	if ((err = skt->initialize(stfd)) != srs_success) {
		return srs_error_wrap(err, "socket initialize");
	}

	if ((err = trd->start()) != srs_success) {
		return srs_error_wrap(err, "rtsp connection");
	}
	return err;
}

std::string SrsGb28181Conn::remote_ip()
{
	// TODO: FIXME: Implement it.
	return "";
}

srs_error_t SrsGb28181Conn::do_cycle()
{
	srs_error_t err = srs_success;

    // retrieve ip of client.
    int fd = srs_netfd_fileno(stfd);
    std::string ip = srs_get_peer_ip(fd);
    int port = srs_get_peer_port(fd);
    int addr_len = sizeof(sockaddr_in);
    sockaddr_in *peer_sockaddr = (sockaddr_in*)malloc(addr_len);
    peer_sockaddr->sin_family = AF_INET;  //设置地址家族
    peer_sockaddr->sin_port = htons(port);  //设置端口
    peer_sockaddr->sin_addr.s_addr = inet_addr(ip.c_str());


    if (ip.empty() && !_srs_config->empty_ip_ok()) {
        srs_warn("empty ip for fd=%d", srs_netfd_fileno(stfd));
    }
    srs_trace("gb28181 new connect by rtp-tcp from: %s:%d", ip.c_str(), port);

    uint32_t left_data_len = 0; //缓存剩余数据
    ssize_t nb_read = 0;
    uint16_t  packet_len = 0; //rtp包长度

    // consume all rtp data.
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtsp cycle");
        }
        nb_read = 0;
        if ((err = skt->read(mbuffer + left_data_len, SRS_RTSP_BUFFER - left_data_len, &nb_read)) != srs_success) {
            return srs_error_wrap(err, "recv data");
        }

        left_data_len = nb_read + left_data_len;
        char * buf = mbuffer;

        uint32_t index = 0;
        for( ; index < left_data_len; ){
            if (index + RTP_TCP_HEADER >= left_data_len){ //less rtp package
                break;
            }
            packet_len = (((uint8_t *) buf)[index] << 8) | ((uint8_t *) buf)[index + 1];
            if (packet_len > MAX_PACKAGE_SIZE){
                //FIXME 自动重新invite?
                srs_error("abnormal RTP packet length:%d, close the tcp conn:%s", packet_len, remote_ip().c_str());
                return err;
            }
            if (index + RTP_TCP_HEADER + packet_len >= left_data_len){
                break;
            }
            processor->on_tcp_packet((sockaddr*)peer_sockaddr, addr_len, buf + index + RTP_TCP_HEADER, packet_len);
            index = index + RTP_TCP_HEADER + packet_len;
        }
        if (index != 0) { //update left data
            left_data_len = left_data_len - index;
            memmove(mbuffer, buf + index, left_data_len);
        }

    }
    free(peer_sockaddr);
    return err;
}

srs_error_t SrsGb28181Conn::cycle()
{
	// serve the rtsp client.
	srs_error_t err = do_cycle();

	caster->remove(this);

	if (err == srs_success) {
		srs_trace("client finished.");
	}
	else if (srs_is_client_gracefully_close(err)) {
		srs_warn("client disconnect peer. code=%d", srs_error_code(err));
		srs_freep(err);
	}

	return err;
}

std::string SrsGb28181Conn::desc()
{
    return "GB28181TcpConn";
}

const SrsContextId& SrsGb28181Conn::get_id()
{
    return trd->cid();
}

SrsGb28181Caster::SrsGb28181Caster(SrsConfDirective* c)
{
	// TODO: FIXME: support reload.
	output = _srs_config->get_stream_caster_output(c);
	config = new SrsGb28181Config(c);
	rtp_processor = new SrsGb28181PsRtpProcessor(config, "");
	manager = new SrsResourceManager("GB28181TCP", true);
}

SrsGb28181Caster::~SrsGb28181Caster()
{
	std::vector<SrsGb28181Conn*>::iterator it;
	for (it = clients.begin(); it != clients.end(); ++it) {
		SrsGb28181Conn* conn = *it;
		manager->remove(conn);
	}
	clients.clear();

	srs_freep(manager);
}

srs_error_t SrsGb28181Caster::initialize()
{
	srs_error_t err = srs_success;
	if ((err = manager->start()) != srs_success) {
		return srs_error_wrap(err, "start manager");
	}
	return err;
}

srs_error_t SrsGb28181Caster::on_tcp_client(srs_netfd_t stfd)
{
	srs_error_t err = srs_success;

	SrsGb28181Conn* conn = new SrsGb28181Conn(this, stfd, rtp_processor);

	if ((err = conn->serve()) != srs_success) {
		srs_freep(conn);
		return srs_error_wrap(err, "serve conn");
	}

	clients.push_back(conn);

	return err;
}

void SrsGb28181Caster::remove(SrsGb28181Conn* conn)
{
	std::vector<SrsGb28181Conn*>::iterator it = find(clients.begin(), clients.end(), conn);
	if (it != clients.end()) {
		clients.erase(it);
	}
	srs_info("rtsp: remove connection from caster.");

	manager->remove(conn);
}

