//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_SERVICE_LOG_HPP
#define SRS_SERVICE_LOG_HPP

#include <srs_core.hpp>

#include <map>
#include <string>

#include <srs_service_st.hpp>
#include <srs_kernel_log.hpp>

// The st thread context, get_id will get the st-thread id,
// which identify the client.
class SrsThreadContext : public ISrsContext
{
private:
    std::map<srs_thread_t, SrsContextId> cache;
public:
    SrsThreadContext();
    virtual ~SrsThreadContext();
public:
    virtual SrsContextId generate_id();
    virtual const SrsContextId& get_id();
    virtual const SrsContextId& set_id(const SrsContextId& v);
public:
    virtual void clear_cid();
};

// The context restore stores the context and restore it when done.
// Usage:
//      SrsContextRestore(_srs_context->get_id());
#define SrsContextRestore(cid) impl_SrsContextRestore _context_restore_instance(cid)
class impl_SrsContextRestore
{
private:
    SrsContextId cid_;
public:
    impl_SrsContextRestore(SrsContextId cid);
    virtual ~impl_SrsContextRestore();
};

// The basic console log, which write log to console.
class SrsConsoleLog : public ISrsLog
{
private:
    SrsLogLevel level;
    bool utc;
private:
    char* buffer;
public:
    SrsConsoleLog(SrsLogLevel l, bool u);
    virtual ~SrsConsoleLog();
// Interface ISrsLog
public:
    virtual srs_error_t initialize();
    virtual void reopen();
    virtual void verbose(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void info(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void trace(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void warn(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void error(const char* tag, SrsContextId context_id, const char* fmt, ...);
};

// Generate the log header.
// @param dangerous Whether log is warning or error, log the errno if true.
// @param utc Whether use UTC time format in the log header.
// @param psize Output the actual header size.
// @remark It's a internal API.
bool srs_log_header(char* buffer, int size, bool utc, bool dangerous, const char* tag, SrsContextId cid, const char* level, int* psize);

#endif
