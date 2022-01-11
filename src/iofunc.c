/*
 *  Hamlib Interface - generic file based io functions
 *  Copyright (c) 2021-2022 by Mikael Nousiainen
 *  Copyright (c) 2000-2012 by Stephane Fillod
 *  Copyright (c) 2000-2003 by Frank Singleton
 *
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 * \file iofunc.c
 * \brief Generic file-based IO functions
 */

/**
 * \addtogroup rig_internal
 * @{
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <hamlib/rig.h>
#include "iofunc.h"
#include "misc.h"

#include "serial.h"
#include "parallel.h"
#include "usb_port.h"
#include "network.h"
#include "cm108.h"
#include "gpio.h"
#include "asyncpipe.h"

#ifdef ASYNC_BUG

#if defined(WIN32) && defined(HAVE_WINDOWS_H)
#include <windows.h>

static void init_sync_data_pipe(hamlib_port_t *p)
{
    p->sync_data_pipe = NULL;
    p->sync_data_error_pipe = NULL;
}

static void close_sync_data_pipe(hamlib_port_t *p)
{
    ENTERFUNC;

    if (p->sync_data_pipe != NULL)
    {
        async_pipe_close(p->sync_data_pipe);
        p->sync_data_pipe = NULL;
    }

    if (p->sync_data_error_pipe != NULL)
    {
        async_pipe_close(p->sync_data_error_pipe);
        p->sync_data_error_pipe = NULL;
    }
}

static int create_sync_data_pipe(hamlib_port_t *p)
{
    int status;

    ENTERFUNC;

    status = async_pipe_create(&p->sync_data_pipe, PIPE_BUFFER_SIZE_DEFAULT, p->timeout);
    if (status < 0)
    {
        close_sync_data_pipe(p);
        RETURNFUNC(-RIG_EINTERNAL);
    }

    status = async_pipe_create(&p->sync_data_error_pipe, PIPE_BUFFER_SIZE_DEFAULT, p->timeout);
    if (status < 0)
    {
        close_sync_data_pipe(p);
        RETURNFUNC(-RIG_EINTERNAL);
    }

    rig_debug(RIG_DEBUG_VERBOSE, "%s: created data pipe for synchronous transactions\n", __func__);

    RETURNFUNC(RIG_OK);
}

#else

static void init_sync_data_pipe(hamlib_port_t *p)
{
    p->fd_sync_write = -1;
    p->fd_sync_read = -1;
    p->fd_sync_error_write = -1;
    p->fd_sync_error_read = -1;
}

static void close_sync_data_pipe(hamlib_port_t *p)
{
    if (p->fd_sync_read != -1) {
        close(p->fd_sync_read);
        p->fd_sync_read = -1;
    }
    if (p->fd_sync_write != -1) {
        close(p->fd_sync_write);
        p->fd_sync_write = -1;
    }


    if (p->fd_sync_error_read != -1) {
        close(p->fd_sync_error_read);
        p->fd_sync_error_read = -1;
    }
    if (p->fd_sync_error_write != -1) {
        close(p->fd_sync_error_write);
        p->fd_sync_error_write = -1;
    }
}

static int create_sync_data_pipe(hamlib_port_t *p)
{
    int status;
    int sync_pipe_fds[2];
    int flags;

    status = pipe(sync_pipe_fds);
    flags = fcntl(sync_pipe_fds[0], F_GETFL);
    flags |= O_NONBLOCK;
    if (fcntl(sync_pipe_fds[0], F_SETFL, flags))
    {
        rig_debug(RIG_DEBUG_ERR, "%s: error setting O_NONBLOCK on sync_read=%s\n", __func__, strerror(errno));
    }
    flags = fcntl(sync_pipe_fds[1], F_GETFL);
    flags |= O_NONBLOCK;
    if (fcntl(sync_pipe_fds[1], F_SETFL, flags))
    {
        rig_debug(RIG_DEBUG_ERR, "%s: error setting O_NONBLOCK on sync_write=%s\n", __func__, strerror(errno));
    }
    if (status != 0)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: synchronous data pipe open status=%d, err=%s\n", __func__,
                status, strerror(errno));
        close_sync_data_pipe(p);
        RETURNFUNC(-RIG_EINTERNAL);
    }

    p->fd_sync_read = sync_pipe_fds[0];
    p->fd_sync_write = sync_pipe_fds[1];

    status = pipe(sync_pipe_fds);
    flags = fcntl(sync_pipe_fds[0], F_GETFL);
    flags |= O_NONBLOCK;
    if (fcntl(sync_pipe_fds[0], F_SETFL, flags))
    {
        rig_debug(RIG_DEBUG_ERR, "%s: error setting O_NONBLOCK on error_read=%s\n", __func__, strerror(errno));
    }
    flags = fcntl(sync_pipe_fds[1], F_GETFL);
    flags |= O_NONBLOCK;
    if (fcntl(sync_pipe_fds[1], F_SETFL, flags))
    {
        rig_debug(RIG_DEBUG_ERR, "%s: error setting O_NONBLOCK on error_write=%s\n", __func__, strerror(errno));
    }
    if (status != 0)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: synchronous data error code pipe open status=%d, err=%s\n", __func__,
                status, strerror(errno));
        close_sync_data_pipe(p);
        RETURNFUNC(-RIG_EINTERNAL);
    }

    p->fd_sync_error_read = sync_pipe_fds[0];
    p->fd_sync_error_write = sync_pipe_fds[1];

    rig_debug(RIG_DEBUG_VERBOSE, "%s: created data pipe for synchronous transactions\n", __func__);

    RETURNFUNC(RIG_OK);
}

#endif
#endif

/**
 * \brief Open a hamlib_port based on its rig port type
 * \param p rig port descriptor
 * \return status
 */
int HAMLIB_API port_open(hamlib_port_t *p)
{
    int status;
    int want_state_delay = 0;

    ENTERFUNC;

    p->fd = -1;
#ifdef ASYNC_BUG
    init_sync_data_pipe(p);
#endif

#ifdef ASYNC_BUG
    if (p->async)
    {
        status = create_sync_data_pipe(p);
        if (status < 0)
        {
            RETURNFUNC(status);
        }
    }
#endif

    switch (p->type.rig)
    {
    case RIG_PORT_SERIAL:
        status = serial_open(p);

        if (status < 0)
        {
            rig_debug(RIG_DEBUG_ERR, "%s: serial_open(%s) status=%d, err=%s\n", __func__,
                      p->pathname, status, strerror(errno));
#ifdef ASYNC_BUG
            close_sync_data_pipe(p);
#endif
            RETURNFUNC(status);
        }

        if (p->parm.serial.rts_state != RIG_SIGNAL_UNSET
                && p->parm.serial.handshake != RIG_HANDSHAKE_HARDWARE)
        {
            status = ser_set_rts(p,
                                 p->parm.serial.rts_state == RIG_SIGNAL_ON);
            want_state_delay = 1;
        }

        if (status != 0)
        {
#ifdef ASYNC_BUG
            close_sync_data_pipe(p);
#endif
            RETURNFUNC(status);
        }

        if (p->parm.serial.dtr_state != RIG_SIGNAL_UNSET)
        {
            status = ser_set_dtr(p,
                                 p->parm.serial.dtr_state == RIG_SIGNAL_ON);
            want_state_delay = 1;
        }

        if (status != 0)
        {
            rig_debug(RIG_DEBUG_ERR, "%s: set_dtr status=%d\n", __func__, status);
#ifdef ASYNC_BUG
            close_sync_data_pipe(p);
#endif
            RETURNFUNC(status);
        }

        /*
         * Wait whatever electrolytics in the circuit come up to voltage.
         * Is 100ms enough? Too much?
         */
        if (want_state_delay)
        {
            hl_usleep(100 * 1000);
        }

        break;

    case RIG_PORT_PARALLEL:
        status = par_open(p);

        if (status < 0)
        {
#ifdef ASYNC_BUG
            close_sync_data_pipe(p);
#endif
            RETURNFUNC(status);
        }

        break;

    case RIG_PORT_CM108:
        status = cm108_open(p);

        if (status < 0)
        {
#ifdef ASYNC_BUG
            close_sync_data_pipe(p);
#endif
            RETURNFUNC(status);
        }

        break;

    case RIG_PORT_DEVICE:
        status = open(p->pathname, O_RDWR, 0);

        if (status < 0)
        {
#ifdef ASYNC_BUG
            close_sync_data_pipe(p);
#endif
            RETURNFUNC(-RIG_EIO);
        }

        p->fd = status;
        break;

#if defined(HAVE_LIBUSB_H) || defined (HAVE_LIBUSB_1_0_LIBUSB_H)

    case RIG_PORT_USB:
        status = usb_port_open(p);

        if (status < 0)
        {
#ifdef ASYNC_BUG
            close_sync_data_pipe(p);
#endif
            RETURNFUNC(status);
        }

        break;
#endif

    case RIG_PORT_NONE:
    case RIG_PORT_RPC:
        break;  /* ez :) */

    case RIG_PORT_NETWORK:
    case RIG_PORT_UDP_NETWORK:
        /* FIXME: hardcoded network port */
        status = network_open(p, 4532);

        if (status < 0)
        {
#ifdef ASYNC_BUG
            close_sync_data_pipe(p);
#endif
            RETURNFUNC(status);
        }

        break;

    default:
#ifdef ASYNC_BUG
        close_sync_data_pipe(p);
#endif
        RETURNFUNC(-RIG_EINVAL);
    }

    RETURNFUNC(RIG_OK);
}


/**
 * \brief Close a hamlib_port
 * \param p rig port descriptor
 * \param port_type equivalent rig port type
 * \return status
 */
int HAMLIB_API port_close(hamlib_port_t *p, rig_port_t port_type)
{
    int ret = RIG_OK;

    ENTERFUNC;

    if (p->fd != -1)
    {
        switch (port_type)
        {
        case RIG_PORT_SERIAL:
            ret = ser_close(p);
            break;

#if defined(HAVE_LIBUSB_H) || defined (HAVE_LIBUSB_1_0_LIBUSB_H)

        case RIG_PORT_USB:
            ret = usb_port_close(p);
            break;
#endif

        case RIG_PORT_NETWORK:
        case RIG_PORT_UDP_NETWORK:
            ret = network_close(p);
            break;

        default:
            rig_debug(RIG_DEBUG_ERR, "%s(): Unknown port type %d\n",
                      __func__, port_type);

        /* fall through */
        case RIG_PORT_DEVICE:
            ret = close(p->fd);
        }

        p->fd = -1;
    }

#ifdef ASYNC_BUG
    close_sync_data_pipe(p);
#endif

    RETURNFUNC(ret);
}


#if defined(WIN32) && !defined(HAVE_TERMIOS_H)
#  include "win32termios.h"

extern int is_uh_radio_fd(int fd);

#ifdef ASYNC_BUG
static int port_read_sync_data_error_code(hamlib_port_t *p)
{
    ssize_t total_bytes_read = 0;
    unsigned char data;
    int result;

    do {
        // Wait for data using a zero-length read
        result = async_pipe_read(p->sync_data_error_pipe, &data, 0, p->timeout);
        if (result < 0)
        {
            if (result == -RIG_ETIMEOUT)
            {
                if (total_bytes_read > 0)
                {
                    return data;
                }
            }

            return result;
        }

        result = async_pipe_read(p->sync_data_error_pipe, &data, 1, p->timeout);
        if (result < 0)
        {
            if (result == -RIG_ETIMEOUT)
            {
                if (total_bytes_read > 0)
                {
                    return data;
                }
            }

            return result;
        }

        total_bytes_read += result;
    } while (result > 0);

    return data;
}

static int port_read_sync_data(hamlib_port_t *p, void *buf, size_t count)
{
    // Wait for data in both the response data pipe and the error code pipe to detect errors occurred during read
    HANDLE event_handles[2] = {
        p->sync_data_pipe->read_overlapped.hEvent,
        p->sync_data_error_pipe->read_overlapped.hEvent,
    };
    HANDLE read_handle = p->sync_data_pipe->read;
    LPOVERLAPPED overlapped = &p->sync_data_pipe->read_overlapped;
    DWORD wait_result;
    int result;
    ssize_t bytes_read;

    result = ReadFile(p->sync_data_pipe->read, buf, count, NULL, overlapped);
    if (!result)
    {
        result = GetLastError();
        switch (result)
        {
            case ERROR_SUCCESS:
                // No error?
                break;
            case ERROR_IO_PENDING:
                wait_result = WaitForMultipleObjects(2, event_handles, FALSE, p->timeout);

                switch (wait_result)
                {
                    case WAIT_OBJECT_0 + 0:
                        break;

                    case WAIT_OBJECT_0 + 1:
                        return port_read_sync_data_error_code(p);

                    case WAIT_TIMEOUT:
                        if (count == 0)
                        {
                            CancelIo(read_handle);
                            return -RIG_ETIMEOUT;
                        }
                        else
                        {
                            // Should not happen
                            return -RIG_EINTERNAL;
                        }

                    default:
                        result = GetLastError();
                        rig_debug(RIG_DEBUG_ERR, "%s(): WaitForMultipleObjects() error: %d\n", __func__, result);
                        return -RIG_EINTERNAL;
                }
                break;
            default:
                rig_debug(RIG_DEBUG_ERR, "%s(): ReadFile() error: %d\n", __func__, result);
                return -RIG_EIO;
        }
    }

    result = GetOverlappedResult(read_handle, overlapped, (LPDWORD) &bytes_read, FALSE);
    if (!result)
    {
        result = GetLastError();
        switch (result)
        {
            case ERROR_SUCCESS:
                // No error?
                break;
            case ERROR_IO_PENDING:
                // Shouldn't happen?
                return -RIG_ETIMEOUT;
            default:
                rig_debug(RIG_DEBUG_ERR, "%s(): GetOverlappedResult() error: %d\n", __func__, result);
                return -RIG_EIO;
        }
    }

    return bytes_read;
}

static int port_wait_for_data_sync_pipe(hamlib_port_t *p)
{
    unsigned char data;
    int result;

    // Use a zero-length read to wait for data in pipe
    result = port_read_sync_data(p, &data, 0);

    if (result > 0)
    {
        return RIG_OK;
    }

    return result;
}

static ssize_t port_read_sync_data_pipe(hamlib_port_t *p, void *buf, size_t count)
{
    return port_read_sync_data(p, buf, count);
}
#endif

/* On MinGW32/MSVC/.. the appropriate accessor must be used
 * depending on the port type, sigh.
 */
static ssize_t port_read_generic(hamlib_port_t *p, void *buf, size_t count, int direct)
{
    int fd = p->fd;
    int i;
    ssize_t bytes_read;

#if ASYNC_BUG
    if (!direct)
    {
        return port_read_sync_data_pipe(p, buf, count);
    }
#endif

    /*
     * Since WIN32 does its special serial read, we have
     * to catch the microHam case to do just "read".
     * Note that we always have RIG_PORT_SERIAL in the
     * microHam case.
     */
    if (is_uh_radio_fd(fd))
    {
        return read(fd, buf, count);
    }

    if (p->type.rig == RIG_PORT_SERIAL)
    {
        bytes_read = win32_serial_read(fd, buf, (int) count);

        if (p->parm.serial.data_bits == 7)
        {
            unsigned char *pbuf = buf;

            /* clear MSB */
            for (i = 0; i < bytes_read; i++)
            {
                pbuf[i] &= ~0x80;
            }
        }

        return bytes_read;
    }
    else if (p->type.rig == RIG_PORT_NETWORK || p->type.rig == RIG_PORT_UDP_NETWORK)
    {
        return recv(fd, buf, count, 0);
    }
    else
    {
        return read(fd, buf, count);
    }
}

static ssize_t port_write(hamlib_port_t *p, const void *buf, size_t count)
{
    /*
     * Since WIN32 does its special serial write, we have
     * to catch the microHam case to do just "write".
     * Note that we always have RIG_PORT_SERIAL in the
     * microHam case.
     */
    if (is_uh_radio_fd(p->fd))
    {
        return write(p->fd, buf, count);
    }

    if (p->type.rig == RIG_PORT_SERIAL)
    {
        return win32_serial_write(p->fd, buf, (int) count);
    }
    else if (p->type.rig == RIG_PORT_NETWORK
             || p->type.rig == RIG_PORT_UDP_NETWORK)
    {
        return send(p->fd, buf, count, 0);
    }
    else
    {
        return write(p->fd, buf, count);
    }
}

static int port_select(hamlib_port_t *p,
                       int n,
                       fd_set *readfds,
                       fd_set *writefds,
                       fd_set *exceptfds,
                       struct timeval *timeout,
                       int direct)
{
#if 1

    /* select does not work very well with writefds/exceptfds
     * So let's pretend there's none of them
     */
    if (exceptfds)
    {
        FD_ZERO(exceptfds);
    }

    if (writefds)
    {
        FD_ZERO(writefds);
    }

    writefds = NULL;
    exceptfds = NULL;
#endif

    /*
     * Since WIN32 does its special serial select, we have
     * to catch the microHam case to do just "select".
     * Note that we always have RIG_PORT_SERIAL in the
     * microHam case.
     */
    if (direct && is_uh_radio_fd(p->fd))
    {
        return select(n, readfds, writefds, exceptfds, timeout);
    }

    if (direct && p->type.rig == RIG_PORT_SERIAL)
    {
        return win32_serial_select(n, readfds, writefds, exceptfds, timeout);
    }
    else
    {
        return select(n, readfds, writefds, exceptfds, timeout);
    }
}

static int port_wait_for_data_direct(hamlib_port_t *p)
{
    fd_set rfds, efds;
    int fd = p->fd;
    struct timeval tv, tv_timeout;
    int result;

    tv_timeout.tv_sec = p->timeout / 1000;
    tv_timeout.tv_usec = (p->timeout % 1000) * 1000;

    tv = tv_timeout;    /* select may have updated it */

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    efds = rfds;

    result = port_select(p, fd + 1, &rfds, NULL, &efds, &tv, 1);

    if (result == 0)
    {
        return -RIG_ETIMEOUT;
    }
    else if (result < 0)
    {
        rig_debug(RIG_DEBUG_ERR,
                    "%s(): select() error: %s\n",
                    __func__,
                    strerror(errno));
        return -RIG_EIO;
    }

    if (FD_ISSET(fd, &efds))
    {
        rig_debug(RIG_DEBUG_ERR, "%s(): fd error\n", __func__);
        return -RIG_EIO;
    }

    return RIG_OK;
}

static int port_wait_for_data(hamlib_port_t *p, int direct)
{
    if (direct)
    {
        return port_wait_for_data_direct(p);
    }
#ifdef ASYNC_BUG
    return port_wait_for_data_sync_pipe(p);
#else
    RETURNFUNC(-RIG_EINTERNAL);
#endif
}

#ifdef ASYNC_BUG
int HAMLIB_API write_block_sync(hamlib_port_t *p, const unsigned char *txbuffer, size_t count)
{
    return async_pipe_write(p->sync_data_pipe, txbuffer, count, p->timeout);
}

int HAMLIB_API write_block_sync_error(hamlib_port_t *p, const unsigned char *txbuffer, size_t count)
{
    return async_pipe_write(p->sync_data_error_pipe, txbuffer, count, p->timeout);
}
#endif

#else

/* POSIX */

static ssize_t port_read_generic(hamlib_port_t *p, void *buf, size_t count, int direct)
{
#ifdef ASYNC_BUG
    int fd = direct ? p->fd : p->fd_sync_read;
#else
    int fd = p->fd;
#endif

    if (p->type.rig == RIG_PORT_SERIAL && p->parm.serial.data_bits == 7)
    {
        unsigned char *pbuf = buf;

        ssize_t ret = read(fd, buf, count);

        /* clear MSB */
        ssize_t i;

        for (i = 0; i < ret; i++)
        {
            pbuf[i] &= ~0x80;
        }

        return ret;
    }
    else
    {
        return read(fd, buf, count);
    }
}

//! @cond Doxygen_Suppress
#define port_write(p,b,c) write((p)->fd,(b),(c))
#define port_select(p,n,r,w,e,t,d) select((n),(r),(w),(e),(t))
//! @endcond

static int flush_and_read_last_byte(hamlib_port_t *p, int fd, int direct)
{
    fd_set rfds, efds;
    ssize_t bytes_read;
    struct timeval tv_timeout;
    int result;
    char data;

    do {
        tv_timeout.tv_sec = 0;
        tv_timeout.tv_usec = 0;

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        efds = rfds;

        result = port_select(p, fd + 1, &rfds, NULL, &efds, &tv_timeout, direct);
        if (result < 0)
        {
            return -RIG_ETIMEOUT;
        }
        if (result == 0)
        {
            return -RIG_EIO;
        }

        if (FD_ISSET(fd, &efds))
        {
            return -RIG_EIO;
        }

        bytes_read = read(fd, &data, 1);
    } while (bytes_read > 0);

    return data;
}

static int port_wait_for_data(hamlib_port_t *p, int direct)
{
    fd_set rfds, efds;
    int fd, errorfd, maxfd;
    struct timeval tv, tv_timeout;
    int result;

#if ASYNC_BUG
    fd = direct ? p->fd : p->fd_sync_read;
    errorfd = direct ? -1 : p->fd_sync_error_read;
    maxfd = (fd > errorfd) ? fd : errorfd;
#else
    fd = p->fd;
    errorfd = -1;
    maxfd = (fd > errorfd) ? fd : errorfd;
#endif

    tv_timeout.tv_sec = p->timeout / 1000;
    tv_timeout.tv_usec = (p->timeout % 1000) * 1000;

    tv = tv_timeout;    /* select may have updated it */

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    if (!direct)
    {
        FD_SET(errorfd, &rfds);
    }
    efds = rfds;

    result = port_select(p, maxfd + 1, &rfds, NULL, &efds, &tv, direct);

    if (result == 0)
    {
        return -RIG_ETIMEOUT;
    }
    else if (result < 0)
    {
        rig_debug(RIG_DEBUG_ERR,
                    "%s(): select() error: %s\n",
                    __func__,
                    strerror(errno));
        return -RIG_EIO;
    }

    if (FD_ISSET(fd, &efds))
    {
        rig_debug(RIG_DEBUG_ERR, "%s(): fd error\n", __func__);
        return -RIG_EIO;
    }
    if (!direct)
    {
        if (FD_ISSET(errorfd, &efds))
        {
            rig_debug(RIG_DEBUG_ERR, "%s(): fd error from sync error pipe\n", __func__);
            return -RIG_EIO;
        }

        if (FD_ISSET(errorfd, &rfds))
        {
            return flush_and_read_last_byte(p, errorfd, 0);
        }
    }

    return RIG_OK;
}

#ifdef ASYNC_BUG
int HAMLIB_API write_block_sync(hamlib_port_t *p, const unsigned char *txbuffer, size_t count)
{

    if (!p->async)
    {
        return -RIG_EINTERNAL;
    }

    return (int) write(p->fd_sync_write, txbuffer, count);
}

int HAMLIB_API write_block_sync_error(hamlib_port_t *p, const unsigned char *txbuffer, size_t count)
{
    if (!p->async)
    {
        return -RIG_EINTERNAL;
    }

    return (int) write(p->fd_sync_error_write, txbuffer, count);
}
#endif

#endif

/**
 * \brief Write a block of characters to an fd.
 * \param p rig port descriptor
 * \param txbuffer command sequence to be sent
 * \param count number of bytes to send
 * \return 0 = OK, <0 = NOK
 *
 * Write a block of count characters to port file descriptor,
 * with a pause between each character if write_delay is > 0
 *
 * The write_delay is for Yaesu type rigs..require 5 character
 * sequence to be sent with 50-200msec between each char.
 *
 * Also, post_write_delay is for some Yaesu rigs (eg: FT747) that
 * get confused with sequential fast writes between cmd sequences.
 *
 * input:
 *
 * fd - file descriptor to write to
 * txbuffer - pointer to a command sequence array
 * count - count of byte to send from the txbuffer
 * write_delay - write delay in ms between 2 chars
 * post_write_delay - minimum delay between two writes
 * post_write_date - timeval of last write
 *
 * Actually, this function has nothing specific to serial comm,
 * it could work very well also with any file handle, like a socket.
 */

int HAMLIB_API write_block(hamlib_port_t *p, const unsigned char *txbuffer, size_t count)
{
    int ret;
    int method=0;

    if (p->fd < 0)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: port not open\n", __func__);
        RETURNFUNC(-RIG_EIO);
    }
#ifdef WANT_NON_ACTIVE_POST_WRITE_DELAY

    if (p->post_write_date.tv_sec != 0)
    {
        signed int date_delay;    /* in us */
        struct timeval tv;

        /* FIXME in Y2038 ... */
        gettimeofday(&tv, NULL);
        date_delay = p->post_write_delay * 1000 -
                     ((tv.tv_sec - p->post_write_date.tv_sec) * 1000000 +
                      (tv.tv_usec - p->post_write_date.tv_usec));

        if (date_delay > 0)
        {
            /*
             * optional delay after last write
             */
            hl_sleep(date_delay);
        }

        p->post_write_date.tv_sec = 0;
    }

#endif

    if (p->write_delay > 0)
    {
        int i;
        method = 1;

        for (i = 0; i < count; i++)
        {
            ret = port_write(p, txbuffer + i, 1);

            if (ret != 1)
            {
                rig_debug(RIG_DEBUG_ERR,
                          "%s():%d failed %d - %s\n",
                          __func__,
                          __LINE__,
                          ret,
                          strerror(errno));

                return -RIG_EIO;
            }

            if (p->write_delay > 0) { hl_usleep(p->write_delay * 1000); }
        }
    }
    else
    {
        method = 2;
        ret = port_write(p, txbuffer, count);

        if (ret != count)
        {
            rig_debug(RIG_DEBUG_ERR,
                      "%s():%d failed %d - %s\n",
                      __func__,
                      __LINE__,
                      ret,
                      strerror(errno));

            return -RIG_EIO;
        }
    }

    if (p->post_write_delay > 0)
    {
        method |= 4;
#ifdef WANT_NON_ACTIVE_POST_WRITE_DELAY
#define POST_WRITE_DELAY_TRSHLD 10

        if (p->post_write_delay > POST_WRITE_DELAY_TRSHLD)
        {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            p->post_write_date.tv_sec = tv.tv_sec;
            p->post_write_date.tv_usec = tv.tv_usec;
        }
        else
#endif
            hl_usleep(p->post_write_delay * 1000); /* optional delay after last write */

        /* otherwise some yaesu rigs get confused */
        /* with sequential fast writes*/
    }

    rig_debug(RIG_DEBUG_TRACE, "%s(): TX %d bytes, method=%d\n", __func__, (int)count, method);
    dump_hex((unsigned char *) txbuffer, count);

    return RIG_OK;
}

static int read_block_generic(hamlib_port_t *p, unsigned char *rxbuffer, size_t count, int direct)
{
    struct timeval start_time, end_time, elapsed_time;
    int total_count = 0;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

#ifdef ASYNC_BUG
    if (!p->async && !direct)
#else
    if (!direct)
#endif
    {
        return -RIG_EINTERNAL;
    }

    /* Store the time of the read loop start */
    gettimeofday(&start_time, NULL);

    while (count > 0)
    {
        int result;
        int rd_count;

        result = port_wait_for_data(p, direct);

        if (result == -RIG_ETIMEOUT)
        {
            /* Record timeout time and calculate elapsed time */
            gettimeofday(&end_time, NULL);
            timersub(&end_time, &start_time, &elapsed_time);

            if (direct)
            {
                dump_hex((unsigned char *) rxbuffer, total_count);
            }

            rig_debug(RIG_DEBUG_WARN,
                      "%s(): Timed out %d.%d seconds after %d chars\n",
                      __func__,
                      (int)elapsed_time.tv_sec,
                      (int)elapsed_time.tv_usec,
                      total_count);

            return -RIG_ETIMEOUT;
        }

        if (result < 0)
        {
            if (direct)
            {
                dump_hex((unsigned char *) rxbuffer, total_count);
            }
            rig_debug(RIG_DEBUG_ERR, "%s(): I/O error after %d chars: %d\n", __func__, total_count, result);
            return result;
        }

        /*
         * grab bytes from the rig
         * The file descriptor must have been set up non blocking.
         */
        rd_count = (int) port_read_generic(p, rxbuffer + total_count, count, direct);

        if (rd_count < 0)
        {
            rig_debug(RIG_DEBUG_ERR, "%s(): read failed - %s\n", __func__, strerror(errno));
            return -RIG_EIO;
        }

        total_count += rd_count;
        count -= rd_count;
    }

    if (direct)
    {
        rig_debug(RIG_DEBUG_TRACE, "%s(): RX %d bytes\n", __func__, total_count);
        dump_hex((unsigned char *) rxbuffer, total_count);
    }

    return total_count;           /* return bytes count read */
}


/**
 * \brief Read bytes from the device directly or from the synchronous data pipe, depending on the device caps
 * \param p rig port descriptor
 * \param rxbuffer buffer to receive text
 * \param count number of bytes
 * \return count of bytes received
 *
 * Read "num" bytes from "fd" and put results into
 * an array of unsigned char pointed to by "rxbuffer"
 *
 * Blocks on read until timeout hits.
 *
 * It then reads "num" bytes into rxbuffer.
 *
 * Actually, this function has nothing specific to serial comm,
 * it could work very well also with any file handle, like a socket.
 */

int HAMLIB_API read_block(hamlib_port_t *p, unsigned char *rxbuffer, size_t count)
{
#ifdef ASYNC_BUG
    return read_block_generic(p, rxbuffer, count, !p->async);
#else
    return read_block_generic(p, rxbuffer, count, 1);
#endif
}

/**
 * \brief Read bytes directly from the device file descriptor
 * \param p rig port descriptor
 * \param rxbuffer buffer to receive text
 * \param count number of bytes
 * \return count of bytes received
 *
 * Read "num" bytes from "fd" and put results into
 * an array of unsigned char pointed to by "rxbuffer"
 *
 * Blocks on read until timeout hits.
 *
 * It then reads "num" bytes into rxbuffer.
 *
 * Actually, this function has nothing specific to serial comm,
 * it could work very well also with any file handle, like a socket.
 */

int HAMLIB_API read_block_direct(hamlib_port_t *p, unsigned char *rxbuffer, size_t count)
{
    return read_block_generic(p, rxbuffer, count, 1);
}

static int read_string_generic(hamlib_port_t *p,
                               unsigned char *rxbuffer,
                               size_t rxmax,
                               const char *stopset,
                               int stopset_len,
                               int flush_flag,
                               int expected_len,
                               int direct)
{
    struct timeval start_time, end_time, elapsed_time;
    int total_count = 0;
    int i = 0;
    static int minlen = 1; // dynamic minimum length of rig response data

#ifdef ASYNC_BUG
    if (!p->async && !direct)
#else
    if (!direct)
#endif
    {
        return -RIG_EINTERNAL;
    }

    rig_debug(RIG_DEBUG_TRACE, "%s called, rxmax=%d\n", __func__, (int)rxmax);

    if (!p || !rxbuffer)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: error p=%p, rxbuffer=%p\n", __func__, p,
                  rxbuffer);
        return -RIG_EINVAL;
    }

    if (rxmax < 1)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: error rxmax=%ld\n", __func__, (long)rxmax);
        return 0;
    }

    /* Store the time of the read loop start */
    gettimeofday(&start_time, NULL);

    memset(rxbuffer, 0, rxmax);

    while (total_count < rxmax - 1) // allow 1 byte for end-of-string
    {
        ssize_t rd_count = 0;
        int result;

        result = port_wait_for_data(p, direct);

        if (result == -RIG_ETIMEOUT)
        {
            if (0 == total_count)
            {
                /* Record timeout time and calculate elapsed time */
                gettimeofday(&end_time, NULL);
                timersub(&end_time, &start_time, &elapsed_time);

                if (direct)
                {
                    dump_hex((unsigned char *) rxbuffer, total_count);
                }
                if (!flush_flag)
                {
                    rig_debug(RIG_DEBUG_WARN,
                            "%s(): Timed out %d.%03d seconds after %d chars\n",
                            __func__,
                            (int)elapsed_time.tv_sec,
                            (int)elapsed_time.tv_usec / 1000,
                            total_count);
                }

                return -RIG_ETIMEOUT;
            }

            break; /* return what we have read */
        }

        if (result < 0)
        {
            if (direct)
            {
                dump_hex(rxbuffer, total_count);
            }
            rig_debug(RIG_DEBUG_ERR, "%s(): I/O error after %d chars: %d\n", __func__, total_count, result);
            return result;
        }

        /*
         * read 1 character from the rig, (check if in stop set)
         * The file descriptor must have been set up non blocking.
         */
        do
        {
            rd_count = port_read_generic(p, &rxbuffer[total_count], expected_len == 1 ? 1 : minlen, direct);
            minlen -= rd_count;
            if (errno == EAGAIN)
            {
                hl_usleep(5 * 1000);
                rig_debug(RIG_DEBUG_WARN, "%s: port_read is busy?\n", __func__);
            }
        }
        while (++i < 10 && errno == EBUSY);   // 50ms should be enough

        /* if we get 0 bytes or an error something is wrong */
        if (rd_count <= 0)
        {
            if (direct)
            {
                dump_hex((unsigned char *) rxbuffer, total_count);
            }
            rig_debug(RIG_DEBUG_ERR, "%s(): read failed - %s\n", __func__, strerror(errno));

            return -RIG_EIO;
        }

        // check to see if our string startis with \...if so we need more chars
        if (total_count == 0 && rxbuffer[total_count] == '\\') { rxmax = (rxmax - 1) * 5; }

        total_count += (int) rd_count;

        if (stopset && memchr(stopset, rxbuffer[total_count - 1], stopset_len))
        {
            if (minlen == 1) { minlen = total_count; }

            if (minlen < total_count)
            {
                minlen = total_count;
                rig_debug(RIG_DEBUG_VERBOSE, "%s: minlen now %d\n", __func__, minlen);
            }

            break;
        }
    }

    /*
     * Doesn't hurt anyway. But be aware, some binary protocols may have
     * null chars within the received buffer.
     */
    rxbuffer[total_count] = '\000';

    if (direct)
    {
        rig_debug(RIG_DEBUG_TRACE,
                "%s(): RX %d characters\n",
                __func__,
                total_count);
        dump_hex((unsigned char *) rxbuffer, total_count);
    }

    return total_count;           /* return bytes count read */
}

/**
 * \brief Read a string from the device directly or from the synchronous data pipe, depending on the device caps
 * \param p Hamlib port descriptor
 * \param rxbuffer buffer to receive string
 * \param rxmax maximum string size + 1
 * \param stopset string of recognized end of string characters
 * \param stopset_len length of stopset
 * \return number of characters read if the operation has been successful,
 * otherwise a negative value if an error occurred (in which case, cause is
 * set appropriately).
 *
 * Read a string from "fd" and put result into
 * an array of unsigned char pointed to by "rxbuffer"
 *
 * Blocks on read until timeout hits.
 *
 * It then reads characters until one of the characters in
 * "stopset" is found, or until "rxmax-1" characters was copied
 * into rxbuffer.  String termination character is added at the end.
 *
 * Actually, this function has nothing specific to serial comm,
 * it could work very well also with any file handle, like a socket.
 *
 * Assumes rxbuffer!=NULL
 */

int HAMLIB_API read_string(hamlib_port_t *p,
        unsigned char *rxbuffer,
        size_t rxmax,
        const char *stopset,
        int stopset_len,
        int flush_flag,
        int expected_len)
{
#ifdef ASYNC_BUG
    return read_string_generic(p, rxbuffer, rxmax, stopset, stopset_len, flush_flag, expected_len, !p->async);
#else
    return read_string_generic(p, rxbuffer, rxmax, stopset, stopset_len, flush_flag, expected_len, 1);
#endif
}


/**
 * \brief Read a string directly from the device file descriptor
 * \param p Hamlib port descriptor
 * \param rxbuffer buffer to receive string
 * \param rxmax maximum string size + 1
 * \param stopset string of recognized end of string characters
 * \param stopset_len length of stopset
 * \return number of characters read if the operation has been successful,
 * otherwise a negative value if an error occurred (in which case, cause is
 * set appropriately).
 *
 * Read a string from "fd" and put result into
 * an array of unsigned char pointed to by "rxbuffer"
 *
 * Blocks on read until timeout hits.
 *
 * It then reads characters until one of the characters in
 * "stopset" is found, or until "rxmax-1" characters was copied
 * into rxbuffer.  String termination character is added at the end.
 *
 * Actually, this function has nothing specific to serial comm,
 * it could work very well also with any file handle, like a socket.
 *
 * Assumes rxbuffer!=NULL
 */

int HAMLIB_API read_string_direct(hamlib_port_t *p,
                                  unsigned char *rxbuffer,
                                  size_t rxmax,
                                  const char *stopset,
                                  int stopset_len,
                                  int flush_flag,
                                  int expected_len)
{
    return read_string_generic(p, rxbuffer, rxmax, stopset, stopset_len, flush_flag, expected_len, 1);
}

/** @} */
