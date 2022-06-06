//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_http_api.hpp>

#include <sstream>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_json.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_statistic.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_app_server.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_coworkers.hpp>

srs_error_t srs_api_response_jsonp(ISrsHttpResponseWriter* w, string callback, string data)
{
    srs_error_t err = srs_success;
    
    SrsHttpHeader* h = w->header();
    
    h->set_content_length(data.length() + callback.length() + 2);
    h->set_content_type("text/javascript");
    
    if (!callback.empty() && (err = w->write((char*)callback.data(), (int)callback.length())) != srs_success) {
        return srs_error_wrap(err, "write jsonp callback");
    }
    
    static char* c0 = (char*)"(";
    if ((err = w->write(c0, 1)) != srs_success) {
        return srs_error_wrap(err, "write jsonp left token");
    }
    if ((err = w->write((char*)data.data(), (int)data.length())) != srs_success) {
        return srs_error_wrap(err, "write jsonp data");
    }
    
    static char* c1 = (char*)")";
    if ((err = w->write(c1, 1)) != srs_success) {
        return srs_error_wrap(err, "write jsonp right token");
    }
    
    return err;
}

srs_error_t srs_api_response_jsonp_code(ISrsHttpResponseWriter* w, string callback, int code)
{
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(code));
    
    return srs_api_response_jsonp(w, callback, obj->dumps());
}

srs_error_t srs_api_response_jsonp_code(ISrsHttpResponseWriter* w, string callback, srs_error_t err)
{
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(srs_error_code(err)));
    
    return srs_api_response_jsonp(w, callback, obj->dumps());
}

srs_error_t srs_api_response_json(ISrsHttpResponseWriter* w, string data)
{
    srs_error_t err = srs_success;
    
    SrsHttpHeader* h = w->header();
    
    h->set_content_length(data.length());
    h->set_content_type("application/json");
    
    if ((err = w->write((char*)data.data(), (int)data.length())) != srs_success) {
        return srs_error_wrap(err, "write json");
    }
    
    return err;
}

srs_error_t srs_api_response_json_code(ISrsHttpResponseWriter* w, int code)
{
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(code));
    
    return srs_api_response_json(w, obj->dumps());
}

srs_error_t srs_api_response_json_code(ISrsHttpResponseWriter* w, srs_error_t code)
{
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(srs_error_code(code)));
    
    return srs_api_response_json(w, obj->dumps());
}

srs_error_t srs_api_response(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string json)
{
    // no jsonp, directly response.
    if (!r->is_jsonp()) {
        return srs_api_response_json(w, json);
    }
    
    // jsonp, get function name from query("callback")
    string callback = r->query_get("callback");
    return srs_api_response_jsonp(w, callback, json);
}

srs_error_t srs_api_response_code(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, int code)
{
    // no jsonp, directly response.
    if (!r->is_jsonp()) {
        return srs_api_response_json_code(w, code);
    }
    
    // jsonp, get function name from query("callback")
    string callback = r->query_get("callback");
    return srs_api_response_jsonp_code(w, callback, code);
}

// @remark we will free the code.
srs_error_t srs_api_response_code(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, srs_error_t code)
{
    srs_error_t err = srs_success;
    
    // no jsonp, directly response.
    if (!r->is_jsonp()) {
        err = srs_api_response_json_code(w, code);
    } else {
        // jsonp, get function name from query("callback")
        string callback = r->query_get("callback");
        err = srs_api_response_jsonp_code(w, callback, code);
    }
    
    if (code != srs_success) {
        srs_warn("error %s", srs_error_desc(code).c_str());
        srs_freep(code);
    }
    return err;
}

SrsGoApiRoot::SrsGoApiRoot()
{
}

SrsGoApiRoot::~SrsGoApiRoot()
{
}

srs_error_t SrsGoApiRoot::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* urls = SrsJsonAny::object();
    obj->set("urls", urls);
    
    urls->set("api", SrsJsonAny::str("the api root"));

    if (true) {
        SrsJsonObject* rtc = SrsJsonAny::object();
        urls->set("rtc", rtc);

        SrsJsonObject* v1 = SrsJsonAny::object();
        rtc->set("v1", v1);

        v1->set("play", SrsJsonAny::str("Play stream"));
        v1->set("publish", SrsJsonAny::str("Publish stream"));
        v1->set("nack", SrsJsonAny::str("Simulate the NACK"));
    }

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiApi::SrsGoApiApi()
{
}

SrsGoApiApi::~SrsGoApiApi()
{
}

srs_error_t SrsGoApiApi::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* urls = SrsJsonAny::object();
    obj->set("urls", urls);
    
    urls->set("v1", SrsJsonAny::str("the api version 1.0"));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiV1::SrsGoApiV1()
{
}

SrsGoApiV1::~SrsGoApiV1()
{
}

srs_error_t SrsGoApiV1::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* urls = SrsJsonAny::object();
    obj->set("urls", urls);
    
    urls->set("versions", SrsJsonAny::str("the version of SRS"));
    urls->set("summaries", SrsJsonAny::str("the summary(pid, argv, pwd, cpu, mem) of SRS"));
    urls->set("rusages", SrsJsonAny::str("the rusage of SRS"));
    urls->set("self_proc_stats", SrsJsonAny::str("the self process stats"));
    urls->set("system_proc_stats", SrsJsonAny::str("the system process stats"));
    urls->set("meminfos", SrsJsonAny::str("the meminfo of system"));
    urls->set("authors", SrsJsonAny::str("the license, copyright, authors and contributors"));
    urls->set("features", SrsJsonAny::str("the supported features of SRS"));
    urls->set("requests", SrsJsonAny::str("the request itself, for http debug"));
    urls->set("vhosts", SrsJsonAny::str("manage all vhosts or specified vhost"));
    urls->set("streams", SrsJsonAny::str("manage all streams or specified stream"));
    urls->set("clients", SrsJsonAny::str("manage all clients or specified client, default query top 10 clients"));
    urls->set("raw", SrsJsonAny::str("raw api for srs, support CUID srs for instance the config"));
    urls->set("clusters", SrsJsonAny::str("origin cluster server API"));
    urls->set("perf", SrsJsonAny::str("System performance stat"));
    urls->set("tcmalloc", SrsJsonAny::str("tcmalloc api with params ?page=summary|api"));

    SrsJsonObject* tests = SrsJsonAny::object();
    obj->set("tests", tests);
    
    tests->set("requests", SrsJsonAny::str("show the request info"));
    tests->set("errors", SrsJsonAny::str("always return an error 100"));
    tests->set("redirects", SrsJsonAny::str("always redirect to /api/v1/test/errors"));
    tests->set("[vhost]", SrsJsonAny::str("http vhost for http://error.srs.com:1985/api/v1/tests/errors"));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiVersion::SrsGoApiVersion()
{
}

SrsGoApiVersion::~SrsGoApiVersion()
{
}

srs_error_t SrsGoApiVersion::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    data->set("major", SrsJsonAny::integer(VERSION_MAJOR));
    data->set("minor", SrsJsonAny::integer(VERSION_MINOR));
    data->set("revision", SrsJsonAny::integer(VERSION_REVISION));
    data->set("version", SrsJsonAny::str(RTMP_SIG_SRS_VERSION));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiSummaries::SrsGoApiSummaries()
{
}

SrsGoApiSummaries::~SrsGoApiSummaries()
{
}

srs_error_t SrsGoApiSummaries::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    srs_api_dump_summaries(obj);
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiRusages::SrsGoApiRusages()
{
}

SrsGoApiRusages::~SrsGoApiRusages()
{
}

srs_error_t SrsGoApiRusages::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    SrsRusage* ru = srs_get_system_rusage();
    
    data->set("ok", SrsJsonAny::boolean(ru->ok));
    data->set("sample_time", SrsJsonAny::integer(ru->sample_time));
    data->set("ru_utime", SrsJsonAny::integer(ru->r.ru_utime.tv_sec));
    data->set("ru_stime", SrsJsonAny::integer(ru->r.ru_stime.tv_sec));
    data->set("ru_maxrss", SrsJsonAny::integer(ru->r.ru_maxrss));
    data->set("ru_ixrss", SrsJsonAny::integer(ru->r.ru_ixrss));
    data->set("ru_idrss", SrsJsonAny::integer(ru->r.ru_idrss));
    data->set("ru_isrss", SrsJsonAny::integer(ru->r.ru_isrss));
    data->set("ru_minflt", SrsJsonAny::integer(ru->r.ru_minflt));
    data->set("ru_majflt", SrsJsonAny::integer(ru->r.ru_majflt));
    data->set("ru_nswap", SrsJsonAny::integer(ru->r.ru_nswap));
    data->set("ru_inblock", SrsJsonAny::integer(ru->r.ru_inblock));
    data->set("ru_oublock", SrsJsonAny::integer(ru->r.ru_oublock));
    data->set("ru_msgsnd", SrsJsonAny::integer(ru->r.ru_msgsnd));
    data->set("ru_msgrcv", SrsJsonAny::integer(ru->r.ru_msgrcv));
    data->set("ru_nsignals", SrsJsonAny::integer(ru->r.ru_nsignals));
    data->set("ru_nvcsw", SrsJsonAny::integer(ru->r.ru_nvcsw));
    data->set("ru_nivcsw", SrsJsonAny::integer(ru->r.ru_nivcsw));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiSelfProcStats::SrsGoApiSelfProcStats()
{
}

SrsGoApiSelfProcStats::~SrsGoApiSelfProcStats()
{
}

srs_error_t SrsGoApiSelfProcStats::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    
    string state;
    state += (char)u->state;
    
    data->set("ok", SrsJsonAny::boolean(u->ok));
    data->set("sample_time", SrsJsonAny::integer(u->sample_time));
    data->set("percent", SrsJsonAny::number(u->percent));
    data->set("pid", SrsJsonAny::integer(u->pid));
    data->set("comm", SrsJsonAny::str(u->comm));
    data->set("state", SrsJsonAny::str(state.c_str()));
    data->set("ppid", SrsJsonAny::integer(u->ppid));
    data->set("pgrp", SrsJsonAny::integer(u->pgrp));
    data->set("session", SrsJsonAny::integer(u->session));
    data->set("tty_nr", SrsJsonAny::integer(u->tty_nr));
    data->set("tpgid", SrsJsonAny::integer(u->tpgid));
    data->set("flags", SrsJsonAny::integer(u->flags));
    data->set("minflt", SrsJsonAny::integer(u->minflt));
    data->set("cminflt", SrsJsonAny::integer(u->cminflt));
    data->set("majflt", SrsJsonAny::integer(u->majflt));
    data->set("cmajflt", SrsJsonAny::integer(u->cmajflt));
    data->set("utime", SrsJsonAny::integer(u->utime));
    data->set("stime", SrsJsonAny::integer(u->stime));
    data->set("cutime", SrsJsonAny::integer(u->cutime));
    data->set("cstime", SrsJsonAny::integer(u->cstime));
    data->set("priority", SrsJsonAny::integer(u->priority));
    data->set("nice", SrsJsonAny::integer(u->nice));
    data->set("num_threads", SrsJsonAny::integer(u->num_threads));
    data->set("itrealvalue", SrsJsonAny::integer(u->itrealvalue));
    data->set("starttime", SrsJsonAny::integer(u->starttime));
    data->set("vsize", SrsJsonAny::integer(u->vsize));
    data->set("rss", SrsJsonAny::integer(u->rss));
    data->set("rsslim", SrsJsonAny::integer(u->rsslim));
    data->set("startcode", SrsJsonAny::integer(u->startcode));
    data->set("endcode", SrsJsonAny::integer(u->endcode));
    data->set("startstack", SrsJsonAny::integer(u->startstack));
    data->set("kstkesp", SrsJsonAny::integer(u->kstkesp));
    data->set("kstkeip", SrsJsonAny::integer(u->kstkeip));
    data->set("signal", SrsJsonAny::integer(u->signal));
    data->set("blocked", SrsJsonAny::integer(u->blocked));
    data->set("sigignore", SrsJsonAny::integer(u->sigignore));
    data->set("sigcatch", SrsJsonAny::integer(u->sigcatch));
    data->set("wchan", SrsJsonAny::integer(u->wchan));
    data->set("nswap", SrsJsonAny::integer(u->nswap));
    data->set("cnswap", SrsJsonAny::integer(u->cnswap));
    data->set("exit_signal", SrsJsonAny::integer(u->exit_signal));
    data->set("processor", SrsJsonAny::integer(u->processor));
    data->set("rt_priority", SrsJsonAny::integer(u->rt_priority));
    data->set("policy", SrsJsonAny::integer(u->policy));
    data->set("delayacct_blkio_ticks", SrsJsonAny::integer(u->delayacct_blkio_ticks));
    data->set("guest_time", SrsJsonAny::integer(u->guest_time));
    data->set("cguest_time", SrsJsonAny::integer(u->cguest_time));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiSystemProcStats::SrsGoApiSystemProcStats()
{
}

SrsGoApiSystemProcStats::~SrsGoApiSystemProcStats()
{
}

srs_error_t SrsGoApiSystemProcStats::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    SrsProcSystemStat* s = srs_get_system_proc_stat();
    
    data->set("ok", SrsJsonAny::boolean(s->ok));
    data->set("sample_time", SrsJsonAny::integer(s->sample_time));
    data->set("percent", SrsJsonAny::number(s->percent));
    data->set("user", SrsJsonAny::integer(s->user));
    data->set("nice", SrsJsonAny::integer(s->nice));
    data->set("sys", SrsJsonAny::integer(s->sys));
    data->set("idle", SrsJsonAny::integer(s->idle));
    data->set("iowait", SrsJsonAny::integer(s->iowait));
    data->set("irq", SrsJsonAny::integer(s->irq));
    data->set("softirq", SrsJsonAny::integer(s->softirq));
    data->set("steal", SrsJsonAny::integer(s->steal));
    data->set("guest", SrsJsonAny::integer(s->guest));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiMemInfos::SrsGoApiMemInfos()
{
}

SrsGoApiMemInfos::~SrsGoApiMemInfos()
{
}

srs_error_t SrsGoApiMemInfos::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    SrsMemInfo* m = srs_get_meminfo();
    
    data->set("ok", SrsJsonAny::boolean(m->ok));
    data->set("sample_time", SrsJsonAny::integer(m->sample_time));
    data->set("percent_ram", SrsJsonAny::number(m->percent_ram));
    data->set("percent_swap", SrsJsonAny::number(m->percent_swap));
    data->set("MemActive", SrsJsonAny::integer(m->MemActive));
    data->set("RealInUse", SrsJsonAny::integer(m->RealInUse));
    data->set("NotInUse", SrsJsonAny::integer(m->NotInUse));
    data->set("MemTotal", SrsJsonAny::integer(m->MemTotal));
    data->set("MemFree", SrsJsonAny::integer(m->MemFree));
    data->set("Buffers", SrsJsonAny::integer(m->Buffers));
    data->set("Cached", SrsJsonAny::integer(m->Cached));
    data->set("SwapTotal", SrsJsonAny::integer(m->SwapTotal));
    data->set("SwapFree", SrsJsonAny::integer(m->SwapFree));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiAuthors::SrsGoApiAuthors()
{
}

SrsGoApiAuthors::~SrsGoApiAuthors()
{
}

srs_error_t SrsGoApiAuthors::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    data->set("license", SrsJsonAny::str(RTMP_SIG_SRS_LICENSE));
    data->set("contributors", SrsJsonAny::str(SRS_CONSTRIBUTORS));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiFeatures::SrsGoApiFeatures()
{
}

SrsGoApiFeatures::~SrsGoApiFeatures()
{
}

srs_error_t SrsGoApiFeatures::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    data->set("options", SrsJsonAny::str(SRS_USER_CONFIGURE));
    data->set("options2", SrsJsonAny::str(SRS_CONFIGURE));
    data->set("build", SrsJsonAny::str(SRS_BUILD_DATE));
    data->set("build2", SrsJsonAny::str(SRS_BUILD_TS));
    
    SrsJsonObject* features = SrsJsonAny::object();
    data->set("features", features);
    
    features->set("ssl", SrsJsonAny::boolean(true));
    features->set("hls", SrsJsonAny::boolean(true));
#ifdef SRS_HDS
    features->set("hds", SrsJsonAny::boolean(true));
#else
    features->set("hds", SrsJsonAny::boolean(false));
#endif
    features->set("callback", SrsJsonAny::boolean(true));
    features->set("api", SrsJsonAny::boolean(true));
    features->set("httpd", SrsJsonAny::boolean(true));
    features->set("dvr", SrsJsonAny::boolean(true));
    features->set("transcode", SrsJsonAny::boolean(true));
    features->set("ingest", SrsJsonAny::boolean(true));
    features->set("stat", SrsJsonAny::boolean(true));
    features->set("caster", SrsJsonAny::boolean(true));
#ifdef SRS_PERF_COMPLEX_SEND
    features->set("complex_send", SrsJsonAny::boolean(true));
#else
    features->set("complex_send", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_PERF_TCP_NODELAY
    features->set("tcp_nodelay", SrsJsonAny::boolean(true));
#else
    features->set("tcp_nodelay", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_PERF_SO_SNDBUF_SIZE
    features->set("so_sendbuf", SrsJsonAny::boolean(true));
#else
    features->set("so_sendbuf", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_PERF_MERGED_READ
    features->set("mr", SrsJsonAny::boolean(true));
#else
    features->set("mr", SrsJsonAny::boolean(false));
#endif
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiRequests::SrsGoApiRequests()
{
}

SrsGoApiRequests::~SrsGoApiRequests()
{
}

srs_error_t SrsGoApiRequests::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    data->set("uri", SrsJsonAny::str(r->uri().c_str()));
    data->set("path", SrsJsonAny::str(r->path().c_str()));
    
    // method
    data->set("METHOD", SrsJsonAny::str(r->method_str().c_str()));
    
    // request headers
    SrsJsonObject* headers = SrsJsonAny::object();
    data->set("headers", headers);
    r->header()->dumps(headers);
    
    // server informations
    SrsJsonObject* server = SrsJsonAny::object();
    data->set("headers", server);
    
    server->set("sigature", SrsJsonAny::str(RTMP_SIG_SRS_KEY));
    server->set("version", SrsJsonAny::str(RTMP_SIG_SRS_VERSION));
    server->set("link", SrsJsonAny::str(RTMP_SIG_SRS_URL));
    server->set("time", SrsJsonAny::integer(srsu2ms(srs_get_system_time())));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiVhosts::SrsGoApiVhosts()
{
}

SrsGoApiVhosts::~SrsGoApiVhosts()
{
}

srs_error_t SrsGoApiVhosts::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    SrsStatistic* stat = SrsStatistic::instance();
    
    // path: {pattern}{vhost_id}
    // e.g. /api/v1/vhosts/100     pattern= /api/v1/vhosts/, vhost_id=100
    string vid = r->parse_rest_id(entry->pattern);
    SrsStatisticVhost* vhost = NULL;
    
    if (!vid.empty() && (vhost = stat->find_vhost_by_id(vid)) == NULL) {
        return srs_api_response_code(w, r, ERROR_RTMP_VHOST_NOT_FOUND);
    }
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    if (r->is_http_get()) {
        if (!vhost) {
            SrsJsonArray* data = SrsJsonAny::array();
            obj->set("vhosts", data);
            
            if ((err = stat->dumps_vhosts(data)) != srs_success) {
                int code = srs_error_code(err);
                srs_error_reset(err);
                return srs_api_response_code(w, r, code);
            }
        } else {
            SrsJsonObject* data = SrsJsonAny::object();
            obj->set("vhost", data);;
            
            if ((err = vhost->dumps(data)) != srs_success) {
                int code = srs_error_code(err);
                srs_error_reset(err);
                return srs_api_response_code(w, r, code);
            }
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiStreams::SrsGoApiStreams()
{
}

SrsGoApiStreams::~SrsGoApiStreams()
{
}

srs_error_t SrsGoApiStreams::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    SrsStatistic* stat = SrsStatistic::instance();
    
    // path: {pattern}{stream_id}
    // e.g. /api/v1/streams/100     pattern= /api/v1/streams/, stream_id=100
    string sid = r->parse_rest_id(entry->pattern);
    
    SrsStatisticStream* stream = NULL;
    if (!sid.empty() && (stream = stat->find_stream(sid)) == NULL) {
        return srs_api_response_code(w, r, ERROR_RTMP_STREAM_NOT_FOUND);
    }
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    if (r->is_http_get()) {
        if (!stream) {
            SrsJsonArray* data = SrsJsonAny::array();
            obj->set("streams", data);

            std::string rstart = r->query_get("start");
            std::string rcount = r->query_get("count");
            int start = srs_max(0, atoi(rstart.c_str()));
            int count = srs_max(10, atoi(rcount.c_str()));
            if ((err = stat->dumps_streams(data, start, count)) != srs_success) {
                int code = srs_error_code(err);
                srs_error_reset(err);
                return srs_api_response_code(w, r, code);
            }
        } else {
            SrsJsonObject* data = SrsJsonAny::object();
            obj->set("stream", data);;
            
            if ((err = stream->dumps(data)) != srs_success) {
                int code = srs_error_code(err);
                srs_error_reset(err);
                return srs_api_response_code(w, r, code);
            }
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiClients::SrsGoApiClients()
{
}

SrsGoApiClients::~SrsGoApiClients()
{
}

srs_error_t SrsGoApiClients::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    SrsStatistic* stat = SrsStatistic::instance();
    
    // path: {pattern}{client_id}
    // e.g. /api/v1/clients/100     pattern= /api/v1/clients/, client_id=100
    string client_id = r->parse_rest_id(entry->pattern);
    
    SrsStatisticClient* client = NULL;
    if (!client_id.empty() && (client = stat->find_client(client_id)) == NULL) {
        return srs_api_response_code(w, r, ERROR_RTMP_CLIENT_NOT_FOUND);
    }
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    
    if (r->is_http_get()) {
        if (!client) {
            SrsJsonArray* data = SrsJsonAny::array();
            obj->set("clients", data);
            
            std::string rstart = r->query_get("start");
            std::string rcount = r->query_get("count");
            int start = srs_max(0, atoi(rstart.c_str()));
            int count = srs_max(10, atoi(rcount.c_str()));
            if ((err = stat->dumps_clients(data, start, count)) != srs_success) {
                int code = srs_error_code(err);
                srs_error_reset(err);
                return srs_api_response_code(w, r, code);
            }
        } else {
            SrsJsonObject* data = SrsJsonAny::object();
            obj->set("client", data);;
            
            if ((err = client->dumps(data)) != srs_success) {
                int code = srs_error_code(err);
                srs_error_reset(err);
                return srs_api_response_code(w, r, code);
            }
        }
    } else if (r->is_http_delete()) {
        if (!client) {
            return srs_api_response_code(w, r, ERROR_RTMP_CLIENT_NOT_FOUND);
        }

        if (client->conn) {
            client->conn->expire();
            srs_warn("kickoff client id=%s ok", client_id.c_str());
        } else {
            srs_error("kickoff client id=%s error", client_id.c_str());
            return srs_api_response_code(w, r, SRS_CONSTS_HTTP_BadRequest);
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiRaw::SrsGoApiRaw(SrsServer* svr)
{
    server = svr;
    
    raw_api = _srs_config->get_raw_api();
    allow_reload = _srs_config->get_raw_api_allow_reload();
    allow_query = _srs_config->get_raw_api_allow_query();
    allow_update = _srs_config->get_raw_api_allow_update();
    
    _srs_config->subscribe(this);
}

SrsGoApiRaw::~SrsGoApiRaw()
{
    _srs_config->unsubscribe(this);
}

srs_error_t SrsGoApiRaw::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    std::string rpc = r->query_get("rpc");
    
    // the object to return for request.
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    
    // for rpc=raw, to query the raw api config for http api.
    if (rpc == "raw") {
        // query global scope.
        if ((err = _srs_config->raw_to_json(obj)) != srs_success) {
            int code = srs_error_code(err);
            srs_error_reset(err);
            return srs_api_response_code(w, r, code);
        }
        
        return srs_api_response(w, r, obj->dumps());
    }
    
    // whether enabled the HTTP RAW API.
    if (!raw_api) {
        return srs_api_response_code(w, r, ERROR_SYSTEM_CONFIG_RAW_DISABLED);
    }
    
    //////////////////////////////////////////////////////////////////////////
    // the rpc is required.
    // the allowd rpc method check.
    if (rpc.empty() || rpc != "reload") {
        return srs_api_response_code(w, r, ERROR_SYSTEM_CONFIG_RAW);
    }
    
    // for rpc=reload, trigger the server to reload the config.
    if (rpc == "reload") {
        if (!allow_reload) {
            return srs_api_response_code(w, r, ERROR_SYSTEM_CONFIG_RAW_DISABLED);
        }
        
        server->on_signal(SRS_SIGNAL_RELOAD);
        return srs_api_response_code(w, r, ERROR_SUCCESS);
    }
    
    return err;
}

srs_error_t SrsGoApiRaw::on_reload_http_api_raw_api()
{
    raw_api = _srs_config->get_raw_api();
    allow_reload = _srs_config->get_raw_api_allow_reload();
    allow_query = _srs_config->get_raw_api_allow_query();
    allow_update = _srs_config->get_raw_api_allow_update();
    
    return srs_success;
}

SrsGoApiClusters::SrsGoApiClusters()
{
}

SrsGoApiClusters::~SrsGoApiClusters()
{
}

srs_error_t SrsGoApiClusters::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    string ip = r->query_get("ip");
    string vhost = r->query_get("vhost");
    string app = r->query_get("app");
    string stream = r->query_get("stream");
    string coworker = r->query_get("coworker");
    data->set("query", SrsJsonAny::object()
              ->set("ip", SrsJsonAny::str(ip.c_str()))
              ->set("vhost", SrsJsonAny::str(vhost.c_str()))
              ->set("app", SrsJsonAny::str(app.c_str()))
              ->set("stream", SrsJsonAny::str(stream.c_str())));
    
    SrsCoWorkers* coworkers = SrsCoWorkers::instance();
    data->set("origin", coworkers->dumps(vhost, coworker, app, stream));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiError::SrsGoApiError()
{
}

SrsGoApiError::~SrsGoApiError()
{
}

srs_error_t SrsGoApiError::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    return srs_api_response_code(w, r, 100);
}

#ifdef SRS_GB28181
SrsGoApiGb28181::SrsGoApiGb28181()
{
}

SrsGoApiGb28181::~SrsGoApiGb28181()
{
}

srs_error_t SrsGoApiGb28181::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;

    if ((err = do_serve_http(w, r)) != srs_success) {
        srs_warn("Server GB28181 err %s", srs_error_desc(err).c_str());
        int code = srs_error_code(err); srs_error_reset(err);
        return srs_api_response_code(w, r, code);
    }

    return err;
}

srs_error_t SrsGoApiGb28181::do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;

    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    string id = r->query_get("id");
    string action = r->query_get("action");
    string vhost = r->query_get("vhost");
    string app = r->query_get("app");
    string stream = r->query_get("stream");
    //fixed, random
    string port_mode = r->query_get("port_mode");
   
    if (!_srs_gb28181) {
        return srs_error_new(ERROR_GB28181_SERVER_NOT_RUN, "no gb28181 engine");
    }

    if(action == "create_channel"){
        if (id.empty()){
            return srs_error_new(ERROR_GB28181_VALUE_EMPTY, "no id");
        }

        SrsGb28181StreamChannel channel;
        channel.set_channel_id(id);
        channel.set_app(app);
        channel.set_stream(stream);
        channel.set_port_mode(port_mode);

        if ((err = _srs_gb28181->create_stream_channel(&channel)) != srs_success) {
            return srs_error_wrap(err, "create stream channel");
        }

        data->set("query", SrsJsonAny::object()
          ->set("id", SrsJsonAny::str(channel.get_channel_id().c_str()))
          ->set("ip", SrsJsonAny::str(channel.get_ip().c_str()))
          ->set("rtmp_port", SrsJsonAny::integer(channel.get_rtmp_port()))
          ->set("app", SrsJsonAny::str(channel.get_app().c_str()))
          ->set("stream", SrsJsonAny::str(channel.get_stream().c_str()))
          ->set("rtp_port", SrsJsonAny::integer(channel.get_rtp_port()))
          ->set("ssrc", SrsJsonAny::integer(channel.get_ssrc())));
        return srs_api_response(w, r, obj->dumps());

    } else if(action == "delete_channel"){
        string chid = r->query_get("chid");
        if (id.empty() || chid.empty()){
            return srs_error_new(ERROR_GB28181_VALUE_EMPTY, "no id or chid");
        }

        if ((err = _srs_gb28181->delete_stream_channel(id, chid)) != srs_success) {
            return srs_error_wrap(err, "delete stream channel");
        }

        return srs_api_response_code(w, r, 0);
    } else if(action == "query_channel") {
        SrsJsonArray* arr = SrsJsonAny::array();
        data->set("channels", arr);

        if ((err = _srs_gb28181->query_stream_channel(id, arr)) != srs_success) {
            return srs_error_wrap(err, "query stream channel");
        }

        return srs_api_response(w, r, obj->dumps());
    } else if(action == "sip_invite"){
        string chid = r->query_get("chid");
        if (id.empty() || chid.empty()){
            return srs_error_new(ERROR_GB28181_VALUE_EMPTY, "no id or chid");
        }

        string ssrc = r->query_get("ssrc");
        string rtp_port = r->query_get("rtp_port");
        string ip = r->query_get("ip");

        int _port = strtoul(rtp_port.c_str(), NULL, 10);
        uint32_t _ssrc = (uint32_t)(strtoul(ssrc.c_str(), NULL, 10));

        if ((err = _srs_gb28181->notify_sip_invite(id, ip, _port, _ssrc, chid)) != srs_success) {
            return srs_error_wrap(err, "notify sip invite");
        }

        return srs_api_response_code(w, r, 0);
    } else if(action == "sip_bye"){
        string chid = r->query_get("chid");
        if (id.empty() || chid.empty()){
            return srs_error_new(ERROR_GB28181_VALUE_EMPTY, "no id or chid");
        }

        if ((err = _srs_gb28181->notify_sip_bye(id, chid)) != srs_success) {
            return srs_error_wrap(err, "notify sip bye");
        }

        return srs_api_response_code(w, r, 0);
    } else if(action == "sip_ptz"){
        string chid = r->query_get("chid");
        string ptzcmd = r->query_get("ptzcmd");
        string speed = r->query_get("speed");
        string priority = r->query_get("priority");
        if (id.empty() || chid.empty() || ptzcmd.empty() || speed.empty()){
            return srs_error_new(ERROR_GB28181_VALUE_EMPTY, "no id or chid or ptzcmd or speed");
        }

        uint8_t _speed = (uint8_t)(strtoul(speed.c_str(), NULL, 10));
        int _priority = (int)(strtoul(priority.c_str(), NULL, 10));

        if ((err = _srs_gb28181->notify_sip_ptz(id, chid, ptzcmd, _speed, _priority)) != srs_success) {
            return srs_error_wrap(err, "notify sip ptz");
        }

        return srs_api_response_code(w, r, 0);
    } else if(action == "sip_unregister"){
        if (id.empty()){
            return srs_error_new(ERROR_GB28181_VALUE_EMPTY, "no id");
        }

        if ((err = _srs_gb28181->notify_sip_unregister(id)) != srs_success) {
            return srs_error_wrap(err, "notify sip unregister");
        }

        return srs_api_response_code(w, r, 0);
    } else if(action == "sip_query_catalog"){
        if (id.empty()){
            return srs_error_new(ERROR_GB28181_VALUE_EMPTY, "no id");
        }

        if ((err = _srs_gb28181->notify_sip_query_catalog(id)) != srs_success) {
            return srs_error_wrap(err, "notify sip query catelog");
        }

        return srs_api_response_code(w, r, 0);
    } else if(action == "sip_query_devicelist"){
        SrsJsonArray* arr = SrsJsonAny::array();
        data->set("PlatformID", SrsJsonAny::str(_srs_gb28181->get_gb28181_config_ptr()->sip_serial.c_str()));
        data->set("DeviceList", arr);

        if ((err = _srs_gb28181->query_device_list("", arr)) != srs_success) {
            return srs_error_wrap(err, "query device list");
        }

        return srs_api_response(w, r, obj->dumps());
    } else if(action == "sip_query_session"){
        SrsJsonArray* arr = SrsJsonAny::array();
        data->set("sessions", arr);

        if ((err = _srs_gb28181->query_sip_session(id, arr)) != srs_success) {
            return srs_error_wrap(err, "notify sip session");
        }

        return srs_api_response(w, r, obj->dumps());
    } else {
        return srs_error_new(ERROR_GB28181_ACTION_INVALID, "action %s", action.c_str());
    }
}
#endif

#ifdef SRS_GPERF
#include <gperftools/malloc_extension.h>

SrsGoApiTcmalloc::SrsGoApiTcmalloc()
{
}

SrsGoApiTcmalloc::~SrsGoApiTcmalloc()
{
}

srs_error_t SrsGoApiTcmalloc::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;

    string page = r->query_get("page");
    srs_trace("query page=%s", page.c_str());

    if (page == "summary") {
        char buffer[32 * 1024];
        MallocExtension::instance()->GetStats(buffer, sizeof(buffer));

        string data(buffer);
        if ((err = w->write((char*)data.data(), (int)data.length())) != srs_success) {
            return srs_error_wrap(err, "write");
        }

        return err;
    }

    // By default, response the json style response.
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);

    if (true) {
        SrsJsonObject* p = SrsJsonAny::object();
        data->set("query", p);

        p->set("page", SrsJsonAny::str(page.c_str()));
        p->set("help", SrsJsonAny::str("?page=summary|detail"));
    }

    size_t value = 0;

    // @see https://gperftools.github.io/gperftools/tcmalloc.html
    data->set("release_rate", SrsJsonAny::number(MallocExtension::instance()->GetMemoryReleaseRate()));

    if (true) {
        SrsJsonObject* p = SrsJsonAny::object();
        data->set("generic", p);

        MallocExtension::instance()->GetNumericProperty("generic.current_allocated_bytes", &value);
        p->set("current_allocated_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("generic.heap_size", &value);
        p->set("heap_size", SrsJsonAny::integer(value));
    }

    if (true) {
        SrsJsonObject* p = SrsJsonAny::object();
        data->set("tcmalloc", p);

        MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_free_bytes", &value);
        p->set("pageheap_free_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_unmapped_bytes", &value);
        p->set("pageheap_unmapped_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("tcmalloc.slack_bytes", &value);
        p->set("slack_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("tcmalloc.max_total_thread_cache_bytes", &value);
        p->set("max_total_thread_cache_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("tcmalloc.current_total_thread_cache_bytes", &value);
        p->set("current_total_thread_cache_bytes", SrsJsonAny::integer(value));
    }

    return srs_api_response(w, r, obj->dumps());
}
#endif

SrsHttpApi::SrsHttpApi(bool https, ISrsResourceManager* cm, srs_netfd_t fd, SrsHttpServeMux* m, string cip, int port)
{
    // Create a identify for this client.
    _srs_context->set_id(_srs_context->generate_id());

    manager = cm;
    skt = new SrsTcpConnection(fd);

    if (https) {
        ssl = new SrsSslConnection(skt);
        conn = new SrsHttpConn(this, ssl, m, cip, port);
    } else {
        ssl = NULL;
        conn = new SrsHttpConn(this, skt, m, cip, port);
    }

    _srs_config->subscribe(this);
}

SrsHttpApi::~SrsHttpApi()
{
    _srs_config->unsubscribe(this);

    srs_freep(conn);
    srs_freep(ssl);
    srs_freep(skt);
}

srs_error_t SrsHttpApi::on_start()
{
    srs_error_t err = srs_success;

    if ((err = conn->set_jsonp(true)) != srs_success) {
        return srs_error_wrap(err, "set jsonp");
    }

    if (ssl) {
        srs_utime_t starttime = srs_update_system_time();
        string crt_file = _srs_config->get_https_api_ssl_cert();
        string key_file = _srs_config->get_https_api_ssl_key();
        if ((err = ssl->handshake(key_file, crt_file)) != srs_success) {
            return srs_error_wrap(err, "handshake");
        }

        int cost = srsu2msi(srs_update_system_time() - starttime);
        srs_trace("https: api server done, use key %s and cert %s, cost=%dms",
            key_file.c_str(), crt_file.c_str(), cost);
    }

    return err;
}

srs_error_t SrsHttpApi::on_http_message(ISrsHttpMessage* r, SrsHttpResponseWriter* w)
{
    srs_error_t err = srs_success;

    // After parsed the message, set the schema to https.
    if (ssl) {
        SrsHttpMessage* hm = dynamic_cast<SrsHttpMessage*>(r);
        hm->set_https(true);
    }

    // TODO: For each API session, we use short-term HTTP connection.
    //SrsHttpHeader* hdr = w->header();
    //hdr->set("Connection", "Close");

    return err;
}

srs_error_t SrsHttpApi::on_message_done(ISrsHttpMessage* r, SrsHttpResponseWriter* w)
{
    srs_error_t err = srs_success;

    // read all rest bytes in request body.
    char buf[SRS_HTTP_READ_CACHE_BYTES];
    ISrsHttpResponseReader* br = r->body_reader();
    while (!br->eof()) {
        if ((err = br->read(buf, SRS_HTTP_READ_CACHE_BYTES, NULL)) != srs_success) {
            return srs_error_wrap(err, "read response");
        }
    }

    return err;
}

srs_error_t SrsHttpApi::on_conn_done(srs_error_t r0)
{
    // Because we use manager to manage this object,
    // not the http connection object, so we must remove it here.
    manager->remove(this);

    // For HTTP-API timeout, we think it's done successfully,
    // because there may be no request or response for HTTP-API.
    if (srs_error_code(r0) == ERROR_SOCKET_TIMEOUT) {
        srs_freep(r0);
        return srs_success;
    }

    return r0;
}

std::string SrsHttpApi::desc()
{
    if (ssl) {
        return "HttpsConn";
    }
    return "HttpConn";
}

void SrsHttpApi::remark(int64_t* in, int64_t* out)
{
    conn->remark(in, out);
}

srs_error_t SrsHttpApi::on_reload_http_api_crossdomain()
{
    bool v = _srs_config->get_http_api_crossdomain();
    return conn->set_crossdomain_enabled(v);
}

srs_error_t SrsHttpApi::start()
{
    srs_error_t err = srs_success;

    bool v = _srs_config->get_http_api_crossdomain();
    if ((err = conn->set_crossdomain_enabled(v)) != srs_success) {
        return srs_error_wrap(err, "set cors=%d", v);
    }

    if ((err = skt->initialize()) != srs_success) {
        return srs_error_wrap(err, "init socket");
    }

    return conn->start();
}

string SrsHttpApi::remote_ip()
{
    return conn->remote_ip();
}

const SrsContextId& SrsHttpApi::get_id()
{
    return conn->get_id();
}

