/*
 *  Hamlib CI-V backend - description of IC-7800 and variations
 *  Copyright (c) 2009-2010 by Stephane Fillod
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
#include <string.h>  /* String function definitions */

#include <hamlib/rig.h>
#include "token.h"
#include "idx_builtin.h"

#include "icom.h"
#include "icom_defs.h"
#include "frame.h"
#include "misc.h"
#include "bandplan.h"

#define IC7800_ALL_RX_MODES (RIG_MODE_AM|RIG_MODE_CW|RIG_MODE_CWR|RIG_MODE_SSB|RIG_MODE_RTTY|RIG_MODE_RTTYR|RIG_MODE_FM|RIG_MODE_PSK|RIG_MODE_PSKR|RIG_MODE_PKTLSB|RIG_MODE_PKTUSB|RIG_MODE_PKTAM|RIG_MODE_PKTFM)
#define IC7800_1HZ_TS_MODES IC7800_ALL_RX_MODES
#define IC7800_OTHER_TX_MODES (RIG_MODE_AM|RIG_MODE_CW|RIG_MODE_CWR|RIG_MODE_SSB|RIG_MODE_RTTY|RIG_MODE_RTTYR|RIG_MODE_FM|RIG_MODE_PSK|RIG_MODE_PSKR|RIG_MODE_PKTLSB|RIG_MODE_PKTUSB|RIG_MODE_PKTFM)
#define IC7800_AM_TX_MODES (RIG_MODE_AM|RIG_MODE_PKTAM)

#define IC7800_FUNCS (RIG_FUNC_NB|RIG_FUNC_COMP|RIG_FUNC_VOX|RIG_FUNC_TONE|RIG_FUNC_TSQL|RIG_FUNC_SBKIN|RIG_FUNC_FBKIN|RIG_FUNC_NR|RIG_FUNC_MON|RIG_FUNC_MN|RIG_FUNC_ANF|RIG_FUNC_VSC|RIG_FUNC_LOCK|RIG_FUNC_RIT|RIG_FUNC_XIT|RIG_FUNC_TUNER|RIG_FUNC_APF|RIG_FUNC_DUAL_WATCH)

#define IC7800_LEVELS (RIG_LEVEL_PREAMP|RIG_LEVEL_ATT|RIG_LEVEL_AGC|RIG_LEVEL_COMP|RIG_LEVEL_BKINDL|RIG_LEVEL_BALANCE|RIG_LEVEL_NR|RIG_LEVEL_PBT_IN|RIG_LEVEL_PBT_OUT|RIG_LEVEL_CWPITCH|RIG_LEVEL_RFPOWER|RIG_LEVEL_MICGAIN|RIG_LEVEL_KEYSPD|RIG_LEVEL_NOTCHF_RAW|RIG_LEVEL_SQL|RIG_LEVEL_RAWSTR|RIG_LEVEL_STRENGTH|RIG_LEVEL_AF|RIG_LEVEL_RF|RIG_LEVEL_APF|RIG_LEVEL_VOXGAIN|RIG_LEVEL_ANTIVOX|RIG_LEVEL_VOXDELAY|RIG_LEVEL_SWR|RIG_LEVEL_ALC|RIG_LEVEL_RFPOWER_METER|RIG_LEVEL_COMP_METER|RIG_LEVEL_VD_METER|RIG_LEVEL_ID_METER|RIG_LEVEL_MONITOR_GAIN|RIG_LEVEL_NB)

#define IC7800_VFOS (RIG_VFO_MAIN|RIG_VFO_SUB|RIG_VFO_MEM)
#define IC7800_PARMS (RIG_PARM_ANN|RIG_PARM_BACKLIGHT)

#define IC7800_VFO_OPS (RIG_OP_CPY|RIG_OP_XCHG|RIG_OP_FROM_VFO|RIG_OP_TO_VFO|RIG_OP_MCL|RIG_OP_TUNE)
#define IC7800_SCAN_OPS (RIG_SCAN_MEM|RIG_SCAN_VFO|RIG_SCAN_PROG|RIG_SCAN_DELTA|RIG_SCAN_PRIO)

#define IC7800_ANTS (RIG_ANT_1|RIG_ANT_2|RIG_ANT_3|RIG_ANT_4)

// IC-7800 S-meter calibration data based on manual
#define IC7800_STR_CAL { 3, \
    { \
        {   0, -54 }, /* S0 */ \
        { 120,   0 }, /* S9 */ \
        { 241,  60 }  /* S9+60 */ \
    } }

#define IC7800_SWR_CAL { 5, \
    { \
         { 0, 1.0f }, \
         { 48, 1.5f }, \
         { 80, 2.0f }, \
         { 120, 3.0f }, \
         { 240, 6.0f } \
    } }

#define IC7800_ALC_CAL { 2, \
    { \
         { 0, 0.0f }, \
         { 120, 1.0f } \
    } }

#define IC7800_RFPOWER_METER_CAL { 3, \
    { \
         { 0, 0.0f }, \
         { 143, 0.5f }, \
         { 213, 1.0f } \
    } }

#define IC7800_COMP_METER_CAL { 3, \
    { \
         { 0, 0.0f }, \
         { 130, 15.0f }, \
         { 241, 30.0f } \
    } }

#define IC7800_VD_METER_CAL { 4, \
    { \
         { 0, 0.0f }, \
         { 151, 44.0f }, \
         { 180, 48.0f }, \
         { 211, 52.0f } \
    } }

#define IC7800_ID_METER_CAL { 3, \
    { \
         { 0, 0.0f }, \
         { 165, 10.0f }, \
         { 241, 15.0f } \
    } }

int ic7800_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val);
int ic7800_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val);

/*
 * IC-7800 rig capabilities.
 *
 * TODO: complete command set (esp. the $1A bunch!) and testing..
 */
static const struct icom_priv_caps ic7800_priv_caps =
{
    0x6a,    /* default address */
    0,       /* 731 mode */
    0,       /* no XCHG */
    ic756pro_ts_sc_list,
    .antack_len = 4,
    .ant_count = 3,
    .agc_levels_present = 1,
    .agc_levels = {
        { .level = RIG_AGC_OFF, .icom_level = 0 },
        { .level = RIG_AGC_FAST, .icom_level = 1 },
        { .level = RIG_AGC_MEDIUM, .icom_level = 2 },
        { .level = RIG_AGC_SLOW, .icom_level = 3 },
        { .level = -1, .icom_level = 0 },
    },
};

const struct confparams ic7800_ext_levels[] =
{
    {
        TOK_DRIVE_GAIN, "drive_gain", "Drive gain", "Drive gain",
        NULL, RIG_CONF_NUMERIC, { .n = { 0, 255, 1 } },
    },
    {
        TOK_DIGI_SEL_FUNC, "digi_sel", "DIGI-SEL enable", "DIGI-SEL enable",
        NULL, RIG_CONF_CHECKBUTTON, { },
    },
    {
        TOK_DIGI_SEL_LEVEL, "digi_sel_level", "DIGI-SEL level", "DIGI-SEL level",
        NULL, RIG_CONF_NUMERIC, { .n = { 0, 255, 1 } },
    },
    { RIG_CONF_END, NULL, }
};

const struct rig_caps ic7800_caps =
{
    RIG_MODEL(RIG_MODEL_IC7800),
    .model_name = "IC-7800",
    .mfg_name =  "Icom",
    .version =  BACKEND_VER ".0",
    .copyright =  "LGPL",
    .status =  RIG_STATUS_UNTESTED,
    .rig_type =  RIG_TYPE_TRANSCEIVER,
    .ptt_type =  RIG_PTT_RIG,
    .dcd_type =  RIG_DCD_RIG,
    .port_type =  RIG_PORT_SERIAL,
    .serial_rate_min =  300,
    .serial_rate_max =  19200,
    .serial_data_bits =  8,
    .serial_stop_bits =  1,
    .serial_parity =  RIG_PARITY_NONE,
    .serial_handshake =  RIG_HANDSHAKE_NONE,
    .write_delay =  0,
    .post_write_delay =  0,
    .timeout =  1000,
    .retry =  3,
    .has_get_func =  IC7800_FUNCS,
    .has_set_func =  IC7800_FUNCS,
    .has_get_level =  IC7800_LEVELS,
    .has_set_level =  RIG_LEVEL_SET(IC7800_LEVELS),
    .has_get_parm =  IC7800_PARMS,
    .has_set_parm =  RIG_PARM_SET(IC7800_PARMS),    /* FIXME: parms */
    .level_gran = {
        [LVL_RAWSTR] = { .min = { .i = 0 }, .max = { .i = 255 } },
        [LVL_VOXDELAY] = { .min = { .i = 0 }, .max = { .i = 20 }, .step = { .i = 1 } },
        [LVL_KEYSPD] = { .min = { .i = 6 }, .max = { .i = 48 }, .step = { .i = 1 } },
        [LVL_CWPITCH] = { .min = { .i = 300 }, .max = { .i = 900 }, .step = { .i = 1 } },
    },
    .parm_gran =  {},
    .extlevels = ic7800_ext_levels,
    .ctcss_list =  common_ctcss_list,
    .dcs_list =  NULL,
    .preamp =   { 10, 20, RIG_DBLST_END, }, /* FIXME: TBC */
    .attenuator =   { 3, 6, 9, 12, 15, 18, 21, RIG_DBLST_END, },
    .max_rit =  Hz(9999),
    .max_xit =  Hz(9999),
    .max_ifshift =  Hz(0),
    .targetable_vfo =  0,
    .vfo_ops =  IC7800_VFO_OPS,
    .scan_ops =  IC7800_SCAN_OPS,
    .transceive =  RIG_TRN_RIG,
    .bank_qty =   0,
    .chan_desc_sz =  0,

    .chan_list =  {
        {   1,  99, RIG_MTYPE_MEM  },
        { 100, 101, RIG_MTYPE_EDGE },    /* two by two */
        RIG_CHAN_END,
    },

    .rx_range_list1 =   { {kHz(30), MHz(60), IC7800_ALL_RX_MODES, -1, -1, IC7800_VFOS, IC7800_ANTS},
        RIG_FRNG_END,
    },
    .tx_range_list1 =   {
        FRQ_RNG_HF(1, IC7800_OTHER_TX_MODES, W(5), W(200), IC7800_VFOS, IC7800_ANTS),
        FRQ_RNG_6m(1, IC7800_OTHER_TX_MODES, W(5), W(200), IC7800_VFOS, IC7800_ANTS),
        FRQ_RNG_HF(1, IC7800_AM_TX_MODES, W(5), W(50), IC7800_VFOS, IC7800_ANTS), /* AM class */
        FRQ_RNG_6m(1, IC7800_AM_TX_MODES, W(5), W(50), IC7800_VFOS, IC7800_ANTS), /* AM class */
        RIG_FRNG_END,
    },

    .rx_range_list2 =   { {kHz(30), MHz(60), IC7800_ALL_RX_MODES, -1, -1, IC7800_VFOS, IC7800_ANTS},
        RIG_FRNG_END,
    },
    .tx_range_list2 =  {
        FRQ_RNG_HF(2, IC7800_OTHER_TX_MODES, W(5), W(200), IC7800_VFOS, IC7800_ANTS),
        FRQ_RNG_6m(2, IC7800_OTHER_TX_MODES, W(5), W(200), IC7800_VFOS, IC7800_ANTS),
        FRQ_RNG_HF(2, IC7800_AM_TX_MODES, W(5), W(50), IC7800_VFOS, IC7800_ANTS), /* AM class */
        FRQ_RNG_6m(2, IC7800_AM_TX_MODES, W(5), W(50), IC7800_VFOS, IC7800_ANTS), /* AM class */
        /* USA only, TBC: end of range and modes */
        {MHz(5.33050), MHz(5.33350), IC7800_OTHER_TX_MODES, W(2), W(100), IC7800_VFOS, IC7800_ANTS}, /* USA only */
        {MHz(5.34650), MHz(5.34950), IC7800_OTHER_TX_MODES, W(2), W(100), IC7800_VFOS, IC7800_ANTS}, /* USA only */
        {MHz(5.36650), MHz(5.36950), IC7800_OTHER_TX_MODES, W(2), W(100), IC7800_VFOS, IC7800_ANTS}, /* USA only */
        {MHz(5.37150), MHz(5.37450), IC7800_OTHER_TX_MODES, W(2), W(100), IC7800_VFOS, IC7800_ANTS}, /* USA only */
        {MHz(5.40350), MHz(5.40650), IC7800_OTHER_TX_MODES, W(2), W(100), IC7800_VFOS, IC7800_ANTS}, /* USA only */
        RIG_FRNG_END,
    },

    .tuning_steps =     {
        {IC7800_1HZ_TS_MODES, 1},
        {IC7800_ALL_RX_MODES, Hz(100)},
        {IC7800_ALL_RX_MODES, kHz(1)},
        {IC7800_ALL_RX_MODES, kHz(5)},
        {IC7800_ALL_RX_MODES, kHz(9)},
        {IC7800_ALL_RX_MODES, kHz(10)},
        {IC7800_ALL_RX_MODES, kHz(12.5)},
        {IC7800_ALL_RX_MODES, kHz(20)},
        {IC7800_ALL_RX_MODES, kHz(25)},
        RIG_TS_END,
    },
    /* mode/filter list, remember: order matters! */
    .filters =  {
        {RIG_MODE_SSB | RIG_MODE_PKTLSB | RIG_MODE_PKTUSB, kHz(2.4)},
        {RIG_MODE_SSB | RIG_MODE_PKTLSB | RIG_MODE_PKTUSB, kHz(1.8)},
        {RIG_MODE_SSB | RIG_MODE_PKTLSB | RIG_MODE_PKTUSB, kHz(3)},
        {RIG_MODE_CW | RIG_MODE_CWR | RIG_MODE_RTTY | RIG_MODE_RTTYR | RIG_MODE_PSK | RIG_MODE_PSKR, Hz(400)},
        {RIG_MODE_CW | RIG_MODE_CWR | RIG_MODE_RTTY | RIG_MODE_RTTYR | RIG_MODE_PSK | RIG_MODE_PSKR, Hz(50)},
        {RIG_MODE_CW | RIG_MODE_CWR | RIG_MODE_PSK | RIG_MODE_PSKR, kHz(1.0)},
        {RIG_MODE_RTTY | RIG_MODE_RTTYR, kHz(2.4)},
        {RIG_MODE_AM | RIG_MODE_PKTAM, kHz(6)},
        {RIG_MODE_AM | RIG_MODE_PKTAM, kHz(3)},
        {RIG_MODE_AM | RIG_MODE_PKTAM, kHz(9)},
        {RIG_MODE_FM | RIG_MODE_PKTFM, kHz(12)},
        {RIG_MODE_FM | RIG_MODE_PKTFM, kHz(8)},
        {RIG_MODE_FM | RIG_MODE_PKTFM, kHz(15)},
        RIG_FLT_END,
    },
    .str_cal = IC7800_STR_CAL,
    .swr_cal = IC7800_SWR_CAL,
    .alc_cal = IC7800_ALC_CAL,
    .rfpower_meter_cal = IC7800_RFPOWER_METER_CAL,
    .comp_meter_cal = IC7800_COMP_METER_CAL,
    .vd_meter_cal = IC7800_VD_METER_CAL,
    .id_meter_cal = IC7800_ID_METER_CAL,

    .cfgparams =  icom_cfg_params,
    .set_conf =  icom_set_conf,
    .get_conf =  icom_get_conf,

    .priv = (void *)& ic7800_priv_caps,
    .rig_init =   icom_init,
    .rig_cleanup =   icom_cleanup,
    .rig_open =  icom_rig_open,
    .rig_close =  icom_rig_close,

    .set_freq =  icom_set_freq,
    .get_freq =  icom_get_freq,
    .set_mode =  icom_set_mode_with_data,
    .get_mode =  icom_get_mode_with_data,
    .set_vfo =  icom_set_vfo,
    .set_ant =  icom_set_ant,
    .get_ant =  icom_get_ant,

    .set_rit =  icom_set_rit_new,
    .get_rit =  icom_get_rit_new,
    .get_xit =  icom_get_rit_new,
    .set_xit =  icom_set_xit_new,

    .decode_event =  icom_decode_event,
    .set_level =  ic7800_set_level,
    .get_level =  ic7800_get_level,
    .set_ext_level =  icom_set_ext_level,
    .get_ext_level =  icom_get_ext_level,
    .set_func =  icom_set_func,
    .get_func =  icom_get_func,
    .set_parm =  icom_set_parm,
    .get_parm =  icom_get_parm,
    .set_mem =  icom_set_mem,
    .vfo_op =  icom_vfo_op,
    .scan =  icom_scan,
    .set_ptt =  icom_set_ptt,
    .get_ptt =  icom_get_ptt,
    .get_dcd =  icom_get_dcd,
    .set_ts =  icom_set_ts,
    .get_ts =  icom_get_ts,
    .set_ctcss_tone =  icom_set_ctcss_tone,
    .get_ctcss_tone =  icom_get_ctcss_tone,
    .set_ctcss_sql =  icom_set_ctcss_sql,
    .get_ctcss_sql =  icom_get_ctcss_sql,
    .set_split_freq =  icom_set_split_freq,
    .get_split_freq =  icom_get_split_freq,
    .set_split_mode =  icom_set_split_mode,
    .get_split_mode =  icom_get_split_mode,
    .set_split_vfo =  icom_set_split_vfo,
    .get_split_vfo =  icom_get_split_vfo,
    .set_powerstat = icom_set_powerstat,
    .get_powerstat = icom_get_powerstat,
    .send_morse = icom_send_morse
};

/*
 * IC-7800 has 0x11 command using index instead of backend's real dB value
 *
 * c.f. http://www.plicht.de/ekki/civ/civ-p42.html
 */
int ic7800_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val)
{
    unsigned char cmdbuf[MAXFRAMELEN];

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    switch (level)
    {
    case RIG_LEVEL_ATT:
        if (val.i != 0)
        {
            /* Convert dB to index */
            int i;

            for (i = 0; i < 7; i++)
            {
                if (val.i == rig->state.attenuator[i])
                {
                    val.i = i + 1;
                    break;
                }
            }

            /* TODO: Should fail when not found? */
        }

        return icom_set_level(rig, vfo, level, val);

    case RIG_LEVEL_VOXDELAY:
        cmdbuf[0] = 0x01;
        cmdbuf[1] = 0x83;
        return icom_set_level_raw(rig, level, C_CTL_MEM, 0x05, 2, cmdbuf, 1, val);

    default:
        return icom_set_level(rig, vfo, level, val);
    }
}

/*
 * IC-7800 has 0x11 command using index instead of backend's real dB value
 */
int ic7800_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val)
{
    unsigned char cmdbuf[MAXFRAMELEN];
    int retval;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    switch (level)
    {
    case RIG_LEVEL_ATT:
        retval = icom_get_level(rig, vfo, level, val);

        if (retval != RIG_OK)
        {
            return retval;
        }

        /* Convert index to dB
         * Rem: ATT index 0 means attenuator Off
         */
        if (val->i > 0 && val->i <= 7)
        {
            val->i = rig->state.attenuator[val->i - 1];
        }

        break;

    case RIG_LEVEL_VOXDELAY:
        cmdbuf[0] = 0x01;
        cmdbuf[1] = 0x83;
        return icom_get_level_raw(rig, level, C_CTL_MEM, 0x05, 2, cmdbuf, val);

    default:
        return icom_get_level(rig, vfo, level, val);
    }

    return RIG_OK;
}
