//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_SERVICE_CONN_HPP
#define SRS_SERVICE_CONN_HPP

#include <srs_core.hpp>

#include <string>

// The resource managed by ISrsResourceManager.
class ISrsResource
{
public:
    ISrsResource();
    virtual ~ISrsResource();
public:
    // Get the context id of connection.
    virtual const SrsContextId& get_id() = 0;
    // The resource description, optional.
    virtual std::string desc() = 0;
};

// The manager for resource.
class ISrsResourceManager
{
public:
    ISrsResourceManager();
    virtual ~ISrsResourceManager();
public:
    // Remove then free the specified connection.
    virtual void remove(ISrsResource* c) = 0;
};

// The connection interface for all HTTP/RTMP/RTSP object.
class ISrsConnection : public ISrsResource
{
public:
    ISrsConnection();
    virtual ~ISrsConnection();
public:
    // Get remote ip address.
    virtual std::string remote_ip() = 0;
};

#endif

