/*
 * devicedb.h
 * Internal device database interface (not installed)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef IRECV_DEVICEDB_H
#define IRECV_DEVICEDB_H

#include "libirecovery.h"

/* built-in table generated from data/devices.txt; terminated by a NULL entry */
extern struct irecv_device irecv_devices_builtin[];

void irecv_devicedb_init(void);
void irecv_devicedb_cleanup(void);
void irecv_devicedb_set_debug(int level);

/* NULL-terminated table (built-in data merged with the device files).
 * Never NULL, and the returned table stays valid until library unload. */
struct irecv_device* irecv_devicedb_get(void);

#endif
