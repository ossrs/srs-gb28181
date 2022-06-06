//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_BANDWIDTH_HPP
#define SRS_APP_BANDWIDTH_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>

class SrsKbps;
class SrsRequest;
class SrsRtmpServer;
class SrsKbpsLimit;
class ISrsProtocolStatistic;

// The bandwidth check/test sample.
class SrsBandwidthSample
{
public:
    // The plan, how long to do the test,
    // if exceed the duration, abort the test.
    srs_utime_t duration;
    // The plan, interval for each check/test packet
    srs_utime_t interval;
public:
    // The actual test duration.
    srs_utime_t actual_duration;
    // The actual test bytes
    int bytes;
    // The actual test kbps
    int kbps;
public:
    SrsBandwidthSample();
    virtual ~SrsBandwidthSample();
public:
    // Update the bytes and actual duration, then calc the kbps.
    // @param _bytes update the sample bytes.
    // @param _duration update the actual duration.
    virtual void calc_kbps(int _bytes, srs_utime_t _duration);
};

// The bandwidth test agent which provides the interfaces for bandwidth check.
// 1. if vhost disabled bandwidth check, ignore.
// 2. otherwise, check the key, error if verify failed.
// 3. check the interval limit, error if bandwidth in the interval window.
// 4. check the bandwidth under the max kbps.
// 5. send the bandwidth data to client.
// bandwidth workflow:
//  +------------+             +----------+
//  |  Client    |             |  Server  |
//  +-----+------+             +-----+----+
//        |                          |
//        |  connect vhost------>    | if vhost enable bandwidth,
//        |  <-----result(success)   | do bandwidth check.
//        |                          |
//        |  <----call(start play)   | onSrsBandCheckStartPlayBytes
//        |  result(playing)----->   | onSrsBandCheckStartingPlayBytes
//        |  <-------data(playing)   | onSrsBandCheckStartingPlayBytes
//        |  <-----call(stop play)   | onSrsBandCheckStopPlayBytes
//        |  result(stopped)----->   | onSrsBandCheckStoppedPlayBytes
//        |                          |
//        |  <-call(start publish)   | onSrsBandCheckStartPublishBytes
//        |  result(publishing)-->   | onSrsBandCheckStartingPublishBytes
//        |  data(publishing)(3)->   | onSrsBandCheckStartingPublishBytes
//        |  <--call(stop publish)   | onSrsBandCheckStopPublishBytes
//        |  result(stopped)(1)-->   | onSrsBandCheckStoppedPublishBytes
//        |                          |
//        |  <--------------report   |
//        |  final(2)------------>   | finalClientPacket
//        |          <END>           |
// 
// 1. when flash client, server never wait the stop publish response,
//   for the flash client queue is fullfill with other packets.
// 2. when flash client, server never wait the final packet,
//   for the flash client directly close when got report packet.
// 3. for linux client, it will send the publish data then send a stop publish,
//   for the linux client donot know when to stop the publish.
//   when server got publishing and stop publish, stop publish.
class SrsBandwidth
{
private:
    SrsRequest* _req;
    SrsRtmpServer* _rtmp;
public:
    SrsBandwidth();
    virtual ~SrsBandwidth();
public:
    // Do the bandwidth check.
    // @param rtmp, server RTMP protocol object, send/recv RTMP packet to/from client.
    // @param io_stat, the underlayer io statistic, provides send/recv bytes count.
    // @param req, client request object, specifies the request info from client.
    // @param local_ip, the ip of server which client connected at
    virtual srs_error_t bandwidth_check(SrsRtmpServer* rtmp, ISrsProtocolStatistic* io_stat, SrsRequest* req, std::string local_ip);
private:
    // Used to process band width check from client.
    // @param limit, the bandwidth limit object, to slowdown if exceed the kbps.
    virtual srs_error_t do_bandwidth_check(SrsKbpsLimit* limit);
    // play check/test, downloading bandwidth kbps.
private:
    // Start play/download bandwidth check/test,
    // send start-play command to client, client must response starting-play
    // to start the test.
    virtual srs_error_t play_start(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    // Do play/download bandwidth check/test,
    // server send call messages to client in specified time,
    // calc the time and bytes sent, then we got the kbps.
    virtual srs_error_t play_checking(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    // stop play/download bandwidth check/test,
    // send stop-play command to client, client must response stopped-play
    // to stop the test.
    virtual srs_error_t play_stop(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    // publish check/test, publishing bandwidth kbps.
private:
    // Start publish/upload bandwidth check/test,
    // send start-publish command to client, client must response starting-publish
    // to start the test.
    virtual srs_error_t publish_start(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    // Do publish/upload bandwidth check/test,
    // client send call messages to client in specified time,
    // server calc the time and bytes received, then we got the kbps.
    // @remark, for linux client, it will send a stop publish client, server will stop publishing.
    //       then enter the publish-stop stage with client.
    // @remark, for flash client, it will send many many call messages, that is,
    //       the send queue is fullfill with call messages, so we should never expect the
    //       response message in the publish-stop stage.
    virtual srs_error_t publish_checking(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    // Stop publish/upload bandwidth check/test,
    // send stop-publish command to client,
    // for linux client, always expect a stopped-publish response from client,
    // for flash client, the sent queue is fullfill with publishing call messages,
    //       so server never expect the stopped-publish from it.
    virtual srs_error_t publish_stop(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
private:
    // Report and final packet
    // report a finish packet, with the bytes/time/kbps bandwidth check/test result,
    // for linux client, server always expect a final packet from client,
    // for flash client, the sent queue is fullfill with publishing call messages,
    //       so server never expect the final packet from it.
    virtual srs_error_t do_final(SrsBandwidthSample& play_sample, SrsBandwidthSample& publish_sample, srs_utime_t start_time, srs_utime_t& end_time);
};

// The kbps limit, if exceed the kbps, slow down.
class SrsKbpsLimit
{
private:
    int _limit_kbps;
    SrsKbps* _kbps;
public:
    SrsKbpsLimit(SrsKbps* kbps, int limit_kbps);
    virtual ~SrsKbpsLimit();
public:
    // Get the system limit kbps.
    virtual int limit_kbps();
    // Limit the recv bandwidth.
    virtual void recv_limit();
    // Limit the send bandwidth.
    virtual void send_limit();
};

#endif
