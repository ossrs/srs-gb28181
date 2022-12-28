//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_config.hpp>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
// file operations.
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef __linux__
#include <linux/version.h>
#include <sys/utsname.h>
#endif

#include <vector>
#include <algorithm>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_source.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_performance.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_app_statistic.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_rtmp_stack.hpp>

using namespace srs_internal;

// @global the version to identify the core.
const char* _srs_version = "XCORE-" RTMP_SIG_SRS_SERVER;

// when user config an invalid value, macros to perfer true or false.
#define SRS_CONF_PERFER_FALSE(conf_arg) conf_arg == "on"
#define SRS_CONF_PERFER_TRUE(conf_arg) conf_arg != "off"

// default config file.
#define SRS_CONF_DEFAULT_COFNIG_FILE SRS_DEFAULT_CONFIG

// '\n'
#define SRS_LF (char)SRS_CONSTS_LF

// '\r'
#define SRS_CR (char)SRS_CONSTS_CR

/**
 * dumps the ingest/transcode-engine in @param dir to amf0 object @param engine.
 * @param dir the transcode or ingest config directive.
 * @param engine the amf0 object to dumps to.
 */
srs_error_t srs_config_dumps_engine(SrsConfDirective* dir, SrsJsonObject* engine);

/**
 * whether the ch is common space.
 */
bool is_common_space(char ch)
{
    return (ch == ' ' || ch == '\t' || ch == SRS_CR || ch == SRS_LF);
}

namespace srs_internal
{
    SrsConfigBuffer::SrsConfigBuffer()
    {
        line = 1;
        
        pos = last = start = NULL;
        end = start;
    }
    
    SrsConfigBuffer::~SrsConfigBuffer()
    {
        srs_freepa(start);
    }

    // LCOV_EXCL_START
    srs_error_t SrsConfigBuffer::fullfill(const char* filename)
    {
        srs_error_t err = srs_success;
        
        SrsFileReader reader;
        
        // open file reader.
        if ((err = reader.open(filename)) != srs_success) {
            return srs_error_wrap(err, "open file=%s", filename);
        }
        
        // read all.
        int filesize = (int)reader.filesize();
        
        // create buffer
        srs_freepa(start);
        pos = last = start = new char[filesize];
        end = start + filesize;
        
        // read total content from file.
        ssize_t nread = 0;
        if ((err = reader.read(start, filesize, &nread)) != srs_success) {
            return srs_error_wrap(err, "read %d only %d bytes", filesize, (int)nread);
        }
        
        return err;
    }
    // LCOV_EXCL_STOP
    
    bool SrsConfigBuffer::empty()
    {
        return pos >= end;
    }
};

bool srs_directive_equals_self(SrsConfDirective* a, SrsConfDirective* b)
{
    // both NULL, equal.
    if (!a && !b) {
        return true;
    }
    
    if (!a || !b) {
        return false;
    }
    
    if (a->name != b->name) {
        return false;
    }
    
    if (a->args.size() != b->args.size()) {
        return false;
    }
    
    for (int i = 0; i < (int)a->args.size(); i++) {
        if (a->args.at(i) != b->args.at(i)) {
            return false;
        }
    }
    
    if (a->directives.size() != b->directives.size()) {
        return false;
    }
    
    return true;
}

bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b)
{
    // both NULL, equal.
    if (!a && !b) {
        return true;
    }
    
    if (!srs_directive_equals_self(a, b)) {
        return false;
    }
    
    for (int i = 0; i < (int)a->directives.size(); i++) {
        SrsConfDirective* a0 = a->at(i);
        SrsConfDirective* b0 = b->at(i);
        
        if (!srs_directive_equals(a0, b0)) {
            return false;
        }
    }
    
    return true;
}

bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b, string except)
{
    // both NULL, equal.
    if (!a && !b) {
        return true;
    }
    
    if (!srs_directive_equals_self(a, b)) {
        return false;
    }
    
    for (int i = 0; i < (int)a->directives.size(); i++) {
        SrsConfDirective* a0 = a->at(i);
        SrsConfDirective* b0 = b->at(i);
        
        // donot compare the except child directive.
        if (a0->name == except) {
            continue;
        }
        
        if (!srs_directive_equals(a0, b0, except)) {
            return false;
        }
    }
    
    return true;
}

void set_config_directive(SrsConfDirective* parent, string dir, string value)
{
    SrsConfDirective* d = parent->get_or_create(dir);
    d->name = dir;
    d->args.clear();
    d->args.push_back(value);
}

bool srs_config_hls_is_on_error_ignore(string strategy)
{
    return strategy == "ignore";
}

bool srs_config_hls_is_on_error_continue(string strategy)
{
    return strategy == "continue";
}

bool srs_config_ingest_is_file(string type)
{
    return type == "file";
}

bool srs_config_ingest_is_stream(string type)
{
    return type == "stream";
}

bool srs_config_dvr_is_plan_segment(string plan)
{
    return plan == "segment";
}

bool srs_config_dvr_is_plan_session(string plan)
{
    return plan == "session";
}

bool srs_stream_caster_is_udp(string caster)
{
    return caster == "mpegts_over_udp";
}

bool srs_stream_caster_is_flv(string caster)
{
    return caster == "flv";
}

bool srs_stream_caster_is_gb28181(string caster)
{
    return caster == "gb28181";
}

bool srs_config_apply_filter(SrsConfDirective* dvr_apply, SrsRequest* req)
{
    static bool DEFAULT = true;
    
    if (!dvr_apply || dvr_apply->args.empty()) {
        return DEFAULT;
    }
    
    vector<string>& args = dvr_apply->args;
    if (args.size() == 1 && dvr_apply->arg0() == "all") {
        return true;
    }
    
    string id = req->app + "/" + req->stream;
    if (std::find(args.begin(), args.end(), id) != args.end()) {
        return true;
    }
    
    return false;
}

string srs_config_bool2switch(string sbool)
{
    return sbool == "true"? "on":"off";
}

srs_error_t srs_config_transform_vhost(SrsConfDirective* root)
{
    srs_error_t err = srs_success;
    
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* dir = root->directives.at(i);
        
        // SRS2.0, rename global http_stream to http_server.
        //  SRS1:
        //      http_stream {}
        //  SRS2+:
        //      http_server {}
        if (dir->name == "http_stream") {
            dir->name = "http_server";
            continue;
        }

        // SRS4.0, removed the support of configs:
        //      rtc_server { perf_stat; queue_length; }
        if (dir->name == "rtc_server") {
            std::vector<SrsConfDirective*>::iterator it;
            for (it = dir->directives.begin(); it != dir->directives.end();) {
                SrsConfDirective* conf = *it;

                if (conf->name == "perf_stat" || conf->name == "queue_length") {
                    dir->directives.erase(it);
                    srs_freep(conf);
                    continue;
                }

                ++it;
            }
        }

        // For GB28181, remove the directive jitterbuffer_enable.
        if (dir->name == "stream_caster") {
            if (dir->get_or_create("caster")->arg0() == "gb28181") {
                dir->remove(dir->get("jitterbuffer_enable"));
            }
        }
        
        if (!dir->is_vhost()) {
            continue;
        }
        
        // for each directive of vhost.
        std::vector<SrsConfDirective*>::iterator it;
        for (it = dir->directives.begin(); it != dir->directives.end();) {
            SrsConfDirective* conf = *it;
            string n = conf->name;
            
            // SRS2.0, rename vhost http to http_static
            //  SRS1:
            //      vhost { http {} }
            //  SRS2+:
            //      vhost { http_static {} }
            if (n == "http") {
                conf->name = "http_static";
                srs_warn("transform: vhost.http => vhost.http_static for %s", dir->name.c_str());
                ++it;
                continue;
            }
            
            // SRS3.0, ignore hstrs, always on.
            // SRS1/2:
            //      vhost { http_remux { hstrs; } }
            if (n == "http_remux") {
                SrsConfDirective* hstrs = conf->get("hstrs");
                conf->remove(hstrs);
                srs_freep(hstrs);
            }
            
            // SRS3.0, change the refer style
            //  SRS1/2:
            //      vhost { refer; refer_play; refer_publish; }
            //  SRS3+:
            //      vhost { refer { enabled; all; play; publish; } }
            if ((n == "refer" && conf->directives.empty()) || n == "refer_play" || n == "refer_publish") {
                // remove the old one first, for name duplicated.
                it = dir->directives.erase(it);
                
                SrsConfDirective* refer = dir->get_or_create("refer");
                refer->get_or_create("enabled", "on");
                if (n == "refer") {
                    SrsConfDirective* all = refer->get_or_create("all");
                    all->args = conf->args;
                    srs_warn("transform: vhost.refer to vhost.refer.all for %s", dir->name.c_str());
                } else if (n == "refer_play") {
                    SrsConfDirective* play = refer->get_or_create("play");
                    play->args = conf->args;
                    srs_warn("transform: vhost.refer_play to vhost.refer.play for %s", dir->name.c_str());
                } else if (n == "refer_publish") {
                    SrsConfDirective* publish = refer->get_or_create("publish");
                    publish->args = conf->args;
                    srs_warn("transform: vhost.refer_publish to vhost.refer.publish for %s", dir->name.c_str());
                }
                
                // remove the old directive.
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the mr style
            //  SRS2:
            //      vhost { mr { enabled; latency; } }
            //  SRS3+:
            //      vhost { publish { mr; mr_latency; } }
            if (n == "mr") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* publish = dir->get_or_create("publish");
                
                SrsConfDirective* enabled = conf->get("enabled");
                if (enabled) {
                    SrsConfDirective* mr = publish->get_or_create("mr");
                    mr->args = enabled->args;
                    srs_warn("transform: vhost.mr.enabled to vhost.publish.mr for %s", dir->name.c_str());
                }
                
                SrsConfDirective* latency = conf->get("latency");
                if (latency) {
                    SrsConfDirective* mr_latency = publish->get_or_create("mr_latency");
                    mr_latency->args = latency->args;
                    srs_warn("transform: vhost.mr.latency to vhost.publish.mr_latency for %s", dir->name.c_str());
                }
                
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the publish_1stpkt_timeout
            //  SRS2:
            //      vhost { publish_1stpkt_timeout; }
            //  SRS3+:
            //      vhost { publish { firstpkt_timeout; } }
            if (n == "publish_1stpkt_timeout") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* publish = dir->get_or_create("publish");
                
                SrsConfDirective* firstpkt_timeout = publish->get_or_create("firstpkt_timeout");
                firstpkt_timeout->args = conf->args;
                srs_warn("transform: vhost.publish_1stpkt_timeout to vhost.publish.firstpkt_timeout for %s", dir->name.c_str());
                
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the publish_normal_timeout
            //  SRS2:
            //      vhost { publish_normal_timeout; }
            //  SRS3+:
            //      vhost { publish { normal_timeout; } }
            if (n == "publish_normal_timeout") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* publish = dir->get_or_create("publish");
                
                SrsConfDirective* normal_timeout = publish->get_or_create("normal_timeout");
                normal_timeout->args = conf->args;
                srs_warn("transform: vhost.publish_normal_timeout to vhost.publish.normal_timeout for %s", dir->name.c_str());
                
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the bellow like a shadow:
            //      time_jitter, mix_correct, atc, atc_auto, mw_latency, gop_cache, queue_length
            //  SRS1/2:
            //      vhost { shadow; }
            //  SRS3+:
            //      vhost { play { shadow; } }
            if (n == "time_jitter" || n == "mix_correct" || n == "atc" || n == "atc_auto"
                || n == "mw_latency" || n == "gop_cache" || n == "queue_length" || n == "send_min_interval"
                || n == "reduce_sequence_header") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* play = dir->get_or_create("play");
                SrsConfDirective* shadow = play->get_or_create(conf->name);
                shadow->args = conf->args;
                srs_warn("transform: vhost.%s to vhost.play.%s of %s", n.c_str(), n.c_str(), dir->name.c_str());
                
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the forward.
            //  SRS1/2:
            //      vhost { forward target; }
            //  SRS3+:
            //      vhost { forward { enabled; destination target; } }
            if (n == "forward" && conf->directives.empty() && !conf->args.empty()) {
                conf->get_or_create("enabled")->set_arg0("on");
                
                SrsConfDirective* destination = conf->get_or_create("destination");
                destination->args = conf->args;
                conf->args.clear();
                srs_warn("transform: vhost.forward to vhost.forward.destination for %s", dir->name.c_str());
                
                ++it;
                continue;
            }

            // SRS3.0, change the bellow like a shadow:
            //      mode, origin, token_traverse, vhost, debug_srs_upnode
            //  SRS1/2:
            //      vhost { shadow; }
            //  SRS3+:
            //      vhost { cluster { shadow; } }
            if (n == "mode" || n == "origin" || n == "token_traverse" || n == "vhost" || n == "debug_srs_upnode") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* cluster = dir->get_or_create("cluster");
                SrsConfDirective* shadow = cluster->get_or_create(conf->name);
                shadow->args = conf->args;
                srs_warn("transform: vhost.%s to vhost.cluster.%s of %s", n.c_str(), n.c_str(), dir->name.c_str());
                
                srs_freep(conf);
                continue;
            }

            // SRS4.0, move nack/twcc to rtc:
            //      vhost { nack {enabled; no_copy;} twcc {enabled} }
            // as:
            //      vhost { rtc { nack on; nack_no_copy on; twcc on; } }
            if (n == "nack" || n == "twcc") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* rtc = dir->get_or_create("rtc");
                if (n == "nack") {
                    if (conf->get("enabled")) {
                        rtc->get_or_create("nack")->args = conf->get("enabled")->args;
                    }

                    if (conf->get("no_copy")) {
                        rtc->get_or_create("nack_no_copy")->args = conf->get("no_copy")->args;
                    }
                } else if (n == "twcc") {
                    if (conf->get("enabled")) {
                        rtc->get_or_create("twcc")->args = conf->get("enabled")->args;
                    }
                }
                srs_warn("transform: vhost.%s to vhost.rtc.%s of %s", n.c_str(), n.c_str(), dir->name.c_str());
                
                srs_freep(conf);
                continue;
            }

            // SRS3.0, change the forward.
            //  SRS1/2:
            //      vhost { rtc { aac; } }
            //  SRS3+:
            //      vhost { rtc { rtmp_to_rtc; } }
            if (n == "rtc") {
                SrsConfDirective* aac = conf->get("aac");
                if (aac) {
                    string v = aac->arg0() == "transcode" ? "on" : "off";
                    conf->get_or_create("rtmp_to_rtc")->set_arg0(v);
                    conf->remove(aac); srs_freep(aac);
                    srs_warn("transform: vhost.rtc.aac to vhost.rtc.rtmp_to_rtc %s", v.c_str());
                }

                SrsConfDirective* bframe = conf->get("bframe");
                if (bframe) {
                    string v = bframe->arg0() == "keep" ? "on" : "off";
                    conf->get_or_create("keep_bframe")->set_arg0(v);
                    conf->remove(bframe); srs_freep(bframe);
                    srs_warn("transform: vhost.rtc.bframe to vhost.rtc.keep_bframe %s", v.c_str());
                }

                ++it;
                continue;
            }
            
            ++it;
        }
    }
    
    return err;
}

// LCOV_EXCL_START
srs_error_t srs_config_dumps_engine(SrsConfDirective* dir, SrsJsonObject* engine)
{
    srs_error_t err = srs_success;
    
    SrsConfDirective* conf = NULL;
    
    engine->set("id", dir->dumps_arg0_to_str());
    engine->set("enabled", SrsJsonAny::boolean(_srs_config->get_engine_enabled(dir)));
    
    if ((conf = dir->get("iformat")) != NULL) {
        engine->set("iformat", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("vfilter")) != NULL) {
        SrsJsonObject* vfilter = SrsJsonAny::object();
        engine->set("vfilter", vfilter);
        
        for (int i = 0; i < (int)conf->directives.size(); i++) {
            SrsConfDirective* sdir = conf->directives.at(i);
            vfilter->set(sdir->name, sdir->dumps_arg0_to_str());
        }
    }
    
    if ((conf = dir->get("vcodec")) != NULL) {
        engine->set("vcodec", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("vbitrate")) != NULL) {
        engine->set("vbitrate", conf->dumps_arg0_to_integer());
    }
    
    if ((conf = dir->get("vfps")) != NULL) {
        engine->set("vfps", conf->dumps_arg0_to_number());
    }
    
    if ((conf = dir->get("vwidth")) != NULL) {
        engine->set("vwidth", conf->dumps_arg0_to_integer());
    }
    
    if ((conf = dir->get("vheight")) != NULL) {
        engine->set("vheight", conf->dumps_arg0_to_integer());
    }
    
    if ((conf = dir->get("vthreads")) != NULL) {
        engine->set("vthreads", conf->dumps_arg0_to_integer());
    }
    
    if ((conf = dir->get("vprofile")) != NULL) {
        engine->set("vprofile", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("vpreset")) != NULL) {
        engine->set("vpreset", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("vparams")) != NULL) {
        SrsJsonObject* vparams = SrsJsonAny::object();
        engine->set("vparams", vparams);
        
        for (int i = 0; i < (int)conf->directives.size(); i++) {
            SrsConfDirective* sdir = conf->directives.at(i);
            vparams->set(sdir->name, sdir->dumps_arg0_to_str());
        }
    }
    
    if ((conf = dir->get("acodec")) != NULL) {
        engine->set("acodec", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("abitrate")) != NULL) {
        engine->set("abitrate", conf->dumps_arg0_to_integer());
    }
    
    if ((conf = dir->get("asample_rate")) != NULL) {
        engine->set("asample_rate", conf->dumps_arg0_to_integer());
    }
    
    if ((conf = dir->get("achannels")) != NULL) {
        engine->set("achannels", conf->dumps_arg0_to_integer());
    }
    
    if ((conf = dir->get("aparams")) != NULL) {
        SrsJsonObject* aparams = SrsJsonAny::object();
        engine->set("aparams", aparams);
        
        for (int i = 0; i < (int)conf->directives.size(); i++) {
            SrsConfDirective* sdir = conf->directives.at(i);
            aparams->set(sdir->name, sdir->dumps_arg0_to_str());
        }
    }
    
    if ((conf = dir->get("oformat")) != NULL) {
        engine->set("oformat", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("output")) != NULL) {
        engine->set("output", conf->dumps_arg0_to_str());
    }
    
    return err;
}
// LCOV_EXCL_STOP

SrsConfDirective::SrsConfDirective()
{
    conf_line = 0;
}

SrsConfDirective::~SrsConfDirective()
{
    std::vector<SrsConfDirective*>::iterator it;
    for (it = directives.begin(); it != directives.end(); ++it) {
        SrsConfDirective* directive = *it;
        srs_freep(directive);
    }
    directives.clear();
}

SrsConfDirective* SrsConfDirective::copy()
{
    return copy("");
}

SrsConfDirective* SrsConfDirective::copy(string except)
{
    SrsConfDirective* cp = new SrsConfDirective();
    
    cp->conf_line = conf_line;
    cp->name = name;
    cp->args = args;
    
    for (int i = 0; i < (int)directives.size(); i++) {
        SrsConfDirective* directive = directives.at(i);
        if (!except.empty() && directive->name == except) {
            continue;
        }
        cp->directives.push_back(directive->copy(except));
    }
    
    return cp;
}

string SrsConfDirective::arg0()
{
    if (args.size() > 0) {
        return args.at(0);
    }
    
    return "";
}

string SrsConfDirective::arg1()
{
    if (args.size() > 1) {
        return args.at(1);
    }
    
    return "";
}

string SrsConfDirective::arg2()
{
    if (args.size() > 2) {
        return args.at(2);
    }
    
    return "";
}

string SrsConfDirective::arg3()
{
    if (args.size() > 3) {
        return args.at(3);
    }
    
    return "";
}

SrsConfDirective* SrsConfDirective::at(int index)
{
    srs_assert(index < (int)directives.size());
    return directives.at(index);
}

SrsConfDirective* SrsConfDirective::get(string _name)
{
    std::vector<SrsConfDirective*>::iterator it;
    for (it = directives.begin(); it != directives.end(); ++it) {
        SrsConfDirective* directive = *it;
        if (directive->name == _name) {
            return directive;
        }
    }
    
    return NULL;
}

SrsConfDirective* SrsConfDirective::get(string _name, string _arg0)
{
    std::vector<SrsConfDirective*>::iterator it;
    for (it = directives.begin(); it != directives.end(); ++it) {
        SrsConfDirective* directive = *it;
        if (directive->name == _name && directive->arg0() == _arg0) {
            return directive;
        }
    }
    
    return NULL;
}

SrsConfDirective* SrsConfDirective::get_or_create(string n)
{
    SrsConfDirective* conf = get(n);
    
    if (!conf) {
        conf = new SrsConfDirective();
        conf->name = n;
        directives.push_back(conf);
    }
    
    return conf;
}

SrsConfDirective* SrsConfDirective::get_or_create(string n, string a0)
{
    SrsConfDirective* conf = get(n, a0);
    
    if (!conf) {
        conf = new SrsConfDirective();
        conf->name = n;
        conf->args.push_back(a0);
        directives.push_back(conf);
    }
    
    return conf;
}

SrsConfDirective* SrsConfDirective::get_or_create(string n, string a0, string a1)
{
    SrsConfDirective* conf = get(n, a0);

    if (!conf) {
        conf = new SrsConfDirective();
        conf->name = n;
        conf->args.push_back(a0);
        conf->args.push_back(a1);
        directives.push_back(conf);
    }

    return conf;
}

SrsConfDirective* SrsConfDirective::set_arg0(string a0)
{
    if (arg0() == a0) {
        return this;
    }
    
    // update a0.
    if (!args.empty()) {
        args.erase(args.begin());
    }
    
    args.insert(args.begin(), a0);
    
    return this;
}

void SrsConfDirective::remove(SrsConfDirective* v)
{
    std::vector<SrsConfDirective*>::iterator it;
    if ((it = std::find(directives.begin(), directives.end(), v)) != directives.end()) {
        directives.erase(it);
    }
}

bool SrsConfDirective::is_vhost()
{
    return name == "vhost";
}

bool SrsConfDirective::is_stream_caster()
{
    return name == "stream_caster";
}

srs_error_t SrsConfDirective::parse(SrsConfigBuffer* buffer, SrsConfig* conf)
{
    return parse_conf(buffer, SrsDirectiveContextFile, conf);
}

srs_error_t SrsConfDirective::persistence(SrsFileWriter* writer, int level)
{
    srs_error_t err = srs_success;
    
    static char SPACE = SRS_CONSTS_SP;
    static char SEMICOLON = SRS_CONSTS_SE;
    static char LF = SRS_CONSTS_LF;
    static char LB = SRS_CONSTS_LB;
    static char RB = SRS_CONSTS_RB;
    static const char* INDENT = "    ";
    
    // for level0 directive, only contains sub directives.
    if (level > 0) {
        // indent by (level - 1) * 4 space.
        for (int i = 0; i < level - 1; i++) {
            if ((err = writer->write((char*)INDENT, 4, NULL)) != srs_success) {
                return srs_error_wrap(err, "write indent");
            }
        }
        
        // directive name.
        if ((err = writer->write((char*)name.c_str(), (int)name.length(), NULL)) != srs_success) {
            return srs_error_wrap(err, "write name");
        }
        if (!args.empty() && (err = writer->write((char*)&SPACE, 1, NULL)) != srs_success) {
            return srs_error_wrap(err, "write name space");
        }
        
        // directive args.
        for (int i = 0; i < (int)args.size(); i++) {
            std::string& arg = args.at(i);
            if ((err = writer->write((char*)arg.c_str(), (int)arg.length(), NULL)) != srs_success) {
                return srs_error_wrap(err, "write arg");
            }
            if (i < (int)args.size() - 1 && (err = writer->write((char*)&SPACE, 1, NULL)) != srs_success) {
                return srs_error_wrap(err, "write arg space");
            }
        }
        
        // native directive, without sub directives.
        if (directives.empty()) {
            if ((err = writer->write((char*)&SEMICOLON, 1, NULL)) != srs_success) {
                return srs_error_wrap(err, "write arg semicolon");
            }
        }
    }
    
    // persistence all sub directives.
    if (level > 0) {
        if (!directives.empty()) {
            if ((err = writer->write((char*)&SPACE, 1, NULL)) != srs_success) {
                return srs_error_wrap(err, "write sub-dir space");
            }
            if ((err = writer->write((char*)&LB, 1, NULL)) != srs_success) {
                return srs_error_wrap(err, "write sub-dir left-brace");
            }
        }
        
        if ((err = writer->write((char*)&LF, 1, NULL)) != srs_success) {
            return srs_error_wrap(err, "write sub-dir linefeed");
        }
    }
    
    for (int i = 0; i < (int)directives.size(); i++) {
        SrsConfDirective* dir = directives.at(i);
        if ((err = dir->persistence(writer, level + 1)) != srs_success) {
            return srs_error_wrap(err, "sub-dir %s", dir->name.c_str());
        }
    }
    
    if (level > 0 && !directives.empty()) {
        // indent by (level - 1) * 4 space.
        for (int i = 0; i < level - 1; i++) {
            if ((err = writer->write((char*)INDENT, 4, NULL)) != srs_success) {
                return srs_error_wrap(err, "write sub-dir indent");
            }
        }
        
        if ((err = writer->write((char*)&RB, 1, NULL)) != srs_success) {
            return srs_error_wrap(err, "write sub-dir right-brace");
        }
        
        if ((err = writer->write((char*)&LF, 1, NULL)) != srs_success) {
            return srs_error_wrap(err, "write sub-dir linefeed");
        }
    }
    
    
    return err;
}

// LCOV_EXCL_START
SrsJsonArray* SrsConfDirective::dumps_args()
{
    SrsJsonArray* arr = SrsJsonAny::array();
    for (int i = 0; i < (int)args.size(); i++) {
        string arg = args.at(i);
        arr->append(SrsJsonAny::str(arg.c_str()));
    }
    return arr;
}

SrsJsonAny* SrsConfDirective::dumps_arg0_to_str()
{
    return SrsJsonAny::str(arg0().c_str());
}

SrsJsonAny* SrsConfDirective::dumps_arg0_to_integer()
{
    return SrsJsonAny::integer(::atoll(arg0().c_str()));
}

SrsJsonAny* SrsConfDirective::dumps_arg0_to_number()
{
    return SrsJsonAny::number(::atof(arg0().c_str()));
}

SrsJsonAny* SrsConfDirective::dumps_arg0_to_boolean()
{
    return SrsJsonAny::boolean(arg0() == "on");
}
// LCOV_EXCL_STOP

// see: ngx_conf_parse
srs_error_t SrsConfDirective::parse_conf(SrsConfigBuffer* buffer, SrsDirectiveContext ctx, SrsConfig* conf)
{
    srs_error_t err = srs_success;
    
    while (true) {
        std::vector<string> args;
        int line_start = 0;
        SrsDirectiveState state = SrsDirectiveStateInit;
        if ((err = read_token(buffer, args, line_start, state)) != srs_success) {
            return srs_error_wrap(err, "read token, line=%d, state=%d", line_start, state);
        }

        if (state == SrsDirectiveStateBlockEnd) {
            return ctx == SrsDirectiveContextBlock ? srs_success : srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected \"}\"", buffer->line);
        }
        if (state == SrsDirectiveStateEOF) {
            return ctx != SrsDirectiveContextBlock ? srs_success : srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected end of file, expecting \"}\"", conf_line);
        }
        if (args.empty()) {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "line %d: empty directive", conf_line);
        }
        
        // Build normal directive which is not "include".
        if (args.at(0) != "include") {
            SrsConfDirective* directive = new SrsConfDirective();

            directive->conf_line = line_start;
            directive->name = args[0];
            args.erase(args.begin());
            directive->args.swap(args);

            directives.push_back(directive);

            if (state == SrsDirectiveStateBlockStart) {
                if ((err = directive->parse_conf(buffer, SrsDirectiveContextBlock, conf)) != srs_success) {
                    return srs_error_wrap(err, "parse dir");
                }
            }
            continue;
        }

        // Parse including, allow multiple files.
        vector<string> files(args.begin() + 1, args.end());
        if (files.empty()) {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "line %d: include is empty directive", buffer->line);
        }
        if (!conf) {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "line %d: no config", buffer->line);
        }

        for (int i = 0; i < (int)files.size(); i++) {
            std::string file = files.at(i);
            srs_assert(!file.empty());
            srs_trace("config parse include %s", file.c_str());

            SrsConfigBuffer* include_file_buffer = NULL;
            SrsAutoFree(SrsConfigBuffer, include_file_buffer);
            if ((err = conf->build_buffer(file, &include_file_buffer)) != srs_success) {
                return srs_error_wrap(err, "buffer fullfill %s", file.c_str());
            }

            if ((err = parse_conf(include_file_buffer, SrsDirectiveContextFile, conf)) != srs_success) {
                return srs_error_wrap(err, "parse include buffer");
            }
        }
    }
    
    return err;
}

// see: ngx_conf_read_token
srs_error_t SrsConfDirective::read_token(SrsConfigBuffer* buffer, vector<string>& args, int& line_start, SrsDirectiveState& state)
{
    srs_error_t err = srs_success;
    
    char* pstart = buffer->pos;
    
    bool sharp_comment = false;
    
    bool d_quoted = false;
    bool s_quoted = false;
    
    bool need_space = false;
    bool last_space = true;
    
    while (true) {
        if (buffer->empty()) {
            if (!args.empty() || !last_space) {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID,
                    "line %d: unexpected end of file, expecting ; or \"}\"",
                    buffer->line);
            }
            srs_trace("config parse complete");

            state = SrsDirectiveStateEOF;
            return err;
        }
        
        char ch = *buffer->pos++;
        
        if (ch == SRS_LF) {
            buffer->line++;
            sharp_comment = false;
        }
        
        if (sharp_comment) {
            continue;
        }
        
        if (need_space) {
            if (is_common_space(ch)) {
                last_space = true;
                need_space = false;
                continue;
            }
            if (ch == ';') {
                state = SrsDirectiveStateEntire;
                return err;
            }
            if (ch == '{') {
                state = SrsDirectiveStateBlockStart;
                return err;
            }
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected '%c'", buffer->line, ch);
        }
        
        // last charecter is space.
        if (last_space) {
            if (is_common_space(ch)) {
                continue;
            }
            pstart = buffer->pos - 1;
            switch (ch) {
                case ';':
                    if (args.size() == 0) {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected ';'", buffer->line);
                    }
                    state = SrsDirectiveStateEntire;
                    return err;
                case '{':
                    if (args.size() == 0) {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected '{'", buffer->line);
                    }
                    state = SrsDirectiveStateBlockStart;
                    return err;
                case '}':
                    if (args.size() != 0) {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected '}'", buffer->line);
                    }
                    state = SrsDirectiveStateBlockEnd;
                    return err;
                case '#':
                    sharp_comment = 1;
                    continue;
                case '"':
                    pstart++;
                    d_quoted = true;
                    last_space = 0;
                    continue;
                case '\'':
                    pstart++;
                    s_quoted = true;
                    last_space = 0;
                    continue;
                default:
                    last_space = 0;
                    continue;
            }
        } else {
            // last charecter is not space
            if (line_start == 0) {
                line_start = buffer->line;
            }
            
            bool found = false;
            if (d_quoted) {
                if (ch == '"') {
                    d_quoted = false;
                    need_space = true;
                    found = true;
                }
            } else if (s_quoted) {
                if (ch == '\'') {
                    s_quoted = false;
                    need_space = true;
                    found = true;
                }
            } else if (is_common_space(ch) || ch == ';' || ch == '{') {
                last_space = true;
                found = 1;
            }
            
            if (found) {
                int len = (int)(buffer->pos - pstart);
                char* aword = new char[len];
                memcpy(aword, pstart, len);
                aword[len - 1] = 0;
                
                string word_str = aword;
                if (!word_str.empty()) {
                    args.push_back(word_str);
                }
                srs_freepa(aword);
                
                if (ch == ';') {
                    state = SrsDirectiveStateEntire;
                    return err;
                }
                if (ch == '{') {
                    state = SrsDirectiveStateBlockStart;
                    return err;
                }
            }
        }
    }
    
    return err;
}

SrsConfig::SrsConfig()
{
    dolphin = false;
    
    show_help = false;
    show_version = false;
    test_conf = false;
    show_signature = false;
    
    root = new SrsConfDirective();
    root->conf_line = 0;
    root->name = "root";
}

SrsConfig::~SrsConfig()
{
    srs_freep(root);
}

bool SrsConfig::is_dolphin()
{
    return dolphin;
}

void SrsConfig::subscribe(ISrsReloadHandler* handler)
{
    std::vector<ISrsReloadHandler*>::iterator it;
    
    it = std::find(subscribes.begin(), subscribes.end(), handler);
    if (it != subscribes.end()) {
        return;
    }
    
    subscribes.push_back(handler);
}

void SrsConfig::unsubscribe(ISrsReloadHandler* handler)
{
    std::vector<ISrsReloadHandler*>::iterator it;
    
    it = std::find(subscribes.begin(), subscribes.end(), handler);
    if (it == subscribes.end()) {
        return;
    }
    
    subscribes.erase(it);
}

// LCOV_EXCL_START
srs_error_t SrsConfig::reload()
{
    srs_error_t err = srs_success;
    
    SrsConfig conf;
    
    if ((err = conf.parse_file(config_file.c_str())) != srs_success) {
        return srs_error_wrap(err, "parse file");
    }
    srs_info("config reloader parse file success.");
    
    // transform config to compatible with previous style of config.
    if ((err = srs_config_transform_vhost(conf.root)) != srs_success) {
        return srs_error_wrap(err, "transform config");
    }
    
    if ((err = conf.check_config()) != srs_success) {
        return srs_error_wrap(err, "check config");
    }
    
    if ((err = reload_conf(&conf)) != srs_success) {
        return srs_error_wrap(err, "reload config");
    }
    
    return err;
}
// LCOV_EXCL_STOP

srs_error_t SrsConfig::reload_vhost(SrsConfDirective* old_root)
{
    srs_error_t err = srs_success;
    
    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;
    
    // following directly support reload.
    //      origin, token_traverse, vhost, debug_srs_upnode
    
    // state graph
    //      old_vhost       new_vhost
    //      DISABLED    =>  ENABLED
    //      ENABLED     =>  DISABLED
    //      ENABLED     =>  ENABLED (modified)
    
    // collect all vhost names
    std::vector<std::string> vhosts;
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* vhost = root->at(i);
        if (vhost->name != "vhost") {
            continue;
        }
        vhosts.push_back(vhost->arg0());
    }
    for (int i = 0; i < (int)old_root->directives.size(); i++) {
        SrsConfDirective* vhost = old_root->at(i);
        if (vhost->name != "vhost") {
            continue;
        }
        if (root->get("vhost", vhost->arg0())) {
            continue;
        }
        vhosts.push_back(vhost->arg0());
    }
    
    // process each vhost
    for (int i = 0; i < (int)vhosts.size(); i++) {
        std::string vhost = vhosts.at(i);
        
        SrsConfDirective* old_vhost = old_root->get("vhost", vhost);
        SrsConfDirective* new_vhost = root->get("vhost", vhost);
        
        //      DISABLED    =>  ENABLED
        if (!get_vhost_enabled(old_vhost) && get_vhost_enabled(new_vhost)) {
            if ((err = do_reload_vhost_added(vhost)) != srs_success) {
                return srs_error_wrap(err, "reload vhost added");
            }
            continue;
        }
        
        //      ENABLED     =>  DISABLED
        if (get_vhost_enabled(old_vhost) && !get_vhost_enabled(new_vhost)) {
            if ((err = do_reload_vhost_removed(vhost)) != srs_success) {
                return srs_error_wrap(err, "reload vhost removed");
            }
            continue;
        }
        
        // cluster.mode, never supports reload.
        // first, for the origin and edge role change is too complex.
        // second, the vhosts in origin device group normally are all origin,
        //      they never change to edge sometimes.
        // third, the origin or upnode device can always be restart,
        //      edge will retry and the users connected to edge are ok.
        // it's ok to add or remove edge/origin vhost.
        if (get_vhost_is_edge(old_vhost) != get_vhost_is_edge(new_vhost)) {
            return srs_error_new(ERROR_RTMP_EDGE_RELOAD, "vhost mode changed");
        }
        
        // the auto reload configs:
        //      publish.parse_sps
        
        //      ENABLED     =>  ENABLED (modified)
        if (get_vhost_enabled(new_vhost) && get_vhost_enabled(old_vhost)) {
            srs_trace("vhost %s maybe modified, reload its detail.", vhost.c_str());
            // chunk_size, only one per vhost.
            if (!srs_directive_equals(new_vhost->get("chunk_size"), old_vhost->get("chunk_size"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_chunk_size(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes chunk_size failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload chunk_size success.", vhost.c_str());
            }
            
            // tcp_nodelay, only one per vhost
            if (!srs_directive_equals(new_vhost->get("tcp_nodelay"), old_vhost->get("tcp_nodelay"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_tcp_nodelay(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes tcp_nodelay failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload tcp_nodelay success.", vhost.c_str());
            }
            
            // min_latency, only one per vhost
            if (!srs_directive_equals(new_vhost->get("min_latency"), old_vhost->get("min_latency"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_realtime(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes min_latency failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload min_latency success.", vhost.c_str());
            }
            
            // play, only one per vhost
            if (!srs_directive_equals(new_vhost->get("play"), old_vhost->get("play"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_play(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes play failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload play success.", vhost.c_str());
            }
            
            // forward, only one per vhost
            if (!srs_directive_equals(new_vhost->get("forward"), old_vhost->get("forward"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_forward(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes forward failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload forward success.", vhost.c_str());
            }
            
            // To reload DASH.
            if (!srs_directive_equals(new_vhost->get("dash"), old_vhost->get("dash"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_dash(vhost)) != srs_success) {
                        return srs_error_wrap(err, "Reload vhost %s dash failed", vhost.c_str());
                    }
                }
                srs_trace("Reload vhost %s dash ok.", vhost.c_str());
            }
            
            // hls, only one per vhost
            // @remark, the hls_on_error directly support reload.
            if (!srs_directive_equals(new_vhost->get("hls"), old_vhost->get("hls"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_hls(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes hls failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload hls success.", vhost.c_str());
            }
            
            // hds reload
            if (!srs_directive_equals(new_vhost->get("hds"), old_vhost->get("hds"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_hds(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes hds failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload hds success.", vhost.c_str());
            }
            
            // dvr, only one per vhost, except the dvr_apply
            if (!srs_directive_equals(new_vhost->get("dvr"), old_vhost->get("dvr"), "dvr_apply")) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_dvr(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes dvr failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload dvr success.", vhost.c_str());
            }
            
            // exec, only one per vhost
            if (!srs_directive_equals(new_vhost->get("exec"), old_vhost->get("exec"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_exec(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes exec failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload exec success.", vhost.c_str());
            }
            
            // publish, only one per vhost
            if (!srs_directive_equals(new_vhost->get("publish"), old_vhost->get("publish"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_publish(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes publish failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload publish success.", vhost.c_str());
            }
            
            // http_static, only one per vhost.
            if (!srs_directive_equals(new_vhost->get("http_static"), old_vhost->get("http_static"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_http_updated()) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes http_static failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload http_static success.", vhost.c_str());
            }
            
            // http_remux, only one per vhost.
            if (!srs_directive_equals(new_vhost->get("http_remux"), old_vhost->get("http_remux"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((err = subscribe->on_reload_vhost_http_remux_updated(vhost)) != srs_success) {
                        return srs_error_wrap(err, "vhost %s notify subscribes http_remux failed", vhost.c_str());
                    }
                }
                srs_trace("vhost %s reload http_remux success.", vhost.c_str());
            }
            
            // transcode, many per vhost.
            if ((err = reload_transcode(new_vhost, old_vhost)) != srs_success) {
                return srs_error_wrap(err, "reload transcode");
            }
            
            // ingest, many per vhost.
            if ((err = reload_ingest(new_vhost, old_vhost)) != srs_success) {
                return srs_error_wrap(err, "reload ingest");
            }
            continue;
        }
        srs_trace("ignore reload vhost, enabled old: %d, new: %d",
                  get_vhost_enabled(old_vhost), get_vhost_enabled(new_vhost));
    }
    
    return err;
}

srs_error_t SrsConfig::reload_conf(SrsConfig* conf)
{
    srs_error_t err = srs_success;
    
    SrsConfDirective* old_root = root;
    SrsAutoFree(SrsConfDirective, old_root);
    
    root = conf->root;
    conf->root = NULL;
    
    // never support reload:
    //      daemon
    //
    // always support reload without additional code:
    //      chunk_size, ff_log_dir,
    //      bandcheck, http_hooks, heartbeat,
    //      security
    
    // merge config: listen
    if (!srs_directive_equals(root->get("listen"), old_root->get("listen"))) {
        if ((err = do_reload_listen()) != srs_success) {
            return srs_error_wrap(err, "listen");
        }
    }
    
    // merge config: pid
    if (!srs_directive_equals(root->get("pid"), old_root->get("pid"))) {
        if ((err = do_reload_pid()) != srs_success) {
            return srs_error_wrap(err, "pid");;
        }
    }
    
    // merge config: srs_log_tank
    if (!srs_directive_equals(root->get("srs_log_tank"), old_root->get("srs_log_tank"))) {
        if ((err = do_reload_srs_log_tank()) != srs_success) {
            return srs_error_wrap(err, "log tank");;
        }
    }
    
    // merge config: srs_log_level
    if (!srs_directive_equals(root->get("srs_log_level"), old_root->get("srs_log_level"))) {
        if ((err = do_reload_srs_log_level()) != srs_success) {
            return srs_error_wrap(err, "log level");;
        }
    }
    
    // merge config: srs_log_file
    if (!srs_directive_equals(root->get("srs_log_file"), old_root->get("srs_log_file"))) {
        if ((err = do_reload_srs_log_file()) != srs_success) {
            return srs_error_wrap(err, "log file");;
        }
    }
    
    // merge config: max_connections
    if (!srs_directive_equals(root->get("max_connections"), old_root->get("max_connections"))) {
        if ((err = do_reload_max_connections()) != srs_success) {
            return srs_error_wrap(err, "max connections");;
        }
    }
    
    // merge config: utc_time
    if (!srs_directive_equals(root->get("utc_time"), old_root->get("utc_time"))) {
        if ((err = do_reload_utc_time()) != srs_success) {
            return srs_error_wrap(err, "utc time");;
        }
    }
    
    // merge config: pithy_print_ms
    if (!srs_directive_equals(root->get("pithy_print_ms"), old_root->get("pithy_print_ms"))) {
        if ((err = do_reload_pithy_print_ms()) != srs_success) {
            return srs_error_wrap(err, "pithy print ms");;
        }
    }
    
    // merge config: http_api
    if ((err = reload_http_api(old_root)) != srs_success) {
        return srs_error_wrap(err, "http api");;
    }
    
    // merge config: http_stream
    if ((err = reload_http_stream(old_root)) != srs_success) {
        return srs_error_wrap(err, "http steram");;
    }

    // Merge config: rtc_server
    if ((err = reload_rtc_server(old_root)) != srs_success) {
        return srs_error_wrap(err, "http steram");;
    }
    
    // TODO: FIXME: support reload stream_caster.
    
    // merge config: vhost
    if ((err = reload_vhost(old_root)) != srs_success) {
        return srs_error_wrap(err, "vhost");;
    }
    
    return err;
}

srs_error_t SrsConfig::reload_http_api(SrsConfDirective* old_root)
{
    srs_error_t err = srs_success;
    
    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;
    
    // state graph
    //      old_http_api    new_http_api
    //      DISABLED    =>  ENABLED
    //      ENABLED     =>  DISABLED
    //      ENABLED     =>  ENABLED (modified)
    
    SrsConfDirective* new_http_api = root->get("http_api");
    SrsConfDirective* old_http_api = old_root->get("http_api");
    
    // DISABLED    =>      ENABLED
    if (!get_http_api_enabled(old_http_api) && get_http_api_enabled(new_http_api)) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((err = subscribe->on_reload_http_api_enabled()) != srs_success) {
                return srs_error_wrap(err, "http api off=>on");
            }
        }
        srs_trace("reload off=>on http_api success.");
        return err;
    }
    
    // ENABLED     =>      DISABLED
    if (get_http_api_enabled(old_http_api) && !get_http_api_enabled(new_http_api)) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((err = subscribe->on_reload_http_api_disabled()) != srs_success) {
                return srs_error_wrap(err, "http api on=>off");
            }
        }
        srs_trace("reload http_api on=>off success.");
        return err;
    }
    
    //      ENABLED     =>  ENABLED (modified)
    if (get_http_api_enabled(old_http_api) && get_http_api_enabled(new_http_api)
        && !srs_directive_equals(old_http_api, new_http_api)
        ) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((err = subscribe->on_reload_http_api_enabled()) != srs_success) {
                return srs_error_wrap(err, "http api enabled");
            }
        }
        srs_trace("reload http api enabled success.");
        
        if (!srs_directive_equals(old_http_api->get("crossdomain"), new_http_api->get("crossdomain"))) {
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((err = subscribe->on_reload_http_api_crossdomain()) != srs_success) {
                    return srs_error_wrap(err, "http api crossdomain");
                }
            }
        }
        srs_trace("reload http api crossdomain success.");
        
        if (!srs_directive_equals(old_http_api->get("raw_api"), new_http_api->get("raw_api"))) {
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((err = subscribe->on_reload_http_api_raw_api()) != srs_success) {
                    return srs_error_wrap(err, "http api raw_api");
                }
            }
        }
        srs_trace("reload http api raw_api success.");
        return err;
    }
    
    srs_trace("reload http_api success, nothing changed.");
    return err;
}

srs_error_t SrsConfig::reload_http_stream(SrsConfDirective* old_root)
{
    srs_error_t err = srs_success;
    
    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;
    
    // state graph
    //      old_http_stream     new_http_stream
    //      DISABLED    =>      ENABLED
    //      ENABLED     =>      DISABLED
    //      ENABLED     =>      ENABLED (modified)
    
    SrsConfDirective* new_http_stream = root->get("http_server");
    SrsConfDirective* old_http_stream = old_root->get("http_server");
    
    // DISABLED    =>      ENABLED
    if (!get_http_stream_enabled(old_http_stream) && get_http_stream_enabled(new_http_stream)) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((err = subscribe->on_reload_http_stream_enabled()) != srs_success) {
                return srs_error_wrap(err, "http stream off=>on");
            }
        }
        srs_trace("reload http stream off=>on success.");
        return err;
    }
    
    // ENABLED     =>      DISABLED
    if (get_http_stream_enabled(old_http_stream) && !get_http_stream_enabled(new_http_stream)) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((err = subscribe->on_reload_http_stream_disabled()) != srs_success) {
                return srs_error_wrap(err, "http stream on=>off");
            }
        }
        srs_trace("reload http stream on=>off success.");
        return err;
    }
    
    //      ENABLED     =>  ENABLED (modified)
    if (get_http_stream_enabled(old_http_stream) && get_http_stream_enabled(new_http_stream)
        && !srs_directive_equals(old_http_stream, new_http_stream)
        ) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((err = subscribe->on_reload_http_stream_updated()) != srs_success) {
                return srs_error_wrap(err, "http stream enabled");
            }
        }
        srs_trace("reload http stream enabled success.");
        
        if (!srs_directive_equals(old_http_stream->get("crossdomain"), new_http_stream->get("crossdomain"))) {
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((err = subscribe->on_reload_http_stream_crossdomain()) != srs_success) {
                    return srs_error_wrap(err, "http stream crossdomain");
                }
            }
        }
        srs_trace("reload http stream crossdomain success.");
        return err;
    }
    
    srs_trace("reload http stream success, nothing changed.");
    return err;
}

srs_error_t SrsConfig::reload_rtc_server(SrsConfDirective* old_root)
{
    srs_error_t err = srs_success;

    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;

    // state graph
    //      old_rtc_server     new_rtc_server
    //      ENABLED     =>      ENABLED (modified)

    SrsConfDirective* new_rtc_server = root->get("rtc_server");
    SrsConfDirective* old_rtc_server = old_root->get("rtc_server");

    // TODO: FIXME: Support disable or enable reloading.

    //      ENABLED     =>  ENABLED (modified)
    if (get_rtc_server_enabled(old_rtc_server) && get_rtc_server_enabled(new_rtc_server)
        && !srs_directive_equals(old_rtc_server, new_rtc_server)
        ) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((err = subscribe->on_reload_rtc_server()) != srs_success) {
                return srs_error_wrap(err, "rtc server enabled");
            }
        }
        srs_trace("reload rtc server success.");
        return err;
    }

    srs_trace("reload rtc server success, nothing changed.");
    return err;
}

srs_error_t SrsConfig::reload_transcode(SrsConfDirective* new_vhost, SrsConfDirective* old_vhost)
{
    srs_error_t err = srs_success;
    
    std::vector<SrsConfDirective*> old_transcoders;
    for (int i = 0; i < (int)old_vhost->directives.size(); i++) {
        SrsConfDirective* conf = old_vhost->at(i);
        if (conf->name == "transcode") {
            old_transcoders.push_back(conf);
        }
    }
    
    std::vector<SrsConfDirective*> new_transcoders;
    for (int i = 0; i < (int)new_vhost->directives.size(); i++) {
        SrsConfDirective* conf = new_vhost->at(i);
        if (conf->name == "transcode") {
            new_transcoders.push_back(conf);
        }
    }
    
    std::vector<ISrsReloadHandler*>::iterator it;
    
    std::string vhost = new_vhost->arg0();
    
    // to be simple:
    // whatever, once tiny changed of transcode,
    // restart all ffmpeg of vhost.
    bool changed = false;
    
    // discovery the removed ffmpeg.
    for (int i = 0; !changed && i < (int)old_transcoders.size(); i++) {
        SrsConfDirective* old_transcoder = old_transcoders.at(i);
        std::string transcoder_id = old_transcoder->arg0();
        
        // if transcoder exists in new vhost, not removed, ignore.
        if (new_vhost->get("transcode", transcoder_id)) {
            continue;
        }
        
        changed = true;
    }
    
    // discovery the added ffmpeg.
    for (int i = 0; !changed && i < (int)new_transcoders.size(); i++) {
        SrsConfDirective* new_transcoder = new_transcoders.at(i);
        std::string transcoder_id = new_transcoder->arg0();
        
        // if transcoder exists in old vhost, not added, ignore.
        if (old_vhost->get("transcode", transcoder_id)) {
            continue;
        }
        
        changed = true;
    }
    
    // for updated transcoders, restart them.
    for (int i = 0; !changed && i < (int)new_transcoders.size(); i++) {
        SrsConfDirective* new_transcoder = new_transcoders.at(i);
        std::string transcoder_id = new_transcoder->arg0();
        SrsConfDirective* old_transcoder = old_vhost->get("transcode", transcoder_id);
        srs_assert(old_transcoder);
        
        if (srs_directive_equals(new_transcoder, old_transcoder)) {
            continue;
        }
        
        changed = true;
    }
    
    // transcode, many per vhost
    if (changed) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((err = subscribe->on_reload_vhost_transcode(vhost)) != srs_success) {
                return srs_error_wrap(err, "vhost %s notify subscribes transcode failed", vhost.c_str());
            }
        }
        srs_trace("vhost %s reload transcode success.", vhost.c_str());
    }
    
    return err;
}

srs_error_t SrsConfig::reload_ingest(SrsConfDirective* new_vhost, SrsConfDirective* old_vhost)
{
    srs_error_t err = srs_success;
    
    std::vector<SrsConfDirective*> old_ingesters;
    for (int i = 0; i < (int)old_vhost->directives.size(); i++) {
        SrsConfDirective* conf = old_vhost->at(i);
        if (conf->name == "ingest") {
            old_ingesters.push_back(conf);
        }
    }
    
    std::vector<SrsConfDirective*> new_ingesters;
    for (int i = 0; i < (int)new_vhost->directives.size(); i++) {
        SrsConfDirective* conf = new_vhost->at(i);
        if (conf->name == "ingest") {
            new_ingesters.push_back(conf);
        }
    }
    
    std::vector<ISrsReloadHandler*>::iterator it;
    
    std::string vhost = new_vhost->arg0();
    
    // for removed ingesters, stop them.
    for (int i = 0; i < (int)old_ingesters.size(); i++) {
        SrsConfDirective* old_ingester = old_ingesters.at(i);
        std::string ingest_id = old_ingester->arg0();
        SrsConfDirective* new_ingester = new_vhost->get("ingest", ingest_id);
        
        // ENABLED => DISABLED
        if (get_ingest_enabled(old_ingester) && !get_ingest_enabled(new_ingester)) {
            // notice handler ingester removed.
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((err = subscribe->on_reload_ingest_removed(vhost, ingest_id)) != srs_success) {
                    return srs_error_wrap(err, "vhost %s notify subscribes ingest=%s removed failed", vhost.c_str(), ingest_id.c_str());
                }
            }
            srs_trace("vhost %s reload ingest=%s removed success.", vhost.c_str(), ingest_id.c_str());
        }
    }
    
    // for added ingesters, start them.
    for (int i = 0; i < (int)new_ingesters.size(); i++) {
        SrsConfDirective* new_ingester = new_ingesters.at(i);
        std::string ingest_id = new_ingester->arg0();
        SrsConfDirective* old_ingester = old_vhost->get("ingest", ingest_id);
        
        // DISABLED => ENABLED
        if (!get_ingest_enabled(old_ingester) && get_ingest_enabled(new_ingester)) {
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((err = subscribe->on_reload_ingest_added(vhost, ingest_id)) != srs_success) {
                    return srs_error_wrap(err, "vhost %s notify subscribes ingest=%s added failed", vhost.c_str(), ingest_id.c_str());
                }
            }
            srs_trace("vhost %s reload ingest=%s added success.", vhost.c_str(), ingest_id.c_str());
        }
    }
    
    // for updated ingesters, restart them.
    for (int i = 0; i < (int)new_ingesters.size(); i++) {
        SrsConfDirective* new_ingester = new_ingesters.at(i);
        std::string ingest_id = new_ingester->arg0();
        SrsConfDirective* old_ingester = old_vhost->get("ingest", ingest_id);
        
        // ENABLED => ENABLED
        if (get_ingest_enabled(old_ingester) && get_ingest_enabled(new_ingester)) {
            if (srs_directive_equals(new_ingester, old_ingester)) {
                continue;
            }
            
            // notice handler ingester removed.
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((err = subscribe->on_reload_ingest_updated(vhost, ingest_id)) != srs_success) {
                    return srs_error_wrap(err, "vhost %s notify subscribes ingest=%s updated failed", vhost.c_str(), ingest_id.c_str());
                }
            }
            srs_trace("vhost %s reload ingest=%s updated success.", vhost.c_str(), ingest_id.c_str());
        }
    }
    
    srs_trace("ingest nothing changed for vhost=%s", vhost.c_str());
    
    return err;
}

// see: ngx_get_options
// LCOV_EXCL_START
srs_error_t SrsConfig::parse_options(int argc, char** argv)
{
    srs_error_t err = srs_success;
    
    // argv
    for (int i = 0; i < argc; i++) {
        _argv.append(argv[i]);
        
        if (i < argc - 1) {
            _argv.append(" ");
        }
    }
    
    // Show help if has any argv
    show_help = argc > 1;
    for (int i = 1; i < argc; i++) {
        if ((err = parse_argv(i, argv)) != srs_success) {
            return srs_error_wrap(err, "parse argv");
        }
    }
    
    if (show_help) {
        print_help(argv);
        exit(0);
    }
    
    if (show_version) {
        fprintf(stderr, "%s\n", RTMP_SIG_SRS_VERSION);
        exit(0);
    }
    if (show_signature) {
        fprintf(stderr, "%s\n", RTMP_SIG_SRS_SERVER);
        exit(0);
    }
    
    // first hello message.
    srs_trace(_srs_version);

    // Try config files as bellow:
    //      User specified config(not empty), like user/docker.conf
    //      If user specified *docker.conf, try *srs.conf, like user/srs.conf
    //      Try the default srs config, defined as SRS_CONF_DEFAULT_COFNIG_FILE, like conf/srs.conf
    //      Try config for FHS, like /etc/srs/srs.conf @see https://github.com/ossrs/srs/pull/2711
    if (true) {
        vector<string> try_config_files;
        if (!config_file.empty()) {
            try_config_files.push_back(config_file);
        }
        if (srs_string_ends_with(config_file, "docker.conf")) {
            try_config_files.push_back(srs_string_replace(config_file, "docker.conf", "srs.conf"));
        }
        try_config_files.push_back(SRS_CONF_DEFAULT_COFNIG_FILE);
        try_config_files.push_back("/etc/srs/srs.conf");

        // Match the first exists file.
        string exists_config_file;
        for (int i = 0; i < (int) try_config_files.size(); i++) {
            string try_config_file = try_config_files.at(i);
            if (srs_path_exists(try_config_file)) {
                exists_config_file = try_config_file;
                break;
            }
        }

        if (exists_config_file.empty()) {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "no config file at %s", srs_join_vector_string(try_config_files, ", ").c_str());
        }

        if (config_file != exists_config_file) {
            srs_warn("user config %s does not exists, use %s instead", config_file.c_str(), exists_config_file.c_str());
            config_file = exists_config_file;
        }
    }

    // Parse the matched config file.
    err = parse_file(config_file.c_str());

    if (test_conf) {
        // the parse_file never check the config,
        // we check it when user requires check config file.
        if (err == srs_success && (err = srs_config_transform_vhost(root)) == srs_success) {
            if ((err = check_config()) == srs_success) {
                srs_trace("the config file %s syntax is ok", config_file.c_str());
                srs_trace("config file %s test is successful", config_file.c_str());
                exit(0);
            }
        }

        srs_trace("invalid config%s in %s", srs_error_summary(err).c_str(), config_file.c_str());
        srs_trace("config file %s test is failed", config_file.c_str());
        exit(srs_error_code(err));
    }

    if (err != srs_success) {
        return srs_error_wrap(err, "invalid config");
    }

    // transform config to compatible with previous style of config.
    if ((err = srs_config_transform_vhost(root)) != srs_success) {
        return srs_error_wrap(err, "transform");
    }

    ////////////////////////////////////////////////////////////////////////
    // check log name and level
    ////////////////////////////////////////////////////////////////////////
    if (true) {
        std::string log_filename = this->get_log_file();
        if (get_log_tank_file() && log_filename.empty()) {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "no log file");
        }
        if (get_log_tank_file()) {
            srs_trace("you can check log by: tail -n 30 -f %s", log_filename.c_str());
            srs_trace("please check SRS by: ./etc/init.d/srs status");
        } else {
            srs_trace("write log to console");
        }
    }
    
    return err;
}

srs_error_t SrsConfig::initialize_cwd()
{
    // cwd
    char cwd[256];
    getcwd(cwd, sizeof(cwd));
    _cwd = cwd;
    
    return srs_success;
}

srs_error_t SrsConfig::persistence()
{
    srs_error_t err = srs_success;
    
    // write to a tmp file, then mv to the config.
    std::string path = config_file + ".tmp";
    
    // open the tmp file for persistence
    SrsFileWriter fw;
    if ((err = fw.open(path)) != srs_success) {
        return srs_error_wrap(err, "open file");
    }
    
    // do persistence to writer.
    if ((err = do_persistence(&fw)) != srs_success) {
        ::unlink(path.c_str());
        return srs_error_wrap(err, "persistence");
    }
    
    // rename the config file.
    if (::rename(path.c_str(), config_file.c_str()) < 0) {
        ::unlink(path.c_str());
        return srs_error_new(ERROR_SYSTEM_CONFIG_PERSISTENCE, "rename %s=>%s", path.c_str(), config_file.c_str());
    }
    
    return err;
}

srs_error_t SrsConfig::do_persistence(SrsFileWriter* fw)
{
    srs_error_t err = srs_success;
    
    // persistence root directive to writer.
    if ((err = root->persistence(fw, 0)) != srs_success) {
        return srs_error_wrap(err, "root persistence");
    }
    
    return err;
}

srs_error_t SrsConfig::raw_to_json(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;

    SrsJsonObject* sobj = SrsJsonAny::object();
    obj->set("http_api", sobj);

    sobj->set("enabled", SrsJsonAny::boolean(get_http_api_enabled()));
    sobj->set("listen", SrsJsonAny::str(get_http_api_listen().c_str()));
    sobj->set("crossdomain", SrsJsonAny::boolean(get_http_api_crossdomain()));

    SrsJsonObject* ssobj = SrsJsonAny::object();
    sobj->set("raw_api", ssobj);

    ssobj->set("enabled", SrsJsonAny::boolean(get_raw_api()));
    ssobj->set("allow_reload", SrsJsonAny::boolean(get_raw_api_allow_reload()));
    ssobj->set("allow_query", SrsJsonAny::boolean(get_raw_api_allow_query()));
    ssobj->set("allow_update", SrsJsonAny::boolean(get_raw_api_allow_update()));

    return err;
}

srs_error_t SrsConfig::do_reload_listen()
{
    srs_error_t err = srs_success;
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_listen()) != srs_success) {
            return srs_error_wrap(err, "notify subscribes reload listen failed");
        }
    }
    srs_trace("reload listen success.");
    
    return err;
}

srs_error_t SrsConfig::do_reload_pid()
{
    srs_error_t err = srs_success;
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_pid()) != srs_success) {
            return srs_error_wrap(err, "notify subscribes reload pid failed");
        }
    }
    srs_trace("reload pid success.");
    
    return err;
}

srs_error_t SrsConfig::do_reload_srs_log_tank()
{
    srs_error_t err = srs_success;
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_log_tank()) != srs_success) {
            return srs_error_wrap(err, "notify subscribes reload srs_log_tank failed");
        }
    }
    srs_trace("reload srs_log_tank success.");
    
    return err;
}

srs_error_t SrsConfig::do_reload_srs_log_level()
{
    srs_error_t err = srs_success;
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_log_level()) != srs_success) {
            return srs_error_wrap(err, "notify subscribes reload srs_log_level failed");
        }
    }
    srs_trace("reload srs_log_level success.");
    
    return err;
}

srs_error_t SrsConfig::do_reload_srs_log_file()
{
    srs_error_t err = srs_success;
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_log_file()) != srs_success) {
            return srs_error_wrap(err, "notify subscribes reload srs_log_file failed");
        }
    }
    srs_trace("reload srs_log_file success.");
    
    return err;
}

srs_error_t SrsConfig::do_reload_max_connections()
{
    srs_error_t err = srs_success;
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_max_conns()) != srs_success) {
            return srs_error_wrap(err, "notify subscribes reload max_connections failed");
        }
    }
    srs_trace("reload max_connections success.");
    
    return err;
}

srs_error_t SrsConfig::do_reload_utc_time()
{
    srs_error_t err = srs_success;
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_utc_time()) != srs_success) {
            return srs_error_wrap(err, "utc_time");
        }
    }
    srs_trace("reload utc_time success.");
    
    return err;
}

srs_error_t SrsConfig::do_reload_pithy_print_ms()
{
    srs_error_t err = srs_success;
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_pithy_print()) != srs_success) {
            return srs_error_wrap(err, "notify subscribes pithy_print_ms failed");
        }
    }
    srs_trace("reload pithy_print_ms success.");
    
    return err;
}

srs_error_t SrsConfig::do_reload_vhost_added(string vhost)
{
    srs_error_t err = srs_success;
    
    srs_trace("vhost %s added, reload it.", vhost.c_str());
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_vhost_added(vhost)) != srs_success) {
            return srs_error_wrap(err, "notify subscribes added vhost %s failed", vhost.c_str());
        }
    }
    
    srs_trace("reload new vhost %s success.", vhost.c_str());
    
    return err;
}

srs_error_t SrsConfig::do_reload_vhost_removed(string vhost)
{
    srs_error_t err = srs_success;
    
    srs_trace("vhost %s removed, reload it.", vhost.c_str());
    
    vector<ISrsReloadHandler*>::iterator it;
    for (it = subscribes.begin(); it != subscribes.end(); ++it) {
        ISrsReloadHandler* subscribe = *it;
        if ((err = subscribe->on_reload_vhost_removed(vhost)) != srs_success) {
            return srs_error_wrap(err, "notify subscribes removed vhost %s failed", vhost.c_str());
        }
    }
    srs_trace("reload removed vhost %s success.", vhost.c_str());
    
    return err;
}

string SrsConfig::config()
{
    return config_file;
}

// LCOV_EXCL_START
srs_error_t SrsConfig::parse_argv(int& i, char** argv)
{
    srs_error_t err = srs_success;
    
    char* p = argv[i];
    
    if (*p++ != '-') {
        show_help = true;
        return err;
    }
    
    while (*p) {
        switch (*p++) {
            case '?':
            case 'h':
                show_help = true;
                break;
            case 't':
                show_help = false;
                test_conf = true;
                break;
            case 'p':
                dolphin = true;
                if (*p) {
                    dolphin_rtmp_port = p;
                    continue;
                }
                if (argv[++i]) {
                    dolphin_rtmp_port = argv[i];
                    continue;
                }
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "-p requires params");
            case 'x':
                dolphin = true;
                if (*p) {
                    dolphin_http_port = p;
                    continue;
                }
                if (argv[++i]) {
                    dolphin_http_port = argv[i];
                    continue;
                }
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "-x requires params");
            case 'v':
            case 'V':
                show_help = false;
                show_version = true;
                break;
            case 'g':
            case 'G':
                show_help = false;
                show_signature = true;
                break;
            case 'c':
                show_help = false;
                if (*p) {
                    config_file = p;
                    continue;
                }
                if (argv[++i]) {
                    config_file = argv[i];
                    continue;
                }
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "-c requires params");
            default:
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "invalid option: \"%c\", read help: %s -h",
                    *(p - 1), argv[0]);
        }
    }
    
    return err;
}

void SrsConfig::print_help(char** argv)
{
    printf(
           "%s, %s, %s, created by %sand %s\n\n"
           "Usage: %s <-h?vVgG>|<[-t] -c filename>\n"
           "Options:\n"
           "   -?, -h              : Show this help and exit 0.\n"
           "   -v, -V              : Show version and exit 0.\n"
           "   -g, -G              : Show server signature and exit 0.\n"
           "   -t                  : Test configuration file, exit with error code(0 for success).\n"
           "   -c filename         : Use config file to start server.\n"
           "For example:\n"
           "   %s -v\n"
           "   %s -t -c %s\n"
           "   %s -c %s\n",
           RTMP_SIG_SRS_SERVER, RTMP_SIG_SRS_URL, RTMP_SIG_SRS_LICENSE,
           RTMP_SIG_SRS_AUTHORS, SRS_CONSTRIBUTORS,
           argv[0], argv[0], argv[0], SRS_CONF_DEFAULT_COFNIG_FILE,
           argv[0], SRS_CONF_DEFAULT_COFNIG_FILE);
}

srs_error_t SrsConfig::parse_file(const char* filename)
{
    srs_error_t err = srs_success;
    
    config_file = filename;
    
    if (config_file.empty()) {
        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "empty config");
    }

    SrsConfigBuffer* buffer = NULL;
    SrsAutoFree(SrsConfigBuffer, buffer);
    if ((err = build_buffer(config_file, &buffer)) != srs_success) {
        return srs_error_wrap(err, "buffer fullfill %s", config_file.c_str());
    }
    
    if ((err = parse_buffer(buffer)) != srs_success) {
        return srs_error_wrap(err, "parse buffer");
    }
    
    return err;
}

srs_error_t SrsConfig::build_buffer(string src, SrsConfigBuffer** pbuffer)
{
    srs_error_t err = srs_success;

    SrsConfigBuffer* buffer = new SrsConfigBuffer();

    if ((err = buffer->fullfill(src.c_str())) != srs_success) {
        srs_freep(buffer);
        return srs_error_wrap(err, "read from src %s", src.c_str());
    }

    *pbuffer = buffer;
    return err;
}
// LCOV_EXCL_STOP

srs_error_t SrsConfig::check_config()
{
    srs_error_t err = srs_success;
    
    if ((err = check_normal_config()) != srs_success) {
        return srs_error_wrap(err, "check normal");
    }
    
    if ((err = check_number_connections()) != srs_success) {
        return srs_error_wrap(err, "check connections");
    }

    // If use the full.conf, fail.
    if (is_full_config()) {
        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID,
            "never use full.conf(%s)", config_file.c_str());
    }
    
    return err;
}

srs_error_t SrsConfig::check_normal_config()
{
    srs_error_t err = srs_success;
    
    srs_trace("srs checking config...");
    
    ////////////////////////////////////////////////////////////////////////
    // check empty
    ////////////////////////////////////////////////////////////////////////
    if (root->directives.size() == 0) {
        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "conf is empty");
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check root directives.
    ////////////////////////////////////////////////////////////////////////
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        std::string n = conf->name;
        if (n != "listen" && n != "pid" && n != "chunk_size" && n != "ff_log_dir"
            && n != "srs_log_tank" && n != "srs_log_level" && n != "srs_log_file"
            && n != "max_connections" && n != "daemon" && n != "heartbeat"
            && n != "http_api" && n != "stats" && n != "vhost" && n != "pithy_print_ms"
            && n != "http_server" && n != "stream_caster" && n != "rtc_server" && n != "srt_server"
            && n != "utc_time" && n != "work_dir" && n != "asprocess"
            && n != "ff_log_level" && n != "grace_final_wait" && n != "force_grace_quit"
            && n != "grace_start_wait" && n != "empty_ip_ok" && n != "disable_daemon_for_docker"
            && n != "inotify_auto_reload" && n != "auto_reload_for_docker" && n != "tcmalloc_release_rate"
            && n != "query_latest_version"
            && n != "circuit_breaker" && n != "is_full" && n != "in_docker"
            ) {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal directive %s", n.c_str());
        }
    }
    if (true) {
        SrsConfDirective* conf = root->get("http_api");
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            SrsConfDirective* obj = conf->at(i);
            string n = obj->name;
            if (n != "enabled" && n != "listen" && n != "crossdomain" && n != "raw_api" && n != "https") {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal http_api.%s", n.c_str());
            }
            
            if (n == "raw_api") {
                for (int j = 0; j < (int)obj->directives.size(); j++) {
                    string m = obj->at(j)->name;
                    if (m != "enabled" && m != "allow_reload" && m != "allow_query" && m != "allow_update") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal http_api.raw_api.%s", m.c_str());
                    }
                }
            }
        }
    }
    if (true) {
        SrsConfDirective* conf = root->get("http_server");
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            string n = conf->at(i)->name;
            if (n != "enabled" && n != "listen" && n != "dir" && n != "crossdomain" && n != "https") {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal http_stream.%s", n.c_str());
            }
        }
    }
    if (true) {
        SrsConfDirective* conf = root->get("srt_server");
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            string n = conf->at(i)->name;
            if (n != "enabled" && n != "listen" && n != "maxbw"
                && n != "mss" && n != "latency" && n != "recvlatency"
                && n != "peerlatency" && n != "tlpkdrop" && n != "connect_timeout"
                && n != "sendbuf" && n != "recvbuf" && n != "payloadsize"
                && n != "default_app" && n != "mix_correct" && n != "sei_filter") {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal srt_stream.%s", n.c_str());
            }
        }
    }
    if (true) {
        SrsConfDirective* conf = get_heartbeart();
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            string n = conf->at(i)->name;
            if (n != "enabled" && n != "interval" && n != "url"
                && n != "device_id" && n != "summaries") {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal heartbeat.%s", n.c_str());
            }
        }
    }
    if (true) {
        SrsConfDirective* conf = get_stats();
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            string n = conf->at(i)->name;
            if (n != "enabled" && n != "network" && n != "disk") {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal stats.%s", n.c_str());
            }
        }
    }
    if (true) {
        SrsConfDirective* conf = root->get("rtc_server");
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            string n = conf->at(i)->name;
            if (n != "enabled" && n != "listen" && n != "dir" && n != "candidate" && n != "ecdsa"
                && n != "encrypt" && n != "reuseport" && n != "merge_nalus" && n != "black_hole"
                && n != "ip_family" && n != "api_as_candidates") {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal rtc_server.%s", n.c_str());
            }
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check listen for rtmp.
    ////////////////////////////////////////////////////////////////////////
    if (true) {
        vector<string> listens = get_listens();
        if (listens.size() <= 0) {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "listen requires params");
        }
        for (int i = 0; i < (int)listens.size(); i++) {
            int port; string ip;
            srs_parse_endpoint(listens[i], ip, port);

            // check ip
            if (!srs_check_ip_addr_valid(ip)) {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "listen.ip=%s is invalid", ip.c_str());
            }

            // check port
            if (port <= 0) {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "listen.port=%d is invalid", port);
            }
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check heartbeat
    ////////////////////////////////////////////////////////////////////////
    if (get_heartbeat_interval() <= 0) {
        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "invalid heartbeat.interval=%" PRId64,
            get_heartbeat_interval());
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check stats
    ////////////////////////////////////////////////////////////////////////
    if (get_stats_network() < 0) {
        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "invalid stats.network=%d", get_stats_network());
    }
    if (true) {
        vector<SrsIPAddress*> ips = srs_get_local_ips();
        int index = get_stats_network();
        if (index >= (int)ips.size()) {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "invalid stats.network=%d of %d",
                index, (int)ips.size());
        }

        SrsIPAddress* addr = ips.at(index);
        srs_warn("stats network use index=%d, ip=%s, ifname=%s", index, addr->ip.c_str(), addr->ifname.c_str());
    }
    if (true) {
        SrsConfDirective* conf = get_stats_disk_device();
        if (conf == NULL || (int)conf->args.size() <= 0) {
            srs_warn("stats disk not configed, disk iops disabled.");
        } else {
            string disks;
            for (int i = 0; i < (int)conf->args.size(); i++) {
                disks += conf->args.at(i);
                disks += " ";
            }
            srs_warn("stats disk list: %s", disks.c_str());
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check http api
    ////////////////////////////////////////////////////////////////////////
    if (get_http_api_listen().empty()) {
        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "http_api.listen requires params");
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check http stream
    ////////////////////////////////////////////////////////////////////////
    if (get_http_stream_listen().empty()) {
        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "http_stream.listen requires params");
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check log name and level
    ////////////////////////////////////////////////////////////////////////
    if (true) {
        std::string log_filename = this->get_log_file();
        if (get_log_tank_file() && log_filename.empty()) {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "log file is empty");
        }
        if (get_log_tank_file()) {
            srs_trace("you can check log by: tail -n 30 -f %s", log_filename.c_str());
            srs_trace("please check SRS by: ./etc/init.d/srs status");
        } else {
            srs_trace("write log to console");
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check features
    ////////////////////////////////////////////////////////////////////////
    vector<SrsConfDirective*> stream_casters = get_stream_casters();
    for (int n = 0; n < (int)stream_casters.size(); n++) {
        SrsConfDirective* stream_caster = stream_casters[n];
        for (int i = 0; stream_caster && i < (int)stream_caster->directives.size(); i++) {
            SrsConfDirective* conf = stream_caster->at(i);
            string n = conf->name;
            if (n != "enabled" && n != "caster" && n != "output"
                && n != "listen" && n != "tcp_enable" && n != "rtp_port_min" && n != "rtp_port_max"
                && n != "rtp_idle_timeout" && n != "sip"
                && n != "audio_enable" && n != "wait_keyframe"
                && n != "host" && n != "auto_create_channel") {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal stream_caster.%s", n.c_str());
            }

            if (n == "sip") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled"  && m != "listen" && m != "ack_timeout" && m != "keepalive_timeout"
                        && m != "host" && m != "serial" && m != "realm" && m != "auto_play" && m != "invite_port_fixed"
                        && m != "query_catalog_interval") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal stream_caster.%s", m.c_str());
                    }
                }
            }
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check vhosts.
    ////////////////////////////////////////////////////////////////////////
    vector<SrsConfDirective*> vhosts;
    get_vhosts(vhosts);
    for (int n = 0; n < (int)vhosts.size(); n++) {
        SrsConfDirective* vhost = vhosts[n];

        for (int i = 0; vhost && i < (int)vhost->directives.size(); i++) {
            SrsConfDirective* conf = vhost->at(i);
            string n = conf->name;
            if (n != "enabled" && n != "chunk_size" && n != "min_latency" && n != "tcp_nodelay"
                && n != "dvr" && n != "ingest" && n != "hls" && n != "http_hooks"
                && n != "refer" && n != "forward" && n != "transcode" && n != "bandcheck"
                && n != "play" && n != "publish" && n != "cluster"
                && n != "security" && n != "http_remux" && n != "dash"
                && n != "http_static" && n != "hds" && n != "exec"
                && n != "in_ack_size" && n != "out_ack_size" && n != "rtc") {
                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.%s", n.c_str());
            }
            // for each sub directives of vhost.
            if (n == "dvr") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled"  && m != "dvr_apply" && m != "dvr_path" && m != "dvr_plan"
                        && m != "dvr_duration" && m != "dvr_wait_keyframe" && m != "time_jitter") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.dvr.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "refer") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "all" && m != "publish" && m != "play") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.refer.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "exec") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "publish") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.exec.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "play") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "time_jitter" && m != "mix_correct" && m != "atc" && m != "atc_auto" && m != "mw_latency"
                        && m != "gop_cache" && m != "queue_length" && m != "send_min_interval" && m != "reduce_sequence_header"
                        && m != "mw_msgs") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.play.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "cluster") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "mode" && m != "origin" && m != "token_traverse" && m != "vhost" && m != "debug_srs_upnode" && m != "coworkers"
                        && m != "origin_cluster" && m != "protocol" && m != "follow_client") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.cluster.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "publish") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "mr" && m != "mr_latency" && m != "firstpkt_timeout" && m != "normal_timeout" && m != "parse_sps") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.publish.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "ingest") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "input" && m != "ffmpeg" && m != "engine") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.ingest.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "http_static") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "mount" && m != "dir") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.http_static.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "http_remux") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "mount" && m != "fast_cache") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.http_remux.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "dash") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "dash_fragment" && m != "dash_update_period" && m != "dash_timeshift" && m != "dash_path"
                        && m != "dash_mpd_file") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.dash.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "hls") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "hls_entry_prefix" && m != "hls_path" && m != "hls_fragment" && m != "hls_window" && m != "hls_on_error"
                        && m != "hls_storage" && m != "hls_mount" && m != "hls_td_ratio" && m != "hls_aof_ratio" && m != "hls_acodec" && m != "hls_vcodec"
                        && m != "hls_m3u8_file" && m != "hls_ts_file" && m != "hls_ts_floor" && m != "hls_cleanup" && m != "hls_nb_notify"
                        && m != "hls_wait_keyframe" && m != "hls_dispose" && m != "hls_keys" && m != "hls_fragments_per_key" && m != "hls_key_file"
                        && m != "hls_key_file_path" && m != "hls_key_url" && m != "hls_dts_directly") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.hls.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                    
                    // TODO: FIXME: remove it in future.
                    if (m == "hls_storage" || m == "hls_mount") {
                        srs_warn("HLS RAM is removed in SRS3+, read https://github.com/ossrs/srs/issues/513.");
                    }
                }
            } else if (n == "http_hooks") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "on_connect" && m != "on_close" && m != "on_publish"
                        && m != "on_unpublish" && m != "on_play" && m != "on_stop"
                        && m != "on_dvr" && m != "on_hls" && m != "on_hls_notify") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.http_hooks.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "forward") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "destination" && m != "backend") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.forward.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "security") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    SrsConfDirective* security = conf->at(j);
                    string m = security->name.c_str();
                    if (m != "enabled" && m != "deny" && m != "allow") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.security.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "transcode") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    SrsConfDirective* trans = conf->at(j);
                    string m = trans->name.c_str();
                    if (m != "enabled" && m != "ffmpeg" && m != "engine") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.transcode.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                    if (m == "engine") {
                        for (int k = 0; k < (int)trans->directives.size(); k++) {
                            string e = trans->at(k)->name;
                            if (e != "enabled" && e != "vfilter" && e != "vcodec"
                                && e != "vbitrate" && e != "vfps" && e != "vwidth" && e != "vheight"
                                && e != "vthreads" && e != "vprofile" && e != "vpreset" && e != "vparams"
                                && e != "acodec" && e != "abitrate" && e != "asample_rate" && e != "achannels"
                                && e != "aparams" && e != "output" && e != "perfile"
                                && e != "iformat" && e != "oformat") {
                                return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.transcode.engine.%s of %s", e.c_str(), vhost->arg0().c_str());
                            }
                        }
                    }
                }
            } else if (n == "bandcheck") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "key" && m != "interval" && m != "limit_kbps") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.bandcheck.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            } else if (n == "rtc") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name;
                    if (m != "enabled" && m != "nack" && m != "twcc" && m != "nack_no_copy"
                        && m != "bframe" && m != "aac" && m != "stun_timeout" && m != "stun_strict_check"
                        && m != "dtls_role" && m != "dtls_version" && m != "drop_for_pt" && m != "rtc_to_rtmp"
                        && m != "pli_for_rtmp" && m != "rtmp_to_rtc" && m != "keep_bframe") {
                        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.rtc.%s of %s", m.c_str(), vhost->arg0().c_str());
                    }
                }
            }
        }
    }
    // check ingest id unique.
    for (int i = 0; i < (int)vhosts.size(); i++) {
        SrsConfDirective* vhost = vhosts[i];
        std::vector<std::string> ids;
        
        for (int j = 0; j < (int)vhost->directives.size(); j++) {
            SrsConfDirective* conf = vhost->at(j);
            if (conf->name != "ingest") {
                continue;
            }
            
            std::string id = conf->arg0();
            for (int k = 0; k < (int)ids.size(); k++) {
                if (id == ids.at(k)) {
                    return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "ingest id=%s exists for %s",
                        id.c_str(), vhost->arg0().c_str());
                }
            }
            ids.push_back(id);
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check chunk size
    ////////////////////////////////////////////////////////////////////////
    if (get_global_chunk_size() < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE
        || get_global_chunk_size() > SRS_CONSTS_RTMP_MAX_CHUNK_SIZE) {
        srs_warn("chunk_size=%s should be in [%d, %d]", get_global_chunk_size(),
            SRS_CONSTS_RTMP_MIN_CHUNK_SIZE, SRS_CONSTS_RTMP_MAX_CHUNK_SIZE);
    }
    for (int i = 0; i < (int)vhosts.size(); i++) {
        SrsConfDirective* vhost = vhosts[i];
        if (get_chunk_size(vhost->arg0()) < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE
            || get_chunk_size(vhost->arg0()) > SRS_CONSTS_RTMP_MAX_CHUNK_SIZE) {
            srs_warn("chunk_size=%s of %s should be in [%d, %d]", get_global_chunk_size(), vhost->arg0().c_str(),
                SRS_CONSTS_RTMP_MIN_CHUNK_SIZE, SRS_CONSTS_RTMP_MAX_CHUNK_SIZE);
        }
    }
    
    // asprocess conflict with daemon
    if (get_asprocess() && get_daemon()) {
        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "daemon conflicts with asprocess");
    }
    
    return err;
}

// LCOV_EXCL_START
srs_error_t SrsConfig::check_number_connections()
{
    srs_error_t err = srs_success;
    
    ////////////////////////////////////////////////////////////////////////
    // check max connections
    ////////////////////////////////////////////////////////////////////////
    if (get_max_connections() <= 0) {
        return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "max_connections=%d is invalid", get_max_connections());
    }
    
    // check max connections of system limits
    if (true) {
        int nb_consumed_fds = (int)get_listens().size();
        if (!get_http_api_listen().empty()) {
            nb_consumed_fds++;
        }
        if (!get_http_stream_listen().empty()) {
            nb_consumed_fds++;
        }
        if (get_log_tank_file()) {
            nb_consumed_fds++;
        }
        // 0, 1, 2 for stdin, stdout and stderr.
        nb_consumed_fds += 3;
        
        int nb_connections = get_max_connections();
        int nb_total = nb_connections + nb_consumed_fds;
        
        int max_open_files = (int)sysconf(_SC_OPEN_MAX);
        int nb_canbe = max_open_files - nb_consumed_fds - 1;
        
        // for each play connections, we open a pipe(2fds) to convert SrsConsumver to io,
        if (nb_total >= max_open_files) {
            srs_error("invalid max_connections=%d, required=%d, system limit to %d, "
                      "total=%d(max_connections=%d, nb_consumed_fds=%d). "
                      "you can change max_connections from %d to %d, or "
                      "you can login as root and set the limit: ulimit -HSn %d",
                      nb_connections, nb_total + 100, max_open_files,
                      nb_total, nb_connections, nb_consumed_fds,
                      nb_connections, nb_canbe, nb_total + 100);
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "%d exceed max open files=%d",
                nb_total, max_open_files);
        }
    }
    
    return err;
}
// LCOV_EXCL_STOP

srs_error_t SrsConfig::parse_buffer(SrsConfigBuffer* buffer)
{
    srs_error_t err = srs_success;

    // We use a new root to parse buffer, to allow parse multiple times.
    srs_freep(root);
    root = new SrsConfDirective();

    // Parse root tree from buffer.
    if ((err = root->parse(buffer, this)) != srs_success) {
        return srs_error_wrap(err, "root parse");
    }
    
    // mock by dolphin mode.
    // for the dolphin will start srs with specified params.
    if (dolphin) {
        // for RTMP.
        set_config_directive(root, "listen", dolphin_rtmp_port);
        
        // for HTTP
        set_config_directive(root, "http_server", "");
        SrsConfDirective* http_server = root->get("http_server");
        set_config_directive(http_server, "enabled", "on");
        set_config_directive(http_server, "listen", dolphin_http_port);
        
        // others.
        set_config_directive(root, "daemon", "off");
        set_config_directive(root, "srs_log_tank", "console");
    }
    
    return err;
}

string SrsConfig::cwd()
{
    return _cwd;
}

string SrsConfig::argv()
{
    return _argv;
}

bool SrsConfig::get_daemon()
{
    SrsConfDirective* conf = root->get("daemon");
    if (!conf || conf->arg0().empty()) {
        return true;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_in_docker()
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = root->get("in_docker");
    if (!conf) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::is_full_config()
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = root->get("is_full");
    if (!conf) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_root()
{
    return root;
}

int SrsConfig::get_max_connections()
{
    static int DEFAULT = 1000;
    
    SrsConfDirective* conf = root->get("max_connections");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

vector<string> SrsConfig::get_listens()
{
    std::vector<string> ports;
    
    SrsConfDirective* conf = root->get("listen");
    if (!conf) {
        return ports;
    }
    
    for (int i = 0; i < (int)conf->args.size(); i++) {
        ports.push_back(conf->args.at(i));
    }
    
    return ports;
}

string SrsConfig::get_pid_file()
{
    static string DEFAULT = "./objs/srs.pid";
    
    SrsConfDirective* conf = root->get("pid");
    
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

srs_utime_t SrsConfig::get_pithy_print()
{
    static srs_utime_t DEFAULT = 10 * SRS_UTIME_SECONDS;
    
    SrsConfDirective* conf = root->get("pithy_print_ms");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_MILLISECONDS);
}

bool SrsConfig::get_utc_time()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("utc_time");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_work_dir() {
    static string DEFAULT = "./";
    
    SrsConfDirective* conf = root->get("work_dir");
    if( !conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_asprocess()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("asprocess");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::whether_query_latest_version()
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = root->get("query_latest_version");
    if (!conf) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::empty_ip_ok()
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = root->get("empty_ip_ok");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

srs_utime_t SrsConfig::get_grace_start_wait()
{
    static srs_utime_t DEFAULT = 2300 * SRS_UTIME_MILLISECONDS;

    SrsConfDirective* conf = root->get("grace_start_wait");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return (srs_utime_t)(::atol(conf->arg0().c_str()) * SRS_UTIME_MILLISECONDS);
}

srs_utime_t SrsConfig::get_grace_final_wait()
{
    static srs_utime_t DEFAULT = 3200 * SRS_UTIME_MILLISECONDS;

    SrsConfDirective* conf = root->get("grace_final_wait");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return (srs_utime_t)(::atol(conf->arg0().c_str()) * SRS_UTIME_MILLISECONDS);
}

bool SrsConfig::is_force_grace_quit()
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = root->get("force_grace_quit");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::disable_daemon_for_docker()
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = root->get("disable_daemon_for_docker");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::inotify_auto_reload()
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = root->get("inotify_auto_reload");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::auto_reload_for_docker()
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = root->get("auto_reload_for_docker");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

// TODO: FIXME: Support reload.
double SrsConfig::tcmalloc_release_rate()
{
    static double DEFAULT = SRS_PERF_TCMALLOC_RELEASE_RATE;

    SrsConfDirective* conf = root->get("tcmalloc_release_rate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    double trr = ::atof(conf->arg0().c_str());
    trr = srs_min(10, trr);
    trr = srs_max(0, trr);
    return trr;
}

bool SrsConfig::get_circuit_breaker()
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = root->get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_high_threshold()
{
    static int DEFAULT = 90;

    SrsConfDirective* conf = root->get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("high_threshold");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_high_pulse()
{
    static int DEFAULT = 2;

    SrsConfDirective* conf = root->get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("high_pulse");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_critical_threshold()
{
    static int DEFAULT = 95;

    SrsConfDirective* conf = root->get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("critical_threshold");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_critical_pulse()
{
    static int DEFAULT = 1;

    SrsConfDirective* conf = root->get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("critical_pulse");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_dying_threshold()
{
    static int DEFAULT = 99;

    SrsConfDirective* conf = root->get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("dying_threshold");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_dying_pulse()
{
    static int DEFAULT = 5;

    SrsConfDirective* conf = root->get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("dying_pulse");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

vector<SrsConfDirective*> SrsConfig::get_stream_casters()
{
    srs_assert(root);
    
    std::vector<SrsConfDirective*> stream_casters;
    
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_stream_caster()) {
            continue;
        }
        
        stream_casters.push_back(conf);
    }
    
    return stream_casters;
}

bool SrsConfig::get_stream_caster_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_stream_caster_engine(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("caster");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_stream_caster_output(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("output");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_stream_caster_listen(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_stream_caster_tcp_enable(SrsConfDirective* conf)
{
	static bool DEFAULT = false;

	if (!conf) {
		return DEFAULT;
	}

	conf = conf->get("tcp_enable");
	if (!conf || conf->arg0().empty()) {
		return DEFAULT;
	}

	return SRS_CONF_PERFER_FALSE(conf->arg0());
}

int SrsConfig::get_stream_caster_rtp_port_min(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("rtp_port_min");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_stream_caster_rtp_port_max(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("rtp_port_max");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

srs_utime_t SrsConfig::get_stream_caster_gb28181_rtp_idle_timeout(SrsConfDirective* conf)
{
    static srs_utime_t DEFAULT = 30 * SRS_UTIME_SECONDS;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("rtp_idle_timeout");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

srs_utime_t SrsConfig::get_stream_caster_gb28181_ack_timeout(SrsConfDirective* conf)
{
    static srs_utime_t DEFAULT = 30 * SRS_UTIME_SECONDS;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("ack_timeout");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return  (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

srs_utime_t SrsConfig::get_stream_caster_gb28181_keepalive_timeout(SrsConfDirective* conf)
{
    static srs_utime_t DEFAULT = 120 * SRS_UTIME_SECONDS;


    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("keepalive_timeout");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

string SrsConfig::get_stream_caster_gb28181_host(SrsConfDirective* conf)
{
    static string DEFAULT = "*";

    conf = conf->get("host");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    string eip = srs_getenv(conf->arg0());
    if (!eip.empty()) {
        return eip;
    }

    // If configed as ENV, but no ENV set, use default value.
    if (srs_string_starts_with(conf->arg0(), "$")) {
        return DEFAULT;
    }

    return conf->arg0();
}

string SrsConfig::get_stream_caster_gb28181_serial(SrsConfDirective* conf)
{
    static string DEFAULT = "34020000002000000001";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("serial");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

string SrsConfig::get_stream_caster_gb28181_realm(SrsConfDirective* conf)
{
    static string DEFAULT = "3402000000";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("realm");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

bool SrsConfig::get_stream_caster_gb28181_audio_enable(SrsConfDirective* conf)
{
    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("audio_enable");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}


bool SrsConfig::get_stream_caster_gb28181_wait_keyframe(SrsConfDirective* conf)
{
    static bool DEFAULT = true;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("wait_keyframe");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_stream_caster_gb28181_sip_enable(SrsConfDirective* conf)
{
    static bool DEFAULT = true;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_stream_caster_gb28181_sip_auto_play(SrsConfDirective* conf)
{
    static bool DEFAULT = true;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("auto_play");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());

}

int SrsConfig::get_stream_caster_gb28181_sip_listen(SrsConfDirective* conf)
{
    static int DEFAULT = 5060;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());

}

bool SrsConfig::get_stream_caster_gb28181_sip_invite_port_fixed(SrsConfDirective* conf)
{
    static bool DEFAULT = true;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("invite_port_fixed");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());

}

bool SrsConfig::get_stream_caster_gb28181_auto_create_channel(SrsConfDirective* conf)
{
    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }
   
    conf = conf->get("auto_create_channel");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_stream_caster_gb28181_sip_query_catalog_interval(SrsConfDirective* conf)
{
    static srs_utime_t DEFAULT = 60 * SRS_UTIME_SECONDS;


    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("query_catalog_interval");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

bool SrsConfig::get_rtc_server_enabled()
{
    SrsConfDirective* conf = root->get("rtc_server");
    return get_rtc_server_enabled(conf);
}

bool SrsConfig::get_rtc_server_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

int SrsConfig::get_rtc_server_listen()
{
    static int DEFAULT = 8000;

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

std::string SrsConfig::get_rtc_server_candidates()
{
    static string DEFAULT = "*";

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("candidate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    string eip = srs_getenv(conf->arg0());
    if (!eip.empty()) {
        return eip;
    }

    // If configed as ENV, but no ENV set, use default value.
    if (srs_string_starts_with(conf->arg0(), "$")) {
        return DEFAULT;
    }

    return conf->arg0();
}

bool SrsConfig::get_api_as_candidates()
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("api_as_candidates");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

std::string SrsConfig::get_rtc_server_ip_family()
{
    static string DEFAULT = "ipv4";

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("ip_family");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

bool SrsConfig::get_rtc_server_ecdsa()
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("ecdsa");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_rtc_server_encrypt()
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("encrypt");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_rtc_server_reuseport()
{
    int v = get_rtc_server_reuseport2();

#if !defined(SO_REUSEPORT)
    if (v > 1) {
        srs_warn("REUSEPORT not supported, reset to 1");
        v = 1;
    }
#endif

    return v;
}

int SrsConfig::get_rtc_server_reuseport2()
{
    static int DEFAULT = 1;

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("reuseport");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_rtc_server_merge_nalus()
{
    static int DEFAULT = false;

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("merge_nalus");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_rtc_server_black_hole()
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("black_hole");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

std::string SrsConfig::get_rtc_server_black_hole_addr()
{
    static string DEFAULT = "";

    SrsConfDirective* conf = root->get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("black_hole");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("addr");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

SrsConfDirective* SrsConfig::get_rtc(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    return conf? conf->get("rtc") : NULL;
}

bool SrsConfig::get_rtc_enabled(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_rtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_rtc_keep_bframe(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_rtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("keep_bframe");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_rtc_from_rtmp(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_rtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("rtmp_to_rtc");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_rtc_stun_timeout(string vhost)
{
    static srs_utime_t DEFAULT = 30 * SRS_UTIME_SECONDS;

    SrsConfDirective* conf = get_rtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("stun_timeout");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

bool SrsConfig::get_rtc_stun_strict_check(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_rtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("stun_strict_check");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

std::string SrsConfig::get_rtc_dtls_role(string vhost)
{
    static std::string DEFAULT = "passive";

    SrsConfDirective* conf = get_rtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("dtls_role");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

std::string SrsConfig::get_rtc_dtls_version(string vhost)
{
    static std::string DEFAULT = "auto";

    SrsConfDirective* conf = get_rtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("dtls_version");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

int SrsConfig::get_rtc_drop_for_pt(string vhost)
{
    static int DEFAULT = 0;

    SrsConfDirective* conf = get_rtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("drop_for_pt");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_rtc_to_rtmp(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_rtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("rtc_to_rtmp");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_rtc_pli_for_rtmp(string vhost)
{
    static srs_utime_t DEFAULT = 6 * SRS_UTIME_SECONDS;

    SrsConfDirective* conf = get_rtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("pli_for_rtmp");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    srs_utime_t v = (srs_utime_t)(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
    if (v < 500 * SRS_UTIME_MILLISECONDS || v > 30 * SRS_UTIME_SECONDS) {
        srs_warn("Reset pli %dms to %dms", srsu2msi(v), srsu2msi(DEFAULT));
        return DEFAULT;
    }

    return v;
}

bool SrsConfig::get_rtc_nack_enabled(string vhost)
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = get_rtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("nack");
    if (!conf) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_rtc_nack_no_copy(string vhost)
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = get_rtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("nack_no_copy");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_rtc_twcc_enabled(string vhost)
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = get_rtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("twcc");
    if (!conf) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_vhost(string vhost, bool try_default_vhost)
{
    srs_assert(root);
    
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_vhost()) {
            continue;
        }
        
        if (conf->arg0() == vhost) {
            return conf;
        }
    }
    
    if (try_default_vhost && vhost != SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        return get_vhost(SRS_CONSTS_RTMP_DEFAULT_VHOST);
    }
    
    return NULL;
}

void SrsConfig::get_vhosts(vector<SrsConfDirective*>& vhosts)
{
    srs_assert(root);
    
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_vhost()) {
            continue;
        }
        
        vhosts.push_back(conf);
    }
}

bool SrsConfig::get_vhost_enabled(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    return get_vhost_enabled(conf);
}

bool SrsConfig::get_vhost_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = true;
    
    // false for NULL vhost.
    if (!conf) {
        return false;
    }
    
    // perfer true for exists one.
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_gop_cache(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_PERF_GOP_CACHE;
    }
    
    conf = conf->get("play");
    if (!conf) {
        return SRS_PERF_GOP_CACHE;
    }
    
    conf = conf->get("gop_cache");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_GOP_CACHE;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_debug_srs_upnode(string vhost)
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("cluster");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("debug_srs_upnode");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_atc(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("atc");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_atc_auto(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("atc_auto");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

int SrsConfig::get_time_jitter(string vhost)
{
    static string DEFAULT = "full";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return srs_time_jitter_string2int(DEFAULT);
    }
    
    conf = conf->get("play");
    if (!conf) {
        return srs_time_jitter_string2int(DEFAULT);
    }
    
    conf = conf->get("time_jitter");
    if (!conf || conf->arg0().empty()) {
        return srs_time_jitter_string2int(DEFAULT);
    }
    
    return srs_time_jitter_string2int(conf->arg0());
}

bool SrsConfig::get_mix_correct(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mix_correct");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_queue_length(string vhost)
{
    static srs_utime_t DEFAULT = SRS_PERF_PLAY_QUEUE;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("queue_length");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return srs_utime_t(::atoi(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

bool SrsConfig::get_refer_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("refer");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_refer_all(string vhost)
{
    static SrsConfDirective* DEFAULT = NULL;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("refer");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->get("all");
}

SrsConfDirective* SrsConfig::get_refer_play(string vhost)
{
    static SrsConfDirective* DEFAULT = NULL;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("refer");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->get("play");
}

SrsConfDirective* SrsConfig::get_refer_publish(string vhost)
{
    static SrsConfDirective* DEFAULT = NULL;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("refer");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->get("publish");
}

int SrsConfig::get_in_ack_size(string vhost)
{
    static int DEFAULT = 0;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("in_ack_size");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_out_ack_size(string vhost)
{
    static int DEFAULT = 2500000;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("out_ack_size");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_chunk_size(string vhost)
{
    if (vhost.empty()) {
        return get_global_chunk_size();
    }
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        // vhost does not specify the chunk size,
        // use the global instead.
        return get_global_chunk_size();
    }
    
    conf = conf->get("chunk_size");
    if (!conf || conf->arg0().empty()) {
        // vhost does not specify the chunk size,
        // use the global instead.
        return get_global_chunk_size();
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_parse_sps(string vhost)
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = get_vhost(vhost);
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("publish");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("parse_sps");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_mr_enabled(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_PERF_MR_ENABLED;
    }
    
    conf = conf->get("publish");
    if (!conf) {
        return SRS_PERF_MR_ENABLED;
    }
    
    conf = conf->get("mr");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_MR_ENABLED;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_mr_sleep(string vhost)
{
    static srs_utime_t DEFAULT = SRS_PERF_MR_SLEEP;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("publish");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mr_latency");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_MILLISECONDS);
}

srs_utime_t SrsConfig::get_mw_sleep(string vhost, bool is_rtc)
{
    static srs_utime_t SYS_DEFAULT = SRS_PERF_MW_SLEEP;
    static srs_utime_t RTC_DEFAULT = 0;

    srs_utime_t DEFAULT = is_rtc? RTC_DEFAULT : SYS_DEFAULT;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mw_latency");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    int v = ::atoi(conf->arg0().c_str());
    if (is_rtc && v > 0) {
        srs_warn("For RTC, we ignore mw_latency");
        return 0;
    }
    
    return (srs_utime_t)(v * SRS_UTIME_MILLISECONDS);
}

int SrsConfig::get_mw_msgs(string vhost, bool is_realtime, bool is_rtc)
{
    int DEFAULT = SRS_PERF_MW_MIN_MSGS;
    if (is_rtc) {
        DEFAULT = SRS_PERF_MW_MIN_MSGS_FOR_RTC;
    }
    if (is_realtime) {
        DEFAULT = SRS_PERF_MW_MIN_MSGS_REALTIME;
    }

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("play");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("mw_msgs");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    int v = ::atoi(conf->arg0().c_str());
    if (v > SRS_PERF_MW_MSGS) {
        srs_warn("reset mw_msgs %d to max %d", v, SRS_PERF_MW_MSGS);
        v = SRS_PERF_MW_MSGS;
    }

    return v;
}

bool SrsConfig::get_realtime_enabled(string vhost, bool is_rtc)
{
    static bool SYS_DEFAULT = SRS_PERF_MIN_LATENCY_ENABLED;
    static bool RTC_DEFAULT = true;

    bool DEFAULT = is_rtc? RTC_DEFAULT : SYS_DEFAULT;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("min_latency");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    if (is_rtc) {
        return SRS_CONF_PERFER_TRUE(conf->arg0());
    } else {
        return SRS_CONF_PERFER_FALSE(conf->arg0());
    }
}

bool SrsConfig::get_tcp_nodelay(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("tcp_nodelay");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_send_min_interval(string vhost)
{
    static srs_utime_t DEFAULT = 0;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("send_min_interval");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return srs_utime_t(::atof(conf->arg0().c_str()) * SRS_UTIME_MILLISECONDS);
}

bool SrsConfig::get_reduce_sequence_header(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("reduce_sequence_header");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_publish_1stpkt_timeout(string vhost)
{
    // when no msg recevied for publisher, use larger timeout.
    static srs_utime_t DEFAULT = 20 * SRS_UTIME_SECONDS;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("publish");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("firstpkt_timeout");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_MILLISECONDS);
}

srs_utime_t SrsConfig::get_publish_normal_timeout(string vhost)
{
    // the timeout for publish recv.
    // we must use more smaller timeout, for the recv never know the status
    // of underlayer socket.
    static srs_utime_t DEFAULT = 5 * SRS_UTIME_SECONDS;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("publish");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("normal_timeout");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_MILLISECONDS);
}

int SrsConfig::get_global_chunk_size()
{
    SrsConfDirective* conf = root->get("chunk_size");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONSTS_RTMP_SRS_CHUNK_SIZE;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_forward_enabled(string vhost) {
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return get_forward_enabled(conf);
}

bool SrsConfig::get_forward_enabled(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = vhost->get("forward");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_forwards(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("forward");
    if (!conf) {
        return NULL;
    }
    
    return conf->get("destination");
}

SrsConfDirective* SrsConfig::get_forward_backend(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("forward");
    if (!conf) {
        return NULL;
    }
    
    return conf->get("backend");
}

SrsConfDirective* SrsConfig::get_vhost_http_hooks(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("http_hooks");
}

bool SrsConfig::get_vhost_http_hooks_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return get_vhost_http_hooks_enabled(conf);
}

bool SrsConfig::get_vhost_http_hooks_enabled(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = vhost->get("http_hooks");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_vhost_on_connect(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_connect");
}

SrsConfDirective* SrsConfig::get_vhost_on_close(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_close");
}

SrsConfDirective* SrsConfig::get_vhost_on_publish(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_publish");
}

SrsConfDirective* SrsConfig::get_vhost_on_unpublish(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_unpublish");
}

SrsConfDirective* SrsConfig::get_vhost_on_play(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_play");
}

SrsConfDirective* SrsConfig::get_vhost_on_stop(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_stop");
}

SrsConfDirective* SrsConfig::get_vhost_on_dvr(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_dvr");
}

SrsConfDirective* SrsConfig::get_vhost_on_hls(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_hls");
}

SrsConfDirective* SrsConfig::get_vhost_on_hls_notify(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_hls_notify");
}

bool SrsConfig::get_bw_check_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("bandcheck");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_bw_check_key(string vhost)
{
    static string DEFAULT = "";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("bandcheck");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("key");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

srs_utime_t SrsConfig::get_bw_check_interval(string vhost)
{
    static int64_t DEFAULT = 30 * SRS_UTIME_SECONDS;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("bandcheck");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("interval");
    if (!conf) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

int SrsConfig::get_bw_check_limit_kbps(string vhost)
{
    static int DEFAULT = 1000;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("bandcheck");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("limit_kbps");
    if (!conf) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_vhost_is_edge(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    return get_vhost_is_edge(conf);
}

bool SrsConfig::get_vhost_is_edge(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = vhost;
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("cluster");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mode");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return "remote" == conf->arg0();
}

SrsConfDirective* SrsConfig::get_vhost_edge_origin(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("cluster");
    if (!conf) {
        return NULL;
    }
    
    return conf->get("origin");
}

string SrsConfig::get_vhost_edge_protocol(string vhost)
{
    static string DEFAULT = "rtmp";

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("cluster");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("protocol");
    if (!conf) {
        return DEFAULT;
    }

    return conf->arg0();
}

bool SrsConfig::get_vhost_edge_follow_client(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("cluster");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("follow_client");
    if (!conf) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_vhost_edge_token_traverse(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("cluster");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("token_traverse");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_vhost_edge_transform_vhost(string vhost)
{
    static string DEFAULT = "[vhost]";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("cluster");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vhost");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_vhost_origin_cluster(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    return get_vhost_origin_cluster(conf);
}

bool SrsConfig::get_vhost_origin_cluster(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = vhost;
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("cluster");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("origin_cluster");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

vector<string> SrsConfig::get_vhost_coworkers(string vhost)
{
    vector<string> coworkers;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return coworkers;
    }

    conf = conf->get("cluster");
    if (!conf) {
        return coworkers;
    }

    conf = conf->get("coworkers");
    for (int i = 0; i < (int)conf->args.size(); i++) {
        coworkers.push_back(conf->args.at(i));
    }

    return coworkers;
}

bool SrsConfig::get_security_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return get_security_enabled(conf);
}

bool SrsConfig::get_security_enabled(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = vhost->get("security");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_security_rules(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("security");
}

SrsConfDirective* SrsConfig::get_transcode(string vhost, string scope)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("transcode");
    if (!conf || conf->arg0() != scope) {
        return NULL;
    }
    
    return conf;
}

bool SrsConfig::get_transcode_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_transcode_ffmpeg(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("ffmpeg");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

vector<SrsConfDirective*> SrsConfig::get_transcode_engines(SrsConfDirective* conf)
{
    vector<SrsConfDirective*> engines;
    
    if (!conf) {
        return engines;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* engine = conf->directives[i];
        
        if (engine->name == "engine") {
            engines.push_back(engine);
        }
    }
    
    return engines;
}

bool SrsConfig::get_engine_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string srs_prefix_underscores_ifno(string name)
{
    if (srs_string_starts_with(name, "-")) {
        return name;
    } else {
        return "-" + name;
    }
}

vector<string> SrsConfig::get_engine_perfile(SrsConfDirective* conf)
{
    vector<string> perfile;
    
    if (!conf) {
        return perfile;
    }
    
    conf = conf->get("perfile");
    if (!conf) {
        return perfile;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* option = conf->directives[i];
        if (!option) {
            continue;
        }
        
        perfile.push_back(srs_prefix_underscores_ifno(option->name));
        if (!option->arg0().empty()) {
            perfile.push_back(option->arg0());
        }
    }
    
    return perfile;
}

string SrsConfig::get_engine_iformat(SrsConfDirective* conf)
{
    static string DEFAULT = "flv";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("iformat");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

vector<string> SrsConfig::get_engine_vfilter(SrsConfDirective* conf)
{
    vector<string> vfilter;
    
    if (!conf) {
        return vfilter;
    }
    
    conf = conf->get("vfilter");
    if (!conf) {
        return vfilter;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* filter = conf->directives[i];
        if (!filter) {
            continue;
        }
        
        vfilter.push_back(srs_prefix_underscores_ifno(filter->name));
        if (!filter->arg0().empty()) {
            vfilter.push_back(filter->arg0());
        }
    }
    
    return vfilter;
}

string SrsConfig::get_engine_vcodec(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vcodec");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_engine_vbitrate(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vbitrate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

double SrsConfig::get_engine_vfps(SrsConfDirective* conf)
{
    static double DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vfps");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

int SrsConfig::get_engine_vwidth(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vwidth");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_vheight(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vheight");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_vthreads(SrsConfDirective* conf)
{
    static int DEFAULT = 1;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vthreads");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

string SrsConfig::get_engine_vprofile(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vprofile");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_engine_vpreset(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vpreset");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

vector<string> SrsConfig::get_engine_vparams(SrsConfDirective* conf)
{
    vector<string> vparams;
    
    if (!conf) {
        return vparams;
    }
    
    conf = conf->get("vparams");
    if (!conf) {
        return vparams;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* filter = conf->directives[i];
        if (!filter) {
            continue;
        }
        
        vparams.push_back(srs_prefix_underscores_ifno(filter->name));
        if (!filter->arg0().empty()) {
            vparams.push_back(filter->arg0());
        }
    }
    
    return vparams;
}

string SrsConfig::get_engine_acodec(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("acodec");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_engine_abitrate(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("abitrate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_asample_rate(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("asample_rate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_achannels(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("achannels");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

vector<string> SrsConfig::get_engine_aparams(SrsConfDirective* conf)
{
    vector<string> aparams;
    
    if (!conf) {
        return aparams;
    }
    
    conf = conf->get("aparams");
    if (!conf) {
        return aparams;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* filter = conf->directives[i];
        if (!filter) {
            continue;
        }
        
        aparams.push_back(srs_prefix_underscores_ifno(filter->name));
        if (!filter->arg0().empty()) {
            aparams.push_back(filter->arg0());
        }
    }
    
    return aparams;
}

string SrsConfig::get_engine_oformat(SrsConfDirective* conf)
{
    static string DEFAULT = "flv";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("oformat");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_engine_output(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("output");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

SrsConfDirective* SrsConfig::get_exec(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("exec");
}

bool SrsConfig::get_exec_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return get_exec_enabled(conf);
}

bool SrsConfig::get_exec_enabled(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = vhost->get("exec");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

vector<SrsConfDirective*> SrsConfig::get_exec_publishs(string vhost)
{
    vector<SrsConfDirective*> eps;
    
    SrsConfDirective* conf = get_exec(vhost);
    if (!conf) {
        return eps;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* ep = conf->at(i);
        if (ep->name == "publish") {
            eps.push_back(ep);
        }
    }
    
    return eps;
}

vector<SrsConfDirective*> SrsConfig::get_ingesters(string vhost)
{
    vector<SrsConfDirective*> integers;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return integers;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* ingester = conf->directives[i];
        
        if (ingester->name == "ingest") {
            integers.push_back(ingester);
        }
    }
    
    return integers;
}

SrsConfDirective* SrsConfig::get_ingest_by_id(string vhost, string ingest_id)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("ingest", ingest_id);
}

bool SrsConfig::get_ingest_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_ingest_ffmpeg(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("ffmpeg");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_ingest_input_type(SrsConfDirective* conf)
{
    static string DEFAULT = "file";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("input");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("type");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_ingest_input_url(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("input");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("url");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

extern bool _srs_in_docker;

bool SrsConfig::get_log_tank_file()
{
    static bool DEFAULT = true;

    if (_srs_in_docker) {
        DEFAULT = false;
    }
    
    SrsConfDirective* conf = root->get("srs_log_tank");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0() != "console";
}

string SrsConfig::get_log_level()
{
    static string DEFAULT = "trace";
    
    SrsConfDirective* conf = root->get("srs_log_level");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_log_file()
{
    static string DEFAULT = "./objs/srs.log";
    
    SrsConfDirective* conf = root->get("srs_log_file");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_ff_log_enabled()
{
    string log = get_ff_log_dir();
    return log != SRS_CONSTS_NULL_FILE;
}

string SrsConfig::get_ff_log_dir()
{
    static string DEFAULT = "./objs";
    
    SrsConfDirective* conf = root->get("ff_log_dir");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_ff_log_level()
{
    static string DEFAULT = "info";

    SrsConfDirective* conf = root->get("ff_log_level");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

SrsConfDirective* SrsConfig::get_dash(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    return conf? conf->get("dash") : NULL;
}

bool SrsConfig::get_dash_enabled(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return get_dash_enabled(conf);
}

bool SrsConfig::get_dash_enabled(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = vhost->get("dash");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_dash_fragment(string vhost)
{
    static int DEFAULT = 30 * SRS_UTIME_SECONDS;
    
    SrsConfDirective* conf = get_dash(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dash_fragment");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

srs_utime_t SrsConfig::get_dash_update_period(string vhost)
{
    static srs_utime_t DEFAULT = 150 * SRS_UTIME_SECONDS;
    
    SrsConfDirective* conf = get_dash(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dash_update_period");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

srs_utime_t SrsConfig::get_dash_timeshift(string vhost)
{
    static srs_utime_t DEFAULT = 300 * SRS_UTIME_SECONDS;
    
    SrsConfDirective* conf = get_dash(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dash_timeshift");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

string SrsConfig::get_dash_path(string vhost)
{
    static string DEFAULT = "./objs/nginx/html";
    
    SrsConfDirective* conf = get_dash(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dash_path");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_dash_mpd_file(string vhost)
{
    static string DEFAULT = "[app]/[stream].mpd";
    
    SrsConfDirective* conf = get_dash(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dash_mpd_file");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

SrsConfDirective* SrsConfig::get_hls(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    return conf? conf->get("hls") : NULL;
}

bool SrsConfig::get_hls_enabled(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return get_hls_enabled(conf);
}

bool SrsConfig::get_hls_enabled(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = vhost->get("hls");
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_hls_entry_prefix(string vhost)
{
    static string DEFAULT = "";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_entry_prefix");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_path(string vhost)
{
    static string DEFAULT = "./objs/nginx/html";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_path");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_m3u8_file(string vhost)
{
    static string DEFAULT = "[app]/[stream].m3u8";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_m3u8_file");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_ts_file(string vhost)
{
    static string DEFAULT = "[app]/[stream]-[seq].ts";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_ts_file");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_hls_ts_floor(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_ts_floor");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_hls_fragment(string vhost)
{
    static srs_utime_t DEFAULT = 10 * SRS_UTIME_SECONDS;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_fragment");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return srs_utime_t(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

double SrsConfig::get_hls_td_ratio(string vhost)
{
    static double DEFAULT = 1.5;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_td_ratio");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

double SrsConfig::get_hls_aof_ratio(string vhost)
{
    static double DEFAULT = 2.0;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_aof_ratio");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

srs_utime_t SrsConfig::get_hls_window(string vhost)
{
    static srs_utime_t DEFAULT = (60 * SRS_UTIME_SECONDS);
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_window");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return srs_utime_t(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

string SrsConfig::get_hls_on_error(string vhost)
{
    // try to ignore the error.
    static string DEFAULT = "continue";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_on_error");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_acodec(string vhost)
{
    static string DEFAULT = "aac";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_acodec");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_vcodec(string vhost)
{
    static string DEFAULT = "h264";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_vcodec");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_vhost_hls_nb_notify(string vhost)
{
    static int DEFAULT = 64;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_nb_notify");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_vhost_hls_dts_directly(string vhost)
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("hls_dts_directly");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_hls_cleanup(string vhost)
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_cleanup");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

srs_utime_t SrsConfig::get_hls_dispose(string vhost)
{
    static srs_utime_t DEFAULT = 0;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_dispose");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

bool SrsConfig::get_hls_wait_keyframe(string vhost)
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_wait_keyframe");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_hls_keys(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_keys");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_hls_fragments_per_key(string vhost)
{
    static int DEFAULT = 5;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_fragments_per_key");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

string SrsConfig::get_hls_key_file(string vhost)
{
    static string DEFAULT = "[app]/[stream]-[seq].key";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_key_file");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_key_file_path(std::string vhost)
{
     //put the key in ts path defaultly.
    static string DEFAULT = get_hls_path(vhost);
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_key_file_path");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_key_url(std::string vhost)
{
     //put the key in ts path defaultly.
    static string DEFAULT = get_hls_path(vhost);
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_key_url");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

SrsConfDirective *SrsConfig::get_hds(const string &vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    if (!conf) {
        return NULL;
    }
    
    return conf->get("hds");
}

bool SrsConfig::get_hds_enabled(const string &vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return get_hds_enabled(conf);
}

bool SrsConfig::get_hds_enabled(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = vhost->get("hds");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_hds_path(const string &vhost)
{
    static string DEFAULT = "./objs/nginx/html";
    
    SrsConfDirective* conf = get_hds(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hds_path");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

srs_utime_t SrsConfig::get_hds_fragment(const string &vhost)
{
    static srs_utime_t DEFAULT = (10 * SRS_UTIME_SECONDS);
    
    SrsConfDirective* conf = get_hds(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hds_fragment");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return srs_utime_t(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

srs_utime_t SrsConfig::get_hds_window(const string &vhost)
{
    static srs_utime_t DEFAULT = (60 * SRS_UTIME_SECONDS);
    
    SrsConfDirective* conf = get_hds(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hds_window");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return srs_utime_t(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

SrsConfDirective* SrsConfig::get_dvr(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("dvr");
}

bool SrsConfig::get_dvr_enabled(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return get_dvr_enabled(conf);
}

bool SrsConfig::get_dvr_enabled(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = vhost->get("dvr");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_dvr_apply(string vhost)
{
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("dvr_apply");
    if (!conf || conf->arg0().empty()) {
        return NULL;
    }
    
    return conf;
    
}

string SrsConfig::get_dvr_path(string vhost)
{
    static string DEFAULT = "./objs/nginx/html/[app]/[stream].[timestamp].flv";
    
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dvr_path");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_dvr_plan(string vhost)
{
    static string DEFAULT = "session";
    
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dvr_plan");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

srs_utime_t SrsConfig::get_dvr_duration(string vhost)
{
    static srs_utime_t DEFAULT = 30 * SRS_UTIME_SECONDS;
    
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dvr_duration");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

bool SrsConfig::get_dvr_wait_keyframe(string vhost)
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dvr_wait_keyframe");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_dvr_time_jitter(string vhost)
{
    static string DEFAULT = "full";
    
    SrsConfDirective* conf = get_dvr(vhost);
    
    if (!conf) {
        return srs_time_jitter_string2int(DEFAULT);
    }
    
    conf = conf->get("time_jitter");
    if (!conf || conf->arg0().empty()) {
        return srs_time_jitter_string2int(DEFAULT);
    }
    
    return srs_time_jitter_string2int(conf->arg0());
}

bool SrsConfig::get_http_api_enabled()
{
    SrsConfDirective* conf = root->get("http_api");
    return get_http_api_enabled(conf);
}

bool SrsConfig::get_http_api_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_http_api_listen()
{
    static string DEFAULT = "1985";
    
    SrsConfDirective* conf = root->get("http_api");
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_http_api_crossdomain()
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = root->get("http_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("crossdomain");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_raw_api()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("http_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("raw_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_raw_api_allow_reload()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("http_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("raw_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("allow_reload");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_raw_api_allow_query()
{
    // Disable RAW API query, @see https://github.com/ossrs/srs/issues/2653#issuecomment-939389178
    return false;
}

bool SrsConfig::get_raw_api_allow_update()
{
    // Disable RAW API update, @see https://github.com/ossrs/srs/issues/2653#issuecomment-939389178
    return false;
}

SrsConfDirective* SrsConfig::get_https_api()
{
    SrsConfDirective* conf = root->get("http_api");
    if (!conf) {
        return NULL;
    }

    return conf->get("https");
}

bool SrsConfig::get_https_api_enabled()
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_https_api();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_https_api_listen()
{
    static string DEFAULT = "1990";

    SrsConfDirective* conf = get_https_api();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("listen");
    if (!conf) {
        return DEFAULT;
    }

    return conf->arg0();
}

string SrsConfig::get_https_api_ssl_key()
{
    static string DEFAULT = "./conf/server.key";

    SrsConfDirective* conf = get_https_api();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("key");
    if (!conf) {
        return DEFAULT;
    }

    return conf->arg0();
}

string SrsConfig::get_https_api_ssl_cert()
{
    static string DEFAULT = "./conf/server.crt";

    SrsConfDirective* conf = get_https_api();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("cert");
    if (!conf) {
        return DEFAULT;
    }

    return conf->arg0();
}

bool SrsConfig::get_srt_enabled()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

unsigned short SrsConfig::get_srt_listen_port()
{
    static unsigned short DEFAULT = 10080;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return (unsigned short)atoi(conf->arg0().c_str());
}

bool SrsConfig::get_srt_mix_correct() {
    static bool DEFAULT = true;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mix_correct");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_srto_maxbw() {
    static int64_t DEFAULT = -1;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("maxbw");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->arg0().c_str());
}

int SrsConfig::get_srto_mss() {
    static int DEFAULT = 1500;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mms");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->arg0().c_str());
}

int SrsConfig::get_srto_latency() {
    static int DEFAULT = 120;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("latency");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->arg0().c_str());
}

int SrsConfig::get_srto_recv_latency() {
    static int DEFAULT = 120;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("recvlatency");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->arg0().c_str());
}

int SrsConfig::get_srto_peer_latency() {
    static int DEFAULT = 0;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("peerlatency");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->arg0().c_str());
}

bool SrsConfig::get_srt_sei_filter() {
    static bool DEFAULT = true;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("sei_filter");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_srto_tlpkdrop() {
    static bool DEFAULT = true;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("tlpkdrop");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_srto_conntimeout() {
    static int DEFAULT = 3000;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("connect_timeout");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->arg0().c_str());
}

int SrsConfig::get_srto_sendbuf() {
    static int64_t DEFAULT = 8192 * (1500-28);
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("sendbuf");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->arg0().c_str());
}

int SrsConfig::get_srto_recvbuf() {
    static int64_t DEFAULT = 8192 * (1500-28);
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("recvbuf");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->arg0().c_str());
}

int SrsConfig::get_srto_payloadsize() {
    static int DEFAULT = 1316;
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("payloadsize");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->arg0().c_str());
}

string SrsConfig::get_default_app_name() {
    static string DEFAULT = "live";
    SrsConfDirective* conf = root->get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("default_app");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    return conf->arg0();
}

bool SrsConfig::get_http_stream_enabled()
{
    SrsConfDirective* conf = root->get("http_server");
    return get_http_stream_enabled(conf);
}

bool SrsConfig::get_http_stream_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_http_stream_listen()
{
    static string DEFAULT = "8080";
    
    SrsConfDirective* conf = root->get("http_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_http_stream_dir()
{
    static string DEFAULT = "./objs/nginx/html";
    
    SrsConfDirective* conf = root->get("http_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dir");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_http_stream_crossdomain()
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = root->get("http_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("crossdomain");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_https_stream()
{
    SrsConfDirective* conf = root->get("http_server");
    if (!conf) {
        return NULL;
    }

    return conf->get("https");
}

bool SrsConfig::get_https_stream_enabled()
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_https_stream();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_https_stream_listen()
{
    static string DEFAULT = "8088";

    SrsConfDirective* conf = get_https_stream();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("listen");
    if (!conf) {
        return DEFAULT;
    }

    return conf->arg0();
}

string SrsConfig::get_https_stream_ssl_key()
{
    static string DEFAULT = "./conf/server.key";

    SrsConfDirective* conf = get_https_stream();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("key");
    if (!conf) {
        return DEFAULT;
    }

    return conf->arg0();
}

string SrsConfig::get_https_stream_ssl_cert()
{
    static string DEFAULT = "./conf/server.crt";

    SrsConfDirective* conf = get_https_stream();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("cert");
    if (!conf) {
        return DEFAULT;
    }

    return conf->arg0();
}

bool SrsConfig::get_vhost_http_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_static");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_vhost_http_mount(string vhost)
{
    static string DEFAULT = "[vhost]/";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_static");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mount");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_vhost_http_dir(string vhost)
{
    static string DEFAULT = "./objs/nginx/html";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_static");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dir");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_vhost_http_remux_enabled(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return get_vhost_http_remux_enabled(conf);
}

bool SrsConfig::get_vhost_http_remux_enabled(SrsConfDirective* vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = vhost->get("http_remux");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_vhost_http_remux_fast_cache(string vhost)
{
    static srs_utime_t DEFAULT = 0;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("fast_cache");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return srs_utime_t(::atof(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

string SrsConfig::get_vhost_http_remux_mount(string vhost)
{
    static string DEFAULT = "[vhost]/[app]/[stream].flv";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mount");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

SrsConfDirective* SrsConfig::get_heartbeart()
{
    return root->get("heartbeat");
}

bool SrsConfig::get_heartbeat_enabled()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

srs_utime_t SrsConfig::get_heartbeat_interval()
{
    static srs_utime_t DEFAULT = (srs_utime_t)(10 * SRS_UTIME_SECONDS);
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("interval");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (srs_utime_t)(::atoi(conf->arg0().c_str()) * SRS_UTIME_SECONDS);
}

string SrsConfig::get_heartbeat_url()
{
    static string DEFAULT = "http://" SRS_CONSTS_LOCALHOST ":8085/api/v1/servers";
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("url");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_heartbeat_device_id()
{
    static string DEFAULT = "";
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("device_id");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_heartbeat_summaries()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("summaries");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_stats()
{
    return root->get("stats");
}

bool SrsConfig::get_stats_enabled()
{
    static bool DEFAULT = true;

    SrsConfDirective* conf = get_stats();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_stats_network()
{
    static int DEFAULT = 0;
    
    SrsConfDirective* conf = get_stats();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("network");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

SrsConfDirective* SrsConfig::get_stats_disk_device()
{
    SrsConfDirective* conf = get_stats();
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("disk");
    if (!conf || conf->args.size() == 0) {
        return NULL;
    }
    
    return conf;
}
