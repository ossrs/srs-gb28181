//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_file.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#include <sys/uio.h>
#endif

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>

// For utest to mock it.
srs_open_t _srs_open_fn = ::open;
srs_write_t _srs_write_fn = ::write;
srs_read_t _srs_read_fn = ::read;
srs_lseek_t _srs_lseek_fn = ::lseek;
srs_close_t _srs_close_fn = ::close;

SrsFileWriter::SrsFileWriter()
{
    fd = -1;
}

SrsFileWriter::~SrsFileWriter()
{
    close();
}

srs_error_t SrsFileWriter::open(string p)
{
    srs_error_t err = srs_success;
    
    if (fd > 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_ALREADY_OPENED, "file %s already opened", p.c_str());
    }
    
    int flags = O_CREAT|O_WRONLY|O_TRUNC;
    mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
    
    if ((fd = _srs_open_fn(p.c_str(), flags, mode)) < 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_OPENE, "open file %s failed", p.c_str());
    }
    
    path = p;
    
    return err;
}

srs_error_t SrsFileWriter::open_append(string p)
{
    srs_error_t err = srs_success;
    
    if (fd > 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_ALREADY_OPENED, "file %s already opened", path.c_str());
    }
    
    int flags = O_CREAT|O_APPEND|O_WRONLY;
    mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
    
    if ((fd = _srs_open_fn(p.c_str(), flags, mode)) < 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_OPENE, "open file %s failed", p.c_str());
    }
    
    path = p;
    
    return err;
}

void SrsFileWriter::close()
{
    if (fd < 0) {
        return;
    }
    
    if (_srs_close_fn(fd) < 0) {
        srs_warn("close file %s failed", path.c_str());
    }
    fd = -1;
    
    return;
}

bool SrsFileWriter::is_open()
{
    return fd > 0;
}

void SrsFileWriter::seek2(int64_t offset)
{
    off_t r0 = _srs_lseek_fn(fd, (off_t)offset, SEEK_SET);
    srs_assert(r0 != -1);
}

int64_t SrsFileWriter::tellg()
{
    return (int64_t)_srs_lseek_fn(fd, 0, SEEK_CUR);
}

srs_error_t SrsFileWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;
    
    ssize_t nwrite;
    // TODO: FIXME: use st_write.
#ifdef _WIN32
    if ((nwrite = ::_write(fd, buf, (unsigned int)count)) < 0) {
#else
    if ((nwrite = _srs_write_fn(fd, buf, count)) < 0) {
#endif
        return srs_error_new(ERROR_SYSTEM_FILE_WRITE, "write to file %s failed", path.c_str());
    }
    
    if (pnwrite != NULL) {
        *pnwrite = nwrite;
    }
    
    return err;
}

srs_error_t SrsFileWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;
    
    ssize_t nwrite = 0;
    for (int i = 0; i < iovcnt; i++) {
        const iovec* piov = iov + i;
        ssize_t this_nwrite = 0;
        if ((err = write(piov->iov_base, piov->iov_len, &this_nwrite)) != srs_success) {
            return srs_error_wrap(err, "write file");
        }
        nwrite += this_nwrite;
    }
    
    if (pnwrite) {
        *pnwrite = nwrite;
    }
    
    return err;
}

srs_error_t SrsFileWriter::lseek(off_t offset, int whence, off_t* seeked)
{
    off_t sk = _srs_lseek_fn(fd, offset, whence);
    if (sk < 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_SEEK, "seek file");
    }
    
    if (seeked) {
        *seeked = sk;
    }
    
    return srs_success;
}

ISrsFileReaderFactory::ISrsFileReaderFactory()
{
}

ISrsFileReaderFactory::~ISrsFileReaderFactory()
{
}

SrsFileReader* ISrsFileReaderFactory::create_file_reader()
{
    return new SrsFileReader();
}

SrsFileReader::SrsFileReader()
{
    fd = -1;
}

SrsFileReader::~SrsFileReader()
{
    close();
}

srs_error_t SrsFileReader::open(string p)
{
    srs_error_t err = srs_success;
    
    if (fd > 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_ALREADY_OPENED, "file %s already opened", path.c_str());
    }
    
    if ((fd = _srs_open_fn(p.c_str(), O_RDONLY)) < 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_OPENE, "open file %s failed", p.c_str());
    }
    
    path = p;
    
    return err;
}

void SrsFileReader::close()
{
    int ret = ERROR_SUCCESS;
    
    if (fd < 0) {
        return;
    }
    
    if (_srs_close_fn(fd) < 0) {
        srs_warn("close file %s failed. ret=%d", path.c_str(), ret);
    }
    fd = -1;
    
    return;
}

bool SrsFileReader::is_open()
{
    return fd > 0;
}

int64_t SrsFileReader::tellg()
{
    return (int64_t)_srs_lseek_fn(fd, 0, SEEK_CUR);
}

void SrsFileReader::skip(int64_t size)
{
    off_t r0 = _srs_lseek_fn(fd, (off_t)size, SEEK_CUR);
    srs_assert(r0 != -1);
}

int64_t SrsFileReader::seek2(int64_t offset)
{
    return (int64_t)_srs_lseek_fn(fd, (off_t)offset, SEEK_SET);
}

int64_t SrsFileReader::filesize()
{
    int64_t cur = tellg();
    int64_t size = (int64_t)_srs_lseek_fn(fd, 0, SEEK_END);
    
    off_t r0 = _srs_lseek_fn(fd, (off_t)cur, SEEK_SET);
    srs_assert(r0 != -1);
    
    return size;
}

srs_error_t SrsFileReader::read(void* buf, size_t count, ssize_t* pnread)
{
    srs_error_t err = srs_success;
    
    ssize_t nread;
    // TODO: FIXME: use st_read.
#ifdef _WIN32
    if ((nread = _read(fd, buf, (unsigned int)count)) < 0) {
#else
    if ((nread = _srs_read_fn(fd, buf, count)) < 0) {
#endif
        return srs_error_new(ERROR_SYSTEM_FILE_READ, "read from file %s failed", path.c_str());
    }
    
    if (nread == 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_EOF, "file EOF");
    }
    
    if (pnread != NULL) {
        *pnread = nread;
    }
    
    return err;
}

srs_error_t SrsFileReader::lseek(off_t offset, int whence, off_t* seeked)
{
    off_t sk = _srs_lseek_fn(fd, offset, whence);
    if (sk < 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_SEEK, "seek %d failed", (int)sk);
    }
    
    if (seeked) {
        *seeked = sk;
    }
    
    return srs_success;
}

