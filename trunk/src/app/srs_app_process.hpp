//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_PROCESS_HPP
#define SRS_APP_PROCESS_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

// Start and stop a process. Call cycle to restart the process when terminated.
// The usage:
//      // the binary is the process to fork.
//      binary = "./objs/ffmpeg/bin/ffmpeg";
//      // where argv is a array contains each params.
//      argv = ["./objs/ffmpeg/bin/ffmpeg", "-i", "in.flv", "1", ">", "/dev/null", "2", ">", "/dev/null"];
//
//      process = new SrsProcess();
//      if ((ret = process->initialize(binary, argv)) != ERROR_SUCCESS) { return ret; }
//      if ((ret = process->start()) != ERROR_SUCCESS) { return ret; }
//      if ((ret = process->cycle()) != ERROR_SUCCESS) { return ret; }
//      process->fast_stop();
//      process->stop();
class SrsProcess
{
private:
    bool is_started;
    // Whether SIGTERM send but need to wait or SIGKILL.
    bool fast_stopped;
    pid_t pid;
private:
    std::string bin;
    std::string stdout_file;
    std::string stderr_file;
    std::vector<std::string> params;
    // The cli to fork process.
    std::string cli;
    std::string actual_cli;
public:
    SrsProcess();
    virtual ~SrsProcess();
public:
    // Get pid of process.
    virtual int get_pid();
    // whether process is already started.
    virtual bool started();
    // Initialize the process with binary and argv.
    // @param binary the binary path to exec.
    // @param argv the argv for binary path, the argv[0] generally is the binary.
    // @remark the argv[0] must be the binary.
    virtual srs_error_t initialize(std::string binary, std::vector<std::string> argv);
private:
    // Redirect standard I/O.
    virtual srs_error_t redirect_io();
public:
    // Start the process, ignore when already started.
    virtual srs_error_t start();
    // cycle check the process, update the state of process.
    // @remark when process terminated(not started), user can restart it again by start().
    virtual srs_error_t cycle();
    // Send SIGTERM then SIGKILL to ensure the process stopped.
    // the stop will wait [0, SRS_PROCESS_QUIT_TIMEOUT_MS] depends on the
    // process quit timeout.
    // @remark use fast_stop before stop one by one, when got lots of process to quit.
    virtual void stop();
public:
    // The fast stop is to send a SIGTERM.
    // for example, the ingesters owner lots of FFMPEG, it will take a long time
    // to stop one by one, instead the ingesters can fast_stop all FFMPEG, then
    // wait one by one to stop, it's more faster.
    // @remark user must use stop() to ensure the ffmpeg to stopped.
    // @remark we got N processes to stop, compare the time we spend,
    //      when use stop without fast_stop, we spend maybe [0, SRS_PROCESS_QUIT_TIMEOUT_MS * N]
    //      but use fast_stop then stop, the time is almost [0, SRS_PROCESS_QUIT_TIMEOUT_MS].
    virtual void fast_stop();
    // Directly kill process, never use it except server quiting.
    virtual void fast_kill();
};

#endif

