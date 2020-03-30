/*
 *  Hamlib PCR backend - PCR-2500 description
 *  Copyright (c) 2001-2010 by Stephane Fillod
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
#include "config.h"
#endif

#include <stdlib.h>

#include <hamlib/rig.h>

#include "pcr.h"
#include "idx_builtin.h"

#define PCR2500_MODES_WIDE ( RIG_MODE_AM | RIG_MODE_FM | RIG_MODE_WFM )
#define PCR2500_MODES_NAR ( RIG_MODE_CW | RIG_MODE_SSB )
#define PCR2500_MODES ( PCR2500_MODES_NAR | PCR2500_MODES_WIDE )

#define PCR2500_FUNC ( RIG_FUNC_NB | RIG_FUNC_TSQL | RIG_FUNC_NB | \
            RIG_FUNC_ANF | RIG_FUNC_NR | RIG_FUNC_AFC )

#define PCR2500_LEVEL ( \
            RIG_LEVEL_ATT | RIG_LEVEL_AF | RIG_LEVEL_SQL | RIG_LEVEL_IF | \
            RIG_LEVEL_AGC | RIG_LEVEL_STRENGTH | RIG_LEVEL_RAWSTR | \
            RIG_LEVEL_NR )


static const struct confparams pcr2500_ext_levels[] =
{
    {
        TOK_EL_DIVERSITY, "DIV", "Diversity", "Antenna/tuner diversity",
        NULL, RIG_CONF_CHECKBUTTON
    },
    { RIG_CONF_END, NULL, }
};

static const struct pcr_priv_caps pcr2500_priv =
{
    .reply_size = 6,
    .reply_offset   = 0,
    .always_sync    = 0,
};

/*
 * IC PCR2500 rigs capabilities.
 */
const struct rig_caps pcr2500_caps =
{
    RIG_MODEL(RIG_MODEL_PCR2500),
    .model_name     = "IC-PCR2500",
    .mfg_name       = "Icom",
    .version        = BACKEND_VER ".0",
    .copyright      = "LGPL",
    .status         = RIG_STATUS_BETA,

    .rig_type       = RIG_TYPE_PCRECEIVER,
    .ptt_type       = RIG_PTT_NONE,
    .dcd_type       = RIG_DCD_RIG,
    .port_type      = RIG_PORT_SERIAL,

    .serial_rate_min    = 9600, /* lower speeds gave troubles */
    .serial_rate_max    = 38400,
    .serial_data_bits   = 8,
    .serial_stop_bits   = 1,
    .serial_parity      = RIG_PARITY_NONE,
    .serial_handshake   = RIG_HANDSHAKE_HARDWARE,

    .write_delay        = 0,
    .post_write_delay   = 0,
    .timeout        = 400,
    .retry          = 3,

    .has_get_func       = PCR2500_FUNC,
    .has_set_func       = PCR2500_FUNC,
    .has_get_level      = PCR2500_LEVEL,
    .has_set_level      = RIG_LEVEL_SET(PCR2500_LEVEL),
    .has_get_parm       = RIG_PARM_NONE,
    .has_set_parm       = RIG_PARM_NONE,

    .level_gran = {
        [LVL_RAWSTR] = { .min = { .i = 0 }, .max = { .i = 255 } },
        /* XXX check this */
        [LVL_IF] = { .min = { .i = -1270 }, .max = { .i = 1270 }, .step = { .i = 10 } },
    },
    .parm_gran      = {},

    .ctcss_list     = pcr_ctcss_list,
    .dcs_list       = pcr_dcs_list,
    .preamp         = { RIG_DBLST_END },
    .attenuator     = { 20, RIG_DBLST_END },

    .max_rit        = Hz(0),
    .max_xit        = Hz(0),
    .max_ifshift        = kHz(1.2),

    .targetable_vfo     = RIG_TARGETABLE_ALL,
    .transceive     = RIG_TRN_RIG,
    .bank_qty       = 0,
    .chan_desc_sz       = 0,
    .chan_list      = { RIG_CHAN_END },

    .rx_range_list1 = {
        { kHz(100), GHz(1.3), PCR2500_MODES_NAR,  -1, -1, RIG_VFO_MAIN, RIG_ANT_1 },
        { kHz(10), GHz(3.3) - kHz(1), PCR2500_MODES_WIDE, -1, -1, RIG_VFO_MAIN, RIG_ANT_1 },

        { MHz(50), GHz(1.3), PCR2500_MODES_WIDE,  -1, -1, RIG_VFO_SUB, RIG_ANT_2 },
        RIG_FRNG_END
    },
    .tx_range_list1 = { RIG_FRNG_END },
    .rx_range_list2 = {
        { kHz(495), MHz(824) - 10, PCR2500_MODES_NAR, -1, -1, RIG_VFO_MAIN, RIG_ANT_1 },
        { MHz(849) + 10, MHz(869) - 10, PCR2500_MODES_NAR, -1, -1, RIG_VFO_MAIN, RIG_ANT_1 },
        { MHz(894) + 10, GHz(1.3), PCR2500_MODES_NAR, -1, -1, RIG_VFO_MAIN, RIG_ANT_1 },

        { kHz(10), MHz(824) - 10, PCR2500_MODES_WIDE, -1, -1, RIG_VFO_MAIN, RIG_ANT_1 },
        { MHz(849) + 10, MHz(869) - 10, PCR2500_MODES_WIDE, -1, -1, RIG_VFO_MAIN, RIG_ANT_1 },
        { MHz(894) + 10, GHz(3.3) - kHz(1), PCR2500_MODES_WIDE, -1, -1, RIG_VFO_MAIN, RIG_ANT_1 },

        { MHz(50), GHz(1.3), PCR2500_MODES_WIDE,  -1, -1, RIG_VFO_SUB, RIG_ANT_2 },
        RIG_FRNG_END
    },
    .tx_range_list2 = { RIG_FRNG_END }, /* no TX ranges, this is a receiver */

    .tuning_steps = {
        { PCR2500_MODES, Hz(1) },
        RIG_TS_END,
    },
    /* mode/filter list, remember: order matters! */
    .filters = {
        { RIG_MODE_FM | RIG_MODE_AM, kHz(15) },
        { RIG_MODE_CW | RIG_MODE_USB | RIG_MODE_LSB | RIG_MODE_AM, kHz(2.8)},
        { RIG_MODE_CW | RIG_MODE_USB | RIG_MODE_LSB | RIG_MODE_AM | RIG_MODE_FM, kHz(6) },
        { RIG_MODE_WFM, kHz(230) },
        { RIG_MODE_WFM | RIG_MODE_FM | RIG_MODE_AM, kHz(50) },
        RIG_FLT_END,
    },

    .extlevels = pcr2500_ext_levels,

    .priv = (void *)& pcr2500_priv,

    /* XXX fake */
    .str_cal = { 3, { { 0, -60 }, { 127, 0 }, { 255, 60 } } },

    .rig_init   = pcr_init,
    .rig_cleanup    = pcr_cleanup,
    .rig_open   = pcr_open,
    .rig_close  = pcr_close,

    .set_vfo    = pcr_set_vfo,
    .get_vfo    = pcr_get_vfo,

    .set_freq   = pcr_set_freq,
    .get_freq   = pcr_get_freq,
    .set_mode   = pcr_set_mode,
    .get_mode   = pcr_get_mode,

    .get_info   = pcr_get_info,

    .set_level  = pcr_set_level,
    .get_level  = pcr_get_level,

    .set_ext_level  = pcr_set_ext_level,

    .set_func   = pcr_set_func,
    .get_func   = pcr_get_func,

    .set_ctcss_sql  = pcr_set_ctcss_sql,
    .get_ctcss_sql  = pcr_get_ctcss_sql,
    .set_dcs_sql    = pcr_set_dcs_sql,
    .get_dcs_sql    = pcr_get_dcs_sql,

    .set_trn    = pcr_set_trn,
    .decode_event   = pcr_decode_event,
    .get_dcd    = pcr_get_dcd,

    .set_powerstat  = pcr_set_powerstat,
    .get_powerstat  = pcr_get_powerstat,
};

