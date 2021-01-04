/*
 *  Hamlib Barrett 950 backend - main file
 *  Copyright (c) 2017-2020 by Michael Black W9MDB
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
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <hamlib/rig.h>
#include "serial.h"
#include "misc.h"
#include "cal.h"
#include "token.h"
#include "register.h"

#include "barrett.h"

#define MAXCMDLEN 32

#define BARRETT950_VFOS (RIG_VFO_A|RIG_VFO_MEM)

#define BARRETT950_MODES (RIG_MODE_AM | RIG_MODE_CW | RIG_MODE_RTTY | RIG_MODE_SSB)

#define BARRETT950_LEVELS (RIG_LEVEL_NONE)


static int barrett950_set_freq(RIG *rig, vfo_t vfo, freq_t freq);

static int barrett950_get_level(RIG *rig, vfo_t vfo, setting_t level,
                             value_t *val);

static const char *barrett950_get_info(RIG *rig);


const struct rig_caps barrett950_caps =
{
    RIG_MODEL(RIG_MODEL_BARRETT_950),
    .model_name =       "950",
    .mfg_name =         "Barrett",
    .version =          BACKEND_VER ".0",
    .copyright =        "LGPL",
    .status =           RIG_STATUS_BETA,
    .rig_type =         RIG_TYPE_TRANSCEIVER,
    .targetable_vfo =   RIG_TARGETABLE_FREQ | RIG_TARGETABLE_MODE,
    .ptt_type =         RIG_PTT_RIG,
    .dcd_type =         RIG_DCD_NONE,
    .port_type =        RIG_PORT_SERIAL,
    .serial_rate_min =  9600,
    .serial_rate_max =  9600,
    .serial_data_bits = 8,
    .serial_stop_bits = 1,
    .serial_parity =    RIG_PARITY_NONE,
    .serial_handshake = RIG_HANDSHAKE_XONXOFF,
    .write_delay =      0,
    .post_write_delay = 50,
    .timeout =          1000,
    .retry =            3,

    .has_get_func =     RIG_FUNC_NONE,
    .has_set_func =     RIG_FUNC_NONE,
    .has_get_level =    BARRETT950_LEVELS,
    .has_set_level =    RIG_LEVEL_NONE,
    .has_get_parm =     RIG_PARM_NONE,
    .has_set_parm =     RIG_PARM_NONE,
    .transceive =       RIG_TRN_RIG,
    .rx_range_list1 = {{
            .startf = kHz(1600), .endf = MHz(30), .modes = BARRETT950_MODES,
            .low_power = -1, .high_power = -1, BARRETT950_VFOS, RIG_ANT_1
        },
        RIG_FRNG_END,
    },
    .rx_range_list2 = {RIG_FRNG_END,},
    .tx_range_list1 = {RIG_FRNG_END,},
    .tx_range_list2 = {RIG_FRNG_END,},
    .tuning_steps =  { {BARRETT950_MODES, 1}, {BARRETT950_MODES, RIG_TS_ANY}, RIG_TS_END, },
    .filters = {
        {RIG_MODE_SSB | RIG_MODE_CW | RIG_MODE_RTTY, kHz(2.4)},
        {RIG_MODE_CW, Hz(500)},
        {RIG_MODE_AM, kHz(8)},
        {RIG_MODE_AM, kHz(2.4)},
        RIG_FLT_END,
    },
    .priv = NULL,

    .rig_init =     barrett_init,
    .rig_cleanup =  barrett_cleanup,

    .set_freq = barrett950_set_freq,
    .get_freq = barrett_get_freq,
    .set_mode = barrett_set_mode,
    .get_mode = barrett_get_mode,

    .get_level =    barrett950_get_level,

    .get_info =     barrett950_get_info,
    .set_ptt =      barrett_set_ptt,
    .get_ptt =      NULL,
    .set_split_freq =   barrett_set_split_freq,
    .set_split_vfo =    barrett_set_split_vfo,
    .get_split_vfo =    barrett_get_split_vfo,
};

/*
 * barrett950_set_freq
 * assumes rig!=NULL, rig->state.priv!=NULL
 */
int barrett950_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
    char cmd_buf[MAXCMDLEN];
    int retval;
    struct barrett_priv_data *priv = rig->state.priv;

    rig_debug(RIG_DEBUG_VERBOSE, "%s: vfo=%s freq=%.0f\n", __func__,
              rig_strvfo(vfo), freq);

    // If we are not explicitly asking for VFO_B then we'll set the receive side also
    if (vfo != RIG_VFO_B)
    {
        char *response = NULL;
        sprintf((char *) cmd_buf, "PR%08.0f", freq);
        retval = barrett_transaction(rig, cmd_buf, 0, &response);

        if (retval < 0)
        {
            return retval;
        }

        //dump_hex((unsigned char *)response, strlen(response));

        if (strncmp(response, "OK", 2) != 0)
        {
            rig_debug(RIG_DEBUG_ERR, "%s: Expected OK, got '%s'\n", __func__, response);
            return -RIG_EPROTO;
        }
    }

    if (priv->split == 0
            || vfo == RIG_VFO_B)   // if we aren't in split mode we have to set the TX VFO too
    {

        char *response = NULL;
        sprintf((char *) cmd_buf, "PT%08.0f", freq);
        retval = barrett_transaction(rig, cmd_buf, 0, &response);

        if (retval < 0)
        {
            return retval;
        }

        if (strncmp(response, "OK", 2) != 0)
        {
            rig_debug(RIG_DEBUG_ERR, "%s: Expected OK, got '%s'\n", __func__, response);
            return -RIG_EPROTO;
        }
    }

    return RIG_OK;
}

/*
 * barrett950_get_level
 */
int barrett950_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val)
{
    return -RIG_ENIMPL;
}

/*
 * barrett950_get_info
 */
const char *barrett950_get_info(RIG *rig)
{
    char *response = NULL;
    int retval;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    retval = barrett_transaction(rig, "IV", 0, &response);

    if (retval == RIG_OK)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: result=%s\n", __func__, response);
    }
    else
    {
        rig_debug(RIG_DEBUG_VERBOSE, "Software Version %s\n", response);
    }

    return response;
}
