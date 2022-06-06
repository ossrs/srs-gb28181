//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_CORE_PERFORMANCE_HPP
#define SRS_CORE_PERFORMANCE_HPP

#include <srs_core.hpp>

/**
 * this file defines the perfromance options.
 */

/**
 * to improve read performance, merge some packets then read,
 * when it on and read small bytes, we sleep to wait more data.,
 * that is, we merge some data to read together.
 * @see SrsConfig::get_mr_enabled()
 * @see SrsConfig::get_mr_sleep_ms()
 * @see https://github.com/ossrs/srs/issues/241
 * @example, for the default settings, this algorithm will use:
 *       that is, when got nread bytes smaller than 4KB, sleep(780ms).
 */
/**
 * https://github.com/ossrs/srs/issues/241#issuecomment-65554690
 * The merged read algorithm is ok and can be simplified for:
 *   1. Suppose the client network is ok. All algorithm go wrong when netowrk is not ok.
 *   2. Suppose the client send each packet one by one. Although send some together, it's same.
 *   3. SRS MR algorithm will read all data then sleep.
 * So, the MR algorithm is:
 *   while true:
 *       read all data from socket.
 *       sleep a while
 * For example, sleep 120ms. Then there is, and always 120ms data in buffer.
 * That is, the latency is 120ms(the sleep time).
 */
#define SRS_PERF_MERGED_READ
// the default config of mr.
#define SRS_PERF_MR_ENABLED false
#define SRS_PERF_MR_SLEEP (350 * SRS_UTIME_MILLISECONDS)

// For tcmalloc, set the default release rate.
// @see https://gperftools.github.io/gperftools/tcmalloc.html
#define SRS_PERF_TCMALLOC_RELEASE_RATE 0.8

/**
 * the MW(merged-write) send cache time in srs_utime_t.
 * the default value, user can override it in config.
 * to improve send performance, cache msgs and send in a time.
 * for example, cache 500ms videos and audios, then convert all these
 * msgs to iovecs, finally use writev to send.
 * @remark this largely improve performance, from 3.5k+ to 7.5k+.
 *       the latency+ when cache+.
 * @remark the socket send buffer default to 185KB, it large enough.
 * @see SrsConfig::get_mw_sleep_ms()
 * @remark the mw sleep and msgs to send, maybe:
 *       mw_sleep        msgs        iovs
 *       350             43          86
 *       400             44          88
 *       500             46          92
 *       600             46          92
 *       700             82          164
 *       800             81          162
 *       900             80          160
 *       1000            88          176
 *       1100            91          182
 *       1200            89          178
 *       1300            119         238
 *       1400            120         240
 *       1500            119         238
 *       1600            131         262
 *       1700            131         262
 *       1800            133         266
 *       1900            141         282
 *       2000            150         300
 */
// the default config of mw.
#define SRS_PERF_MW_SLEEP (350 * SRS_UTIME_MILLISECONDS)
/**
 * how many msgs can be send entirely.
 * for play clients to get msgs then totally send out.
 * for the mw sleep set to 1800, the msgs is about 133.
 * @remark, recomment to 128.
 */
#define SRS_PERF_MW_MSGS 128

/**
 * whether set the socket send buffer size.
 */
#define SRS_PERF_MW_SO_SNDBUF

/**
 * whether set the socket recv buffer size.
 */
#undef SRS_PERF_MW_SO_RCVBUF
/**
 * whether enable the fast vector for qeueue.
 */
#define SRS_PERF_QUEUE_FAST_VECTOR
/**
 * whether use cond wait to send messages.
 * @remark this improve performance for large connectios.
 */
// TODO: FIXME: Should always enable it.
#define SRS_PERF_QUEUE_COND_WAIT
#ifdef SRS_PERF_QUEUE_COND_WAIT
    // For RTMP, use larger wait queue.
    #define SRS_PERF_MW_MIN_MSGS 8
    // For RTC, use smaller wait queue.
    #define SRS_PERF_MW_MIN_MSGS_FOR_RTC 1
    // For Real-Time, never wait messages.
    #define SRS_PERF_MW_MIN_MSGS_REALTIME 0
#endif
/**
 * the default value of vhost for
 * SRS whether use the min latency mode.
 * for min latence mode:
 * 1. disable the mr for vhost.
 * 2. use timeout for cond wait for consumer queue.
 * @see https://github.com/ossrs/srs/issues/257
 */
#define SRS_PERF_MIN_LATENCY_ENABLED false

/**
 * how many chunk stream to cache, [0, N].
 * to imporove about 10% performance when chunk size small, and 5% for large chunk.
 * @see https://github.com/ossrs/srs/issues/249
 * @remark 0 to disable the chunk stream cache.
 */
#define SRS_PERF_CHUNK_STREAM_CACHE 16

/**
 * the gop cache and play cache queue.
 */
// whether gop cache is on.
#define SRS_PERF_GOP_CACHE true
// in srs_utime_t, the live queue length.
#define SRS_PERF_PLAY_QUEUE (30 * SRS_UTIME_SECONDS)

/**
 * whether always use complex send algorithm.
 * for some network does not support the complex send,
 * @see https://github.com/ossrs/srs/issues/320
 */
//#undef SRS_PERF_COMPLEX_SEND
#define SRS_PERF_COMPLEX_SEND
/**
 * whether enable the TCP_NODELAY
 * user maybe need send small tcp packet for some network.
 * @see https://github.com/ossrs/srs/issues/320
 */
#undef SRS_PERF_TCP_NODELAY
#define SRS_PERF_TCP_NODELAY
/**
 * set the socket send buffer,
 * to force the server to send smaller tcp packet.
 * @see https://github.com/ossrs/srs/issues/320
 * @remark undef it to auto calc it by merged write sleep ms.
 * @remark only apply it when SRS_PERF_MW_SO_SNDBUF is defined.
 */
#ifdef SRS_PERF_MW_SO_SNDBUF
    //#define SRS_PERF_SO_SNDBUF_SIZE 1024
    #undef SRS_PERF_SO_SNDBUF_SIZE
#endif

/**
 * whether ensure glibc memory check.
 */
#define SRS_PERF_GLIBC_MEMORY_CHECK
#undef SRS_PERF_GLIBC_MEMORY_CHECK

#endif

