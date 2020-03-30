/*
 *  Hamlib Tuner backend - Video4Linux (v2) description
 *  Copyright (c) 2004-2011 by Stephane Fillod
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

#include <stdlib.h>
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <math.h>
#include <errno.h>

#include "hamlib/rig.h"
#include "tuner.h"  /* include config.h */

#include "misc.h"

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef V4L_IOCTL

#include <stdlib.h>
#include "idx_builtin.h"


#define V4L2_FUNC (RIG_FUNC_MUTE)

#define V4L2_LEVEL_ALL (RIG_LEVEL_AF|RIG_LEVEL_RAWSTR)

#define V4L2_PARM_ALL (RIG_PARM_NONE)

#define V4L2_VFO (RIG_VFO_A)

/* FIXME: per card measures? */
#define V4L2_STR_CAL { 2, { \
                    {     0, -60 }, \
                    { 65535, 60 }, \
            } }

static int v4l2_init(RIG *rig);
static int v4l2_open(RIG *rig);
static int v4l2_set_freq(RIG *rig, vfo_t vfo, freq_t freq);
static int v4l2_get_freq(RIG *rig, vfo_t vfo, freq_t *freq);
static int v4l2_set_func(RIG *rig, vfo_t vfo, setting_t func, int status);
static int v4l2_get_func(RIG *rig, vfo_t vfo, setting_t func, int *status);
static int v4l2_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val);
static int v4l2_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val);
static const char *v4l2_get_info(RIG *rig);

/*
 * v4l (v1) rig capabilities.
 *
 *
 */
const struct rig_caps v4l2_caps =
{
    RIG_MODEL(RIG_MODEL_V4L2),
    .model_name = "SW/FM radio",
    .mfg_name =  "Video4Linux2",
    .version =  "20191223.0",
    .copyright =  "LGPL",
    .status =  RIG_STATUS_ALPHA,
    .rig_type =  RIG_TYPE_PCRECEIVER,
    .ptt_type =  RIG_PTT_NONE,
    .dcd_type =  RIG_DCD_NONE,
    .port_type =  RIG_PORT_DEVICE,
    .write_delay =  0,
    .post_write_delay =  0,
    .timeout =  2000,
    .retry =  1,

    .has_get_func =  V4L2_FUNC,
    .has_set_func =  V4L2_FUNC,
    .has_get_level =  V4L2_LEVEL_ALL,
    .has_set_level =  RIG_LEVEL_SET(V4L2_LEVEL_ALL),
    .has_get_parm =  V4L2_PARM_ALL,
    .has_set_parm =  RIG_PARM_SET(V4L2_PARM_ALL),
    .vfo_ops =  RIG_OP_NONE,
    .level_gran = {
        [LVL_RAWSTR] = { .min = { .i = 0 }, .max = { .i = 65535 } },
    },
    .preamp =   { RIG_DBLST_END },
    .attenuator =   { RIG_DBLST_END },
    .max_rit =  Hz(0),
    .max_xit =  Hz(0),
    .max_ifshift =  Hz(0),
    .targetable_vfo =  0,
    .transceive =  RIG_TRN_OFF,
    .bank_qty =   0,
    .chan_desc_sz =  0,

    .chan_list =  { RIG_CHAN_END, },

    /* will be rewritten at runtime */
    .rx_range_list1 =  {
        {MHz(87.9), MHz(108.9), RIG_MODE_WFM, -1, -1, V4L2_VFO},
        RIG_FRNG_END,
    },
    .tx_range_list1 =  { RIG_FRNG_END, },
    .rx_range_list2 =  {
        {MHz(87.9), MHz(108.9), RIG_MODE_WFM, -1, -1, V4L2_VFO},
        RIG_FRNG_END,
    },
    .tx_range_list2 =  { RIG_FRNG_END, },
    .tuning_steps =  {
        {RIG_MODE_WFM, 100},
        RIG_TS_END,
    },
    /* mode/filter list, remember: order matters! */
    .filters =  {
        {RIG_MODE_WFM, kHz(230)},   /* guess */
        {RIG_MODE_AM, kHz(8)},  /* guess */
        RIG_FLT_END,
    },
    .str_cal = V4L2_STR_CAL,

    .rig_init =  v4l2_init,
    .rig_open =  v4l2_open,

    .set_freq =  v4l2_set_freq,
    .get_freq =  v4l2_get_freq,
    .set_func =  v4l2_set_func,
    .get_func =  v4l2_get_func,
    .set_level =  v4l2_set_level,
    .get_level =  v4l2_get_level,

    .get_info =  v4l2_get_info,

};

/*
 * Function definitions below
 */


#include "videodev2.h"


#define DEFAULT_V4L2_PATH "/dev/radio0"

int v4l2_init(RIG *rig)
{
    rig->state.rigport.type.rig = RIG_PORT_DEVICE;
    strncpy(rig->state.rigport.pathname, DEFAULT_V4L2_PATH, FILPATHLEN - 1);

    return RIG_OK;
}

int v4l2_open(RIG *rig)
{
    int i;
    struct v4l2_tuner vt;
    struct rig_state *rs = &rig->state;

    for (i = 0; i < 8; i++)
    {
        int ret;
        double fact;
        vt.index = i;
        ret = ioctl(rig->state.rigport.fd, VIDIOC_G_TUNER, &vt);

        if (ret < 0)
        {
            break;
        }

        fact = (vt.capability & V4L2_TUNER_CAP_LOW) == 0 ? 16 : 16000;
        rs->rx_range_list[i].startf = vt.rangelow / fact;
        rs->rx_range_list[i].endf = vt.rangehigh / fact;
        rs->rx_range_list[i].modes = vt.rangehigh / fact < MHz(30) ? RIG_MODE_AM :
                                     RIG_MODE_WFM;
        /* hack! hack! store the resolution in low_power! */
        rs->rx_range_list[i].low_power = rint(fact);
    }

    return RIG_OK;
}


int v4l2_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
    struct rig_state *rs = &rig->state;
    struct      v4l2_tuner vt;
    const freq_range_t *range;
    unsigned long f;
    double fact;
    int ret;

    /* AM or WFM */
    range = rig_get_range(rs->rx_range_list, freq, RIG_MODE_AM | RIG_MODE_WFM);

    if (!range)
    {
        return -RIG_ECONF;
    }

    /* at this point, we are trying to tune to a frequency */

    vt.index = (rs->rx_range_list - range) / sizeof(freq_range_t);

    ret = ioctl(rig->state.rigport.fd, VIDIOC_S_TUNER, &vt);    /* set tuner # */

    if (ret < 0)
    {
        rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_S_TUNER: %s\n",
                  strerror(errno));
        return -RIG_EIO;
    }

    fact = range->low_power;

    f = rint(freq * fact);  /* rounding to nearest int */

    ret = ioctl(rig->state.rigport.fd, VIDIOC_S_FREQUENCY, &f);

    if (ret < 0)
    {
        rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_S_FREQUENCY: %s\n",
                  strerror(errno));
        return -RIG_EIO;
    }

    return RIG_OK;
}

int v4l2_get_freq(RIG *rig, vfo_t vfo, freq_t *freq)
{
    struct rig_state *rs = &rig->state;
    const freq_range_t *range;
    unsigned long f;
    double fact;
    int ret;

    /* FIXME */
    ret = ioctl(rig->state.rigport.fd, VIDIOC_G_FREQUENCY, &f);

    if (ret < 0)
    {
        rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_G_FREQUENCY: %s\n",
                  strerror(errno));
        return -RIG_EIO;
    }

    /* FIXME: remember tuner and current *fact* */
    range = rig_get_range(rs->rx_range_list, f / 16, RIG_MODE_AM | RIG_MODE_WFM);

    if (!range)
    {
        return -RIG_ECONF;
    }

    fact = range->low_power;


    *freq = f / fact;

    return RIG_OK;
}

int v4l2_set_func(RIG *rig, vfo_t vfo, setting_t func, int status)
{
    struct v4l2_audio va;
    int ret;

    switch (func)
    {
    case RIG_FUNC_MUTE:
        ret = ioctl(rig->state.rigport.fd, VIDIOC_G_AUDIO, &va);

        if (ret < 0)
        {
            rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_G_AUDIO: %s\n",
                      strerror(errno));
            return -RIG_EIO;
        }

        va.capability = status ? V4L2_CID_AUDIO_MUTE : 0;
        ret = ioctl(rig->state.rigport.fd, VIDIOC_S_AUDIO, &va);

        if (ret < 0)
        {
            rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_S_AUDIO: %s\n",
                      strerror(errno));
            return -RIG_EIO;
        }

        break;

    default:
        return -RIG_EINVAL;
    }

    return RIG_OK;
}

int v4l2_get_func(RIG *rig, vfo_t vfo, setting_t func, int *status)
{
    struct v4l2_audio va;
    int ret;

    switch (func)
    {
    case RIG_FUNC_MUTE:
        ret = ioctl(rig->state.rigport.fd, VIDIOC_G_AUDIO, &va);

        if (ret < 0)
        {
            rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_G_AUDIO: %s\n",
                      strerror(errno));
            return -RIG_EIO;
        }

        *status = (va.capability & V4L2_CID_AUDIO_MUTE) == V4L2_CID_AUDIO_MUTE ;
        break;

    default:
        return -RIG_EINVAL;
    }

    return RIG_OK;
}


int v4l2_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val)
{
    struct v4l2_audio va;
    int ret;

    ret = ioctl(rig->state.rigport.fd, VIDIOC_G_AUDIO, &va);

    if (ret < 0)
    {
        rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_G_AUDIO: %s\n",
                  strerror(errno));
        return -RIG_EIO;
    }

// TODO RIG_LEVEL_AGC:V4L2_CID_AUTOGAIN & RIG_LEVEL_RF:V4L2_CID_GAIN
//V4L2_CID_AUDIO_VOLUME
    switch (level)
    {
    case RIG_LEVEL_AF:
        //va.volume = val.f * 65535;
        break;

    default:
        return -RIG_EINVAL;
    }

    ret = ioctl(rig->state.rigport.fd, VIDIOC_S_AUDIO, &va);

    if (ret < 0)
    {
        rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_S_AUDIO: %s\n",
                  strerror(errno));
        return -RIG_EIO;
    }

    return RIG_OK;
}

int v4l2_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val)
{
    struct v4l2_audio va;
    struct v4l2_tuner vt;
    int ret;

    switch (level)
    {
    case RIG_LEVEL_AF:
        ret = ioctl(rig->state.rigport.fd, VIDIOC_G_AUDIO, &va);

        if (ret < 0)
        {
            rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_G_AUDIO: %s\n",
                      strerror(errno));
            return -RIG_EIO;
        }

        //val->f = (float)va.volume / 65535.;
        break;

    case RIG_LEVEL_RAWSTR:
        /* FE_READ_SIGNAL_STRENGTH ? */
        ret = ioctl(rig->state.rigport.fd, VIDIOC_G_TUNER, &vt);    /* get info */

        if (ret < 0)
        {
            rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_G_TUNER: %s\n",
                      strerror(errno));
            return -RIG_EIO;
        }

        val->i = vt.signal;
        break;

    default:
        return -RIG_EINVAL;
    }

    return RIG_OK;
}

/*
 * FIXME: static buf does not allow reentrancy!
 */
const char *v4l2_get_info(RIG *rig)
{
    static struct v4l2_tuner vt;
    int ret;

    vt.index = 0;
    ret = ioctl(rig->state.rigport.fd, VIDIOC_G_TUNER, &vt);

    if (ret < 0)
    {
        rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOC_G_TUNER: %s\n",
                  strerror(errno));
        return "Get info failed";
    }

    return (const char *)vt.name;
}

#endif  /* V4L_IOCTL */

