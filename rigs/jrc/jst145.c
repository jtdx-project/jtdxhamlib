/*
 *  Hamlib JRC backend - JST-145 description
 *  Copyright (c) 2001-2009 by Stephane Fillod
 *  Copyright (c) 2021 by Michael Black W9MDB
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
#include <string.h>

#include "hamlib/rig.h"
#include "iofunc.h"
#include "jrc.h"


static int jst145_init(RIG *rig);
static int jst145_open(RIG *rig);
static int jst145_close(RIG *rig);
static int jst145_set_vfo(RIG *rig, vfo_t vfo);
static int jst145_get_vfo(RIG *rig, vfo_t *vfo);
static int jst145_set_freq(RIG *rig, vfo_t vfo, freq_t freq);
static int jst145_get_freq(RIG *rig, vfo_t vfo, freq_t *freq);
static int jst145_set_mode(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width);
static int jst145_get_mode(RIG *rig, vfo_t vfo, rmode_t *mode,
                           pbwidth_t *width);
static int jst145_set_func(RIG *rig, vfo_t vfo, setting_t func, int status);
static int jst145_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val);
static int jst145_vfo_op(RIG *rig, vfo_t vfo, vfo_op_t op);
static int jst145_set_mem(RIG *rig, vfo_t vfo, int ch);
static int jst145_set_ptt(RIG *rig, vfo_t vfo, ptt_t ptt);
static int jst145_get_ptt(RIG *rig, vfo_t vfo, ptt_t *ptt);

#define MAX_LEN 24

#define JST145_MODES (RIG_MODE_AM|RIG_MODE_CW|RIG_MODE_SSB|RIG_MODE_FM|RIG_MODE_RTTY|RIG_MODE_FAX)

#define JST145_LEVEL (RIG_LEVEL_AGC)

// Rig has VFOB but for now we won't do much with it except set freq
#define JST145_VFO (RIG_VFO_VFO)

#define JST145_MEM_CAP { \
    .freq = 1,      \
    .mode = 1,      \
    .width = 1,     \
    .levels = RIG_LEVEL_AGC, \
}

struct jst145_priv_data
{
    ptt_t ptt;
    freq_t freqA, freqB;
    mode_t mode;
};


/*
 * JST-145 rig capabilities.
 *
 */
const struct rig_caps jst145_caps =
{
    RIG_MODEL(RIG_MODEL_JST145),
    .model_name = "JST-145",
    .mfg_name =  "JRC",
    .version =  BACKEND_VER ".2",
    .copyright =  "LGPL",
    .status =  RIG_STATUS_STABLE,
    .rig_type =  RIG_TYPE_TRANSCEIVER,
    .ptt_type =  RIG_PTT_RIG,
    .dcd_type =  RIG_DCD_NONE,
    .port_type =  RIG_PORT_SERIAL,
    .serial_rate_min =  4800,
    .serial_rate_max =  4800,
    .serial_data_bits =  8,
    .serial_stop_bits =  1,
    .serial_parity =  RIG_PARITY_NONE,
    .serial_handshake =  RIG_HANDSHAKE_NONE,
    .write_delay =  0,
    .post_write_delay =  0,
    .timeout =  1000,
    .retry =  1,

    .has_get_func =  RIG_FUNC_NONE,
    .has_set_func =  RIG_FUNC_NONE,
    .has_get_level =  RIG_LEVEL_NONE,
    .has_set_level =  JST145_LEVEL,
    .has_get_parm =  RIG_PARM_NONE,
    .has_set_parm =  RIG_PARM_NONE,
    .level_gran = {},
    .parm_gran =  {},
    .ctcss_list =  NULL,
    .dcs_list =  NULL,
    .preamp =   { RIG_DBLST_END },
    .attenuator =   { 20, RIG_DBLST_END },
    .max_rit =  Hz(0),
    .max_xit =  Hz(0),
    .max_ifshift =  Hz(0),
    .targetable_vfo =  RIG_TARGETABLE_FREQ,
    .transceive =  RIG_TRN_OFF,
    .vfo_ops =  RIG_OP_FROM_VFO,
    .scan_ops =  RIG_SCAN_NONE,
    .bank_qty =   0,
    .chan_desc_sz =  0,

    .chan_list =  {
        { 0, 199, RIG_MTYPE_MEM, JST145_MEM_CAP },
        RIG_CHAN_END
    },

    .rx_range_list1 =  {
        {kHz(100), MHz(30), JST145_MODES, -1, -1, JST145_VFO},
        RIG_FRNG_END,
    },
    .tx_range_list1 =  { RIG_FRNG_END, },
    .rx_range_list2 =  {
        {kHz(100), MHz(30), JST145_MODES, -1, -1, JST145_VFO},
        RIG_FRNG_END,
    },
    .tx_range_list2 =  { RIG_FRNG_END, },

    .tuning_steps =  {
        {JST145_MODES, 10},
        RIG_TS_END,
    },
    /* mode/filter list, .remember =  order matters! */
    .filters =  {
        {RIG_MODE_FM, kHz(12)},
        {RIG_MODE_FM, kHz(6)},
        {RIG_MODE_AM, kHz(6)},
        {RIG_MODE_AM, kHz(2)},
        {RIG_MODE_AM, kHz(12)},
        {RIG_MODE_SSB | RIG_MODE_RTTY | RIG_MODE_FAX, kHz(2)},
        {RIG_MODE_SSB | RIG_MODE_RTTY | RIG_MODE_FAX, kHz(1)},
        {RIG_MODE_SSB | RIG_MODE_RTTY | RIG_MODE_FAX, kHz(6)},
        {RIG_MODE_CW, kHz(1)},
        {RIG_MODE_CW, kHz(2)},
        RIG_FLT_END,
    },

    .rig_open =  jst145_open,
    .rig_close =  jst145_close,
    .set_vfo = jst145_set_vfo,
    .get_vfo = jst145_get_vfo,
    .set_freq =  jst145_set_freq,
    .get_freq =  jst145_get_freq,
    .set_mode =  jst145_set_mode,
    .get_mode =  jst145_get_mode,
    .set_func =  jst145_set_func,
    .set_level =  jst145_set_level,
    .set_mem =  jst145_set_mem,
    .vfo_op =  jst145_vfo_op,
    .set_ptt = jst145_set_ptt,
    .get_ptt = jst145_get_ptt
};

/*
 * JST-245 rig capabilities.
 *
 */
const struct rig_caps jst245_caps =
{
    RIG_MODEL(RIG_MODEL_JST245),
    .model_name = "JST-245",
    .mfg_name =  "JRC",
    .version =  BACKEND_VER ".2",
    .copyright =  "LGPL",
    .status =  RIG_STATUS_STABLE,
    .rig_type =  RIG_TYPE_TRANSCEIVER,
    .ptt_type =  RIG_PTT_RIG,
    .dcd_type =  RIG_DCD_NONE,
    .port_type =  RIG_PORT_SERIAL,
    .serial_rate_min =  4800,
    .serial_rate_max =  4800,
    .serial_data_bits =  8,
    .serial_stop_bits =  1,
    .serial_parity =  RIG_PARITY_NONE,
    .serial_handshake =  RIG_HANDSHAKE_NONE,
    .write_delay =  0,
    .post_write_delay =  20,
    .timeout =  1000,
    .retry =  0,

    .has_get_func =  RIG_FUNC_NONE,
    .has_set_func =  RIG_FUNC_NONE,
    .has_get_level =  RIG_LEVEL_NONE,
    .has_set_level =  JST145_LEVEL,
    .has_get_parm =  RIG_PARM_NONE,
    .has_set_parm =  RIG_PARM_NONE,
    .level_gran = {},
    .parm_gran =  {},
    .ctcss_list =  NULL,
    .dcs_list =  NULL,
    .preamp =   { RIG_DBLST_END },
    .attenuator =   { 20, RIG_DBLST_END },
    .max_rit =  Hz(0),
    .max_xit =  Hz(0),
    .max_ifshift =  Hz(0),
    .targetable_vfo =  RIG_TARGETABLE_FREQ,
    .transceive =  RIG_TRN_OFF,
    .vfo_ops =  RIG_OP_FROM_VFO,
    .scan_ops =  RIG_SCAN_NONE,
    .bank_qty =   0,
    .chan_desc_sz =  0,

    .chan_list =  {
        { 0, 199, RIG_MTYPE_MEM, JST145_MEM_CAP },
        RIG_CHAN_END
    },

    .rx_range_list1 =  {
        {kHz(100), MHz(54), JST145_MODES, -1, -1, JST145_VFO},
        RIG_FRNG_END,
    },
    .tx_range_list1 =  { RIG_FRNG_END, },
    .rx_range_list2 =  {
        {kHz(100), MHz(54), JST145_MODES, -1, -1, JST145_VFO},
        RIG_FRNG_END,
    },
    .tx_range_list2 =  { RIG_FRNG_END, },

    .tuning_steps =  {
        {JST145_MODES, 10},
        RIG_TS_END,
    },
    /* mode/filter list, .remember =  order matters! */
    .filters =  {
        {RIG_MODE_FM, kHz(12)},
        {RIG_MODE_FM, kHz(6)},
        {RIG_MODE_AM, kHz(6)},
        {RIG_MODE_AM, kHz(2)},
        {RIG_MODE_AM, kHz(12)},
        {RIG_MODE_SSB | RIG_MODE_RTTY | RIG_MODE_FAX, kHz(2)},
        {RIG_MODE_SSB | RIG_MODE_RTTY | RIG_MODE_FAX, kHz(1)},
        {RIG_MODE_SSB | RIG_MODE_RTTY | RIG_MODE_FAX, kHz(6)},
        {RIG_MODE_CW, kHz(1)},
        {RIG_MODE_CW, kHz(2)},
        RIG_FLT_END,
    },

    .rig_init = jst145_init,
    .rig_open =  jst145_open,
    .rig_close =  jst145_close,
    .set_vfo = jst145_set_vfo,
    .get_vfo = jst145_get_vfo,
    .set_freq =  jst145_set_freq,
    .get_freq =  jst145_get_freq,
    .set_mode =  jst145_set_mode,
    .get_mode =  jst145_get_mode,
    .set_func =  jst145_set_func,
    .set_level =  jst145_set_level,
    .set_mem =  jst145_set_mem,
    .vfo_op =  jst145_vfo_op,
    .set_ptt = jst145_set_ptt,
    .get_ptt = jst145_get_ptt
};

/*
 * Function definitions below
 */


static int jst145_init(RIG *rig)
{
    struct jst145_priv_data *priv;
    priv = (struct jst145_priv_data *)malloc(sizeof(struct jst145_priv_data));

    if (!priv)
    {
        return -RIG_ENOMEM;
    }

    rig->state.priv = (void *)priv;
    return RIG_OK;
}

static int jst145_open(RIG *rig)
{
    int retval;
    freq_t freq;
    rmode_t mode;
    pbwidth_t width;
    struct jst145_priv_data *priv = rig->state.priv;

    retval = write_block(&rig->state.rigport, "H1\r", 3);

    if (retval != RIG_OK)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: H1 failed: %s\n", __func__, rigerror(retval));
        return retval;
    }

    jst145_get_freq(rig, RIG_VFO_A, &freq);
    priv->freqA = freq;
    jst145_get_freq(rig, RIG_VFO_B, &freq);
    priv->freqB = freq;
    jst145_get_mode(rig, RIG_VFO_A, &mode, &width);
    priv->mode = mode;
    return retval;
}

static int jst145_close(RIG *rig)
{
    return write_block(&rig->state.rigport, "H0\r", 3);
}

static int jst145_set_vfo(RIG *rig, vfo_t vfo)
{
    char cmd[MAX_LEN];
    snprintf(cmd, sizeof(cmd), "F%c\r", vfo == RIG_VFO_A ? 'A' : 'B');

    return write_block(&rig->state.rigport, cmd, strlen(cmd));
}

static int jst145_get_vfo(RIG *rig, vfo_t *vfo)
{
    char cmd[MAX_LEN];
    char channel[MAX_LEN];
    int channel_size = sizeof(channel);
    int retval;
    ptt_t ptt;

    jst145_get_ptt(rig, RIG_VFO_A, &ptt); // set priv->ptt to current transmit status

    if (ptt)  // can't get vfo while transmitting
    {
        *vfo = RIG_VFO_A;
        return RIG_OK;
    }

    snprintf(cmd, sizeof(cmd), "L\r");

    retval = jrc_transaction(rig, cmd, strlen(cmd), channel, &channel_size);

    if (retval != RIG_OK)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: jrc_transaction error: %s\n", __func__,
                  rigerror(retval));
        return retval;
    }

    *vfo = channel[1] == 'A' ? RIG_VFO_A : RIG_VFO_B;

    return RIG_OK;
}

static int jst145_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
    char freqbuf[MAX_LEN];
    struct jst145_priv_data *priv = rig->state.priv;

    snprintf(freqbuf, sizeof(freqbuf), "F%08u%c\r", (unsigned)(freq),
             vfo == RIG_VFO_A ? 'A' : 'B');

    if (vfo == RIG_VFO_B)
    {
        priv->freqB = freq;
    }
    else
    {
        priv->freqA = freq;
    }

    return write_block(&rig->state.rigport, freqbuf, strlen(freqbuf));
}


static int jst145_get_freq(RIG *rig, vfo_t vfo, freq_t *freq)
{
    char freqbuf[MAX_LEN];
    char cmd[MAX_LEN];
    int freqbuf_size = sizeof(freqbuf);
    int retval;
    int n;

    struct jst145_priv_data *priv = rig->state.priv;

    if (!priv->ptt)  // can't get freq while transmitting
    {
        jst145_set_vfo(rig, vfo);
        //*freq = vfo == RIG_VFO_B ? priv->freqB : priv->freqA;
        //return RIG_OK;
    }

    snprintf(cmd, sizeof(cmd), "I\r");
    retval = jrc_transaction(rig, cmd, strlen(cmd), freqbuf, &freqbuf_size);

    if (retval != RIG_OK)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: jrc_transaction error: %s\n", __func__,
                  rigerror(retval));
        return retval;
    }

    n = sscanf(freqbuf, "I%*c%*c%*c%8lf", freq);

    if (n != 1) { retval = -RIG_EPROTO; }

    return retval;
}

static int jst145_set_mode(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
{
    int retval;
    char *modestr;
    struct jst145_priv_data *priv = rig->state.priv;

    switch (mode)
    {
    case RIG_MODE_RTTY: modestr = "D0\r"; break;

    case RIG_MODE_CW: modestr = "D1\r"; break;

    case RIG_MODE_USB: modestr = "D2\r"; break;

    case RIG_MODE_LSB: modestr = "D3\r"; break;

    case RIG_MODE_AM: modestr = "D4\r"; break;

    case RIG_MODE_FM: modestr = "D5\r"; break;

    default:
        return -RIG_EINVAL;
    }

    retval = write_block(&rig->state.rigport, modestr, strlen(modestr));

    if (retval != RIG_OK)
    {
        return retval;
    }

    priv->mode = mode;

    if (RIG_PASSBAND_NOCHANGE == width) { return retval; }


    // TODO: width -- could use B command but let user handle it for now

    return retval;
}

static int jst145_get_mode(RIG *rig, vfo_t vfo, rmode_t *mode, pbwidth_t *width)
{
    char cmd[MAX_LEN];
    char modebuf[MAX_LEN];
    int modebuf_len = sizeof(modebuf);
    int retval;

    snprintf(cmd, sizeof(cmd), "I\r");

    retval = jrc_transaction(rig, cmd, strlen(cmd), modebuf, &modebuf_len);

    if (retval != RIG_OK)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: jrc_transcation failed: %s\n", __func__,
                  rigerror(retval));
    }

    switch (modebuf[3])
    {
    case '0': *mode = RIG_MODE_RTTY; break;

    case '1': *mode = RIG_MODE_CW; break;

    case '2': *mode = RIG_MODE_USB; break;

    case '3': *mode = RIG_MODE_LSB; break;

    case '4': *mode = RIG_MODE_AM; break;

    case '5': *mode = RIG_MODE_FM; break;
    }

    return retval;
}

static int jst145_set_func(RIG *rig, vfo_t vfo, setting_t func, int status)
{
    return -RIG_ENIMPL;
}

static int jst145_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val)
{
    switch (level)
    {

    case RIG_LEVEL_AGC:
        return write_block(&rig->state.rigport,
                           val.i == RIG_AGC_SLOW ? "G0\r" :
                           (val.i == RIG_AGC_FAST ? "G1\r" : "G2\r"), 3);

    default:
        return -RIG_EINVAL;
    }

    return -RIG_EINVAL;
}

static int jst145_set_mem(RIG *rig, vfo_t vfo, int ch)
{
    char membuf[MAX_LEN];

    sprintf(membuf, "C%03d\r", ch);

    return write_block(&rig->state.rigport, membuf, strlen(membuf));
}

static int jst145_vfo_op(RIG *rig, vfo_t vfo, vfo_op_t op)
{
    switch (op)
    {
    case RIG_OP_FROM_VFO:
        return write_block(&rig->state.rigport, "E1\r", 3);

    default:
        return -RIG_EINVAL;
    }

    return -RIG_EINVAL;
}

static int jst145_set_ptt(RIG *rig, vfo_t vfo, ptt_t ptt)
{
    char cmd[MAX_LEN];
    struct jst145_priv_data *priv = rig->state.priv;
    rig_debug(RIG_DEBUG_TRACE, "%s: entered\n", __func__);
    snprintf(cmd, sizeof(cmd), "X%c\r", ptt ? '1' : '0');
    priv->ptt = ptt;
    return write_block(&rig->state.rigport, cmd, strlen(cmd));
}

static int jst145_get_ptt(RIG *rig, vfo_t vfo, ptt_t *ptt)
{
    char cmd[MAX_LEN];
    char pttstatus[MAX_LEN];
    int pttstatus_size = sizeof(pttstatus);
    int retval;
    struct jst145_priv_data *priv = rig->state.priv;

    rig_debug(RIG_DEBUG_TRACE, "%s: entered\n", __func__);

    snprintf(cmd, sizeof(cmd), "X\r");
    retval = jrc_transaction(rig, cmd, strlen(cmd), pttstatus, &pttstatus_size);

    if (retval != RIG_OK)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: jrc_transaction error: %s\n", __func__,
                  rigerror(retval));
        return retval;
    }

    if (pttstatus[1] == '1') { *ptt = RIG_PTT_ON; }
    else { *ptt = RIG_PTT_OFF; }
    priv->ptt = *ptt;

    return RIG_OK;
}
