//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_CODEC_HPP
#define SRS_KERNEL_CODEC_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsBuffer;

/**
 * The video codec id.
 * @doc video_file_format_spec_v10_1.pdf, page78, E.4.3.1 VIDEODATA
 * CodecID UB [4]
 * Codec Identifier. The following values are defined for FLV:
 *      2 = Sorenson H.263
 *      3 = Screen video
 *      4 = On2 VP6
 *      5 = On2 VP6 with alpha channel
 *      6 = Screen video version 2
 *      7 = AVC
 *     12 = HEVC
 */
enum SrsVideoCodecId
{
    // set to the zero to reserved, for array map.
    SrsVideoCodecIdReserved = 0,
    SrsVideoCodecIdForbidden = 0,
    SrsVideoCodecIdReserved1 = 1,
    SrsVideoCodecIdReserved2 = 9,
    
    // for user to disable video, for example, use pure audio hls.
    SrsVideoCodecIdDisabled = 8,
    
    SrsVideoCodecIdSorensonH263 = 2,
    SrsVideoCodecIdScreenVideo = 3,
    SrsVideoCodecIdOn2VP6 = 4,
    SrsVideoCodecIdOn2VP6WithAlphaChannel = 5,
    SrsVideoCodecIdScreenVideoVersion2 = 6,
    SrsVideoCodecIdAVC = 7,
    // See page 79 at @doc https://github.com/CDN-Union/H265/blob/master/Document/video_file_format_spec_v10_1_ksyun_20170615.doc
    SrsVideoCodecIdHEVC = 12,
    // https://mp.weixin.qq.com/s/H3qI7zsON5sdf4oDJ9qlkg
    SrsVideoCodecIdAV1 = 13,
};
std::string srs_video_codec_id2str(SrsVideoCodecId codec);

/**
 * The video AVC frame trait(characteristic).
 * @doc video_file_format_spec_v10_1.pdf, page79, E.4.3.2 AVCVIDEOPACKET
 * AVCPacketType IF CodecID == 7 UI8
 * The following values are defined:
 *      0 = AVC sequence header
 *      1 = AVC NALU
 *      2 = AVC end of sequence (lower level NALU sequence ender is not required or supported)
 */
enum SrsVideoAvcFrameTrait
{
    // set to the max value to reserved, for array map.
    SrsVideoAvcFrameTraitReserved = 3,
    SrsVideoAvcFrameTraitForbidden = 3,
    
    SrsVideoAvcFrameTraitSequenceHeader = 0,
    SrsVideoAvcFrameTraitNALU = 1,
    SrsVideoAvcFrameTraitSequenceHeaderEOF = 2,
};

/**
 * The video AVC frame type, such as I/P/B.
 * @doc video_file_format_spec_v10_1.pdf, page78, E.4.3.1 VIDEODATA
 * Frame Type UB [4]
 * Type of video frame. The following values are defined:
 *      1 = key frame (for AVC, a seekable frame)
 *      2 = inter frame (for AVC, a non-seekable frame)
 *      3 = disposable inter frame (H.263 only)
 *      4 = generated key frame (reserved for server use only)
 *      5 = video info/command frame
 */
enum SrsVideoAvcFrameType
{
    // set to the zero to reserved, for array map.
    SrsVideoAvcFrameTypeReserved = 0,
    SrsVideoAvcFrameTypeForbidden = 0,
    SrsVideoAvcFrameTypeReserved1 = 6,
    
    SrsVideoAvcFrameTypeKeyFrame = 1,
    SrsVideoAvcFrameTypeInterFrame = 2,
    SrsVideoAvcFrameTypeDisposableInterFrame = 3,
    SrsVideoAvcFrameTypeGeneratedKeyFrame = 4,
    SrsVideoAvcFrameTypeVideoInfoFrame = 5,
};

/**
 * The audio codec id.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 * SoundFormat UB [4]
 * Format of SoundData. The following values are defined:
 *     0 = Linear PCM, platform endian
 *     1 = ADPCM
 *     2 = MP3
 *     3 = Linear PCM, little endian
 *     4 = Nellymoser 16 kHz mono
 *     5 = Nellymoser 8 kHz mono
 *     6 = Nellymoser
 *     7 = G.711 A-law logarithmic PCM
 *     8 = G.711 mu-law logarithmic PCM
 *     9 = reserved
 *     10 = AAC
 *     11 = Speex
 *     14 = MP3 8 kHz
 *     15 = Device-specific sound
 * Formats 7, 8, 14, and 15 are reserved.
 * AAC is supported in Flash Player 9,0,115,0 and higher.
 * Speex is supported in Flash Player 10 and higher.
 */
enum SrsAudioCodecId
{
    // set to the max value to reserved, for array map.
    SrsAudioCodecIdReserved1 = 16,
    SrsAudioCodecIdForbidden = 16,
    
    // for user to disable audio, for example, use pure video hls.
    SrsAudioCodecIdDisabled = 17,
    
    SrsAudioCodecIdLinearPCMPlatformEndian = 0,
    SrsAudioCodecIdADPCM = 1,
    SrsAudioCodecIdMP3 = 2,
    SrsAudioCodecIdLinearPCMLittleEndian = 3,
    SrsAudioCodecIdNellymoser16kHzMono = 4,
    SrsAudioCodecIdNellymoser8kHzMono = 5,
    SrsAudioCodecIdNellymoser = 6,
    SrsAudioCodecIdReservedG711AlawLogarithmicPCM = 7,
    SrsAudioCodecIdReservedG711MuLawLogarithmicPCM = 8,
    SrsAudioCodecIdReserved = 9,
    SrsAudioCodecIdAAC = 10,
    SrsAudioCodecIdSpeex = 11,
    // For FLV, it's undefined, we define it as Opus for WebRTC.
    SrsAudioCodecIdOpus = 13,
    SrsAudioCodecIdReservedMP3_8kHz = 14,
    SrsAudioCodecIdReservedDeviceSpecificSound = 15,
};
std::string srs_audio_codec_id2str(SrsAudioCodecId codec);

/**
 * The audio AAC frame trait(characteristic).
 * @doc video_file_format_spec_v10_1.pdf, page 77, E.4.2 Audio Tags
 * AACPacketType IF SoundFormat == 10 or 13 UI8
 * The following values are defined:
 *      0 = AAC sequence header
 *      1 = AAC raw
 */
enum SrsAudioAacFrameTrait
{
    // set to the max value to reserved, for array map.
    SrsAudioAacFrameTraitReserved = 0xff,
    SrsAudioAacFrameTraitForbidden = 0xff,
    
    SrsAudioAacFrameTraitSequenceHeader = 0,
    SrsAudioAacFrameTraitRawData = 1,
    
    // For Opus, the frame trait, may has more than one traits.
    SrsAudioOpusFrameTraitRaw = 2,
    SrsAudioOpusFrameTraitSamplingRate = 4,
    SrsAudioOpusFrameTraitAudioLevel = 8,

    // 16/32 reserved for g711a/g711u 

    // For MP3
    SrsAudioMp3FrameTrait = 64,
};

/**
 * The audio sample rate.
 * @see srs_flv_srates and srs_aac_srates.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 *      0 = 5.5 kHz = 5512 Hz
 *      1 = 11 kHz = 11025 Hz
 *      2 = 22 kHz = 22050 Hz
 *      3 = 44 kHz = 44100 Hz
 * However, we can extends this table.
 * @remark Use srs_flv_srates to convert it.
 */
enum SrsAudioSampleRate
{
    // set to the max value to reserved, for array map.
    SrsAudioSampleRateReserved = 0xff,
    SrsAudioSampleRateForbidden = 0xff,
    
    // For FLV, only support 5, 11, 22, 44KHz sampling rate.
    SrsAudioSampleRate5512 = 0,
    SrsAudioSampleRate11025 = 1,
    SrsAudioSampleRate22050 = 2,
    SrsAudioSampleRate44100 = 3,
    
    // For Opus, support 8, 12, 16, 24, 48KHz
    // We will write a UINT8 sampling rate after FLV audio tag header.
    // @doc https://tools.ietf.org/html/rfc6716#section-2
    SrsAudioSampleRateNB8kHz   = 8,  // NB (narrowband)
    SrsAudioSampleRateMB12kHz  = 12, // MB (medium-band)
    SrsAudioSampleRateWB16kHz  = 16, // WB (wideband)
    SrsAudioSampleRateSWB24kHz = 24, // SWB (super-wideband)
    SrsAudioSampleRateFB48kHz  = 48, // FB (fullband)
};
std::string srs_audio_sample_rate2str(SrsAudioSampleRate v);

/**
 * The frame type, for example, audio, video or data.
 * @doc video_file_format_spec_v10_1.pdf, page 75, E.4.1 FLV Tag
 */
enum SrsFrameType
{
    // set to the zero to reserved, for array map.
    SrsFrameTypeReserved = 0,
    SrsFrameTypeForbidden = 0,
    
    // 8 = audio
    SrsFrameTypeAudio = 8,
    // 9 = video
    SrsFrameTypeVideo = 9,
    // 18 = script data
    SrsFrameTypeScript = 18,
};

/**
 * Fast tough the codec of FLV video.
 * @doc video_file_format_spec_v10_1.pdf, page 78, E.4.3 Video Tags
 */
class SrsFlvVideo
{
public:
    SrsFlvVideo();
    virtual ~SrsFlvVideo();
    // the following function used to finger out the flv/rtmp packet detail.
public:
    /**
     * only check the frame_type, not check the codec type.
     */
    static bool keyframe(char* data, int size);
    /**
     * check codec h264, keyframe, sequence header
     */
    // TODO: FIXME: Remove it, use SrsFormat instead.
    static bool sh(char* data, int size);
    /**
     * check codec h264.
     */
    static bool h264(char* data, int size);
#ifdef SRS_H265
    /**
     * check codec hevc.
     */
    static bool hevc(char* data, int size);
#endif
    /**
     * check the video RTMP/flv header info,
     * @return true if video RTMP/flv header is ok.
     * @remark all type of audio is possible, no need to check audio.
     */
    static bool acceptable(char* data, int size);
};

/**
 * Fast tough the codec of FLV video.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 */
class SrsFlvAudio
{
public:
    SrsFlvAudio();
    virtual ~SrsFlvAudio();
    // the following function used to finger out the flv/rtmp packet detail.
public:
    /**
     * check codec aac, sequence header
     */
    static bool sh(char* data, int size);
    /**
     * check codec aac.
     */
    static bool aac(char* data, int size);
};

/**
 * the public data, event HLS disable, others can use it.
 */
/**
 * the flv sample rate map
 */
extern int srs_flv_srates[];

/**
 * the aac sample rate map
 */
extern int srs_aac_srates[];

// The number of aac samplerates, size for srs_aac_srates.
#define SrsAAcSampleRateNumbers 16

// The impossible aac sample rate index.
#define SrsAacSampleRateUnset 15

// The max number of NALUs in a video, or aac frame in audio packet.
#define SrsMaxNbSamples 256

/**
 * The audio sample size in bits.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 * Size of each audio sample. This parameter only pertains to
 * uncompressed formats. Compressed formats always decode
 * to 16 bits internally.
 *      0 = 8-bit samples
 *      1 = 16-bit samples
 */
enum SrsAudioSampleBits
{
    // set to the max value to reserved, for array map.
    SrsAudioSampleBitsReserved = 2,
    SrsAudioSampleBitsForbidden = 2,
    
    SrsAudioSampleBits8bit = 0,
    SrsAudioSampleBits16bit = 1,
};
std::string srs_audio_sample_bits2str(SrsAudioSampleBits v);

/**
 * The audio channels.
 * @doc video_file_format_spec_v10_1.pdf, page 77, E.4.2 Audio Tags
 * Mono or stereo sound
 *      0 = Mono sound
 *      1 = Stereo sound
 */
enum SrsAudioChannels
{
    // set to the max value to reserved, for array map.
    SrsAudioChannelsReserved = 2,
    SrsAudioChannelsForbidden = 2,
    
    SrsAudioChannelsMono = 0,
    SrsAudioChannelsStereo = 1,
};
std::string srs_audio_channels2str(SrsAudioChannels v);

/**
 * Table 7-1 - NAL unit type codes, syntax element categories, and NAL unit type classes
 * ISO_IEC_14496-10-AVC-2012.pdf, page 83.
 */
enum SrsAvcNaluType
{
    // Unspecified
    SrsAvcNaluTypeReserved = 0,
    SrsAvcNaluTypeForbidden = 0,
    
    // Coded slice of a non-IDR picture slice_layer_without_partitioning_rbsp( )
    SrsAvcNaluTypeNonIDR = 1,
    // Coded slice data partition A slice_data_partition_a_layer_rbsp( )
    SrsAvcNaluTypeDataPartitionA = 2,
    // Coded slice data partition B slice_data_partition_b_layer_rbsp( )
    SrsAvcNaluTypeDataPartitionB = 3,
    // Coded slice data partition C slice_data_partition_c_layer_rbsp( )
    SrsAvcNaluTypeDataPartitionC = 4,
    // Coded slice of an IDR picture slice_layer_without_partitioning_rbsp( )
    SrsAvcNaluTypeIDR = 5,
    // Supplemental enhancement information (SEI) sei_rbsp( )
    SrsAvcNaluTypeSEI = 6,
    // Sequence parameter set seq_parameter_set_rbsp( )
    SrsAvcNaluTypeSPS = 7,
    // Picture parameter set pic_parameter_set_rbsp( )
    SrsAvcNaluTypePPS = 8,
    // Access unit delimiter access_unit_delimiter_rbsp( )
    SrsAvcNaluTypeAccessUnitDelimiter = 9,
    // End of sequence end_of_seq_rbsp( )
    SrsAvcNaluTypeEOSequence = 10,
    // End of stream end_of_stream_rbsp( )
    SrsAvcNaluTypeEOStream = 11,
    // Filler data filler_data_rbsp( )
    SrsAvcNaluTypeFilterData = 12,
    // Sequence parameter set extension seq_parameter_set_extension_rbsp( )
    SrsAvcNaluTypeSPSExt = 13,
    // Prefix NAL unit prefix_nal_unit_rbsp( )
    SrsAvcNaluTypePrefixNALU = 14,
    // Subset sequence parameter set subset_seq_parameter_set_rbsp( )
    SrsAvcNaluTypeSubsetSPS = 15,
    // Coded slice of an auxiliary coded picture without partitioning slice_layer_without_partitioning_rbsp( )
    SrsAvcNaluTypeLayerWithoutPartition = 19,
    // Coded slice extension slice_layer_extension_rbsp( )
    SrsAvcNaluTypeCodedSliceExt = 20,
};
std::string srs_avc_nalu2str(SrsAvcNaluType nalu_type);

#ifdef SRS_H265
enum SrsHevcNaluType
{
	NAL_UNIT_CODED_SLICE_TRAIL_N = 0,
	NAL_UNIT_CODED_SLICE_TRAIL_R, //1
	NAL_UNIT_CODED_SLICE_TSA_N,   //2
	NAL_UNIT_CODED_SLICE_TLA,     //3
	NAL_UNIT_CODED_SLICE_STSA_N,  //4
	NAL_UNIT_CODED_SLICE_STSA_R,  //5
	NAL_UNIT_CODED_SLICE_RADL_N,  //6
	NAL_UNIT_CODED_SLICE_DLP,     //7
	NAL_UNIT_CODED_SLICE_RASL_N,  //8
	NAL_UNIT_CODED_SLICE_TFD,     //9
	NAL_UNIT_RESERVED_10,
	NAL_UNIT_RESERVED_11,
	NAL_UNIT_RESERVED_12,
	NAL_UNIT_RESERVED_13,
	NAL_UNIT_RESERVED_14,
	NAL_UNIT_RESERVED_15,
	NAL_UNIT_CODED_SLICE_BLA,      //16
	NAL_UNIT_CODED_SLICE_BLANT,    //17
	NAL_UNIT_CODED_SLICE_BLA_N_LP, //18
	NAL_UNIT_CODED_SLICE_IDR,      //19
	NAL_UNIT_CODED_SLICE_IDR_N_LP, //20
	NAL_UNIT_CODED_SLICE_CRA,      //21
	NAL_UNIT_RESERVED_22,
	NAL_UNIT_RESERVED_23,
	NAL_UNIT_RESERVED_24,
	NAL_UNIT_RESERVED_25,
	NAL_UNIT_RESERVED_26,
	NAL_UNIT_RESERVED_27,
	NAL_UNIT_RESERVED_28,
	NAL_UNIT_RESERVED_29,
	NAL_UNIT_RESERVED_30,
	NAL_UNIT_RESERVED_31,
	NAL_UNIT_VPS,                   // 32
	NAL_UNIT_SPS,                   // 33
	NAL_UNIT_PPS,                   // 34
	NAL_UNIT_ACCESS_UNIT_DELIMITER, // 35
	NAL_UNIT_EOS,                   // 36
	NAL_UNIT_EOB,                   // 37
	NAL_UNIT_FILLER_DATA,           // 38
	NAL_UNIT_SEI ,                  // 39 Prefix SEI
	NAL_UNIT_SEI_SUFFIX,            // 40 Suffix SEI
	NAL_UNIT_RESERVED_41,
	NAL_UNIT_RESERVED_42,
	NAL_UNIT_RESERVED_43,
	NAL_UNIT_RESERVED_44,
	NAL_UNIT_RESERVED_45,
	NAL_UNIT_RESERVED_46,
	NAL_UNIT_RESERVED_47,
	NAL_UNIT_UNSPECIFIED_48,
	NAL_UNIT_UNSPECIFIED_49,
	NAL_UNIT_UNSPECIFIED_50,
	NAL_UNIT_UNSPECIFIED_51,
	NAL_UNIT_UNSPECIFIED_52,
	NAL_UNIT_UNSPECIFIED_53,
	NAL_UNIT_UNSPECIFIED_54,
	NAL_UNIT_UNSPECIFIED_55,
	NAL_UNIT_UNSPECIFIED_56,
	NAL_UNIT_UNSPECIFIED_57,
	NAL_UNIT_UNSPECIFIED_58,
	NAL_UNIT_UNSPECIFIED_59,
	NAL_UNIT_UNSPECIFIED_60,
	NAL_UNIT_UNSPECIFIED_61,
	NAL_UNIT_UNSPECIFIED_62,
	NAL_UNIT_UNSPECIFIED_63,
	NAL_UNIT_INVALID,
};

//for nalu data first byte
#define HEVC_NALU_TYPE(code) (SrsHevcNaluType)((code & 0x7E)>>1)
#endif

/**
 * Table 7-6 – Name association to slice_type
 * ISO_IEC_14496-10-AVC-2012.pdf, page 105.
 */
enum SrsAvcSliceType
{
    SrsAvcSliceTypeP   = 0,
    SrsAvcSliceTypeB   = 1,
    SrsAvcSliceTypeI   = 2,
    SrsAvcSliceTypeSP  = 3,
    SrsAvcSliceTypeSI  = 4,
    SrsAvcSliceTypeP1  = 5,
    SrsAvcSliceTypeB1  = 6,
    SrsAvcSliceTypeI1  = 7,
    SrsAvcSliceTypeSP1 = 8,
    SrsAvcSliceTypeSI1 = 9,
};

/**
 * the avc payload format, must be ibmf or annexb format.
 * we guess by annexb first, then ibmf for the first time,
 * and we always use the guessed format for the next time.
 */
enum SrsAvcPayloadFormat
{
    SrsAvcPayloadFormatGuess = 0,
    SrsAvcPayloadFormatAnnexb,
    SrsAvcPayloadFormatIbmf,
};

/**
 * the aac profile, for ADTS(HLS/TS)
 * @see https://github.com/ossrs/srs/issues/310
 */
enum SrsAacProfile
{
    SrsAacProfileReserved = 3,
    
    // @see 7.1 Profiles, ISO_IEC_13818-7-AAC-2004.pdf, page 40
    SrsAacProfileMain = 0,
    SrsAacProfileLC = 1,
    SrsAacProfileSSR = 2,
};
std::string srs_aac_profile2str(SrsAacProfile aac_profile);

/**
 * the aac object type, for RTMP sequence header
 * for AudioSpecificConfig, @see ISO_IEC_14496-3-AAC-2001.pdf, page 33
 * for audioObjectType, @see ISO_IEC_14496-3-AAC-2001.pdf, page 23
 */
enum SrsAacObjectType
{
    SrsAacObjectTypeReserved = 0,
    SrsAacObjectTypeForbidden = 0,
    
    // Table 1.1 - Audio Object Type definition
    // @see @see ISO_IEC_14496-3-AAC-2001.pdf, page 23
    SrsAacObjectTypeAacMain = 1,
    SrsAacObjectTypeAacLC = 2,
    SrsAacObjectTypeAacSSR = 3,
    
    // AAC HE = LC+SBR
    SrsAacObjectTypeAacHE = 5,
    // AAC HEv2 = LC+SBR+PS
    SrsAacObjectTypeAacHEV2 = 29,
};
std::string srs_aac_object2str(SrsAacObjectType aac_object);
// ts/hls/adts audio header profile to RTMP sequence header object type.
SrsAacObjectType srs_aac_ts2rtmp(SrsAacProfile profile);
// RTMP sequence header object type to ts/hls/adts audio header profile.
SrsAacProfile srs_aac_rtmp2ts(SrsAacObjectType object_type);

/**
 * the profile for avc/h.264.
 * @see Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 205.
 */
enum SrsAvcProfile
{
    SrsAvcProfileReserved = 0,
    
    // @see ffmpeg, libavcodec/avcodec.h:2713
    SrsAvcProfileBaseline = 66,
    // FF_PROFILE_H264_CONSTRAINED  (1<<9)  // 8+1; constraint_set1_flag
    // FF_PROFILE_H264_CONSTRAINED_BASELINE (66|FF_PROFILE_H264_CONSTRAINED)
    SrsAvcProfileConstrainedBaseline = 578,
    SrsAvcProfileMain = 77,
    SrsAvcProfileExtended = 88,
    SrsAvcProfileHigh = 100,
    SrsAvcProfileHigh10 = 110,
    SrsAvcProfileHigh10Intra = 2158,
    SrsAvcProfileHigh422 = 122,
    SrsAvcProfileHigh422Intra = 2170,
    SrsAvcProfileHigh444 = 144,
    SrsAvcProfileHigh444Predictive = 244,
    SrsAvcProfileHigh444Intra = 2192,
};
std::string srs_avc_profile2str(SrsAvcProfile profile);

/**
 * the level for avc/h.264.
 * @see Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 207.
 */
enum SrsAvcLevel
{
    SrsAvcLevelReserved = 0,
    
    SrsAvcLevel_1 = 10,
    SrsAvcLevel_11 = 11,
    SrsAvcLevel_12 = 12,
    SrsAvcLevel_13 = 13,
    SrsAvcLevel_2 = 20,
    SrsAvcLevel_21 = 21,
    SrsAvcLevel_22 = 22,
    SrsAvcLevel_3 = 30,
    SrsAvcLevel_31 = 31,
    SrsAvcLevel_32 = 32,
    SrsAvcLevel_4 = 40,
    SrsAvcLevel_41 = 41,
    SrsAvcLevel_5 = 50,
    SrsAvcLevel_51 = 51,
};
std::string srs_avc_level2str(SrsAvcLevel level);

/**
 * A sample is the unit of frame.
 * It's a NALU for H.264.
 * It's the whole AAC raw data for AAC.
 * @remark Neither SPS/PPS or ASC is sample unit, it's codec sequence header.
 */
class SrsSample
{
public:
    // The size of unit.
    int size;
    // The ptr of unit, user must free it.
    char* bytes;
    // Whether is B frame.
    bool bframe;
public:
    SrsSample();
    SrsSample(char* b, int s);
    ~SrsSample();
public:
    // If we need to know whether sample is bframe, we have to parse the NALU payload.
    srs_error_t parse_bframe();
    // Copy sample, share the bytes pointer.
    SrsSample* copy();
};

/**
 * The codec is the information of encoder,
 * corresponding to the sequence header of FLV,
 * parsed to detail info.
 */
class SrsCodecConfig
{
public:
    SrsCodecConfig();
    virtual ~SrsCodecConfig();
};

/**
 * The audio codec info.
 */
class SrsAudioCodecConfig : public SrsCodecConfig
{
    // In FLV specification.
public:
    // The audio codec id; for FLV, it's SoundFormat.
    SrsAudioCodecId id;
    // The audio sample rate; for FLV, it's SoundRate.
    SrsAudioSampleRate sound_rate;
    // The audio sample size, such as 16 bits; for FLV, it's SoundSize.
    SrsAudioSampleBits sound_size;
    // The audio number of channels; for FLV, it's SoundType.
    // TODO: FIXME: Rename to sound_channels.
    SrsAudioChannels sound_type;
    int audio_data_rate; // in bps
    // In AAC specification.
public:
    /**
     * audio specified
     * audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
     * 1.5.1.1 Audio object type definition, page 23,
     *           in ISO_IEC_14496-3-AAC-2001.pdf.
     */
    SrsAacObjectType aac_object;
    /**
     * samplingFrequencyIndex
     */
    uint8_t aac_sample_rate;
    /**
     * channelConfiguration
     */
    uint8_t aac_channels;
    // Sequence header payload.
public:
    /**
     * the aac extra data, the AAC sequence header,
     * without the flv codec header,
     * @see: ffmpeg, AVCodecContext::extradata
     */
    std::vector<char> aac_extra_data;
public:
    SrsAudioCodecConfig();
    virtual ~SrsAudioCodecConfig();
public:
    virtual bool is_aac_codec_ok();
};

#ifdef SRS_H265
struct HEVCNalData {
    uint16_t nalUnitLength;
    std::vector<uint8_t>  nalUnitData;
};

struct HVCCNALUnit {
    uint8_t  array_completeness;
    uint8_t  NAL_unit_type;
    uint16_t numNalus;
    std::vector<HEVCNalData> nalData_vec;
};

struct HEVCDecoderConfigurationRecord {
    uint8_t  configurationVersion;
    uint8_t  general_profile_space;
    uint8_t  general_tier_flag;
    uint8_t  general_profile_idc;
    uint32_t general_profile_compatibility_flags;
    uint64_t general_constraint_indicator_flags;
    uint8_t  general_level_idc;
    uint16_t min_spatial_segmentation_idc;
    uint8_t  parallelismType;
    uint8_t  chromaFormat;
    uint8_t  bitDepthLumaMinus8;
    uint8_t  bitDepthChromaMinus8;
    uint16_t avgFrameRate;
    uint8_t  constantFrameRate;
    uint8_t  numTemporalLayers;
    uint8_t  temporalIdNested;
    uint8_t  lengthSizeMinusOne;
    std::vector<HVCCNALUnit> nalu_vec;
};
#endif

/**
 * The video codec info.
 */
class SrsVideoCodecConfig : public SrsCodecConfig
{
public:
    SrsVideoCodecId id;
    int video_data_rate; // in bps
    double frame_rate;
    double duration;
    int width;
    int height;
public:
    /**
     * the avc extra data, the AVC sequence header,
     * without the flv codec header,
     * @see: ffmpeg, AVCodecContext::extradata
     */
    std::vector<char> avc_extra_data;
public://H264
    /**
     * video specified
     */
    // profile_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcProfile avc_profile;
    // level_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcLevel avc_level;
    // lengthSizeMinusOne, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    int8_t NAL_unit_length;
    // Note that we may resize the vector, so the under-layer bytes may change.
    std::vector<char> sequenceParameterSetNALUnit;
    std::vector<char> pictureParameterSetNALUnit;
public:
    // the avc payload format.
    SrsAvcPayloadFormat payload_format;
#ifdef SRS_H265
public:
    HEVCDecoderConfigurationRecord _hevcDecConfRecord;
#endif
public:
    SrsVideoCodecConfig();
    virtual ~SrsVideoCodecConfig();
public:
    virtual bool is_avc_codec_ok();
};

// A frame, consists of a codec and a group of samples.
// TODO: FIXME: Rename to packet to follow names of FFmpeg, which means before decoding or after decoding.
class SrsFrame
{
public:
    // The DTS/PTS in milliseconds, which is TBN=1000.
    int64_t dts;
    // PTS = DTS + CTS.
    int32_t cts;
public:
    // The codec info of frame.
    SrsCodecConfig* codec;
    // The actual parsed number of samples.
    int nb_samples;
    // The sampels cache.
    SrsSample samples[SrsMaxNbSamples];
public:
    SrsFrame();
    virtual ~SrsFrame();
public:
    // Initialize the frame, to parse sampels.
    virtual srs_error_t initialize(SrsCodecConfig* c);
    // Add a sample to frame.
    virtual srs_error_t add_sample(char* bytes, int size);
};

// A audio frame, besides a frame, contains the audio frame info, such as frame type.
// TODO: FIXME: Rename to packet to follow names of FFmpeg, which means before decoding or after decoding.
class SrsAudioFrame : public SrsFrame
{
public:
    SrsAudioAacFrameTrait aac_packet_type;
public:
    SrsAudioFrame();
    virtual ~SrsAudioFrame();
public:
    virtual SrsAudioCodecConfig* acodec();
};

// A video frame, besides a frame, contains the video frame info, such as frame type.
// TODO: FIXME: Rename to packet to follow names of FFmpeg, which means before decoding or after decoding.
class SrsVideoFrame : public SrsFrame
{
public:
    // video specified
    SrsVideoAvcFrameType frame_type;
    SrsVideoAvcFrameTrait avc_packet_type;
    // whether sample_units contains IDR frame.
    bool has_idr;
    // Whether exists AUD NALU.
    bool has_aud;
    // Whether exists SPS/PPS NALU.
    bool has_sps_pps;
    // The first nalu type.
    SrsAvcNaluType first_nalu_type;
public:
    SrsVideoFrame();
    virtual ~SrsVideoFrame();
public:
    // Initialize the frame, to parse sampels.
    virtual srs_error_t initialize(SrsCodecConfig* c);
    // Add the sample without ANNEXB or IBMF header, or RAW AAC or MP3 data.
    virtual srs_error_t add_sample(char* bytes, int size);
public:
    virtual SrsVideoCodecConfig* vcodec();
};

/**
 * A codec format, including one or many stream, each stream identified by a frame.
 * For example, a typical RTMP stream format, consits of a video and audio frame.
 * Maybe some RTMP stream only has a audio stream, for instance, redio application.
 */
class SrsFormat
{
public:
    SrsAudioFrame* audio;
    SrsAudioCodecConfig* acodec;
    SrsVideoFrame* video;
    SrsVideoCodecConfig* vcodec;
public:
    char* raw;
    int nb_raw;
public:
    // for sequence header, whether parse the h.264 sps.
    // TODO: FIXME: Refine it.
    bool avc_parse_sps;
public:
    SrsFormat();
    virtual ~SrsFormat();
public:
    // Initialize the format.
    virtual srs_error_t initialize();
    // When got a parsed audio packet.
    // @param data The data in FLV format.
    virtual srs_error_t on_audio(int64_t timestamp, char* data, int size);
    // When got a parsed video packet.
    // @param data The data in FLV format.
    virtual srs_error_t on_video(int64_t timestamp, char* data, int size);
    // When got a audio aac sequence header.
    virtual srs_error_t on_aac_sequence_header(char* data, int size);
public:
    virtual bool is_aac_sequence_header();
    virtual bool is_avc_sequence_header();
private:
    // Demux the video packet in H.264 codec.
    // The packet is muxed in FLV format, defined in flv specification.
    //          Demux the sps/pps from sequence header.
    //          Demux the samples from NALUs.
    virtual srs_error_t video_avc_demux(SrsBuffer* stream, int64_t timestamp);
private:
    // Parse the H.264 SPS/PPS.
    virtual srs_error_t avc_demux_sps_pps(SrsBuffer* stream);
    virtual srs_error_t avc_demux_sps();
    virtual srs_error_t avc_demux_sps_rbsp(char* rbsp, int nb_rbsp);

private:
    srs_error_t  (SrsFormat::*demux_ibmf_format_func)(SrsBuffer* stream);

#ifdef SRS_H265
private:
    // Parse the hevc vps/sps/pps
    virtual srs_error_t hevc_demux_hvcc(SrsBuffer* stream);
    virtual srs_error_t hevc_demux_ibmf_format(SrsBuffer* stream);
    virtual srs_error_t hevc_vps_data(char*& data_p, int& len);
    virtual srs_error_t hevc_pps_data(char*& data_p, int& len);
    virtual srs_error_t hevc_sps_data(char*& data_p, int& len);
#endif

private:
    // Parse the H.264/hevc NALUs.
    virtual srs_error_t video_nalu_demux(SrsBuffer* stream);
    // Demux the avc/hevc NALU in "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    virtual srs_error_t avc_demux_annexb_format(SrsBuffer* stream);
    // Demux the avc NALU in "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    virtual srs_error_t avc_demux_ibmf_format(SrsBuffer* stream);
private:
    // Demux the audio packet in AAC codec.
    //          Demux the asc from sequence header.
    //          Demux the sampels from RAW data.
    virtual srs_error_t audio_aac_demux(SrsBuffer* stream, int64_t timestamp);
    virtual srs_error_t audio_mp3_demux(SrsBuffer* stream, int64_t timestamp);
public:
    // Directly demux the sequence header, without RTMP packet header.
    virtual srs_error_t audio_aac_sequence_header_demux(char* data, int size);
};

#endif

