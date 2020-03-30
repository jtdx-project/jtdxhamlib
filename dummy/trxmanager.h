/*
 *  Hamlib TRXManager backend - main header
 *  Copyright (c) 2017 by Michael Black W9MDB
 *  Copyright (c) 2018 by Michael Black W9MDB
 *  Derived from flrig.h
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

#ifndef _TRXMANAGER_H
#define _TRXMANAGER_H 1

#include "hamlib/rig.h"

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#define BACKEND_VER "20200329"

#define EOM "\r"
#define TRUE 1
#define FALSE 0

extern struct rig_caps trxmanager_caps;

#endif /* _TRXMANAGER_H */
