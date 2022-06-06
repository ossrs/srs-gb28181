//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_async_call.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

ISrsAsyncCallTask::ISrsAsyncCallTask()
{
}

ISrsAsyncCallTask::~ISrsAsyncCallTask()
{
}

SrsAsyncCallWorker::SrsAsyncCallWorker()
{
    trd = new SrsDummyCoroutine();
    wait = srs_cond_new();
    lock = srs_mutex_new();
}

SrsAsyncCallWorker::~SrsAsyncCallWorker()
{
    srs_freep(trd);
    
    std::vector<ISrsAsyncCallTask*>::iterator it;
    for (it = tasks.begin(); it != tasks.end(); ++it) {
        ISrsAsyncCallTask* task = *it;
        srs_freep(task);
    }
    tasks.clear();
    
    srs_cond_destroy(wait);
    srs_mutex_destroy(lock);
}

srs_error_t SrsAsyncCallWorker::execute(ISrsAsyncCallTask* t)
{
    srs_error_t err = srs_success;
    
    tasks.push_back(t);
    srs_cond_signal(wait);
    
    return err;
}

int SrsAsyncCallWorker::count()
{
    return (int)tasks.size();
}

srs_error_t SrsAsyncCallWorker::start()
{
    srs_error_t err = srs_success;
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("async", this, _srs_context->get_id());
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }
    
    return err;
}

void SrsAsyncCallWorker::stop()
{
    flush_tasks();
    srs_cond_signal(wait);
    trd->stop();
}

srs_error_t SrsAsyncCallWorker::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "async call worker");
        }
        
        if (tasks.empty()) {
            srs_cond_wait(wait);
        }

        flush_tasks();
    }
    
    return err;
}

void SrsAsyncCallWorker::flush_tasks()
{
    srs_error_t err = srs_success;

    // Avoid the async call blocking other coroutines.
    std::vector<ISrsAsyncCallTask*> copy;
    if (true) {
        SrsLocker(lock);

        if (tasks.empty()) {
            return;
        }

        copy = tasks;
        tasks.clear();
    }

    std::vector<ISrsAsyncCallTask*>::iterator it;
    for (it = copy.begin(); it != copy.end(); ++it) {
        ISrsAsyncCallTask* task = *it;

        if ((err = task->call()) != srs_success) {
            srs_warn("ignore task failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(task);
    }
}


