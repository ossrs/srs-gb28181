//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_RTSP_HPP
#define SRS_PROTOCOL_RTSP_HPP

#include <srs_core.hpp>

#include <string>
#include <sstream>

#include <srs_kernel_consts.hpp>

class SrsBuffer;
class SrsSimpleStream;
class SrsAudioFrame;
class ISrsProtocolReadWriter;

// From rtsp specification
// CR = <US-ASCII CR, carriage return (13)>
#define SRS_RTSP_CR SRS_CONSTS_CR // 0x0D
// LF = <US-ASCII LF, linefeed (10)>
#define SRS_RTSP_LF SRS_CONSTS_LF // 0x0A
// SP = <US-ASCII SP, space (32)>
#define SRS_RTSP_SP ' ' // 0x20

// 4 RTSP Message, @see rfc2326-1998-rtsp.pdf, page 37
// Lines are terminated by CRLF, but
// receivers should be prepared to also interpret CR and LF by
// themselves as line terminators.
#define SRS_RTSP_CRLF "\r\n" // 0x0D0A
#define SRS_RTSP_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// RTSP token
#define SRS_RTSP_TOKEN_CSEQ "CSeq"
#define SRS_RTSP_TOKEN_PUBLIC "Public"
#define SRS_RTSP_TOKEN_CONTENT_TYPE "Content-Type"
#define SRS_RTSP_TOKEN_CONTENT_LENGTH "Content-Length"
#define SRS_RTSP_TOKEN_TRANSPORT "Transport"
#define SRS_RTSP_TOKEN_SESSION "Session"

// RTSP methods
#define SRS_METHOD_OPTIONS            "OPTIONS"
#define SRS_METHOD_DESCRIBE           "DESCRIBE"
#define SRS_METHOD_ANNOUNCE           "ANNOUNCE"
#define SRS_METHOD_SETUP              "SETUP"
#define SRS_METHOD_PLAY               "PLAY"
#define SRS_METHOD_PAUSE              "PAUSE"
#define SRS_METHOD_TEARDOWN           "TEARDOWN"
#define SRS_METHOD_GET_PARAMETER      "GET_PARAMETER"
#define SRS_METHOD_SET_PARAMETER      "SET_PARAMETER"
#define SRS_METHOD_REDIRECT           "REDIRECT"
#define SRS_METHOD_RECORD             "RECORD"
// Embedded (Interleaved) Binary Data

// RTSP-Version
#define SRS_RTSP_VERSION "RTSP/1.0"

// The rtsp sdp parse state.
enum SrsRtspSdpState
{
    // Other sdp properties.
    SrsRtspSdpStateOthers,
    // Parse sdp audio state.
    SrsRtspSdpStateAudio,
    // Parse sdp video state.
    SrsRtspSdpStateVideo,
};

// 10 Method Definitions, @see rfc2326-1998-rtsp.pdf, page 57
// The method token indicates the method to be performed on the resource
// identified by the Request-URI. The method is case-sensitive. New
// methods may be defined in the future. Method names may not start with
// a $ character (decimal 24) and must be a token. Methods are
// summarized in Table 2.
// Notes on Table 2: PAUSE is recommended, but not required in that a
// fully functional server can be built that does not support this
// method, for example, for live feeds. If a server does not support a
// particular method, it MUST return "501 Not Implemented" and a client
// SHOULD not try this method again for this server.
enum SrsRtspMethod
{
    SrsRtspMethodDescribe = 0x0001,
    SrsRtspMethodAnnounce = 0x0002,
    SrsRtspMethodGetParameter = 0x0004,
    SrsRtspMethodOptions = 0x0008,
    SrsRtspMethodPause = 0x0010,
    SrsRtspMethodPlay = 0x0020,
    SrsRtspMethodRecord = 0x0040,
    SrsRtspMethodRedirect = 0x0080,
    SrsRtspMethodSetup = 0x0100,
    SrsRtspMethodSetParameter = 0x0200,
    SrsRtspMethodTeardown = 0x0400,
};

// The state of rtsp token.
enum SrsRtspTokenState
{
    // Parse token failed, default state.
    SrsRtspTokenStateError = 100,
    // When SP follow the token.
    SrsRtspTokenStateNormal = 101,
    // When CRLF follow the token.
    SrsRtspTokenStateEOF = 102,
};

// The rtp packet.
// 5. RTP Data Transfer Protocol, @see rfc3550-2003-rtp.pdf, page 12
class SrsRtspPacket
{
public:
    // The version (V): 2 bits
    // This field identifies the version of RTP. The version defined by this specification is two (2).
    // (The value 1 is used by the first draft version of RTP and the value 0 is used by the protocol
    // initially implemented in the \vat" audio tool.)
    int8_t version; //2bits
    // The padding (P): 1 bit
    // If the padding bit is set, the packet contains one or more additional padding octets at the
    // end which are not part of the payload. The last octet of the padding contains a count of
    // how many padding octets should be ignored, including itself. Padding may be needed by
    // some encryption algorithms with fixed block sizes or for carrying several RTP packets in a
    // lower-layer protocol data unit.
    int8_t padding; //1bit
    // The extension (X): 1 bit
    // If the extension bit is set, the fixed header must be followed by exactly one header extension,
    // with a format defined in Section 5.3.1.
    int8_t extension; //1bit
    // The CSRC count (CC): 4 bits
    // The CSRC count contains the number of CSRC identifiers that follow the fixed header.
    int8_t csrc_count; //4bits
    // The marker (M): 1 bit
    // The interpretation of the marker is defined by a profile. It is intended to allow significant
    // events such as frame boundaries to be marked in the packet stream. A profile may define
    // additional marker bits or specify that there is no marker bit by changing the number of bits
    // in the payload type field (see Section 5.3).
    int8_t marker; //1bit
    // The payload type (PT): 7 bits
    // This field identifies the format of the RTP payload and determines its interpretation by the
    // application. A profile may specify a default static mapping of payload type codes to payload
    // formats. Additional payload type codes may be defined dynamically through non-RTP means
    // (see Section 3). A set of default mappings for audio and video is specified in the companion
    // RFC 3551 [1]. An RTP source may change the payload type during a session, but this field
    // should not be used for multiplexing separate media streams (see Section 5.2).
    // A receiver must ignore packets with payload types that it does not understand.
    int8_t payload_type; //7bits
    // The sequence number: 16 bits
    // The sequence number increments by one for each RTP data packet sent, and may be used
    // by the receiver to detect packet loss and to restore packet sequence. The initial value of the
    // sequence number should be random (unpredictable) to make known-plaintext attacks on
    // encryption more dicult, even if the source itself does not encrypt according to the method
    // in Section 9.1, because the packets may flow through a translator that does. Techniques for
    // choosing unpredictable numbers are discussed in [17].
    uint16_t sequence_number; //16bits
    // The timestamp: 32 bits
    // The timestamp reflects the sampling instant of the first octet in the RTP data packet. The
    // sampling instant must be derived from a clock that increments monotonically and linearly
    // in time to allow synchronization and jitter calculations (see Section 6.4.1). The resolution
    // of the clock must be sucient for the desired synchronization accuracy and for measuring
    // packet arrival jitter (one tick per video frame is typically not sucient). The clock frequency
    // is dependent on the format of data carried as payload and is specified statically in the profile
    // or payload format specification that defines the format, or may be specified dynamically for
    // payload formats defined through non-RTP means. If RTP packets are generated periodically,
    // The nominal sampling instant as determined from the sampling clock is to be used, not a
    // reading of the system clock. As an example, for fixed-rate audio the timestamp clock would
    // likely increment by one for each sampling period. If an audio application reads blocks covering
    // 160 sampling periods from the input device, the timestamp would be increased by 160 for
    // each such block, regardless of whether the block is transmitted in a packet or dropped as
    // silent.
    // 
    // The initial value of the timestamp should be random, as for the sequence number. Several
    // consecutive RTP packets will have equal timestamps if they are (logically) generated at once,
    // e.g., belong to the same video frame. Consecutive RTP packets may contain timestamps that
    // are not monotonic if the data is not transmitted in the order it was sampled, as in the case
    // of MPEG interpolated video frames. (The sequence numbers of the packets as transmitted
    // will still be monotonic.)
    // 
    // RTP timestamps from different media streams may advance at different rates and usually
    // have independent, random offsets. Therefore, although these timestamps are sucient to
    // reconstruct the timing of a single stream, directly comparing RTP timestamps from different
    // media is not effective for synchronization. Instead, for each medium the RTP timestamp
    // is related to the sampling instant by pairing it with a timestamp from a reference clock
    // (wallclock) that represents the time when the data corresponding to the RTP timestamp was
    // sampled. The reference clock is shared by all media to be synchronized. The timestamp
    // pairs are not transmitted in every data packet, but at a lower rate in RTCP SR packets as
    // described in Section 6.4.
    // 
    // The sampling instant is chosen as the point of reference for the RTP timestamp because it is
    // known to the transmitting endpoint and has a common definition for all media, independent
    // of encoding delays or other processing. The purpose is to allow synchronized presentation of
    // all media sampled at the same time.
    // 
    // Applications transmitting stored data rather than data sampled in real time typically use a
    // virtual presentation timeline derived from wallclock time to determine when the next frame
    // or other unit of each medium in the stored data should be presented. In this case, the RTP
    // timestamp would reflect the presentation time for each unit. That is, the RTP timestamp for
    // each unit would be related to the wallclock time at which the unit becomes current on the
    // virtual presentation timeline. Actual presentation occurs some time later as determined by
    // The receiver.
    // 
    // An example describing live audio narration of prerecorded video illustrates the significance
    // of choosing the sampling instant as the reference point. In this scenario, the video would
    // be presented locally for the narrator to view and would be simultaneously transmitted using
    // RTP. The sampling instant" of a video frame transmitted in RTP would be established by
    // referencing its timestamp to the wallclock time when that video frame was presented to the
    // narrator. The sampling instant for the audio RTP packets containing the narrator's speech
    // would be established by referencing the same wallclock time when the audio was sampled.
    // The audio and video may even be transmitted by different hosts if the reference clocks on
    // The two hosts are synchronized by some means such as NTP. A receiver can then synchronize
    // presentation of the audio and video packets by relating their RTP timestamps using the
    // timestamp pairs in RTCP SR packets.
    uint32_t timestamp; //32bits
    // The SSRC: 32 bits
    // The SSRC field identifies the synchronization source. This identifier should be chosen
    // randomly, with the intent that no two synchronization sources within the same RTP session
    // will have the same SSRC identifier. An example algorithm for generating a random identifier
    // is presented in Appendix A.6. Although the probability of multiple sources choosing the same
    // identifier is low, all RTP implementations must be prepared to detect and resolve collisions.
    // Section 8 describes the probability of collision along with a mechanism for resolving collisions
    // and detecting RTP-level forwarding loops based on the uniqueness of the SSRC identifier. If
    // a source changes its source transport address, it must also choose a new SSRC identifier to
    // avoid being interpreted as a looped source (see Section 8.2).
    uint32_t ssrc; //32bits
    
    // The payload.
    SrsSimpleStream* payload;
    // Whether transport in chunked payload.
    bool chunked;
    // Whether message is completed.
    // normal message always completed.
    // while chunked completed when the last chunk arriaved.
    bool completed;
    
    // The audio samples, one rtp packets may contains multiple audio samples.
    SrsAudioFrame* audio;
public:
    SrsRtspPacket();
    virtual ~SrsRtspPacket();
public:
    // copy the header from src.
    virtual void copy(SrsRtspPacket* src);
    // reap the src to this packet, reap the payload.
    virtual void reap(SrsRtspPacket* src);
    // decode rtp packet from stream.
    virtual srs_error_t decode(SrsBuffer* stream);
private:
    virtual srs_error_t decode_97(SrsBuffer* stream);
    virtual srs_error_t decode_96(SrsBuffer* stream);
};

// The sdp in announce, @see rfc2326-1998-rtsp.pdf, page 159
// Appendix C: Use of SDP for RTSP Session Descriptions
// The Session Description Protocol (SDP, RFC 2327 [6]) may be used to
// describe streams or presentations in RTSP.
class SrsRtspSdp
{
private:
    SrsRtspSdpState state;
public:
    // The version of sdp.
    std::string version;
    // The owner/creator of sdp.
    std::string owner_username;
    std::string owner_session_id;
    std::string owner_session_version;
    std::string owner_network_type;
    std::string owner_address_type;
    std::string owner_address;
    // The session name of sdp.
    std::string session_name;
    // The connection info of sdp.
    std::string connection_network_type;
    std::string connection_address_type;
    std::string connection_address;
    // The tool attribute of sdp.
    std::string tool;
    // The video attribute of sdp.
    std::string video_port;
    std::string video_protocol;
    std::string video_transport_format;
    std::string video_bandwidth_kbps;
    std::string video_codec;
    std::string video_sample_rate;
    std::string video_stream_id;
    // The fmtp
    std::string video_packetization_mode;
    std::string video_sps; // sequence header: sps.
    std::string video_pps; // sequence header: pps.
    // The audio attribute of sdp.
    std::string audio_port;
    std::string audio_protocol;
    std::string audio_transport_format;
    std::string audio_bandwidth_kbps;
    std::string audio_codec;
    std::string audio_sample_rate;
    std::string audio_channel;
    std::string audio_stream_id;
    // The fmtp
    std::string audio_profile_level_id;
    std::string audio_mode;
    std::string audio_size_length;
    std::string audio_index_length;
    std::string audio_index_delta_length;
    std::string audio_sh; // sequence header.
public:
    SrsRtspSdp();
    virtual ~SrsRtspSdp();
public:
    // Parse a line of token for sdp.
    virtual srs_error_t parse(std::string token);
private:
    // generally, the fmtp is the sequence header for video or audio.
    virtual srs_error_t parse_fmtp_attribute(std::string attr);
    // generally, the control is the stream info for video or audio.
    virtual srs_error_t parse_control_attribute(std::string attr);
    // decode the string by base64.
    virtual std::string base64_decode(std::string value);
};

// The rtsp transport.
// 12.39 Transport, @see rfc2326-1998-rtsp.pdf, page 115
// This request header indicates which transport protocol is to be used
// and configures its parameters such as destination address,
// compression, multicast time-to-live and destination port for a single
// stream. It sets those values not already determined by a presentation
// description.
class SrsRtspTransport
{
public:
    // The syntax for the transport specifier is
    //      transport/profile/lower-transport
    std::string transport;
    std::string profile;
    std::string lower_transport;
    // unicast | multicast
    // mutually exclusive indication of whether unicast or multicast
    // delivery will be attempted. Default value is multicast.
    // Clients that are capable of handling both unicast and
    // multicast transmission MUST indicate such capability by
    // including two full transport-specs with separate parameters
    // For each.
    std::string cast_type;
    // The mode parameter indicates the methods to be supported for
    // this session. Valid values are PLAY and RECORD. If not
    // provided, the default is PLAY.
    std::string mode;
    // This parameter provides the unicast RTP/RTCP port pair on
    // which the client has chosen to receive media data and control
    // information. It is specified as a range, e.g.,
    //      client_port=3456-3457.
    // where client will use port in:
    //      [client_port_min, client_port_max)
    int client_port_min;
    int client_port_max;
public:
    SrsRtspTransport();
    virtual ~SrsRtspTransport();
public:
    // Parse a line of token for transport.
    virtual srs_error_t parse(std::string attr);
};

// The rtsp request message.
// 6 Request, @see rfc2326-1998-rtsp.pdf, page 39
// A request message from a client to a server or vice versa includes,
// within the first line of that message, the method to be applied to
// The resource, the identifier of the resource, and the protocol
// version in use.
// Request = Request-Line ; Section 6.1
//          // ( general-header ; Section 5
//           | request-header ; Section 6.2
//           | entity-header ) ; Section 8.1
//           CRLF
//           [ message-body ] ; Section 4.3
class SrsRtspRequest
{
public:
    // 6.1 Request Line
    // Request-Line = Method SP Request-URI SP RTSP-Version CRLF
    std::string method;
    std::string uri;
    std::string version;
    // 12.17 CSeq
    // The CSeq field specifies the sequence number for an RTSP requestresponse
    // pair. This field MUST be present in all requests and
    // responses. For every RTSP request containing the given sequence
    // number, there will be a corresponding response having the same
    // number. Any retransmitted request must contain the same sequence
    // number as the original (i.e. the sequence number is not incremented
    // For retransmissions of the same request).
    long seq;
    // 12.16 Content-Type, @see rfc2326-1998-rtsp.pdf, page 99
    // See [H14.18]. Note that the content types suitable for RTSP are
    // likely to be restricted in practice to presentation descriptions and
    // parameter-value types.
    std::string content_type;
    // 12.14 Content-Length, @see rfc2326-1998-rtsp.pdf, page 99
    // This field contains the length of the content of the method (i.e.
    // after the double CRLF following the last header). Unlike HTTP, it
    // MUST be included in all messages that carry content beyond the header
    // portion of the message. If it is missing, a default value of zero is
    // assumed. It is interpreted according to [H14.14].
    long content_length;
    // The session id.
    std::string session;
    
    // The sdp in announce, NULL for no sdp.
    SrsRtspSdp* sdp;
    // The transport in setup, NULL for no transport.
    SrsRtspTransport* transport;
    // For setup message, parse the stream id from uri.
    int stream_id;
public:
    SrsRtspRequest();
    virtual ~SrsRtspRequest();
public:
    virtual bool is_options();
    virtual bool is_announce();
    virtual bool is_setup();
    virtual bool is_record();
};

// The rtsp response message.
// 7 Response, @see rfc2326-1998-rtsp.pdf, page 43
// [H6] applies except that HTTP-Version is replaced by RTSP-Version.
// Also, RTSP defines additional status codes and does not define some
// HTTP codes. The valid response codes and the methods they can be used
// with are defined in Table 1.
// After receiving and interpreting a request message, the recipient
// responds with an RTSP response message.
//       Response = Status-Line ; Section 7.1
//                  // ( general-header ; Section 5
//                   | response-header ; Section 7.1.2
//                   | entity-header ) ; Section 8.1
//                   CRLF
//                   [ message-body ] ; Section 4.3
class SrsRtspResponse
{
public:
    // 7.1 Status-Line
    // The first line of a Response message is the Status-Line, consisting
    // of the protocol version followed by a numeric status code, and the
    // textual phrase associated with the status code, with each element
    // separated by SP characters. No CR or LF is allowed except in the
    // final CRLF sequence.
    //       Status-Line = RTSP-Version SP Status-Code SP Reason-Phrase CRLF
    // @see about the version of rtsp, see SRS_RTSP_VERSION
    // @see about the status of rtsp, see SRS_CONSTS_RTSP_OK
    int status;
    // 12.17 CSeq, @see rfc2326-1998-rtsp.pdf, page 99
    // The CSeq field specifies the sequence number for an RTSP requestresponse
    // pair. This field MUST be present in all requests and
    // responses. For every RTSP request containing the given sequence
    // number, there will be a corresponding response having the same
    // number. Any retransmitted request must contain the same sequence
    // number as the original (i.e. the sequence number is not incremented
    // For retransmissions of the same request).
    long seq;
    // The session id.
    std::string session;
public:
    SrsRtspResponse(int cseq);
    virtual ~SrsRtspResponse();
public:
    // Encode message to string.
    virtual srs_error_t encode(std::stringstream& ss);
protected:
    // Sub classes override this to encode the headers.
    virtual srs_error_t encode_header(std::stringstream& ss);
};

// 10.1 OPTIONS, @see rfc2326-1998-rtsp.pdf, page 59
// The behavior is equivalent to that described in [H9.2]. An OPTIONS
// request may be issued at any time, e.g., if the client is about to
// try a nonstandard request. It does not influence server state.
class SrsRtspOptionsResponse : public SrsRtspResponse
{
public:
    // Join of SrsRtspMethod
    SrsRtspMethod methods;
public:
    SrsRtspOptionsResponse(int cseq);
    virtual ~SrsRtspOptionsResponse();
protected:
    virtual srs_error_t encode_header(std::stringstream& ss);
};

// 10.4 SETUP, @see rfc2326-1998-rtsp.pdf, page 65
// The SETUP request for a URI specifies the transport mechanism to be
// used for the streamed media. A client can issue a SETUP request for a
// stream that is already playing to change transport parameters, which
// a server MAY allow. If it does not allow this, it MUST respond with
// error "455 Method Not Valid In This State". For the benefit of any
// intervening firewalls, a client must indicate the transport
// parameters even if it has no influence over these parameters, for
// example, where the server advertises a fixed multicast address.
class SrsRtspSetupResponse : public SrsRtspResponse
{
public:
    // The client specified port.
    int client_port_min;
    int client_port_max;
    // The client will use the port in:
    //      [local_port_min, local_port_max)
    int local_port_min;
    int local_port_max;
    // The session.
    std::string session;
public:
    SrsRtspSetupResponse(int cseq);
    virtual ~SrsRtspSetupResponse();
protected:
    virtual srs_error_t encode_header(std::stringstream& ss);
};

// The rtsp protocol stack to parse the rtsp packets.
class SrsRtspStack
{
private:
    // The cached bytes buffer.
    SrsSimpleStream* buf;
    // The underlayer socket object, send/recv bytes.
    ISrsProtocolReadWriter* skt;
public:
    SrsRtspStack(ISrsProtocolReadWriter* s);
    virtual ~SrsRtspStack();
public:
    // Recv rtsp message from underlayer io.
    // @param preq the output rtsp request message, which user must free it.
    // @return an int error code.
    //       ERROR_RTSP_REQUEST_HEADER_EOF indicates request header EOF.
    virtual srs_error_t recv_message(SrsRtspRequest** preq);
    // Send rtsp message over underlayer io.
    // @param res the rtsp response message, which user should never free it.
    // @return an int error code.
    virtual srs_error_t send_message(SrsRtspResponse* res);
private:
    // Recv the rtsp message.
    virtual srs_error_t do_recv_message(SrsRtspRequest* req);
    // Read a normal token from io, error when token state is not normal.
    virtual srs_error_t recv_token_normal(std::string& token);
    // Read a normal token from io, error when token state is not eof.
    virtual srs_error_t recv_token_eof(std::string& token);
    // Read the token util got eof, for example, to read the response status Reason-Phrase
    // @param pconsumed, output the token parsed length. NULL to ignore.
    virtual srs_error_t recv_token_util_eof(std::string& token, int* pconsumed = NULL);
    // Read a token from io, split by SP, endswith CRLF:
    //       token1 SP token2 SP ... tokenN CRLF
    // @param token, output the read token.
    // @param state, output the token parse state.
    // @param normal_ch, the char to indicates the normal token.
    //       the SP use to indicates the normal token, @see SRS_RTSP_SP
    //       the 0x00 use to ignore normal token flag. @see recv_token_util_eof
    // @param pconsumed, output the token parsed length. NULL to ignore.
    virtual srs_error_t recv_token(std::string& token, SrsRtspTokenState& state, char normal_ch = SRS_RTSP_SP, int* pconsumed = NULL);
};

#endif

