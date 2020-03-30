/*
 * hamlib - (C) Frank Singleton 2000 (vk3fcs@ix.netcom.com)
 *          (C) Stephane Fillod 2000-2010
 *
 * ft747.c - (C) Frank Singleton 2000 (vk3fcs@ix.netcom.com)
 * This shared library provides an API for communicating
 * via serial interface to an FT-747GX using the "CAT" interface
 * box (FIF-232C) or similar
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


/*
 * TODO -   FS
 *
 * 1. rationalise code, more helper functions [started]
 * 2. get_channel, set_func/get_func
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */

#include "hamlib/rig.h"
#include "serial.h"
#include "misc.h"
#include "yaesu.h"
#include "ft747.h"


/* Private helper function prototypes */

static int ft747_get_update_data(RIG *rig);
static int ft747_send_priv_cmd(RIG *rig, unsigned char ci);

/* Native ft747 cmd set prototypes. These are READ ONLY as each */
/* rig instance will copy from these and modify if required . */
/* Complete sequences (1) can be read and used directly as a cmd sequence . */
/* Incomplete sequences (0) must be completed with extra parameters */
/* eg: mem number, or freq etc.. */

static const yaesu_cmd_set_t ft747_ncmd[] =
{
    { 1, { 0x00, 0x00, 0x00, 0x00, 0x01 } }, /* split = off */
    { 1, { 0x00, 0x00, 0x00, 0x01, 0x01 } }, /* split = on */
    { 0, { 0x00, 0x00, 0x00, 0x00, 0x02 } }, /* recall memory*/
    { 0, { 0x00, 0x00, 0x00, 0x00, 0x03 } }, /* vfo to  memory*/
    { 1, { 0x00, 0x00, 0x00, 0x00, 0x04 } }, /* dial lock = off */
    { 1, { 0x00, 0x00, 0x00, 0x01, 0x04 } }, /* dial lock = on */
    { 1, { 0x00, 0x00, 0x00, 0x00, 0x05 } }, /* select vfo A */
    { 1, { 0x00, 0x00, 0x00, 0x01, 0x05 } }, /* select vfo B */
    { 0, { 0x00, 0x00, 0x00, 0x00, 0x06 } }, /* memory to vfo*/
    { 1, { 0x00, 0x00, 0x00, 0x00, 0x07 } }, /* up 500 khz */
    { 1, { 0x00, 0x00, 0x00, 0x00, 0x08 } }, /* down 500 khz */
    { 1, { 0x00, 0x00, 0x00, 0x00, 0x09 } }, /* clarify off */
    { 1, { 0x00, 0x00, 0x00, 0x01, 0x09 } }, /* clarify on */
    { 0, { 0x00, 0x00, 0x00, 0x00, 0x0a } }, /* set freq */

    { 1, { 0x00, 0x00, 0x00, 0x00, 0x0c } }, /* mode set LSB */
    { 1, { 0x00, 0x00, 0x00, 0x01, 0x0c } }, /* mode set USB */
    { 1, { 0x00, 0x00, 0x00, 0x02, 0x0c } }, /* mode set CWW */
    { 1, { 0x00, 0x00, 0x00, 0x03, 0x0c } }, /* mode set CWN */
    { 1, { 0x00, 0x00, 0x00, 0x04, 0x0c } }, /* mode set AMW */
    { 1, { 0x00, 0x00, 0x00, 0x05, 0x0c } }, /* mode set AMN */
    { 1, { 0x00, 0x00, 0x00, 0x06, 0x0c } }, /* mode set FMW */
    { 1, { 0x00, 0x00, 0x00, 0x07, 0x0c } }, /* mode set FMN */

    { 0, { 0x00, 0x00, 0x00, 0x00, 0x0e } }, /* pacing set */
    { 1, { 0x00, 0x00, 0x00, 0x00, 0x0f } }, /* ptt off */
    { 1, { 0x00, 0x00, 0x00, 0x01, 0x0f } }, /* ptt on */
    { 1, { 0x00, 0x00, 0x00, 0x00, 0x10 } }, /* request update from rig */

};



/*
 * Receiver caps
 */


#define FT747_ALL_RX_MODES (RIG_MODE_AM|RIG_MODE_CW|RIG_MODE_USB|RIG_MODE_LSB)
#define FT747_SSB_CW_RX_MODES (RIG_MODE_CW|RIG_MODE_USB|RIG_MODE_LSB)
#define FT747_AM_RX_MODES (RIG_MODE_AM)
#define FT747_FM_RX_MODES (RIG_MODE_FM)


/*
 * TX caps
 */

#define FT747_OTHER_TX_MODES (RIG_MODE_CW| RIG_MODE_USB| RIG_MODE_LSB ) /* 100 W class */
#define FT747_AM_TX_MODES (RIG_MODE_AM ) /* set 25W max */

/*
 * no opcode for RIG_FUNC_FAGC|RIG_FUNC_NB|RIG_FUNC_COMP|RIG_FUNC_VOX|RIG_FUNC_TONE|RIG_FUNC_TSQL|RIG_FUNC_SBKIN|RIG_FUNC_FBKIN
 * TODO: LOCK
 */
#define FT747_FUNC_ALL (RIG_FUNC_LOCK)


#define FT747_VFOS (RIG_VFO_A|RIG_VFO_B)

/*
 * FT747 channel caps.
 * Last 2 channels don't have split memory
 */
#define FT747_SPLIT_MEM_CAP {    \
        .freq = 1,  \
        .mode = 1,  \
        .width = 1, \
        .split = 1, \
        .tx_freq = 1,   \
        .tx_mode = 1,   \
        .tx_width = 1,  \
        .funcs = RIG_FUNC_LOCK,  \
}

#define FT747_NOSPLIT_MEM_CAP {    \
        .freq = 1,  \
        .mode = 1,  \
        .width = 1, \
        .funcs = RIG_FUNC_LOCK,  \
}

/*
 * Private data
 *
 */

struct ft747_priv_data
{
    unsigned char
    p_cmd[YAESU_CMD_LENGTH]; /* private copy of 1 constructed CAT cmd */
    unsigned char update_data[FT747_STATUS_UPDATE_DATA_LENGTH]; /* returned data */
    struct timeval status_tv;
};


/*
 * ft747 rigs capabilities.
 * Also this struct is READONLY!
 */

const struct rig_caps ft747_caps =
{
    RIG_MODEL(RIG_MODEL_FT747),
    .model_name =       "FT-747GX",
    .mfg_name =         "Yaesu",
    .version =           "20200323.0",
    .copyright =         "LGPL",
    .status =            RIG_STATUS_BETA,
    .rig_type =          RIG_TYPE_MOBILE,
    .ptt_type =          RIG_PTT_RIG,
    .dcd_type =          RIG_DCD_NONE,
    .port_type =         RIG_PORT_SERIAL,
    .serial_rate_min =   4800,
    .serial_rate_max =   4800,
    .serial_data_bits =  8,
    .serial_stop_bits =  2,
    .serial_parity =     RIG_PARITY_NONE,
    .serial_handshake =  RIG_HANDSHAKE_NONE,
    .write_delay =       FT747_WRITE_DELAY,
    .post_write_delay =  FT747_POST_WRITE_DELAY,
    .timeout =           2000,
    .retry =             0,
    .has_get_func =      FT747_FUNC_ALL,
    .has_set_func =      FT747_FUNC_ALL,
    .has_get_level =     RIG_LEVEL_NONE,
    .has_set_level =     RIG_LEVEL_NONE,
    .has_get_parm =      RIG_PARM_NONE,
    .has_set_parm =      RIG_PARM_NONE,
    .ctcss_list =        NULL,
    .dcs_list =          NULL,
    .preamp =            { RIG_DBLST_END, },
    .attenuator =        { RIG_DBLST_END, },
    .max_rit =           Hz(0), /* 9999 */
    .max_xit =           Hz(0),
    .max_ifshift =       Hz(0),
    .targetable_vfo =    0,
    .transceive =        RIG_TRN_OFF,
    .bank_qty =          0,
    .chan_desc_sz =      0,
    .chan_list =         {
        {   0,  17, RIG_MTYPE_MEM, FT747_SPLIT_MEM_CAP },
        {  18,  19, RIG_MTYPE_MEM, FT747_NOSPLIT_MEM_CAP },
        RIG_CHAN_END,
    },

    .rx_range_list1 =    { RIG_FRNG_END, },    /* FIXME: enter region 1 setting */

    .tx_range_list1 =    { RIG_FRNG_END, },

    .rx_range_list2 =    { {
            .startf = kHz(100), .endf = 29999900,
            .modes = FT747_ALL_RX_MODES, .low_power = -1, .high_power = -1, .vfo = FT747_VFOS
        },
        RIG_FRNG_END,
    }, /* rx range */

    .tx_range_list2 =    { {kHz(1500), 1999900, FT747_OTHER_TX_MODES, .low_power = 5000, .high_power = 100000, .vfo = FT747_VFOS}, /* 100W class */

        {.startf = kHz(1500), .endf = 1999900, FT747_AM_TX_MODES, .low_power = 2000, .high_power = 25000, .vfo = FT747_VFOS}, /* 25W class */

        {.startf = kHz(3500), 3999900, FT747_OTHER_TX_MODES, 5000, 100000, FT747_VFOS},
        {.startf = kHz(3500), 3999900, FT747_AM_TX_MODES, 2000, 25000, FT747_VFOS},

        {.startf = kHz(7000), 7499900, FT747_OTHER_TX_MODES, 5000, 100000, FT747_VFOS},
        {.startf = kHz(7000), 7499900, FT747_AM_TX_MODES, 2000, 25000, FT747_VFOS},

        {.startf = MHz(10), 10499900, FT747_OTHER_TX_MODES, 5000, 100000, FT747_VFOS},
        {.startf = MHz(10), 10499900, FT747_AM_TX_MODES, 2000, 25000, FT747_VFOS},

        {.startf = MHz(14), 14499900, FT747_OTHER_TX_MODES, 5000, 100000, FT747_VFOS},
        {.startf = MHz(14), 14499900, FT747_AM_TX_MODES, 2000, 25000, FT747_VFOS},

        {.startf = MHz(18), 18499900, FT747_OTHER_TX_MODES, 5000, 100000, FT747_VFOS},
        {.startf = MHz(18), 18499900, FT747_AM_TX_MODES, 2000, 25000, FT747_VFOS},

        {.startf = MHz(21), 21499900, FT747_OTHER_TX_MODES, 5000, 100000, FT747_VFOS},
        {.startf = MHz(21), 21499900, FT747_AM_TX_MODES, 2000, 25000, FT747_VFOS},

        {.startf = kHz(24500), 24999900, FT747_OTHER_TX_MODES, 5000, 100000, FT747_VFOS},
        {.startf = kHz(24500), 24999900, FT747_AM_TX_MODES, 2000, 25000, FT747_VFOS},

        {.startf = MHz(28), 29999900, FT747_OTHER_TX_MODES, 5000, 100000, FT747_VFOS},
        {.startf = MHz(28), 29999900, FT747_AM_TX_MODES, 2000, 25000, FT747_VFOS},

        RIG_FRNG_END,
    },



    .tuning_steps =    { {FT747_SSB_CW_RX_MODES, 25}, /* fast off */
        {FT747_SSB_CW_RX_MODES, 2500}, /* fast on */

        {FT747_AM_RX_MODES, kHz(1)}, /* fast off */
        {FT747_AM_RX_MODES, kHz(10)}, /* fast on */

        {FT747_FM_RX_MODES, kHz(5)}, /* fast off */
        {FT747_FM_RX_MODES, 12500}, /* fast on */

        RIG_TS_END,
    },

    /* mode/filter list, .remember =  order matters! */

    .filters =    { {RIG_MODE_SSB, kHz(2.2)}, /* standard SSB filter bandwidth */
        {RIG_MODE_CW, kHz(1.8)}, /* normal CW filter */
        {RIG_MODE_CW, kHz(0.5)}, /* CW filter with narrow selection */
        {RIG_MODE_AM, kHz(6)},   /* normal AM filter */
        {RIG_MODE_AM, kHz(2.4)}, /* AM filter with narrow selection */
        {RIG_MODE_FM, kHz(19)},  /* FM wide filter, with optional FM unit. */
        {RIG_MODE_FM, kHz(8)},   /* FM with optional FM unit */

        RIG_FLT_END,
    },


    .priv =   NULL, /* private data */

    .rig_init =   ft747_init,
    .rig_cleanup =    ft747_cleanup,
    .rig_open =   ft747_open,             /* port opened */
    .rig_close =  ft747_close,                /* port closed */

    .set_freq =   ft747_set_freq,     /* set freq */
    .get_freq =   ft747_get_freq,     /* get freq */
    .set_mode =   ft747_set_mode,     /* set mode */
    .get_mode =   ft747_get_mode,     /* get mode */
    .set_vfo =    ft747_set_vfo,      /* set vfo */
    .get_vfo =    ft747_get_vfo,      /* get vfo */
    .set_split_vfo = ft747_set_split, /* set split */
    .get_split_vfo = ft747_get_split, /* get split */
    .set_ptt =    ft747_set_ptt,      /* set ptt */
    .set_mem =    ft747_set_mem,      /* set mem */
    .get_mem =    ft747_get_mem,      /* get mem */
};


/*
 * _init
 *
 */


int ft747_init(RIG *rig)
{
    rig->state.priv = (struct ft747_priv_data *) calloc(1,
                      sizeof(struct ft747_priv_data));

    if (!rig->state.priv)           /* whoops! memory shortage! */
    {
        return -RIG_ENOMEM;
    }

    rig_debug(RIG_DEBUG_VERBOSE, "%s: called\n", __func__);

    return RIG_OK;
}


/*
 * ft747_cleanup routine
 * the serial port is closed by the frontend
 */

int ft747_cleanup(RIG *rig)
{

    rig_debug(RIG_DEBUG_VERBOSE, "%s: called\n", __func__);

    if (rig->state.priv)
    {
        free(rig->state.priv);
    }

    rig->state.priv = NULL;

    return RIG_OK;
}

/*
 * ft747_open  routine
 *
 */

int ft747_open(RIG *rig)
{
    struct rig_state *rig_s;
    struct ft747_priv_data *p;
    int ret;


    rig_s = &rig->state;
    p = (struct ft747_priv_data *)rig_s->priv;

    rig_debug(RIG_DEBUG_VERBOSE, "ft747:rig_open: write_delay = %i msec \n",
              rig_s->rigport.write_delay);
    rig_debug(RIG_DEBUG_VERBOSE, "ft747:rig_open: post_write_delay = %i msec \n",
              rig_s->rigport.post_write_delay);

    /*
    * Copy native cmd PACING  to private cmd storage area
    */

    memcpy(&p->p_cmd, &ft747_ncmd[FT_747_NATIVE_PACING].nseq, YAESU_CMD_LENGTH);
    p->p_cmd[3] =
        FT747_PACING_DEFAULT_VALUE;     /* get pacing value, and store in private cmd */
    rig_debug(RIG_DEBUG_VERBOSE, "ft747: read pacing = %i \n",
              FT747_PACING_DEFAULT_VALUE);

    /* send PACING cmd to rig, once for all */

    ret = write_block(&rig_s->rigport, (char *)p->p_cmd, YAESU_CMD_LENGTH);

    if (ret < 0)
    {
        return ret;
    }

    rig_force_cache_timeout(&p->status_tv);

    return RIG_OK;
}


/*
 * ft747_close  routine
 *
 */

int ft747_close(RIG *rig)
{

    rig_debug(RIG_DEBUG_VERBOSE, "%s: called \n", __func__);

    return RIG_OK;
}


/*
 * Example of wrapping backend function inside frontend API
 *
 */


int ft747_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
    struct rig_state *rig_s;
    struct ft747_priv_data *p;
    unsigned char *cmd;       /* points to sequence to send */
    // cppcheck-suppress *
    char *fmt = "%s: requested freq after conversion = %"PRIll" Hz \n";

    p = (struct ft747_priv_data *)rig->state.priv;

    rig_s = &rig->state;

    rig_debug(RIG_DEBUG_VERBOSE, "ft747: requested freq = %"PRIfreq" Hz \n", freq);

    /*
     * Copy native cmd freq_set to private cmd storage area
     */

    memcpy(&p->p_cmd, &ft747_ncmd[FT_747_NATIVE_FREQ_SET].nseq, YAESU_CMD_LENGTH);

    /* store bcd format in p_cmd (LSB), round to nearest 10 Hz even
       though the rig will internally then round to 25 Hz steps */
    to_bcd(p->p_cmd, (freq + 5) / 10, 8);

    rig_debug(RIG_DEBUG_VERBOSE, fmt, __func__, (int64_t)from_bcd(p->p_cmd,
              8) * 10);

    rig_force_cache_timeout(&p->status_tv);

    cmd = p->p_cmd; /* get native sequence */
    return write_block(&rig_s->rigport, (char *) cmd, YAESU_CMD_LENGTH);
}


/*
 * Return Freq for a given VFO
 */

int ft747_get_freq(RIG *rig, vfo_t vfo, freq_t *freq)
{
    struct ft747_priv_data *p;
    freq_t f;
    int ret;

    rig_debug(RIG_DEBUG_VERBOSE, "%s: called\n", __func__);

    p = (struct ft747_priv_data *)rig->state.priv;

    ret = ft747_get_update_data(rig); /* get whole shebang from rig */

    if (ret < 0)
    {
        return ret;
    }

    switch (vfo)
    {
    case RIG_VFO_CURR:
        /* grab freq and convert */
        f = from_bcd_be(&(p->update_data[FT747_SUMO_DISPLAYED_FREQ]), 10);
        break;

    case RIG_VFO_A:
        f = from_bcd_be(&(p->update_data[FT747_SUMO_VFO_A_FREQ]), 10);
        break;

    case RIG_VFO_B:
        f = from_bcd_be(&(p->update_data[FT747_SUMO_VFO_B_FREQ]), 10);
        break;

    default:
        return -RIG_EINVAL;     /* sorry, wrong VFO */
    }

    rig_debug(RIG_DEBUG_VERBOSE, "ft747:  freq = %"PRIfreq" Hz  for VFO = %s\n",
              f, rig_strvfo(vfo));

    (*freq) = f;          /* return diplayed frequency */

    return RIG_OK;
}


/*
 * set mode : eg AM, CW etc for a given VFO
 *
 */

int ft747_set_mode(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
{
    unsigned char cmd_index;  /* index of sequence to send */
    pbwidth_t width_normal;

    width_normal = rig_passband_normal(rig, mode);

    if (width == RIG_PASSBAND_NORMAL)
    {
        width = width_normal;
    }

    /*
     * translate mode from generic to ft747 specific
     */

    rig_debug(RIG_DEBUG_VERBOSE, "%s: generic mode = %s \n", __func__,
              rig_strrmode(mode));

    switch (mode)
    {
    case RIG_MODE_AM:
        if (width != RIG_PASSBAND_NOCHANGE
                && width < width_normal)
        {
            cmd_index = FT_747_NATIVE_MODE_SET_AMN;
        }
        else
        {
            cmd_index = FT_747_NATIVE_MODE_SET_AMW;
        }

        break;

    case RIG_MODE_CW:
        if (width != RIG_PASSBAND_NOCHANGE
                && width < width_normal)
        {
            cmd_index = FT_747_NATIVE_MODE_SET_CWN;
        }
        else
        {
            cmd_index = FT_747_NATIVE_MODE_SET_CWW;
        }

        break;

    case RIG_MODE_USB:
        cmd_index = FT_747_NATIVE_MODE_SET_USB;
        break;

    case RIG_MODE_LSB:
        cmd_index = FT_747_NATIVE_MODE_SET_LSB;
        break;

    case RIG_MODE_FM:
        if (width != RIG_PASSBAND_NOCHANGE
                && width < width_normal)
        {
            cmd_index = FT_747_NATIVE_MODE_SET_FMN;
        }
        else
        {
            cmd_index = FT_747_NATIVE_MODE_SET_FMW;
        }

        break;

    default:
        return -RIG_EINVAL;     /* sorry, wrong MODE */
    }

    rig_debug(RIG_DEBUG_VERBOSE, "ft747: cmd_index = %i \n", cmd_index);

    rig_force_cache_timeout(&((struct ft747_priv_data *)
                              rig->state.priv)->status_tv);

    /*
     * phew! now send cmd to rig
     */

    return ft747_send_priv_cmd(rig, cmd_index);
}


int ft747_get_mode(RIG *rig, vfo_t vfo, rmode_t *mode, pbwidth_t *width)
{
    struct ft747_priv_data *p;
    unsigned char mymode;     /* ft747 mode */
    int ret;

    p = (struct ft747_priv_data *)rig->state.priv;

    ret = ft747_get_update_data(rig); /* get whole shebang from rig */

    if (ret < 0)
    {
        return ret;
    }

    mymode = p->update_data[FT747_SUMO_DISPLAYED_MODE];
    mymode &= MODE_MASK; /* mask out bits 5 and 6 */

    rig_debug(RIG_DEBUG_VERBOSE, "ft747: mymode = %x \n", mymode);

    /*
     * translate mode from ft747 to generic.
     */

    switch (mymode & 0x1f)
    {
    case MODE_FM:
        (*mode) = RIG_MODE_FM;
        break;

    case MODE_AM:
        (*mode) = RIG_MODE_AM;
        break;

    case MODE_CW:
        (*mode) = RIG_MODE_CW;
        break;

    case MODE_USB:
        (*mode) = RIG_MODE_USB;
        break;

    case MODE_LSB:
        (*mode) = RIG_MODE_LSB;
        break;

    default:
        return -RIG_EPROTO;     /* sorry, unknown mode */
        break;
    }

    if (mymode & MODE_NAR)
    {
        *width = rig_passband_narrow(rig, *mode);
    }
    else
    {
        *width = rig_passband_normal(rig, *mode);
    }

    return RIG_OK;
}




/*
 * set vfo and store requested vfo for later RIG_VFO_CURR
 * requests.
 *
 */
int ft747_set_vfo(RIG *rig, vfo_t vfo)
{
    struct ft747_priv_data *p;
    unsigned char cmd_index;  /* index of sequence to send */

    p = (struct ft747_priv_data *)rig->state.priv;

    switch (vfo)
    {

    case RIG_VFO_VFO:
    case RIG_VFO_CURR:
        return RIG_OK;

    case RIG_VFO_A:
        cmd_index = FT_747_NATIVE_VFO_A;
        break;

    case RIG_VFO_B:
        cmd_index = FT_747_NATIVE_VFO_B;
        break;

    default:
        return -RIG_EINVAL;     /* sorry, wrong VFO */
    }

    rig_force_cache_timeout(&p->status_tv);

    return ft747_send_priv_cmd(rig, cmd_index);
}


int ft747_get_vfo(RIG *rig, vfo_t *vfo)
{
    struct ft747_priv_data *p;
    unsigned char status;     /* ft747 status flag */
    int ret;

    p = (struct ft747_priv_data *)rig->state.priv;

    ret = ft747_get_update_data(rig); /* get whole shebang from rig */

    if (ret < 0)
    {
        return ret;
    }

    status = p->update_data[FT747_SUMO_DISPLAYED_STATUS];
    status &= SF_VFOAB; /* check VFO bit*/

    rig_debug(RIG_DEBUG_VERBOSE, "ft747: vfo status = %x \n", status);

    /*
     * translate vfo status from ft747 to generic.
     */

    if (status)
    {
        rig_debug(RIG_DEBUG_VERBOSE, "%s: VFO = B\n", __func__);
        (*vfo) = RIG_VFO_B;
        return RIG_OK;
    }
    else
    {
        rig_debug(RIG_DEBUG_VERBOSE, "%s: VFO = A\n", __func__);
        (*vfo) = RIG_VFO_A;
        return RIG_OK;
    }

}

int ft747_set_split(RIG *rig, vfo_t vfo, split_t split, vfo_t tx_vfo)
{
    unsigned char cmd_index;  /* index of sequence to send */

    cmd_index = split == RIG_SPLIT_ON ? FT_747_NATIVE_SPLIT_ON :
                FT_747_NATIVE_SPLIT_OFF;

    rig_force_cache_timeout(&((struct ft747_priv_data *)
                              rig->state.priv)->status_tv);

    return ft747_send_priv_cmd(rig, cmd_index);

}

int ft747_get_split(RIG *rig, vfo_t vfo, split_t *split, vfo_t *tx_vfo)
{
    struct ft747_priv_data *p;
    unsigned char status;     /* ft747 status flag */
    int ret;

    p = (struct ft747_priv_data *)rig->state.priv;

    ret = ft747_get_update_data(rig); /* get whole shebang from rig */

    if (ret < 0)
    {
        return ret;
    }

    status = p->update_data[FT747_SUMO_DISPLAYED_STATUS];

    if (((status & SF_VFOAB) && (status & SF_RXTX)) ||
            (!(status & SF_VFOAB) && !(status & SF_RXTX)))
    {
        *tx_vfo = RIG_VFO_B;
    }
    else
    {
        *tx_vfo = RIG_VFO_A;
    }

    return RIG_OK;
}


int ft747_set_ptt(RIG *rig, vfo_t vfo, ptt_t ptt)
{
    unsigned char cmd_index;  /* index of sequence to send */

    switch (ptt)
    {
    case RIG_PTT_OFF:
        cmd_index = FT_747_NATIVE_PTT_OFF;
        break;

    case RIG_PTT_ON:
        cmd_index = FT_747_NATIVE_PTT_ON;
        break;

    default:
        return -RIG_EINVAL;     /* sorry, wrong VFO */
    }

    rig_force_cache_timeout(&((struct ft747_priv_data *)
                              rig->state.priv)->status_tv);

    /*
     * phew! now send cmd to rig
     */

    return ft747_send_priv_cmd(rig, cmd_index);
}

int ft747_set_mem(RIG *rig, vfo_t vfo, int ch)
{
    struct ft747_priv_data *p;

    p = (struct ft747_priv_data *)rig->state.priv;

    if (ch < 0 || ch > 0x13)
    {
        return -RIG_EINVAL;
    }

    /*
     * Copy native cmd freq_set to private cmd storage area
     */
    memcpy(&p->p_cmd, &ft747_ncmd[FT_747_NATIVE_RECALL_MEM].nseq, YAESU_CMD_LENGTH);

    p->p_cmd[3] = ch;

    rig_force_cache_timeout(&p->status_tv);

    return write_block(&rig->state.rigport, (char *) p->p_cmd, YAESU_CMD_LENGTH);
}

int ft747_get_mem(RIG *rig, vfo_t vfo, int *ch)
{
    struct ft747_priv_data *p;
    unsigned char mem_nb;
    int ret;

    p = (struct ft747_priv_data *)rig->state.priv;

    ret = ft747_get_update_data(rig); /* get whole shebang from rig */

    if (ret < 0)
    {
        return ret;
    }

    mem_nb = p->update_data[FT747_SUMO_DISPLAYED_MEM];

    if (mem_nb > 0x13)
    {
        return -RIG_EPROTO;
    }

    *ch = mem_nb;

    return RIG_OK;
}


/*
 * private helper function. Retrieves update data from rig.
 * using buffer indicated in *priv struct.
 *
 * need to use this when doing ft747_get_* stuff
 */

static int ft747_get_update_data(RIG *rig)
{
    hamlib_port_t *rigport;
    struct ft747_priv_data *p;
    char last_byte;

    p = (struct ft747_priv_data *)rig->state.priv;
    rigport = &rig->state.rigport;

    if (!rig_check_cache_timeout(&p->status_tv, FT747_CACHE_TIMEOUT))
    {
        return RIG_OK;
    }

    if (!rig->state.transmit)     /* rig doesn't respond in Tx mode */
    {
        int ret;
        int port_timeout;
        serial_flush(rigport);

        /* send UPDATE comand to fetch data*/

        ret = ft747_send_priv_cmd(rig, FT_747_NATIVE_UPDATE);

        if (ret < 0)
        {
            return ret;
        }

        ret = read_block(rigport, (char *) p->update_data,
                         FT747_STATUS_UPDATE_DATA_LENGTH);

        if (ret < 0)
        {
            return ret;
        }

        port_timeout = rigport->timeout;
        rigport->timeout = 100; /* ms */
        /* read sometimes-missing last byte (345th), but don't fail */
        read_block(rigport, &last_byte, 1);
        rigport->timeout = port_timeout;
    }

    /* update cache date */
    gettimeofday(&p->status_tv, NULL);

    return RIG_OK;
}


/*
 * private helper function to send a private command
 * sequence . Must only be complete sequences.
 */

static int ft747_send_priv_cmd(RIG *rig, unsigned char ci)
{

    struct rig_state *rig_s;
    unsigned char *cmd;       /* points to sequence to send */
    unsigned char cmd_index;  /* index of sequence to send */

    rig_s = &rig->state;

    cmd_index = ci;       /* get command */

    if (! ft747_ncmd[cmd_index].ncomp)
    {
        rig_debug(RIG_DEBUG_VERBOSE, "%s: attempt to send incomplete sequence\n",
                  __func__);
        return -RIG_EINVAL;
    }

    cmd = (unsigned char *) ft747_ncmd[cmd_index].nseq; /* get native sequence */
    return write_block(&rig_s->rigport, (char *) cmd, YAESU_CMD_LENGTH);

}

