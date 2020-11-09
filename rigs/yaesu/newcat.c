/*
 * hamlib - (C) Frank Singleton 2000 (javabear at users.sourceforge.net)
 *              and the Hamlib Group (hamlib-developer at lists.sourceforge.net)
 *
 * newcat.c - (C) Nate Bargmann 2007 (n0nb at arrl.net)
 *            (C) Stephane Fillod 2008-2010
 *            (C) Terry Embry 2008-2010
 *            (C) David Fannin (kk6df at arrl.net)
 *
 * This shared library provides an API for communicating
 * via serial interface to any newer Yaesu radio using the
 * "new" text CAT interface.
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
#include <math.h>

#include "hamlib/rig.h"
#include "iofunc.h"
#include "misc.h"
#include "cal.h"
#include "newcat.h"

/* global variables */
static const char cat_term = ';';             /* Yaesu command terminator */
// static const char cat_unknown_cmd[] = "?;";   /* Yaesu ? */

/* Internal Backup and Restore VFO Memory Channels */
#define NC_MEM_CHANNEL_NONE  2012
#define NC_MEM_CHANNEL_VFO_A 2013
#define NC_MEM_CHANNEL_VFO_B 2014

/* ID 0310 == 310, Must drop leading zero */
typedef enum nc_rigid_e
{
    NC_RIGID_NONE            = 0,
    NC_RIGID_FT450           = 241,
    NC_RIGID_FT450D          = 244,
    NC_RIGID_FT950           = 310,
    NC_RIGID_FT891           = 135,
    NC_RIGID_FT991           = 135,
    NC_RIGID_FT2000          = 251,
    NC_RIGID_FT2000D         = 252,
    NC_RIGID_FTDX1200        = 583,
    NC_RIGID_FTDX9000D       = 101,
    NC_RIGID_FTDX9000Contest = 102,
    NC_RIGID_FTDX9000MP      = 103,
    NC_RIGID_FTDX5000        = 362,
    NC_RIGID_FTDX3000        = 460,
    NC_RIGID_FTDX101D        = 681,
    NC_RIGID_FTDX101MP       = 682
} nc_rigid_t;


/*
 * The following table defines which commands are valid for any given
 * rig supporting the "new" CAT interface.
 */

typedef struct _yaesu_newcat_commands
{
    char                *command;
    ncboolean           ft450;
    ncboolean           ft950;
    ncboolean           ft891;
    ncboolean           ft991;
    ncboolean           ft2000;
    ncboolean           ft9000;
    ncboolean           ft5000;
    ncboolean           ft1200;
    ncboolean           ft3000;
    ncboolean           ft101;
} yaesu_newcat_commands_t;

/**
 * Yaesu FT-991 S-meter scale, default for new Yaesu rigs.
 * Determined by data from W6HN -- seems to be pretty linear
 *
 * SMeter, rig answer, %fullscale
 * S0    SM0000 0
 * S2    SM0026 10
 * S4    SM0051 20
 * S6    SM0081 30
 * S7.5  SM0105 40
 * S9    SM0130 50
 * +12db SM0157 60
 * +25db SM0186 70
 * +35db SM0203 80
 * +50db SM0237 90
 * +60db SM0255 100
 *
 * 114dB range over 0-255 referenced to S0 of -54dB
 */
const cal_table_t yaesu_default_str_cal =
{
    11,
    {
        { 0, -54, }, // S0
        { 26, -42, }, // S2
        { 51, -30, }, // S4
        { 81, -18, }, // S6
        { 105, -9, }, // S7.5
        { 130, 0, }, // S9
        { 157, 12, }, // S9+12dB
        { 186, 25, }, // S9+25dB
        { 203, 35, }, // S9+35dB
        { 237, 50, }, // S9+50dB
        { 255, 60, }, // S9+60dB
    }
};

/**
 * First cut at generic Yaesu table, need more points probably
 * based on testing by Adam M7OTP on FT-991
 */
const cal_table_float_t yaesu_default_swr_cal =
{
    5,
    {
        {12, 1.0f},
        {39, 1.35f},
        {65, 1.5f},
        {89, 2.0f},
        {242, 5.0f}
    }
};

// TODO: Provide sane defaults
const cal_table_float_t yaesu_default_alc_cal =
{
    3,
    {
        {0, 0.0f},
        {128, 1.0f},
        {255, 2.0f},
    }
};

// TODO: Provide sane defaults
const cal_table_float_t yaesu_default_comp_meter_cal =
{
    2,
    {
        {0, 0.0f},
        {255, 1.0f},
    }
};

// TODO: Provide sane defaults
const cal_table_float_t yaesu_default_rfpower_meter_cal =
{
    2,
    {
        {0, 0.0f},
        {255, 1.0f},
    }
};

// TODO: Provide sane defaults
const cal_table_float_t yaesu_default_vd_meter_cal =
{
    2,
    {
        {0, 0.0f},
        {255, 1.0f},
    }
};

// TODO: Provide sane defaults
const cal_table_float_t yaesu_default_id_meter_cal =
{
    2,
    {
        {0, 0.0f},
        {255, 1.0f},
    }
};

// Easy reference to rig model -- it is set in newcat_valid_command
static ncboolean is_ft450;
static ncboolean is_ft891;
static ncboolean is_ft950;
static ncboolean is_ft991;
static ncboolean is_ft2000;
static ncboolean is_ftdx9000;
static ncboolean is_ftdx5000;
static ncboolean is_ftdx1200;
static ncboolean is_ftdx3000;
static ncboolean is_ftdx101;

/*
 * Even thought this table does make a handy reference, it could be depreciated as it is not really needed.
 * All of the CAT commands used in the newcat interface are available on the FT-950, FT-2000, FT-5000, and FT-9000.
 * There are 5 CAT commands used in the newcat interface that are not available on the FT-450.
 * Thesec CAT commands are XT -TX Clarifier ON/OFF, AN - Antenna select, PL - Speech Proc Level,
 * PR - Speech Proc ON/OFF, and BC - Auto Notch filter ON/OFF.
 * The FT-450 returns -RIG_ENVAIL for these unavailable CAT commands.
 *
 * NOTE: The following table must be in alphabetical order by the
 * command.  This is because it is searched using a binary search
 * to determine whether or not a command is valid for a given rig.
 *
 * The list of supported commands is obtained from the rig's operator's
 * or CAT programming manual.
 *
 */
static const yaesu_newcat_commands_t valid_commands[] =
{
    /* Command  FT-450  FT-950  FT-891  FT-991  FT-2000 FT-9000 FT-5000 FT-1200 FT-3000 FTDX101D */
    {"AB",      FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"AC",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"AG",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"AI",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"AM",      FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"AN",      FALSE,  TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"AO",      FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE    },
    {"BA",      FALSE,  FALSE,  TRUE,   TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   TRUE    },
    {"BC",      FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"BD",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"BI",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"BM",      FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE    },
    {"BP",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"BS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"BU",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"BY",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"CH",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"CN",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"CO",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"CS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"CT",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"DA",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"DN",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"DT",      FALSE,  FALSE,  TRUE,   TRUE,   FALSE,  FALSE,  FALSE,  TRUE,   FALSE,  TRUE    },
    {"DP",      FALSE,  TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   FALSE,  FALSE,  FALSE   },
    {"DS",      TRUE,   FALSE,  FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   FALSE,  FALSE,  FALSE   },
    {"ED",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"EK",      FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   FALSE,  TRUE,   TRUE,   FALSE   },
    {"EN",      FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE,   TRUE,   TRUE    },
    {"EU",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"EX",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"FA",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"FB",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"FK",      FALSE,  TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   FALSE,  FALSE,  FALSE,  FALSE   },
    {"FR",      FALSE,  TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"FS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"FT",      TRUE,   TRUE,   FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"GT",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"ID",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   FALSE,  TRUE    },
    {"IF",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"IS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"KM",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"KP",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"KR",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"KS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"KY",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"LK",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"LM",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"MA",      FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"MB",      FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE    },
    {"MC",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"MD",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"MG",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"MK",      TRUE,   TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   FALSE,  FALSE,  FALSE   },
    {"ML",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"MR",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"MS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"MT",      FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE    },
    {"MW",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"MX",      FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"NA",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   FALSE,  TRUE,   TRUE,   TRUE,   TRUE    },
    {"NB",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"NL",      FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"NR",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"OI",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"OS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"PA",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"PB",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"PC",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"PL",      FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"PR",      FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"PS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"QI",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"QR",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"QS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RA",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RC",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RD",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RF",      FALSE,  TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RG",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RI",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RL",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RM",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RO",      FALSE,  TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   FALSE   },
    {"RP",      TRUE,   FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE   },
    {"RS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RT",      TRUE,   TRUE,   FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"RU",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"SC",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"SD",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"SF",      FALSE,  TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"SH",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"SM",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"SQ",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"SS",      FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE    },
    // ST command has two meanings Step or Split Status
    // If new rig is added that has ST ensure it means Split
    // Otherwise modify newcat_get_tx_vfo
    {"ST",      TRUE,   FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE    },
    {"SV",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"SY",      FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE    },
    {"TS",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"TX",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"UL",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"UP",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"VD",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"VF",      FALSE,  TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   FALSE   },
    {"VG",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"VM",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"VR",      TRUE,   FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE   },
    {"VS",      TRUE,   TRUE,   FALSE,  FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"VT",      FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE    },
    {"VV",      TRUE,   FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  FALSE   },
    {"VX",      TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"XT",      FALSE,  TRUE,   FALSE,  TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE,   TRUE    },
    {"ZI",      FALSE,  FALSE,  TRUE,   TRUE,   FALSE,  FALSE,  FALSE,  FALSE,  FALSE,  TRUE    },
};
int                     valid_commands_count = sizeof(valid_commands) / sizeof(
            yaesu_newcat_commands_t);

/*
 * configuration Tokens
 *
 */

#define TOK_FAST_SET_CMD TOKEN_BACKEND(1)

const struct confparams newcat_cfg_params[] =
{
    {
        TOK_FAST_SET_CMD, "fast_commands_token", "High throughput of commands", "Enabled high throughput of >200 messages/sec by not waiting for ACK/NAK of messages", "0", RIG_CONF_NUMERIC, { .n = { 0, 1, 1 } }
    },
    { RIG_CONF_END, NULL, }
};

/* NewCAT Internal Functions */
static ncboolean newcat_is_rig(RIG *rig, rig_model_t model);

static int newcat_set_vfo_from_alias(RIG *rig, vfo_t *vfo);
static int newcat_scale_float(int scale, float fval);
static int newcat_get_rx_bandwidth(RIG *rig, vfo_t vfo, rmode_t mode,
                                   pbwidth_t *width);
static int newcat_set_rx_bandwidth(RIG *rig, vfo_t vfo, rmode_t mode,
                                   pbwidth_t width);
static int newcat_set_narrow(RIG *rig, vfo_t vfo, ncboolean narrow);
static int newcat_get_narrow(RIG *rig, vfo_t vfo, ncboolean *narrow);
static int newcat_set_faststep(RIG *rig, ncboolean fast_step);
static int newcat_get_faststep(RIG *rig, ncboolean *fast_step);
static int newcat_get_rigid(RIG *rig);
static int newcat_get_vfo_mode(RIG *rig, vfo_t *vfo_mode);
static int newcat_vfomem_toggle(RIG *rig);
static int set_roofing_filter(RIG *rig, vfo_t vfo, int index);
static int set_roofing_filter_for_width(RIG *rig, vfo_t vfo, int width);
static int get_roofing_filter(RIG *rig, vfo_t vfo,
                              struct newcat_roofing_filter **roofing_filter);
static ncboolean newcat_valid_command(RIG *rig, char const *const command);

/*
 * The BS command needs to know what band we're on so we can restore band info
 * So this converts freq to band index
 */
static int newcat_band_index(freq_t freq)
{
    int band = 11; // general

    // restrict band memory recall to ITU 1,2,3 band ranges
    // using < instead of <= for the moment
    // does anybody work LSB or RTTYR at the upper band edge?
    // what about band 13 -- what is it?
    if (freq >= MHz(420) && freq < MHz(470)) { band = 16; }
    // band 14 is RX only
    else if (freq >= MHz(118) && freq < MHz(164)) { band = 14; }
    // override band 14 with 15 if needed
    else if (freq >= MHz(144) && freq < MHz(148)) { band = 15; }
    else if (freq >= MHz(70) && freq < MHz(70.5)) { band = 17; }
    else if (freq >= MHz(50) && freq < MHz(55)) { band = 10; }
    else if (freq >= MHz(28) && freq < MHz(29.7)) { band = 9; }
    else if (freq >= MHz(24.890) && freq < MHz(24.990)) { band = 8; }
    else if (freq >= MHz(21) && freq < MHz(21.45)) { band = 7; }
    else if (freq >= MHz(18) && freq < MHz(18.168)) { band = 6; }
    else if (freq >= MHz(14) && freq < MHz(14.35)) { band = 5; }
    else if (freq >= MHz(10) && freq < MHz(10.15)) { band = 4; }
    else if (freq >= MHz(7) && freq < MHz(7.3)) { band = 3; }
    else if (freq >= MHz(5.3515) && freq < MHz(5.3665)) { band = 2; }
    else if (freq >= MHz(3.5) && freq < MHz(4)) { band = 1; }
    else if (freq >= MHz(1.8) && freq < MHz(2)) { band = 0; }
    else if (freq >= MHz(0.5) && freq < MHz(1.705)) { band = 12; } // MW Medium Wave

    rig_debug(RIG_DEBUG_TRACE, "%s: freq=%g, band=%d\n", __func__, freq, band);
    return band;
}

/*
 * ************************************
 *
 * Hamlib API functions
 *
 * ************************************
 */

/*
 * rig_init
 *
 */

int newcat_init(RIG *rig)
{
    struct newcat_priv_data *priv;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    rig->state.priv = (struct newcat_priv_data *) calloc(1,
                      sizeof(struct newcat_priv_data));

    if (!rig->state.priv)                                  /* whoops! memory shortage! */
    {
        return -RIG_ENOMEM;
    }

    priv = rig->state.priv;

    /* TODO: read pacing from preferences */
    //    priv->pacing = NEWCAT_PACING_DEFAULT_VALUE; /* set pacing to minimum for now */
    priv->read_update_delay =
        NEWCAT_DEFAULT_READ_TIMEOUT; /* set update timeout to safe value */

    //    priv->current_vfo =  RIG_VFO_MAIN;          /* default to whatever */
//    priv->current_vfo = RIG_VFO_A;

    priv->rig_id = NC_RIGID_NONE;
    priv->current_mem = NC_MEM_CHANNEL_NONE;
    priv->fast_set_commands = FALSE;

    return RIG_OK;
}


/*
 * rig_cleanup
 *
 * the serial port is closed by the frontend
 *
 */

int newcat_cleanup(RIG *rig)
{

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (rig->state.priv)
    {
        free(rig->state.priv);
    }

    rig->state.priv = NULL;

    return RIG_OK;
}


/*
 * rig_open
 *
 * New CAT does not support pacing
 *
 */

int newcat_open(RIG *rig)
{
    struct newcat_priv_data *priv = rig->state.priv;
    struct rig_state *rig_s = &rig->state;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    rig_debug(RIG_DEBUG_TRACE, "%s: write_delay = %i msec\n",
              __func__, rig_s->rigport.write_delay);

    rig_debug(RIG_DEBUG_TRACE, "%s: post_write_delay = %i msec\n",
              __func__, rig_s->rigport.post_write_delay);

    /* Ensure rig is powered on */
    if (priv->poweron == 0 && rig_s->auto_power_on)
    {
        rig_set_powerstat(rig, 1);
        priv->poweron = 1;
    }

    priv->question_mark_response_means_rejected = 0;

    /* get current AI state so it can be restored */
    priv->trn_state = -1;

    newcat_get_trn(rig, &priv->trn_state);  /* ignore errors */

    /* Currently we cannot cope with AI mode so turn it off in case
       last client left it on */
    if (priv->trn_state > 0)
    {
        newcat_set_trn(rig, RIG_TRN_OFF);
    } /* ignore status in case it's not supported */

    /* Initialize rig_id in case any subsequent commands need it */
    (void)newcat_get_rigid(rig);

    return RIG_OK;
}


/*
 * rig_close
 *
 */

int newcat_close(RIG *rig)
{

    struct newcat_priv_data *priv = rig->state.priv;
    struct rig_state *rig_s = &rig->state;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!no_restore_ai && priv->trn_state >= 0)
    {
        /* restore AI state */
        newcat_set_trn(rig, priv->trn_state); /* ignore status in
                                                   case it's not
                                                   supported */
    }
    if (priv->poweron != 0 && rig_s->auto_power_on)
    {
        rig_set_powerstat(rig, 0);
        priv->poweron = 0;
    }

    return RIG_OK;
}


/*
 * rig_set_config
 *
 * Set Configuration Token for Yaesu Radios
 */

int newcat_set_conf(RIG *rig, token_t token, const char *val)
{
    int ret = RIG_OK;
    struct newcat_priv_data *priv;

    priv = (struct newcat_priv_data *)rig->state.priv;

    if (priv == NULL)
    {
        return -RIG_EINTERNAL;
    }

    switch (token)
    {
        char *end;
        long value;

    case TOK_FAST_SET_CMD: ;
        //using strtol because atoi can lead to undefined behaviour
        value = strtol(val, &end, 10);

        if (end == val)
        {
            return -RIG_EINVAL;
        }

        if ((value == 0) || (value == 1))
        {
            priv->fast_set_commands = (int)value;
        }
        else
        {
            return -RIG_EINVAL;
        }

        break;

    default:
        ret = -RIG_EINVAL;
    }

    return ret;
}


/*
 * rig_get_config
 *
 * Get Configuration Token for Yaesu Radios
 */

int newcat_get_conf(RIG *rig, token_t token, char *val)
{
    int ret = RIG_OK;
    struct newcat_priv_data *priv;

    priv = (struct newcat_priv_data *)rig->state.priv;

    if (priv == NULL)
    {
        return -RIG_EINTERNAL;
    }

    switch (token)
    {
    case TOK_FAST_SET_CMD:
        if (sizeof(val) < 2)
        {
            return -RIG_ENOMEM;
        }

        sprintf(val, "%d", priv->fast_set_commands);
        break;

    default:
        ret = -RIG_EINVAL;
    }

    return ret;
}




/*
 * rig_set_freq
 *
 * Set frequency for a given VFO
 * RIG_TARGETABLE_VFO
 * Does not SET priv->current_vfo
 *
 */

int newcat_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
    char c;
    char target_vfo;
    int err;
    const struct rig_caps *caps;
    struct newcat_priv_data *priv;
    int special_60m = 0;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "FA"))
    {
        return -RIG_ENAVAIL;
    }

    if (!newcat_valid_command(rig, "FB"))
    {
        return -RIG_ENAVAIL;
    }

    priv = (struct newcat_priv_data *)rig->state.priv;
    caps = rig->caps;

    rig_debug(RIG_DEBUG_TRACE, "%s: passed vfo = %s\n", __func__, rig_strvfo(vfo));
//    rig_debug(RIG_DEBUG_TRACE, "%s: translated vfo = %s\n", __func__, rig_strvfo(tvfo));
    rig_debug(RIG_DEBUG_TRACE, "%s: passed freq = %"PRIfreq" Hz\n", __func__, freq);

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    /* vfo should now be modified to a valid VFO constant. */
    /* DX3000/DX5000 can only do VFO_MEM on 60M */
    /* So we will not change freq in that case */
    special_60m = newcat_is_rig(rig, RIG_MODEL_FTDX3000);
    /* duplicate the following line to add more rigs */
    special_60m |= newcat_is_rig(rig, RIG_MODEL_FTDX5000);

    switch (vfo)
    {
    case RIG_VFO_A:
    case RIG_VFO_MAIN:
        c = 'A';
        break;

    case RIG_VFO_B:
    case RIG_VFO_SUB:
        c = 'B';
        break;

    case RIG_VFO_MEM:
        if (special_60m && (freq >= 5300000 && freq <= 5410000))
        {
            rig_debug(RIG_DEBUG_TRACE, "%s: 60M VFO_MEM exception, no freq change done\n",
                      __func__);
            return RIG_OK; /* make it look like we changed */
        }

        c = 'A';
        break;

    default:
        return -RIG_ENIMPL;             /* Only VFO_A or VFO_B are valid */
    }

    target_vfo = 'A' == c ? '0' : '1';

    if (RIG_MODEL_FT450 == caps->rig_model)
    {
        /* The FT450 only accepts F[A|B]nnnnnnnn; commands for the
           current VFO so we must use the VS[0|1]; command to check
           and select the correct VFO before setting the frequency
        */
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VS%c", cat_term);

        if (RIG_OK != (err = newcat_get_cmd(rig)))
        {
            return err;
        }

        if (priv->ret_data[2] != target_vfo)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VS%c%c", target_vfo, cat_term);
            rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

            if (RIG_OK != (err = newcat_set_cmd(rig)))
            {
                return err;
            }
        }
    }

    // W1HKJ
    // creation of the priv structure guarantees that the string can be NEWCAT_DATA_LEN
    // bytes in length.  the snprintf will only allow (NEWCAT_DATA_LEN - 1) chars
    // followed by the NULL terminator.
    // CAT command string for setting frequency requires that 8 digits be sent
    // including leading fill zeros
    // Call this after open to set width_frequency for later use
    if (priv->width_frequency == 0)
    {
        vfo_t vfo_mode;
        newcat_get_vfo_mode(rig, &vfo_mode);
    }

    //
    // Restore band memory if we can and band is changing -- we do it before we set the frequency
    if (newcat_valid_command(rig, "BS")
            && newcat_band_index(freq) != newcat_band_index(rig->state.current_freq))
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BS%02d%c",
                 newcat_band_index(freq), cat_term);

        if (RIG_OK != (err = newcat_set_cmd(rig)))
        {
            rig_debug(RIG_DEBUG_ERR, "%s: Unexpected error with BS command=%s\n", __func__,
                      rigerror(err));
        }

        // just drop through
    }


    // cppcheck-suppress *
    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "F%c%0*"PRIll"%c", c,
             priv->width_frequency, (int64_t)freq, cat_term);
    rig_debug(RIG_DEBUG_TRACE, "%s:%d cmd_str = %s\n", __func__, __LINE__,
              priv->cmd_str);

    if (RIG_OK != (err = newcat_set_cmd(rig)))
    {
        rig_debug(RIG_DEBUG_VERBOSE, "%s:%d command err = %d\n", __func__, __LINE__,
                  err);
        return err;
    }

    rig_debug(RIG_DEBUG_TRACE, "%s: band changing? old=%d, new=%d\n", __func__,
              newcat_band_index(freq), newcat_band_index(rig->state.current_freq));

    if (RIG_MODEL_FT450 == caps->rig_model && priv->ret_data[2] != target_vfo)
    {
        /* revert current VFO */
        rig_debug(RIG_DEBUG_TRACE, "%s:%d cmd_str = %s\n", __func__, __LINE__,
                  priv->ret_data);

        if (RIG_OK != (err = newcat_set_cmd(rig)))
        {
            rig_debug(RIG_DEBUG_VERBOSE, "%s:%d command err = %d\n", __func__, __LINE__,
                      err);
            return err;
        }
    }

    return RIG_OK;
}


/*
 * rig_get_freq
 *
 * Return Freq for a given VFO
 * RIG_TARGETABLE_FREQ
 * Does not SET priv->current_vfo
 *
 */
int newcat_get_freq(RIG *rig, vfo_t vfo, freq_t *freq)
{
    char command[3];
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    char c;
    int err;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);
    rig_debug(RIG_DEBUG_TRACE, "%s: passed vfo = %s\n", __func__, rig_strvfo(vfo));

    if (!newcat_valid_command(rig, "FA"))
    {
        return -RIG_ENAVAIL;
    }

    if (!newcat_valid_command(rig, "FB"))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    switch (vfo)
    {
    case RIG_VFO_A:
    case RIG_VFO_MAIN: // what about MAIN_A/MAIN_B?
        c = 'A';
        break;

    case RIG_VFO_B:
    case RIG_VFO_SUB: // what about SUB_A/SUB_B?
        c = 'B';
        break;

    case RIG_VFO_MEM:
        c = 'A';
        break;

    default:
        return -RIG_EINVAL;         /* sorry, unsupported VFO */
    }

    /* Build the command string */
    snprintf(command, sizeof(command), "F%c", c);

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", command, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "cmd_str = %s\n", priv->cmd_str);

    /* get freq */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    /* convert the read frequency string into freq_t and store in *freq */
    sscanf(priv->ret_data + 2, "%"SCNfreq, freq);

    rig_debug(RIG_DEBUG_TRACE,
              "%s: freq = %"PRIfreq" Hz for vfo %s\n", __func__, *freq, rig_strvfo(vfo));

    return RIG_OK;
}


int newcat_set_mode(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
{
    struct newcat_priv_data *priv;
    int err;
    split_t split_save = rig->state.cache.split;

    priv = (struct newcat_priv_data *)rig->state.priv;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "MD"))
    {
        return -RIG_ENAVAIL;
    }


    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "MD0x%c", cat_term);

    /* FT9000 RIG_TARGETABLE_MODE (mode and width) */
    /* FT2000 mode only */
    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        priv->cmd_str[2] = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    rig_debug(RIG_DEBUG_VERBOSE, "%s: generic mode = %s \n",
              __func__, rig_strrmode(mode));


    priv->cmd_str[3] = newcat_modechar(mode);

    if (priv->cmd_str[3] == '0')
    {
        return -RIG_EINVAL;
    }

    err = newcat_set_cmd(rig);

    if (err != RIG_OK)
    {
        return err;
    }

    if (RIG_PASSBAND_NOCHANGE == width) { return err; }

    if (RIG_PASSBAND_NORMAL == width)
    {
        width = rig_passband_normal(rig, mode);
    }

    /* Set width after mode has been set */
    err = newcat_set_rx_bandwidth(rig, vfo, mode, width);

    // some rigs if you set mode on VFOB it will turn off split
    // so if we started in split we query split and turn it back on if needed
    if (split_save)
    {
        split_t split;
        vfo_t tx_vfo;
        err = rig_get_split_vfo(rig, RIG_VFO_A, &split, &tx_vfo);

        // we'll just reset to split to what we want if we need to
        if (!split)
        {
            rig_debug(RIG_DEBUG_TRACE, "%s: turning split back on...buggy rig\n", __func__);
            err = rig_set_split_vfo(rig, RIG_VFO_A, split_save, RIG_VFO_B);
        }
    }

    return err;
}


int newcat_get_mode(RIG *rig, vfo_t vfo, rmode_t *mode, pbwidth_t *width)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    char c;
    int err;
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "MD"))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo)  ? '1' : '0';
    }

    /* Build the command string */
    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "MD%c%c", main_sub_vfo,
             cat_term);

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    /* Get MODE */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    /*
     * The current mode value is a digit '0' ... 'C'
     * embedded at ret_data[3] in the read string.
     */
    c = priv->ret_data[3];

    /* default, unless set otherwise */
    *width = RIG_PASSBAND_NORMAL;

    *mode = newcat_rmode_width(rig, vfo, c, width);

    if (*mode == '0')
    {
        return -RIG_EPROTO;
    }

    if (RIG_PASSBAND_NORMAL == *width)
    {
        *width = rig_passband_normal(rig, *mode);
    }

    rig_debug(RIG_DEBUG_TRACE, "%s: returning newcat_get_rx_bandwidth\n", __func__);
    return newcat_get_rx_bandwidth(rig, vfo, *mode, width);
}

/*
 * newcat_set_vfo
 *
 * set vfo and store requested vfo for later RIG_VFO_CURR
 * requests.
 *
 */

int newcat_set_vfo(RIG *rig, vfo_t vfo)
{
    struct newcat_priv_data *priv;
    struct rig_state *state;
    char c;
    int err, mem;
    vfo_t vfo_mode;
    char command[] = "VS";

    priv = (struct newcat_priv_data *)rig->state.priv;
    state = &rig->state;
    priv->cache_start.tv_sec = 0; // invalidate the cache


    rig_debug(RIG_DEBUG_TRACE, "%s: called, passed vfo = %s\n", __func__,
              rig_strvfo(vfo));

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig,
                                    &vfo);   /* passes RIG_VFO_MEM, RIG_VFO_A, RIG_VFO_B */

    if (err < 0)
    {
        return err;
    }

    switch (vfo)
    {
    case RIG_VFO_A:
    case RIG_VFO_B:
    case RIG_VFO_MAIN:
    case RIG_VFO_SUB:
        if (vfo == RIG_VFO_B || vfo == RIG_VFO_SUB)
        {
            c = '1';
        }
        else
        {
            c = '0';
        }

        err = newcat_get_vfo_mode(rig, &vfo_mode);

        if (err != RIG_OK)
        {
            return err;
        }

        if (vfo_mode == RIG_VFO_MEM)
        {
            priv->current_mem = NC_MEM_CHANNEL_NONE;
            state->current_vfo = RIG_VFO_A;
            err = newcat_vfomem_toggle(rig);
            return err;
        }

        break;

    case RIG_VFO_MEM:
        if (priv->current_mem == NC_MEM_CHANNEL_NONE)
        {
            /* Only works correctly for VFO A */
            if (state->current_vfo != RIG_VFO_A && state->current_vfo != RIG_VFO_MAIN)
            {
                return -RIG_ENTARGET;
            }

            /* get current memory channel */
            err = newcat_get_mem(rig, vfo, &mem);

            if (err != RIG_OK)
            {
                return err;
            }

            /* turn on memory channel */
            err = newcat_set_mem(rig, vfo, mem);

            if (err != RIG_OK)
            {
                return err;
            }

            /* Set current_mem now */
            priv->current_mem = mem;
        }

        /* Set current_vfo now */
        state->current_vfo = vfo;
        return RIG_OK;

    default:
        return -RIG_ENIMPL;         /* sorry, VFO not implemented */
    }

    /* Build the command string */
    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c%c", command, c, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "cmd_str = %s\n", priv->cmd_str);

    err = newcat_set_cmd(rig);

    if (err != RIG_OK)
    {
        return err;
    }

    state->current_vfo = vfo;    /* if set_vfo worked, set current_vfo */

    rig_debug(RIG_DEBUG_TRACE, "%s: rig->state.current_vfo = %s\n", __func__,
              rig_strvfo(vfo));

    return RIG_OK;
}

// Either returns a valid RIG_VFO* or if < 0 an error code
static vfo_t newcat_set_vfo_if_needed(RIG *rig, vfo_t vfo)
{
    vfo_t oldvfo = rig->state.current_vfo;

    rig_debug(RIG_DEBUG_TRACE, "%s: vfo=%s, oldvfo=%s\n", __func__, rig_strvfo(vfo),
              rig_strvfo(oldvfo));

    if (oldvfo != vfo)
    {
        int ret;
        ret = newcat_set_vfo(rig, vfo);

        if (ret != RIG_OK)
        {
            rig_debug(RIG_DEBUG_ERR, "%s: error setting vfo=%s\n", __func__,
                      rig_strvfo(vfo));
            return ret;
        }
    }

    return oldvfo;
}

/*
 * rig_get_vfo
 *
 * get current RX vfo/mem and store requested vfo for
 * later RIG_VFO_CURR requests plus pass the tested vfo/mem
 * back to the frontend.
 *
 */

int newcat_get_vfo(RIG *rig, vfo_t *vfo)
{
    struct rig_state *state = &rig->state;
    struct newcat_priv_data *priv  = (struct newcat_priv_data *)rig->state.priv;
    int err;
    vfo_t vfo_mode;
    char const *command = "VS";

    if (!vfo)
    {
        return -RIG_EINVAL;
    }

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    /* Build the command string */
    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s;", command);
    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    /* Get VFO */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    /*
     * The current VFO value is a digit ('0' or '1' ('A' or 'B'
     * respectively)) embedded at ret_data[2] in the read string.
     */
    switch (priv->ret_data[2])
    {
    case '0':
        if (rig->state.vfo_list & RIG_VFO_MAIN) { *vfo = RIG_VFO_MAIN; }
        else { *vfo = RIG_VFO_A; }

        break;

    case '1':
        if (rig->state.vfo_list & RIG_VFO_SUB) { *vfo = RIG_VFO_SUB; }
        else { *vfo = RIG_VFO_B; }

        break;

    default:
        return -RIG_EPROTO;         /* sorry, wrong current VFO */
    }

    /* Check to see if RIG is in MEM mode */
    err = newcat_get_vfo_mode(rig, &vfo_mode);

    if (err != RIG_OK)
    {
        return err;
    }

    if (vfo_mode == RIG_VFO_MEM)
    {
        *vfo = RIG_VFO_MEM;
    }

    state->current_vfo = *vfo;       /* set now */

    rig_debug(RIG_DEBUG_TRACE, "%s: rig->state.current_vfo = %s\n", __func__,
              rig_strvfo(state->current_vfo));

    return RIG_OK;
}


int newcat_set_ptt(RIG *rig, vfo_t vfo, ptt_t ptt)
{
    struct newcat_priv_data *priv  = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char txon[] = "TX1;";
    char txoff[] = "TX0;";

    priv->cache_start.tv_sec = 0; // invalidate the cache

    if (!newcat_valid_command(rig, "TX"))
    {
        return -RIG_ENAVAIL;
    }

    switch (ptt)
    {
    case RIG_PTT_ON:
        /* Build the command string */
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s", txon);
        rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);
        err = newcat_set_cmd(rig);
        break;

    case RIG_PTT_OFF:
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s", txoff);
        rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);
        err = newcat_set_cmd(rig);

        // some rigs like the FT991 need time before doing anything else like set_freq
        // We won't mess with CW mode -- no freq change expected hopefully
        if (rig->state.current_mode != RIG_MODE_CW)
        {
            hl_usleep(100 * 1000);
        }

        break;

    default:
        return -RIG_EINVAL;
    }

    return err;
}


int newcat_get_ptt(RIG *rig, vfo_t vfo, ptt_t *ptt)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    char c;
    int err;

    if (!newcat_valid_command(rig, "TX"))
    {
        return -RIG_ENAVAIL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", "TX", cat_term);

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    /* Get PTT */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    c = priv->ret_data[2];

    switch (c)
    {
    case '0':                 /* FT-950 "TX OFF", Original Release Firmware */
        *ptt = RIG_PTT_OFF;
        break;

    case '1' :                /* Just because,    what the CAT Manual Shows */
    case '2' :                /* FT-950 Radio:    Mic, Dataport, CW "TX ON" */
    case '3' :                /* FT-950 CAT port: Radio in "TX ON" mode     [Not what the CAT Manual Shows] */
        *ptt = RIG_PTT_ON;
        break;

    default:
        return -RIG_EPROTO;
    }

    return RIG_OK;
}


int newcat_get_dcd(RIG *rig, vfo_t vfo, dcd_t *dcd)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_rptr_shift(RIG *rig, vfo_t vfo, rptr_shift_t rptr_shift)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char c;
    char command[] = "OS";
    char main_sub_vfo = '0';

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    /* Main or SUB vfo */
    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    switch (rptr_shift)
    {
    case RIG_RPT_SHIFT_NONE:
        c = '0';
        break;

    case RIG_RPT_SHIFT_PLUS:
        c = '1';
        break;

    case RIG_RPT_SHIFT_MINUS:
        c = '2';
        break;

    default:
        return -RIG_EINVAL;

    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c%c%c", command,
             main_sub_vfo, c, cat_term);
    return newcat_set_cmd(rig);
}


int newcat_get_rptr_shift(RIG *rig, vfo_t vfo, rptr_shift_t *rptr_shift)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char c;
    char command[] = "OS";
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    /* Set Main or SUB vfo */
    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c%c", command, main_sub_vfo,
             cat_term);

    /* Get Rptr Shift */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    c = priv->ret_data[3];

    switch (c)
    {
    case '0':
        *rptr_shift = RIG_RPT_SHIFT_NONE;
        break;

    case '1':
        *rptr_shift = RIG_RPT_SHIFT_PLUS;
        break;

    case '2':
        *rptr_shift = RIG_RPT_SHIFT_MINUS;
        break;

    default:
        return -RIG_EINVAL;
    }

    return RIG_OK;
}


int newcat_set_rptr_offs(RIG *rig, vfo_t vfo, shortfreq_t offs)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char command[16] ;
    freq_t freq;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (newcat_is_rig(rig, RIG_MODEL_FT991))
    {
        freq = 0;
        err = newcat_get_freq(rig, vfo, &freq); // Need to get freq to determine band

        if (err < 0)
        {
            return err;
        }

        if (freq >= 28000000 && freq <= 29700000)
        {
            strcpy(command, "EX080");
        }
        else if (freq >= 50000000 && freq <= 54000000)
        {
            strcpy(command, "EX081");
        }
        else if (freq >= 144000000 && freq <= 148000000)
        {
            strcpy(command, "EX082");
        }
        else if (freq >= 430000000 && freq <= 450000000)
        {
            strcpy(command, "EX083");
        }
        else
        {
            // only valid on 10m to 70cm bands
            return RIG_OK;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%04li%c", command, offs,
                 cat_term);
        return newcat_set_cmd(rig);
    }

    return -RIG_ENAVAIL;
}


int newcat_get_rptr_offs(RIG *rig, vfo_t vfo, shortfreq_t *offs)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    int ret_data_len;
    char *retoffs;
    freq_t freq;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (newcat_is_rig(rig, RIG_MODEL_FT991))
    {
        freq = 0;
        err = newcat_get_freq(rig, vfo, &freq); // Need to get freq to determine band

        if (err < 0)
        {
            return err;
        }

        if (freq >= 28000000 && freq <= 29700000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX080%c", cat_term);
        }
        else if (freq >= 50000000 && freq <= 54000000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX081%c", cat_term);
        }
        else if (freq >= 144000000 && freq <= 148000000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX082%c", cat_term);
        }
        else if (freq >= 430000000 && freq <= 450000000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX083%c", cat_term);
        }
        else
        {
            *offs = 0;     // only valid on 10m to 70cm bands
            return RIG_OK;
        }

        if (RIG_OK != (err = newcat_get_cmd(rig)))
        {
            return err;
        }

        ret_data_len = strlen(priv->ret_data);

        /* skip command */
        retoffs = priv->ret_data + strlen(priv->cmd_str) - 1;
        /* chop term */
        priv->ret_data[ret_data_len - 1] = '\0';
        *offs = atoi(retoffs);
    }
    else
    {
        return -RIG_ENAVAIL;
    }

    return RIG_OK;
}


int newcat_set_split_freq(RIG *rig, vfo_t vfo, freq_t tx_freq)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_get_split_freq(RIG *rig, vfo_t vfo, freq_t *tx_freq)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_split_mode(RIG *rig, vfo_t vfo, rmode_t tx_mode,
                          pbwidth_t tx_width)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_get_split_mode(RIG *rig, vfo_t vfo, rmode_t *tx_mode,
                          pbwidth_t *tx_width)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_split_vfo(RIG *rig, vfo_t vfo, split_t split, vfo_t tx_vfo)
{
    int err;
    vfo_t rx_vfo;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (is_ft991)
    {
        vfo = RIG_VFO_A;
        tx_vfo = RIG_SPLIT_ON == split ? RIG_VFO_B : RIG_VFO_A;
    }
    else if (is_ftdx101)
    {
        vfo = RIG_VFO_MAIN;
        tx_vfo = RIG_SPLIT_ON == split ? RIG_VFO_SUB : RIG_VFO_MAIN;
    }
    else
    {
        err = newcat_get_vfo(rig, &rx_vfo);  /* sync to rig current vfo */

        if (err != RIG_OK)
        {
            return err;
        }
    }

    switch (split)
    {
    case RIG_SPLIT_OFF:
        err = newcat_set_tx_vfo(rig, vfo);

        if (err != RIG_OK)
        {
            return err;
        }

        if (rx_vfo != vfo && newcat_valid_command(rig, "VS"))
        {
            err = newcat_set_vfo(rig, vfo);

            if (err != RIG_OK)
            {
                return err;
            }
        }

        break;

    case RIG_SPLIT_ON:
        err = newcat_set_tx_vfo(rig, tx_vfo);

        if (err != RIG_OK)
        {
            return err;
        }

        if (rx_vfo != vfo)
        {
            err = newcat_set_vfo(rig, vfo);

            if (err != RIG_OK && err != -RIG_ENAVAIL)
            {
                return err;
            }
        }

        break;

    default:
        return -RIG_EINVAL;
    }

    return RIG_OK;
}


int newcat_get_split_vfo(RIG *rig, vfo_t vfo, split_t *split, vfo_t *tx_vfo)
{
    int err;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err != RIG_OK)
    {
        return err;
    }

    err = newcat_get_tx_vfo(rig, tx_vfo);

    if (err != RIG_OK)
    {
        return err;
    }

    // we assume split is always on VFO_B
    if (*tx_vfo == RIG_VFO_B || *tx_vfo == RIG_VFO_SUB)
    {
        *split = RIG_SPLIT_ON;
    }
    else
    {
        *split = RIG_SPLIT_OFF;
    }

    rig_debug(RIG_DEBUG_TRACE, "SPLIT = %d, vfo = %s, TX_vfo = %s\n", *split,
              rig_strvfo(vfo),
              rig_strvfo(*tx_vfo));

    return RIG_OK;
}

int newcat_set_rit(RIG *rig, vfo_t vfo, shortfreq_t rit)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    vfo_t oldvfo;
    int ret;

    if (!newcat_valid_command(rig, "RT"))
    {
        return -RIG_ENAVAIL;
    }

    oldvfo = newcat_set_vfo_if_needed(rig, vfo);

    if (oldvfo < 0) { return oldvfo; }

    if (rit > rig->caps->max_rit)
    {
        rit = rig->caps->max_rit;    /* + */
    }
    else if (labs(rit) > rig->caps->max_rit)
    {
        rit = - rig->caps->max_rit;    /* - */
    }

    if (rit == 0)   // don't turn it off just because it is zero
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RC%c",
                 cat_term);
    }
    else if (rit < 0)
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RC%cRD%04ld%c", cat_term,
                 labs(rit), cat_term);
    }
    else
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RC%cRU%04ld%c", cat_term,
                 labs(rit), cat_term);
    }

    ret = newcat_set_cmd(rig);

    oldvfo = newcat_set_vfo_if_needed(rig, oldvfo);

    if (oldvfo < 0) { return oldvfo; }

    return ret;
}


int newcat_get_rit(RIG *rig, vfo_t vfo, shortfreq_t *rit)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    char *retval;
    int err;
    int offset = 0;
    char *cmd = "IF";

    if (vfo == RIG_VFO_B || vfo == RIG_VFO_SUB)
    {
        cmd = "OI";
    }

    if (!newcat_valid_command(rig, cmd))
    {
        return -RIG_ENAVAIL;
    }

    *rit = 0;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", cmd, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    /* Get RIT */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    // e.g. FT450 has 27 byte IF response, FT991 has 28 byte if response (one more byte for P2 VFO A Freq)
    // so we now check to ensure we know the length of the response

    switch (strlen(priv->ret_data))
    {
    case 27: offset = 13; break;

    case 28: offset = 14; break;

    default: offset = 0;
    }

    if (offset == 0)
    {
        rig_debug(RIG_DEBUG_ERR,
                  "%s: incorrect length of IF response, expected 27 or 28, got %du", __func__,
                  (int)strlen(priv->ret_data));
        return -RIG_EPROTO;
    }

    retval = priv->ret_data + offset;
    retval[5] = '\0';

    // return the current offset even if turned off
    *rit = (shortfreq_t) atoi(retval);

    return RIG_OK;
}


int newcat_set_xit(RIG *rig, vfo_t vfo, shortfreq_t xit)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    vfo_t oldvfo;
    int ret;

    if (!newcat_valid_command(rig, "XT"))
    {
        return -RIG_ENAVAIL;
    }

    oldvfo = newcat_set_vfo_if_needed(rig, vfo);

    if (oldvfo < 0) { return oldvfo; }

    if (xit > rig->caps->max_xit)
    {
        xit = rig->caps->max_xit;    /* + */
    }
    else if (labs(xit) > rig->caps->max_xit)
    {
        xit = - rig->caps->max_xit;    /* - */
    }

    if (xit == 0)
    {
        // don't turn it off just because the offset is zero
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RC%c",
                 cat_term);
    }
    else if (xit < 0)
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RC%cRD%04ld%c", cat_term,
                 labs(xit), cat_term);
    }
    else
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RC%cRU%04ld%c", cat_term,
                 labs(xit), cat_term);
    }

    ret = newcat_set_cmd(rig);

    oldvfo = newcat_set_vfo_if_needed(rig, vfo);

    if (oldvfo < 0) { return oldvfo; }

    return ret;
}


int newcat_get_xit(RIG *rig, vfo_t vfo, shortfreq_t *xit)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    char *retval;
    int err;
    int offset = 0;
    char *cmd = "IF";

    if (vfo == RIG_VFO_B || vfo == RIG_VFO_SUB)
    {
        cmd = "OI";
    }

    if (!newcat_valid_command(rig, cmd))
    {
        return -RIG_ENAVAIL;
    }

    *xit = 0;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", cmd, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    /* Get XIT */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    // e.g. FT450 has 27 byte IF response, FT991 has 28 byte if response (one more byte for P2 VFO A Freq)
    // so we now check to ensure we know the length of the response

    switch (strlen(priv->ret_data))
    {
    case 27: offset = 13; break;

    case 28: offset = 14; break;

    default: offset = 0;
    }

    if (offset == 0)
    {
        rig_debug(RIG_DEBUG_ERR,
                  "%s: incorrect length of IF response, expected 27 or 28, got %du", __func__,
                  (int)strlen(priv->ret_data));
        return -RIG_EPROTO;
    }

    retval = priv->ret_data + offset;
    retval[5] = '\0';

    // return the offset even when turned off
    *xit = (shortfreq_t) atoi(retval);

    return RIG_OK;
}


int newcat_set_ts(RIG *rig, vfo_t vfo, shortfreq_t ts)
{
    int err, i;
    pbwidth_t width;
    rmode_t mode;
    ncboolean ts_match;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    err = newcat_get_mode(rig, vfo, &mode, &width);

    if (err < 0)
    {
        return err;
    }

    /* assume 2 tuning steps per mode */
    for (i = 0, ts_match = FALSE; i < TSLSTSIZ
            && rig->caps->tuning_steps[i].ts; i++)
        if (rig->caps->tuning_steps[i].modes & mode)
        {
            if (ts <= rig->caps->tuning_steps[i].ts)
            {
                err = newcat_set_faststep(rig, FALSE);
            }
            else
            {
                err = newcat_set_faststep(rig, TRUE);
            }

            if (err != RIG_OK)
            {
                return err;
            }

            ts_match = TRUE;
            break;
        }   /* if mode */

    rig_debug(RIG_DEBUG_TRACE, "ts_match = %d, i = %d, ts = %d\n", ts_match, i,
              (int)ts);

    if (ts_match)
    {
        return RIG_OK;
    }
    else
    {
        return -RIG_ENAVAIL;
    }
}


int newcat_get_ts(RIG *rig, vfo_t vfo, shortfreq_t *ts)
{
    pbwidth_t width;
    rmode_t mode;
    int err, i;
    ncboolean ts_match;
    ncboolean fast_step = FALSE;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    err = newcat_get_mode(rig, vfo, &mode, &width);

    if (err < 0)
    {
        return err;
    }

    err = newcat_get_faststep(rig, &fast_step);

    if (err < 0)
    {
        return err;
    }

    /* assume 2 tuning steps per mode */
    for (i = 0, ts_match = FALSE; i < TSLSTSIZ
            && rig->caps->tuning_steps[i].ts; i++)
        if (rig->caps->tuning_steps[i].modes & mode)
        {
            if (fast_step == FALSE)
            {
                *ts = rig->caps->tuning_steps[i].ts;
            }
            else
            {
                *ts = rig->caps->tuning_steps[i + 1].ts;
            }

            ts_match = TRUE;
            break;
        }

    rig_debug(RIG_DEBUG_TRACE, "ts_match = %d, i = %d, i+1 = %d, *ts = %d\n",
              ts_match, i, i + 1, (int)*ts);

    if (ts_match)
    {
        return RIG_OK;
    }
    else
    {
        return -RIG_ENAVAIL;
    }
}


int newcat_set_dcs_code(RIG *rig, vfo_t vfo, tone_t code)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_get_dcs_code(RIG *rig, vfo_t vfo, tone_t *code)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_tone(RIG *rig, vfo_t vfo, tone_t tone)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_get_tone(RIG *rig, vfo_t vfo, tone_t *tone)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_ctcss_tone(RIG *rig, vfo_t vfo, tone_t tone)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    int i;
    ncboolean tone_match;
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "CN"))
    {
        return -RIG_ENAVAIL;
    }

    if (!newcat_valid_command(rig, "CT"))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    for (i = 0, tone_match = FALSE; rig->caps->ctcss_list[i] != 0; i++)
        if (tone == rig->caps->ctcss_list[i])
        {
            tone_match = TRUE;
            break;
        }

    rig_debug(RIG_DEBUG_TRACE, "%s: tone = %u, tone_match = %d, i = %d", __func__,
              tone, tone_match, i);

    if (tone_match == FALSE && tone != 0)
    {
        return -RIG_ENAVAIL;
    }

    if (tone == 0) /* turn off ctcss */
    {
        if (is_ft891 || is_ft991 || is_ftdx101)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "CT%c00%c", main_sub_vfo,
                     cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "CT%c0%c", main_sub_vfo,
                     cat_term);
        }
    }
    else
    {
        if (is_ft891 || is_ft991 || is_ftdx101)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "CN%c0%03d%cCT%c2%c",
                     main_sub_vfo, i, cat_term, main_sub_vfo, cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "CN%c%02d%cCT%c2%c",
                     main_sub_vfo, i, cat_term, main_sub_vfo, cat_term);
        }
    }

    return newcat_set_cmd(rig);
}


int newcat_get_ctcss_tone(RIG *rig, vfo_t vfo, tone_t *tone)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    int t;
    int ret_data_len;
    char *retlvl;
    char cmd[] = "CN";
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, cmd))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    if (is_ft891 || is_ft991 || is_ftdx101)
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c0%c", cmd, main_sub_vfo,
                 cat_term);
    }
    else
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c%c", cmd, main_sub_vfo,
                 cat_term);
    }

    /* Get CTCSS TONE */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    ret_data_len = strlen(priv->ret_data);

    /* skip command */
    retlvl = priv->ret_data + strlen(priv->cmd_str) - 1;
    /* chop term */
    priv->ret_data[ret_data_len - 1] = '\0';

    t = atoi(retlvl);   /*  tone index */

    if (t < 0 || t > 49)
    {
        return -RIG_ENAVAIL;
    }

    *tone = rig->caps->ctcss_list[t];

    return RIG_OK;
}


int newcat_set_dcs_sql(RIG *rig, vfo_t vfo, tone_t code)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_get_dcs_sql(RIG *rig, vfo_t vfo, tone_t *code)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_tone_sql(RIG *rig, vfo_t vfo, tone_t tone)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_get_tone_sql(RIG *rig, vfo_t vfo, tone_t *tone)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_ctcss_sql(RIG *rig, vfo_t vfo, tone_t tone)
{
    int err;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    err = newcat_set_ctcss_tone(rig, vfo, tone);

    if (err != RIG_OK)
    {
        return err;
    }

    /* Change to sql */
    if (tone)
    {
        err = newcat_set_func(rig, vfo, RIG_FUNC_TSQL, TRUE);

        if (err != RIG_OK)
        {
            return err;
        }
    }

    return RIG_OK;
}


int newcat_get_ctcss_sql(RIG *rig, vfo_t vfo, tone_t *tone)
{
    int err;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    err = newcat_get_ctcss_tone(rig, vfo, tone);

    return err;
}


int newcat_power2mW(RIG *rig, unsigned int *mwpower, float power, freq_t freq,
                    rmode_t mode)
{
    int rig_id;
    int maxpower;

    rig_id = newcat_get_rigid(rig);
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    switch (rig_id)
    {
    case NC_RIGID_FT450:
        maxpower = 100000;
        break;

    case NC_RIGID_FT950:
        maxpower = 100000;
        break;

    case NC_RIGID_FT2000:
        maxpower = 100000;
        break;

    case NC_RIGID_FT2000D:
        maxpower = 200000;
        break;

    case NC_RIGID_FTDX5000:
        maxpower = 200000;
        break;

    case NC_RIGID_FTDX9000D:
        maxpower = 200000;
        break;

    case NC_RIGID_FTDX9000Contest:
        maxpower = 200000;
        break;

    case NC_RIGID_FTDX9000MP:
        maxpower = 400000;
        break;

    case NC_RIGID_FTDX1200:
        maxpower = 100000;
        break;

    default:
        maxpower = 100000;
    }
    switch (rig_id)
    {
    default:
        /* 20W = 84/255  50W = 148/255, 100W = 208/255 measured in ftdx3000 */
        if (power < 0.3295)
            *mwpower = power * 0.6071 * maxpower;
        else if (power < 0.5804)
            *mwpower = (power - 0.3295) * 1.196 * maxpower + maxpower / 5;
        else
            *mwpower = (power - 0.5804) * 2.125 * maxpower + maxpower / 2;
        break;
    }
    rig_debug(RIG_DEBUG_TRACE, "rig_id = %d, *mwpower = %d\n", rig_id,*mwpower);

    return RIG_OK;
}


int newcat_mW2power(RIG *rig, float *power, unsigned int mwpower, freq_t freq,
                    rmode_t mode)
{
    int rig_id;

    rig_id = newcat_get_rigid(rig);

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    switch (rig_id)
    {
    case NC_RIGID_FT450:
        /* 100 Watts */
        *power = mwpower / 100000.0;
        rig_debug(RIG_DEBUG_TRACE, "case FT450 - rig_id = %d, *power = %f\n", rig_id,
                  *power);
        break;

    case NC_RIGID_FT950:
        /* 100 Watts */
        *power = mwpower / 100000.0;      /* 0..100 Linear scale */
        rig_debug(RIG_DEBUG_TRACE,
                  "case FT950 - rig_id = %d, mwpower = %u, *power = %f\n", rig_id, mwpower,
                  *power);
        break;

    case NC_RIGID_FT2000:
        /* 100 Watts */
        *power = mwpower / 100000.0;
        rig_debug(RIG_DEBUG_TRACE, "case FT2000 - rig_id = %d, *power = %f\n", rig_id,
                  *power);
        break;

    case NC_RIGID_FT2000D:
        /* 200 Watts */
        *power = mwpower / 200000.0;
        rig_debug(RIG_DEBUG_TRACE, "case FT2000D - rig_id = %d, *power = %f\n", rig_id,
                  *power);
        break;

    case NC_RIGID_FTDX5000:
        /* 200 Watts */
        *power = mwpower / 200000.0;
        rig_debug(RIG_DEBUG_TRACE, "case FTDX5000 - rig_id = %d, *power = %f\n", rig_id,
                  *power);
        break;

    case NC_RIGID_FTDX9000D:
        /* 200 Watts */
        *power = mwpower / 200000.0;
        rig_debug(RIG_DEBUG_TRACE, "case FTDX9000D - rig_id = %d, *power = %f\n",
                  rig_id, *power);
        break;

    case NC_RIGID_FTDX9000Contest:
        /* 200 Watts */
        *power = mwpower / 200000.0;
        rig_debug(RIG_DEBUG_TRACE, "case FTDX9000Contest - rig_id = %d, *power = %f\n",
                  rig_id, *power);
        break;

    case NC_RIGID_FTDX9000MP:
        /* 400 Watts */
        *power = mwpower / 400000.0;
        rig_debug(RIG_DEBUG_TRACE, "case FTDX9000MP - rig_id = %d, *power = %f\n",
                  rig_id, *power);
        break;

    case NC_RIGID_FTDX1200:
        /* 100 Watts */
        *power = mwpower / 100000.0;
        rig_debug(RIG_DEBUG_TRACE, "case FTDX1200 - rig_id = %d, *power = %f\n", rig_id,
                  *power);
        break;

    default:
        /* 100 Watts */
        *power = mwpower / 100000.0;
        rig_debug(RIG_DEBUG_TRACE, "default - rig_id = %d, *power = %f\n", rig_id,
                  *power);
    }

    return RIG_OK;
}


int newcat_set_powerstat(RIG *rig, powerstat_t status)
{
    struct rig_state *state = &rig->state;
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char ps;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "PS"))
    {
        return -RIG_ENAVAIL;
    }

    switch (status)
    {
    case RIG_POWER_ON:
        ps = '1';
        // when powering on need a dummy byte to wake it up
        // then sleep  from 1 to 2 seconds so we'll do 1.5 secs
        write_block(&state->rigport, "\n", 1);
        hl_usleep(1500000);
        break;

    case RIG_POWER_OFF:
    case RIG_POWER_STANDBY:
        ps = '0';
        write_block(&state->rigport, "\n", 0);
        break;

    default:
        return -RIG_ENAVAIL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PS%c%c", ps, cat_term);

    err = write_block(&state->rigport, priv->cmd_str, strlen(priv->cmd_str));

    return err;
}


/*
 *  This functions returns an error if the rig is off,  dah
 */
int newcat_get_powerstat(RIG *rig, powerstat_t *status)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char ps;
    char command[] = "PS";

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    *status = RIG_POWER_OFF;

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", command, cat_term);

    /* Get Power status */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    ps = priv->ret_data[2];

    switch (ps)
    {
    case '1':
        *status = RIG_POWER_ON;
        break;

    case '0':
        *status = RIG_POWER_OFF;
        break;

    default:
        return -RIG_ENAVAIL;
    }

    return RIG_OK;
}


int newcat_reset(RIG *rig, reset_t reset)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_ant(RIG *rig, vfo_t vfo, ant_t ant, value_t option)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char which_ant;
    char command[] = "AN";
    char main_sub_vfo = '0';

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    /* Main or SUB vfo */
    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    switch (ant)
    {
    case RIG_ANT_1:
        which_ant = '1';
        break;

    case RIG_ANT_2:
        which_ant = '2';
        break;

    case RIG_ANT_3:
        if (newcat_is_rig(rig, RIG_MODEL_FT950))
        {
            return -RIG_EINVAL;
        }

        if (newcat_is_rig(rig, RIG_MODEL_FTDX1200))
        {
            return -RIG_EINVAL;
        }

        which_ant = '3';
        break;

    case RIG_ANT_4:
        if (newcat_is_rig(rig, RIG_MODEL_FT950))
        {
            return -RIG_EINVAL;
        }

        if (newcat_is_rig(rig, RIG_MODEL_FTDX1200))
        {
            return -RIG_EINVAL;
        }

        which_ant = '4';
        break;

    case RIG_ANT_5:
        if (newcat_is_rig(rig, RIG_MODEL_FT950))
        {
            return -RIG_EINVAL;
        }

        if (newcat_is_rig(rig, RIG_MODEL_FTDX1200))
        {
            return -RIG_EINVAL;
        }

        /* RX only, on FT-2000/FT-5000/FT-9000 */
        which_ant = '5';
        break;

    default:
        return -RIG_EINVAL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c%c%c", command,
             main_sub_vfo, which_ant, cat_term);
    return newcat_set_cmd(rig);
}


int newcat_get_ant(RIG *rig, vfo_t vfo, ant_t dummy, value_t *option,
                   ant_t *ant_curr, ant_t *ant_tx, ant_t *ant_rx)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char c;
    char command[] = "AN";
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    /* Set Main or SUB vfo */
    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c%c", command, main_sub_vfo,
             cat_term);

    /* Get ANT */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    c = priv->ret_data[3];

    switch (c)
    {
    case '1':
        *ant_curr = RIG_ANT_1;
        break;

    case '2' :
        *ant_curr = RIG_ANT_2;
        break;

    case '3' :
        *ant_curr = RIG_ANT_3;
        break;

    case '4' :
        *ant_curr = RIG_ANT_4;
        break;

    case '5' :
        *ant_curr = RIG_ANT_5;
        break;

    default:
        *ant_curr = RIG_ANT_UNKNOWN;
        return -RIG_EPROTO;
    }

    return RIG_OK;
}


int newcat_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val)
{
    struct rig_state *state = &rig->state;
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    int i;
    int scale;
    int fpf;
    char main_sub_vfo = '0';
    char *format;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    /* Set Main or SUB vfo */
    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_LEVEL)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    switch (level)
    {
    case RIG_LEVEL_RFPOWER:
        if (!newcat_valid_command(rig, "PC"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ft950 || is_ftdx1200 || is_ftdx3000 || is_ft891 || is_ft991
                || is_ftdx101)
        {
            scale = 100.;
        }
        else if (is_ft450 && newcat_get_rigid(rig) == NC_RIGID_FT450D)
        {
            scale = 100.;
        }
        else
        {
            scale = 255.;
        }

        fpf = newcat_scale_float(scale, val.f);

        if (is_ft950 || is_ft891 || is_ft991 || is_ftdx3000 || is_ftdx101)
        {
            // Minimum is 5 watts on these rigs
            if (fpf < 5)
            {
                fpf = 5;
            }
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PC%03d%c", fpf, cat_term);
        break;

    case RIG_LEVEL_AF:
        if (!newcat_valid_command(rig, "AG"))
        {
            return -RIG_ENAVAIL;
        }

        fpf = newcat_scale_float(255, val.f);
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AG%c%03d%c", main_sub_vfo, fpf,
                 cat_term);
        break;

    case RIG_LEVEL_AGC:
        if (!newcat_valid_command(rig, "GT"))
        {
            return -RIG_ENAVAIL;
        }

        switch (val.i)
        {
        case RIG_AGC_OFF:
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "GT00;");
            break;

        case RIG_AGC_FAST:
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "GT01;");
            break;

        case RIG_AGC_MEDIUM:
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "GT02;");
            break;

        case RIG_AGC_SLOW:
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "GT03;");
            break;

        case RIG_AGC_AUTO:
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "GT04;");
            break;

        default:
            return -RIG_EINVAL;
        }

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }
        break;

    case RIG_LEVEL_IF:
        if (!newcat_valid_command(rig, "IS"))
        {
            return -RIG_ENAVAIL;
        }

        rig_debug(RIG_DEBUG_TRACE, "%s: LEVEL_IF val.i=%d\n", __func__, val.i);

        if (abs(val.i) > rig->caps->max_ifshift)
        {
            if (val.i > 0)
            {
                val.i = rig->caps->max_ifshift;
            }
            else
            {
                val.i = rig->caps->max_ifshift * -1;
            }
        }

        if (is_ftdx101)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "IS%c0%+.4d%c", main_sub_vfo,
                     val.i, cat_term);
        }
        else if (is_ft891)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "IS0%d%+.4d%c", val.i == 0 ? 0 : 1,
                     val.i, cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "IS%c%+.4d%c", main_sub_vfo,
                     val.i, cat_term);
        }

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE && !is_ft2000)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        // Some Yaesu rigs reject this command in AM/FM modes
        priv->question_mark_response_means_rejected = 1;
        break;

    case RIG_LEVEL_CWPITCH: {
        int kp;
        if (!newcat_valid_command(rig, "KP"))
        {
            return -RIG_ENAVAIL;
        }

        if (val.i < 300)
        {
            i = 300;
        }
        else if (val.i > 1050)
        {
            i = 1050;
        }
        else
        {
            i = val.i;
        }

        if (is_ft950 || is_ft2000)
        {
            kp = (i - 300) / 50;
        }
        else
        {
            // Most Yaesu rigs seem to use range of 0-75 to represent pitch of 300..1050 Hz in 10 Hz steps
            kp = (i - 300) / 10;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "KP%02d%c", kp, cat_term);
        break;
    }

    case RIG_LEVEL_KEYSPD:
        if (!newcat_valid_command(rig, "KS"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "KS%03d%c", val.i, cat_term);
        break;

    case RIG_LEVEL_MICGAIN:
        if (!newcat_valid_command(rig, "MG"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ftdx1200 || is_ftdx3000 || is_ft891 || is_ft991 || is_ftdx101)
        {
            fpf = newcat_scale_float(100, val.f);
        }
        else
        {
            fpf = newcat_scale_float(255, val.f);
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "MG%03d%c", fpf, cat_term);

        // Some Yaesu rigs reject this command in RTTY modes
        priv->question_mark_response_means_rejected = 1;
        break;

    case RIG_LEVEL_METER:
        if (!newcat_valid_command(rig, "MS"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ftdx101) // new format for the command with VFO selection
        {
            format = "MS0%d;";

            if (vfo == RIG_VFO_SUB)
            {
                format = "MS1%d";
            }
        }
        else
        {
            format = "MS%d";
        }

        rig_debug(RIG_DEBUG_TRACE, "%s: format=%s\n", __func__, format);

        switch (val.i)
        {
        case RIG_METER_ALC: snprintf(priv->cmd_str, sizeof(priv->cmd_str), format, 1);
            break;

        case RIG_METER_PO:
            if (newcat_is_rig(rig, RIG_MODEL_FT950))
            {
                return RIG_OK;
            }
            else
            {
                snprintf(priv->cmd_str, sizeof(priv->cmd_str), format, 2);
            }

            break;

        case RIG_METER_SWR:  snprintf(priv->cmd_str, sizeof(priv->cmd_str), format, 3);
            break;

        case RIG_METER_COMP: snprintf(priv->cmd_str, sizeof(priv->cmd_str), format, 0);
            break;

        case RIG_METER_IC:   snprintf(priv->cmd_str, sizeof(priv->cmd_str), format, 4);
            break;

        case RIG_METER_VDD:  snprintf(priv->cmd_str, sizeof(priv->cmd_str), format, 5);
            break;

            rig_debug(RIG_DEBUG_ERR, "%s: unknown val.i=%d\n", __func__, val.i);

        default: return -RIG_EINVAL;
        }

        break;

    case RIG_LEVEL_PREAMP:
        if (!newcat_valid_command(rig, "PA"))
        {
            return -RIG_ENAVAIL;
        }

        if (val.i == 0)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PA00%c", cat_term);

            if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE && !is_ft2000)
            {
                priv->cmd_str[2] = main_sub_vfo;
            }

            break;
        }

        priv->cmd_str[0] = '\0';

        for (i = 0; state->preamp[i] != RIG_DBLST_END; i++)
        {
            if (state->preamp[i] == val.i)
            {
                snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PA0%d%c", i + 1, cat_term);
                break;
            }
        }

        if (strlen(priv->cmd_str) == 0)
        {
            return -RIG_EINVAL;
        }

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_LEVEL_ATT:
        if (!newcat_valid_command(rig, "RA"))
        {
            return -RIG_ENAVAIL;
        }

        if (val.i == 0)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RA00%c", cat_term);

            if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE && !is_ft2000)
            {
                priv->cmd_str[2] = main_sub_vfo;
            }

            break;
        }

        priv->cmd_str[0] = '\0';

        for (i = 0; state->attenuator[i] != RIG_DBLST_END; i++)
        {
            if (state->attenuator[i] == val.i)
            {
                snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RA0%d%c", i + 1, cat_term);
                break;
            }
        }

        if (strlen(priv->cmd_str) == 0)
        {
            return -RIG_EINVAL;
        }

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_LEVEL_RF:
        if (!newcat_valid_command(rig, "RG"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ft891)
        {
            scale = 30;
        }
        else
        {
            scale = 255;
        }

        fpf = newcat_scale_float(scale, val.f);
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RG%c%03d%c", main_sub_vfo, fpf,
                 cat_term);
        break;

    case RIG_LEVEL_NR:
        if (!newcat_valid_command(rig, "RL"))
        {
            return -RIG_ENAVAIL;
        }

        if (newcat_is_rig(rig, RIG_MODEL_FT450))
        {
            fpf = newcat_scale_float(11, val.f);

            if (fpf < 1)
            {
                fpf = 1;
            }

            if (fpf > 11)
            {
                fpf = 11;
            }

            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RL0%02d%c", fpf, cat_term);
        }
        else
        {
            fpf = newcat_scale_float(15, val.f);

            if (fpf < 1)
            {
                fpf = 1;
            }

            if (fpf > 15)
            {
                fpf = 15;
            }

            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RL0%02d%c", fpf, cat_term);

            if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE && !is_ft2000)
            {
                priv->cmd_str[2] = main_sub_vfo;
            }
        }

        // Some Yaesu rigs reject this command in AM/FM modes
        priv->question_mark_response_means_rejected = 1;
        break;

    case RIG_LEVEL_COMP:
        if (!newcat_valid_command(rig, "PL"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ft2000 || is_ftdx9000 || is_ftdx5000)
        {
            scale = 255;
        }
        else
        {
            scale = 100;
        }

        fpf = newcat_scale_float(scale, val.f);
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PL%03d%c", fpf, cat_term);
        break;

    case RIG_LEVEL_BKINDL: {
        int millis;
        value_t keyspd;

        if (!newcat_valid_command(rig, "SD"))
        {
            return -RIG_ENAVAIL;
        }

        // Convert 10/ths of dots to milliseconds using the current key speed
        err = newcat_get_level(rig, vfo, RIG_LEVEL_KEYSPD, &keyspd);
        if (err != RIG_OK)
        {
            return err;
        }

        millis = dot10ths_to_millis(val.i, keyspd.i);

        if (is_ftdx101)
        {
            if (millis <= 30) { snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD00;"); }
            else if (millis <= 50) { snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD01;"); }
            else if (millis <= 100) { snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD02;"); }
            else if (millis <= 150) { snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD03;"); }
            else if (millis <= 200) { snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD04;"); }
            else if (millis <= 250) { snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD05;"); }
            else if (millis > 2900) { snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD33;"); }
            else
            {
                // This covers 300-2900 06-32
                snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD%02d;", 6 + ((millis - 300) / 100));
            }
        }
        else if (is_ftdx5000)
        {
            if (millis < 20)
            {
                millis = 20;
            }
            if (millis > 5000)
            {
                millis = 5000;
            }

            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD%04d%c", millis, cat_term);
        }
        else if (is_ft950 || is_ft450 || is_ft891 || is_ft991 || is_ftdx1200 || is_ftdx3000)
        {
            if (millis < 30)
            {
                millis = 30;
            }
            if (millis > 3000)
            {
                millis = 3000;
            }

            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD%04d%c", millis, cat_term);
        }
        else if (is_ft2000 || is_ftdx9000)
        {
            if (millis < 0)
            {
                millis = 0;
            }
            if (millis > 5000)
            {
                millis = 5000;
            }

            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD%04d%c", millis, cat_term);
        }
        else // default
        {
            if (millis < 1)
            {
                millis = 1;
            }
            if (millis > 5000)
            {
                millis = 5000;
            }

            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD%04d%c", millis, cat_term);
        }

        break;
    }

    case RIG_LEVEL_SQL:
        if (!newcat_valid_command(rig, "SQ"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ft891 || is_ft991 || is_ftdx101)
        {
            scale = 100;
        }
        else
        {
            scale = 255;
        }

        fpf = newcat_scale_float(scale, val.f);
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SQ%c%03d%c", main_sub_vfo, fpf,
                 cat_term);
        break;

    case RIG_LEVEL_VOXDELAY:
        if (!newcat_valid_command(rig, "VD"))
        {
            return -RIG_ENAVAIL;
        }

        /* VOX delay, api int (tenth of seconds), ms for rig */
        val.i = val.i * 100;
        rig_debug(RIG_DEBUG_TRACE, "%s: vali=%d\n", __func__, val.i);

        if (is_ft950 || is_ft450 || is_ftdx1200)
        {
            if (val.i < 100)         /* min is 30ms but spec is 100ms Unit Intervals */
            {
                val.i = 30;
            }

            if (val.i > 3000)
            {
                val.i = 3000;
            }

            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VD%04d%c", val.i, cat_term);
        }
        else if (is_ftdx101) // new lookup table argument
        {
            rig_debug(RIG_DEBUG_TRACE, "%s: ft101 #1 val.i=%d\n", __func__, val.i);

            if (val.i == 0) { val.i = 0; }
            else if (val.i <= 100) { val.i = 2; }
            else if (val.i <= 200) { val.i = 4; }
            else if (val.i > 3000) { val.i = 33; }
            else { val.i = (val.i - 300) / 100 + 6; }

            rig_debug(RIG_DEBUG_TRACE, "%s: ft101 #1 val.i=%d\n", __func__, val.i);

            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VD%02d%c", val.i, cat_term);
        }
        else if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            if (val.i < 0)
            {
                val.i = 0;
            }

            if (val.i > 5000)
            {
                val.i = 5000;
            }

            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VD%04d%c", val.i, cat_term);
        }

        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VD%04d%c", val.i, cat_term);
        }

        break;

    case RIG_LEVEL_VOXGAIN:
        if (!newcat_valid_command(rig, "VG"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ft2000 || is_ftdx9000 || is_ftdx5000)
        {
            scale = 255;
        }
        else
        {
            scale = 100;
        }

        fpf = newcat_scale_float(scale, val.f);
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VG%03d%c", fpf, cat_term);
        break;

    case RIG_LEVEL_ANTIVOX:
        if (is_ftdx101)
        {
            fpf = newcat_scale_float(100, val.f);
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AV%03d%c", fpf, cat_term);
        }
        else if (is_ftdx5000)
        {
            fpf = newcat_scale_float(100, val.f);
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX176%03d%c", fpf, cat_term);
        }
        else if (is_ftdx3000 || is_ftdx1200)
        {
            fpf = newcat_scale_float(100, val.f);
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX183%03d%c", fpf, cat_term);
        }
        else if (is_ft991)
        {
            fpf = newcat_scale_float(100, val.f);
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX147%03d%c", fpf, cat_term);
        }
        else if (is_ft891)
        {
            fpf = newcat_scale_float(100, val.f);
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX1619%03d%c", fpf, cat_term);
        }
        else if (is_ft950)
        {
            fpf = newcat_scale_float(100, val.f);
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX117%03d%c", fpf, cat_term);
        }
        else if (is_ft2000)
        {
            fpf = newcat_scale_float(100, val.f);
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX042%03d%c", fpf, cat_term);
        }
        else
        {
            return -RIG_EINVAL;
        }

        break;

    case RIG_LEVEL_NOTCHF:
        if (!newcat_valid_command(rig, "BP"))
        {
            return -RIG_ENAVAIL;
        }

        val.i = val.i / 10;

        if (is_ftdx9000)
        {
            if (val.i < 0)
            {
                val.i = 0;
            }
        }
        else
        {
            if (val.i < 1)
            {
                val.i = 1;
            }
        }

        if (is_ft891 || is_ft991 || is_ftdx101)
        {
            if (val.i > 320)
            {
                val.i = 320;
            }
        }
        if (is_ft950 || is_ftdx9000)
        {
            if (val.i > 300)
            {
                val.i = 300;
            }
        }
        else
        {
            if (val.i > 400)
            {
                val.i = 400;
            }
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BP01%03d%c", val.i, cat_term);

        if (is_ftdx9000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BP%03d%c", val.i, cat_term);
        }
        else if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_LEVEL_MONITOR_GAIN:
        if (!newcat_valid_command(rig, "ML"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ftdx1200 || is_ftdx3000 || is_ft891 || is_ft991 || is_ftdx101)
        {
            fpf = newcat_scale_float(100, val.f);
        }
        else
        {
            fpf = newcat_scale_float(255, val.f);
        }

        if (is_ftdx9000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "ML%03d%c", fpf, cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "ML1%03d%c", fpf, cat_term);
        }
        break;

    default:
        return -RIG_EINVAL;
    }

    err = newcat_set_cmd(rig);

    // Clear flag after executing command
    priv->question_mark_response_means_rejected = 0;

    return err;
}


int newcat_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val)
{
    struct rig_state *state = &rig->state;
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    int ret_data_len;
    char *retlvl;
    int retlvl_len;
    float scale;
    char main_sub_vfo = '0';
    int i;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    /* Set Main or SUB vfo */
    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_LEVEL)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    switch (level)
    {
    case RIG_LEVEL_PREAMP:
        if (!newcat_valid_command(rig, "PA"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PA0%c", cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_LEVEL_AF:
        if (!newcat_valid_command(rig, "AG"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AG%c%c", main_sub_vfo,
                 cat_term);
        break;

    case RIG_LEVEL_AGC:
        if (!newcat_valid_command(rig, "GT"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "GT%c%c", main_sub_vfo,
                 cat_term);
        break;

    case RIG_LEVEL_IF:
        if (!newcat_valid_command(rig, "IS"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "IS%c%c", main_sub_vfo,
                 cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }
        break;

    case RIG_LEVEL_CWPITCH:
        if (!newcat_valid_command(rig, "KP"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "KP%c", cat_term);
        break;

    case RIG_LEVEL_KEYSPD:
        if (!newcat_valid_command(rig, "KS"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "KS%c", cat_term);
        break;

    case RIG_LEVEL_MICGAIN:
        if (!newcat_valid_command(rig, "MG"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "MG%c", cat_term);
        break;

    case RIG_LEVEL_METER:
        if (!newcat_valid_command(rig, "MS"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "MS%c", cat_term);
        break;

    case RIG_LEVEL_ATT:
        if (!newcat_valid_command(rig, "RA"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RA0%c", cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_LEVEL_RF:
        if (!newcat_valid_command(rig, "RG"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RG%c%c", main_sub_vfo,
                 cat_term);
        break;

    case RIG_LEVEL_COMP:
        if (!newcat_valid_command(rig, "PL"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PL%c", cat_term);
        break;

    case RIG_LEVEL_NR:
        if (!newcat_valid_command(rig, "RL"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RL0%c", cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_LEVEL_BKINDL:
        if (!newcat_valid_command(rig, "SD"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SD%c", cat_term);
        break;

    case RIG_LEVEL_SQL:
        if (!newcat_valid_command(rig, "SQ"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SQ%c%c", main_sub_vfo,
                 cat_term);
        break;

    case RIG_LEVEL_VOXDELAY:

        /* VOX delay, arg int (tenth of seconds) */
        if (!newcat_valid_command(rig, "VD"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VD%c", cat_term);
        break;

    case RIG_LEVEL_VOXGAIN:
        if (!newcat_valid_command(rig, "VG"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VG%c", cat_term);
        break;

    /*
     * Read only levels
     */
    case RIG_LEVEL_STRENGTH:
    case RIG_LEVEL_RAWSTR:
        if (!newcat_valid_command(rig, "SM"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SM%c%c", main_sub_vfo,
                 cat_term);
        break;

    case RIG_LEVEL_RFPOWER:
        if (!newcat_valid_command(rig, "RM"))
        {
            return -RIG_ENAVAIL;
        }

        if (newcat_is_rig(rig, RIG_MODEL_FT9000))
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RM08%c", cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RM5%c", cat_term);
        }

        break;

    case RIG_LEVEL_SWR:
        if (!newcat_valid_command(rig, "RM"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RM6%c", cat_term);
        break;

    case RIG_LEVEL_ALC:
        if (!newcat_valid_command(rig, "RM"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RM4%c", cat_term);
        break;

    case RIG_LEVEL_RFPOWER_METER:
        if (!newcat_valid_command(rig, "RM"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RM5%c", cat_term);
        break;

    case RIG_LEVEL_COMP_METER:
        if (!newcat_valid_command(rig, "RM"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RM3%c", cat_term);
        break;

    case RIG_LEVEL_VD_METER:
        if (!newcat_valid_command(rig, "RM"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RM8%c", cat_term);
        break;

    case RIG_LEVEL_ID_METER:
        if (!newcat_valid_command(rig, "RM"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RM7%c", cat_term);
        break;

    case RIG_LEVEL_ANTIVOX:
        if (is_ftdx101)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AV%c", cat_term);
        }
        else if (is_ftdx5000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX176%c", cat_term);
        }
        else if (is_ftdx3000 || is_ftdx1200)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX183%c", cat_term);
        }
        else if (is_ftdx1200)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX183%c", cat_term);
        }
        else if (is_ft991)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX147%c", cat_term);
        }
        else if (is_ft891)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX1619%c", cat_term);
        }
        else if (is_ft950)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX117%c", cat_term);
        }
        else if (is_ft2000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "EX042%c", cat_term);
        }
        else
        {
            return -RIG_EINVAL;
        }

        break;

    case RIG_LEVEL_NOTCHF:
        if (!newcat_valid_command(rig, "BP"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BP01%c", cat_term);

        if (is_ftdx9000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BP%c", cat_term);
        }
        else if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_LEVEL_MONITOR_GAIN:
        if (!newcat_valid_command(rig, "ML"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ftdx9000)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "ML%c", cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "ML1%c", cat_term);
        }
        break;

    default:
        return -RIG_EINVAL;
    }

    err = newcat_get_cmd(rig);

    if (err != RIG_OK)
    {
        return err;
    }

    ret_data_len = strlen(priv->ret_data);

    /* skip command */
    retlvl = priv->ret_data + strlen(priv->cmd_str) - 1;
    retlvl_len = strlen(retlvl);
    /* chop term */
    priv->ret_data[ret_data_len - 1] = '\0';

    switch (level)
    {
    case RIG_LEVEL_RFPOWER:
        if (is_ft950 || is_ftdx1200 || is_ftdx3000 || is_ft891 || is_ft991
                || is_ftdx101)
        {
            scale = 100.;
        }
        else if (is_ft450 && newcat_get_rigid(rig) == NC_RIGID_FT450D)
        {
            scale = 100.;
        }
        else
        {
            scale = 255.;
        }

        scale = 255.;   //kui PWERMETER tuleb siis see rida ära
        val->f = (float)atoi(retlvl) / scale;
        break;

    case RIG_LEVEL_VOXGAIN:
    case RIG_LEVEL_COMP:
        if (is_ft2000 || is_ftdx9000 || is_ftdx5000)
        {
            scale = 255;
        }
        else
        {
            scale = 100;
        }

        val->f = (float) atoi(retlvl) / scale;
        break;

    case RIG_LEVEL_ANTIVOX:
        val->f = (float) atoi(retlvl) / 100.;
        break;

    case RIG_LEVEL_SWR:
        if (retlvl_len > 3)
        {
            // Some rigs like FTDX101 have 6-byte return so we just truncate
            retlvl[3] = 0;
        }

        if (rig->caps->swr_cal.size == 0)
        {
            val->f = rig_raw2val_float(atoi(retlvl), &yaesu_default_swr_cal);
        }
        else
        {
            val->f = rig_raw2val_float(atoi(retlvl), &rig->caps->swr_cal);
        }

        break;

    case RIG_LEVEL_ALC:
        if (retlvl_len > 3)
        {
            // Some rigs like FTDX101 have 6-byte return so we just truncate
            retlvl[3] = 0;
        }

        if (rig->caps->alc_cal.size == 0)
        {
            val->f = rig_raw2val_float(atoi(retlvl), &yaesu_default_alc_cal);
        }
        else
        {
            val->f = rig_raw2val_float(atoi(retlvl), &rig->caps->alc_cal);
        }

        break;

    case RIG_LEVEL_RFPOWER_METER:
        if (retlvl_len > 3)
        {
            // Some rigs like FTDX101 have 6-byte return so we just truncate
            retlvl[3] = 0;
        }

        if (rig->caps->rfpower_meter_cal.size == 0)
        {
            val->f = rig_raw2val_float(atoi(retlvl), &yaesu_default_rfpower_meter_cal);
        }
        else
        {
            val->f = rig_raw2val_float(atoi(retlvl), &rig->caps->rfpower_meter_cal);
        }

        break;

    case RIG_LEVEL_COMP_METER:
        if (retlvl_len > 3)
        {
            // Some rigs like FTDX101 have 6-byte return so we just truncate
            retlvl[3] = 0;
        }

        if (rig->caps->comp_meter_cal.size == 0)
        {
            val->f = rig_raw2val_float(atoi(retlvl), &yaesu_default_comp_meter_cal);
        }
        else
        {
            val->f = rig_raw2val_float(atoi(retlvl), &rig->caps->comp_meter_cal);
        }

        break;

    case RIG_LEVEL_VD_METER:
        if (retlvl_len > 3)
        {
            // Some rigs like FTDX101 have 6-byte return so we just truncate
            retlvl[3] = 0;
        }

        if (rig->caps->vd_meter_cal.size == 0)
        {
            val->f = rig_raw2val_float(atoi(retlvl), &yaesu_default_vd_meter_cal);
        }
        else
        {
            val->f = rig_raw2val_float(atoi(retlvl), &rig->caps->vd_meter_cal);
        }

        break;

    case RIG_LEVEL_ID_METER:
        if (retlvl_len > 3)
        {
            // Some rigs like FTDX101 have 6-byte return so we just truncate
            retlvl[3] = 0;
        }

        if (rig->caps->id_meter_cal.size == 0)
        {
            val->f = rig_raw2val_float(atoi(retlvl), &yaesu_default_id_meter_cal);
        }
        else
        {
            val->f = rig_raw2val_float(atoi(retlvl), &rig->caps->id_meter_cal);
        }

        break;

    case RIG_LEVEL_MICGAIN:
        if (is_ftdx1200 || is_ftdx3000 || is_ft891 || is_ft991 || is_ftdx101)
        {
            scale = 100.;
        }
        else
        {
            scale = 255.;
        }

        val->f = (float)atoi(retlvl) / scale;
        break;

    case RIG_LEVEL_AF:
        val->f = (float)atoi(retlvl) / 255;
        break;

    case RIG_LEVEL_RF:
        if (is_ft891)
        {
            scale = 30.;
        }
        else
        {
            scale = 255.;
        }

        val->f = (float)atoi(retlvl) / scale;
        break;

    case RIG_LEVEL_SQL:
        if (is_ft891 || is_ft991 || is_ftdx101)
        {
            scale = 100.;
        }
        else
        {
            scale = 255.;
        }

        val->f = (float)atoi(retlvl) / scale;
        break;

    case RIG_LEVEL_BKINDL: {
        int raw_value = atoi(retlvl);
        int millis;
        value_t keyspd;

        if (is_ftdx101)
        {
            switch (raw_value)
            {
            case 0: millis = 30; break;
            case 1: millis = 50; break;
            case 2: millis = 100; break;
            case 3: millis = 150; break;
            case 4: millis = 200; break;
            case 5: millis = 250; break;
            case 6: millis = 300; break;
            default:
                millis = (raw_value - 6) * 100 + 300;
            }
        }
        else
        {
            // The rest of Yaesu rigs indicate break-in delay directly as milliseconds
            millis = raw_value;
        }

        // Convert milliseconds to 10/ths of dots using the current key speed
        err = newcat_get_level(rig, vfo, RIG_LEVEL_KEYSPD, &keyspd);
        if (err != RIG_OK)
        {
            return err;
        }

        val->i = millis_to_dot10ths(millis, keyspd.i);
        break;
    }
    case RIG_LEVEL_STRENGTH:
        if (rig->caps->str_cal.size > 0)
        {
            val->i = round(rig_raw2val(atoi(retlvl), &rig->caps->str_cal));
            break;
        }

        if (is_ftdx1200 || is_ftdx3000 || is_ftdx5000 || is_ft891 || is_ft991
                || is_ftdx101)
        {
            val->i = round(rig_raw2val(atoi(retlvl), &yaesu_default_str_cal));
        }
        else
        {
            // Some Yaesu rigs return straight S-meter answers
            // Return dbS9 -- does >S9 mean 10dB increments? If not, add to rig driver
            if (val->i > 0)
            {
                val->i = (atoi(retlvl) - 9) * 10;
            }
            else
            {
                val->i = (atoi(retlvl) - 9) * 6;
            }
        }

        break;

    case RIG_LEVEL_RAWSTR:
    case RIG_LEVEL_KEYSPD:
        val->i = atoi(retlvl);
        break;

    case RIG_LEVEL_IF:
        // IS00+0400
        rig_debug(RIG_DEBUG_TRACE, "%s: ret_data=%s(%d), retlvl=%s\n", __func__,
                  priv->ret_data, (int)strlen(priv->ret_data), retlvl);

        if (strlen(priv->ret_data) == 9)
        {
            int n = sscanf(priv->ret_data, "IS%*c0%d\n", &val->i);

            if (n != 1)
            {
                rig_debug(RIG_DEBUG_ERR, "%s: unable to parse level from  %s\n", __func__,
                          priv->ret_data);
            }
        }
        else
        {
            val->i = atoi(retlvl);
        }

        break;

    case RIG_LEVEL_NR:
        if (is_ft450)
        {
            val->f = (float)(atoi(retlvl) / 11.);
        }
        else
        {
            val->f = (float)(atoi(retlvl) / 15.);
        }

        break;

    case RIG_LEVEL_VOXDELAY:
        val->i = atoi(retlvl);

        if (is_ftdx101)
        {
            switch (val->i)
            {
            case 0:  val->i = 0; break; // 30ms=0 we only do tenths

            case 1:  val->i = 0; break; // 50ms=0

            case 2:  val->i = 1; break; // 100ms=1

            case 3:  val->i = 1; break; // 150ms=1

            case 4:  val->i = 2; break; // 200ms=2

            case 5:  val->i = 2; break; // 250ms=2

            default:
                val->i = (val->i - 6) + 3;
                break;
            }
        }
        else
        {
            /* VOX delay, arg int (tenth of seconds), rig in ms */
            val->i /= 10;  // Convert from ms to tenths
        }

        break;

    case RIG_LEVEL_PREAMP: {
        int preamp;

        if (retlvl[0] < '0' || retlvl[0] > '9')
        {
            return -RIG_EPROTO;
        }

        preamp = retlvl[0] - '0';

        val->i = 0;

        if (preamp > 0)
        {
            for (i = 0; state->preamp[i] != RIG_DBLST_END; i++)
            {
                if (i == preamp - 1)
                {
                    val->i = state->preamp[i];
                    break;
                }
            }
        }
        break;
    }

    case RIG_LEVEL_ATT: {
        int att;

        if (retlvl[0] < '0' || retlvl[0] > '9')
        {
            return -RIG_EPROTO;
        }

        att = retlvl[0] - '0';

        val->i = 0;

        if (att > 0)
        {
            for (i = 0; state->attenuator[i] != RIG_DBLST_END; i++)
            {
                if (i == att - 1)
                {
                    val->i = state->attenuator[i];
                    break;
                }
            }
        }
        break;
    }

    case RIG_LEVEL_AGC:
        switch (retlvl[0])
        {
        case '0':
            val->i = RIG_AGC_OFF;
            break;
        case '1':
            val->i = RIG_AGC_FAST;
            break;
        case '2':
            val->i = RIG_AGC_MEDIUM;
            break;
        case '3':
            val->i = RIG_AGC_SLOW;
            break;
        case '4':
        case '5':
        case '6':
            val->i = RIG_AGC_AUTO; break;

        default:
            return -RIG_EPROTO;
        }

        break;

    case RIG_LEVEL_CWPITCH:
        if (is_ft950 || is_ft2000)
        {
            val->i = (atoi(retlvl) * 50) + 300;
        }
        else
        {
            // Most Yaesu rigs seem to use range of 0-75 to represent pitch of 300..1050 Hz in 10 Hz steps
            val->i = (atoi(retlvl) * 10) + 300;
        }
        break;

    case RIG_LEVEL_METER:
        switch (retlvl[0])
        {
        case '0': val->i = RIG_METER_COMP; break;

        case '1': val->i = RIG_METER_ALC; break;

        case '2': val->i = RIG_METER_PO; break;

        case '3': val->i = RIG_METER_SWR; break;

        case '4': val->i = RIG_METER_IC; break;     /* ID CURRENT */

        case '5': val->i = RIG_METER_VDD; break;    /* Final Amp Voltage */

        default: return -RIG_EPROTO;
        }

        break;

    case RIG_LEVEL_NOTCHF:
        val->i = atoi(retlvl) * 10;
        break;

    case RIG_LEVEL_MONITOR_GAIN:
        if (is_ftdx1200 || is_ftdx3000 || is_ft891 || is_ft991 || is_ftdx101)
        {
            scale = 100.;
        }
        else
        {
            scale = 255.;
        }

        val->f = (float)atoi(retlvl) / scale;
        break;

    default:
        return -RIG_EINVAL;
    }

    return RIG_OK;
}


int newcat_set_func(RIG *rig, vfo_t vfo, setting_t func, int status)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    /* Set Main or SUB vfo */
    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & (RIG_TARGETABLE_MODE | RIG_TARGETABLE_TONE))
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    switch (func)
    {
    case RIG_FUNC_ANF:
        if (!newcat_valid_command(rig, "BC"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BC0%d%c", status ? 1 : 0,
                 cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE && !is_ft2000)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        // Some Yaesu rigs reject this command in AM/FM modes
        priv->question_mark_response_means_rejected = 1;
        break;

    case RIG_FUNC_MN:
        if (!newcat_valid_command(rig, "BP"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BP00%03d%c", status ? 1 : 0,
                 cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE && !is_ft2000)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        // Some Yaesu rigs reject this command in AM/FM modes
        priv->question_mark_response_means_rejected = 1;
        break;

    case RIG_FUNC_FBKIN:
        if (!newcat_valid_command(rig, "BI"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BI%d%c", status ? 1 : 0,
                 cat_term);
        break;

    case RIG_FUNC_TONE:
        if (!newcat_valid_command(rig, "CT"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "CT0%d%c", status ? 2 : 0,
                 cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_TONE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_FUNC_TSQL:
        if (!newcat_valid_command(rig, "CT"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "CT0%d%c", status ? 1 : 0,
                 cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_TONE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_FUNC_LOCK:
        if (!newcat_valid_command(rig, "LK"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ftdx1200 || is_ftdx3000 || is_ftdx5000 || is_ftdx101)
        {
            // These rigs can lock Main/Sub VFO dials individually
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "LK%d%c", status ? 7 : 4,
                     cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "LK%d%c", status ? 1 : 0,
                     cat_term);
        }

        break;

    case RIG_FUNC_MON:
        if (!newcat_valid_command(rig, "ML"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "ML0%03d%c", status ? 1 : 0,
                 cat_term);
        break;

    case RIG_FUNC_NB:
        if (!newcat_valid_command(rig, "NB"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "NB0%d%c", status ? 1 : 0,
                 cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_FUNC_NR:
        if (!newcat_valid_command(rig, "NR"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "NR0%d%c", status ? 1 : 0,
                 cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        // Some Yaesu rigs reject this command in AM/FM modes
        priv->question_mark_response_means_rejected = 1;
        break;

    case RIG_FUNC_COMP:
        if (!newcat_valid_command(rig, "PR"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ft891 || is_ft991 || is_ftdx1200 || is_ftdx3000 || is_ftdx101)
        {
            // There seems to be an error in the manuals for some of these rigs stating that values should be 1 = OFF and 2 = ON, but they are 0 = OFF and 1 = ON instead
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PR0%d%c", status ? 1 : 0, cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PR%d%c", status ? 1 : 0, cat_term);
        }

        break;

    case RIG_FUNC_VOX:
        if (!newcat_valid_command(rig, "VX"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VX%d%c", status ? 1 : 0,
                 cat_term);
        break;

    case RIG_FUNC_TUNER:
        if (!newcat_valid_command(rig, "AC"))
        {
            return -RIG_ENAVAIL;
        }

        // some rigs use AC02 to actually start tuning
        if (status == 1 && (is_ftdx101 || is_ftdx5000)) { status = 2; }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AC00%d%c",
                 status == 0 ? 0 : status,
                 cat_term);
        break;

    case RIG_FUNC_RIT:
        if (!newcat_valid_command(rig, "RT"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RT%d%c", status ? 1 : 0,
                 cat_term);
        break;

    case RIG_FUNC_XIT:
        if (!newcat_valid_command(rig, "XT"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "XT%d%c", status ? 1 : 0,
                 cat_term);
        break;

    default:
        return -RIG_EINVAL;
    }

    err = newcat_set_cmd(rig);

    // Clear flag after executing command
    priv->question_mark_response_means_rejected = 0;

    return err;
}


int newcat_get_func(RIG *rig, vfo_t vfo, setting_t func, int *status)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    int ret_data_len;
    int last_char_index;
    char *retfunc;
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (rig->caps->targetable_vfo & (RIG_TARGETABLE_MODE | RIG_TARGETABLE_TONE))
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    switch (func)
    {
    case RIG_FUNC_ANF:
        if (!newcat_valid_command(rig, "BC"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BC0%c", cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_FUNC_MN:
        if (!newcat_valid_command(rig, "BP"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BP00%c", cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_FUNC_FBKIN:
        if (!newcat_valid_command(rig, "BI"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BI%c", cat_term);
        break;

    case RIG_FUNC_TONE:
        if (!newcat_valid_command(rig, "CT"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "CT0%c", cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_TONE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_FUNC_TSQL:
        if (!newcat_valid_command(rig, "CT"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "CT0%c", cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_TONE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_FUNC_LOCK:
        if (!newcat_valid_command(rig, "LK"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "LK%c", cat_term);
        break;

    case RIG_FUNC_MON:
        if (!newcat_valid_command(rig, "ML"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "ML0%c", cat_term);
        break;

    case RIG_FUNC_NB:
        if (!newcat_valid_command(rig, "NB"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "NB0%c", cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_FUNC_NR:
        if (!newcat_valid_command(rig, "NR"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "NR0%c", cat_term);

        if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
        {
            priv->cmd_str[2] = main_sub_vfo;
        }

        break;

    case RIG_FUNC_COMP:
        if (!newcat_valid_command(rig, "PR"))
        {
            return -RIG_ENAVAIL;
        }

        if (is_ftdx1200 || is_ftdx3000 || is_ft891 || is_ft991 || is_ftdx101)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PR0%c", cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "PR%c", cat_term);
        }

        break;

    case RIG_FUNC_VOX:
        if (!newcat_valid_command(rig, "VX"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VX%c", cat_term);
        break;

    case RIG_FUNC_TUNER:
        if (!newcat_valid_command(rig, "AC"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AC%c", cat_term);
        break;

    case RIG_FUNC_RIT:
        if (!newcat_valid_command(rig, "RT"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RT%c", cat_term);
        break;

    case RIG_FUNC_XIT:
        if (!newcat_valid_command(rig, "XT"))
        {
            return -RIG_ENAVAIL;
        }

        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "XT%c", cat_term);
        break;

    default:
        return -RIG_EINVAL;
    }

    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    ret_data_len = strlen(priv->ret_data);

    /* skip command */
    retfunc = priv->ret_data + strlen(priv->cmd_str) - 1;
    /* chop term */
    priv->ret_data[ret_data_len - 1] = '\0';

    last_char_index = strlen(retfunc) - 1;

    rig_debug(RIG_DEBUG_TRACE, "%s: retfunc='%s'\n", __func__, retfunc);

    switch (func)
    {
    case RIG_FUNC_MN:
        *status = (retfunc[2] == '0') ? 0 : 1;
        break;

    case RIG_FUNC_COMP:
        *status = (retfunc[0] == '0') ? 0 : 1;
        break;

    case RIG_FUNC_MON:
        // The number of digits varies by rig, but the last digit indicates the status always
        *status = (retfunc[last_char_index] == '0') ? 0 : 1;
        break;

    case RIG_FUNC_LOCK:
        if (is_ftdx1200 || is_ftdx3000 || is_ftdx5000 || is_ftdx101)
        {
            // These rigs can lock Main/Sub VFO dials individually
            *status = (retfunc[0] == '0' || retfunc[0] == '4') ? 0 : 1;
        }
        else
        {
            *status = (retfunc[0] == '0') ? 0 : 1;
        }

        break;

    case RIG_FUNC_ANF:
    case RIG_FUNC_FBKIN:
    case RIG_FUNC_NB:
    case RIG_FUNC_NR:
    case RIG_FUNC_VOX:
        *status = (retfunc[0] == '0') ? 0 : 1;
        break;

    case RIG_FUNC_TONE:
        *status = (retfunc[0] == '2') ? 1 : 0;
        break;

    case RIG_FUNC_TSQL:
        *status = (retfunc[0] == '1') ? 1 : 0;
        break;

    case RIG_FUNC_TUNER:
        *status = (retfunc[2] == '1') ? 1 : 0;
        break;

    case RIG_FUNC_RIT:
        *status = (retfunc[0] == '1') ? 1 : 0;
        break;

    case RIG_FUNC_XIT:
        *status = (retfunc[0] == '1') ? 1 : 0;
        break;

    default:
        return -RIG_EINVAL;
    }

    return RIG_OK;
}


int newcat_set_parm(RIG *rig, setting_t parm, value_t val)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_get_parm(RIG *rig, setting_t parm, value_t *val)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_ext_level(RIG *rig, vfo_t vfo, token_t token, value_t val)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    switch (token)
    {
    case TOK_ROOFING_FILTER:
        return set_roofing_filter(rig, vfo, val.i);

    default:
        rig_debug(RIG_DEBUG_ERR, "%s: Unsupported ext level %s\n", __func__,
                  rig_strlevel(token));
        return -RIG_EINVAL;
    }
}

int newcat_get_ext_level(RIG *rig, vfo_t vfo, token_t token, value_t *val)
{
    int retval;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    switch (token)
    {
    case TOK_ROOFING_FILTER:
    {
        struct newcat_roofing_filter *roofing_filter;
        retval = get_roofing_filter(rig, vfo, &roofing_filter);

        if (retval != RIG_OK)
        {
            return retval;
        }

        val->i = roofing_filter->index;
        break;
    }

    default:
        rig_debug(RIG_DEBUG_ERR, "%s: Unsupported ext level %s\n", __func__,
                  rig_strlevel(token));
        return -RIG_EINVAL;
    }

    return RIG_OK;
}

int newcat_set_ext_parm(RIG *rig, token_t token, value_t val)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_get_ext_parm(RIG *rig, token_t token, value_t *val)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_send_dtmf(RIG *rig, vfo_t vfo, const char *digits)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_recv_dtmf(RIG *rig, vfo_t vfo, char *digits, int *length)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_send_morse(RIG *rig, vfo_t vfo, const char *msg)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_bank(RIG *rig, vfo_t vfo, int bank)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_mem(RIG *rig, vfo_t vfo, int ch)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err, i;
    ncboolean restore_vfo;
    chan_t *chan_list;
    channel_t valid_chan;
    channel_cap_t *mem_caps = NULL;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "MC"))
    {
        return -RIG_ENAVAIL;
    }

    chan_list = rig->caps->chan_list;

    for (i = 0; i < CHANLSTSIZ && !RIG_IS_CHAN_END(chan_list[i]); i++)
    {
        if (ch >= chan_list[i].startc &&
                ch <= chan_list[i].endc)
        {
            mem_caps = &chan_list[i].mem_caps;
            break;
        }
    }

    /* Test for valid usable channel, skip if empty */
    memset(&valid_chan, 0, sizeof(channel_t));
    valid_chan.channel_num = ch;
    err = newcat_get_channel(rig, &valid_chan, 1);

    if (err < 0)
    {
        return err;
    }

    if (valid_chan.freq <= 1.0)
    {
        mem_caps = NULL;
    }

    rig_debug(RIG_DEBUG_TRACE, "%s: valChan Freq = %f\n", __func__,
              valid_chan.freq);

    /* Out of Range, or empty */
    if (!mem_caps)
    {
        return -RIG_ENAVAIL;
    }

    /* set to usable vfo if needed */
    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    /* Restore to VFO mode or leave in Memory Mode */
    switch (vfo)
    {
    case RIG_VFO_A:
    case RIG_VFO_MAIN:
        /* Jump back from memory channel */
        restore_vfo = TRUE;
        break;

    case RIG_VFO_MEM:
        /* Jump from channel to channel in memory mode */
        restore_vfo = FALSE;
        break;

    case RIG_VFO_B:
    case RIG_VFO_SUB:
    default:
        /* Only works with VFO A */
        return -RIG_ENTARGET;
    }

    /* Set Memory Channel Number ************** */
    rig_debug(RIG_DEBUG_TRACE, "channel_num = %d, vfo = %s\n", ch, rig_strvfo(vfo));

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "MC%03d%c", ch, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    priv->question_mark_response_means_rejected = 1;
    err = newcat_set_cmd(rig);
    priv->question_mark_response_means_rejected = 0;

    if (err != RIG_OK)
    {
        return err;
    }

    /* Restore VFO even if setting to blank memory channel */
    if (restore_vfo)
    {
        err = newcat_vfomem_toggle(rig);

        if (err != RIG_OK)
        {
            return err;
        }
    }

    return RIG_OK;
}


int newcat_get_mem(RIG *rig, vfo_t vfo, int *ch)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "MC"))
    {
        return -RIG_ENAVAIL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "MC%c", cat_term);

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    /* Get Memory Channel Number */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    *ch = atoi(priv->ret_data + 2);

    return RIG_OK;
}

int newcat_vfo_op(RIG *rig, vfo_t vfo, vfo_op_t op)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    /* Set Main or SUB vfo */
    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    switch (op)
    {
    case RIG_OP_TUNE:
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AC002%c", cat_term);
        break;

    case RIG_OP_CPY:
        if (newcat_is_rig(rig, RIG_MODEL_FT450))
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "VV%c", cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AB%c", cat_term);
        }

        break;

    case RIG_OP_XCHG:
    case RIG_OP_TOGGLE:
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SV%c", cat_term);
        break;

    case RIG_OP_UP:
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "UP%c", cat_term);
        break;

    case RIG_OP_DOWN:
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "DN%c", cat_term);
        break;

    case RIG_OP_BAND_UP:
        if (main_sub_vfo == 1)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BU1%c", cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BU0%c", cat_term);
        }

        break;

    case RIG_OP_BAND_DOWN:
        if (main_sub_vfo == 1)
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BD1%c", cat_term);
        }
        else
        {
            snprintf(priv->cmd_str, sizeof(priv->cmd_str), "BD0%c", cat_term);
        }

        break;

    case RIG_OP_FROM_VFO:
        /* VFOA ! */
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AM%c", cat_term);
        break;

    case RIG_OP_TO_VFO:
        /* VFOA ! */
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "MA%c", cat_term);
        break;

    default:
        return -RIG_EINVAL;
    }

    return newcat_set_cmd(rig);
}


int newcat_scan(RIG *rig, vfo_t vfo, scan_t scan, int ch)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_trn(RIG *rig, int trn)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    char c;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "AI"))
    {
        return -RIG_ENAVAIL;
    }

    if (trn  == RIG_TRN_OFF)
    {
        c = '0';
    }
    else
    {
        c = '1';
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "AI%c%c", c, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "cmd_str = %s\n", priv->cmd_str);

    return newcat_set_cmd(rig);
}


int newcat_get_trn(RIG *rig, int *trn)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char c;
    char command[] = "AI";

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", command, cat_term);

    /* Get Auto Information */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    c = priv->ret_data[2];

    if (c == '0')
    {
        *trn = RIG_TRN_OFF;
    }
    else
    {
        *trn = RIG_TRN_RIG;
    }

    return RIG_OK;
}


int newcat_decode_event(RIG *rig)
{
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    return -RIG_ENAVAIL;
}


int newcat_set_channel(RIG *rig, const channel_t *chan)
{
    struct rig_state *state = &rig->state;
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err, i;
    int rxit;
    char c_rit, c_xit, c_mode, c_vfo, c_tone, c_rptr_shift;
    tone_t tone;
    ncboolean restore_vfo;
    chan_t *chan_list;
    channel_cap_t *mem_caps = NULL;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);


    if (!newcat_valid_command(rig, "MW"))
    {
        return -RIG_ENAVAIL;
    }

    chan_list = rig->caps->chan_list;

    for (i = 0; i < CHANLSTSIZ && !RIG_IS_CHAN_END(chan_list[i]); i++)
    {
        if (chan->channel_num >= chan_list[i].startc &&
                chan->channel_num <= chan_list[i].endc &&
                // writable memory types... NOT 60-METERS or READ-ONLY channels
                (chan_list[i].type == RIG_MTYPE_MEM ||
                 chan_list[i].type == RIG_MTYPE_EDGE))
        {
            mem_caps = &chan_list[i].mem_caps;
            break;
        }
    }

    /* Out of Range */
    if (!mem_caps)
    {
        return -RIG_ENAVAIL;
    }

    /* Set Restore to VFO or leave in memory mode */
    switch (state->current_vfo)
    {
    case RIG_VFO_A:
    case RIG_VFO_B:
        /* Jump back from memory channel */
        restore_vfo = TRUE;
        break;

    case RIG_VFO_MEM:
        /* Jump from channel to channel in memory mode */
        restore_vfo = FALSE;
        break;

    case RIG_VFO_SUB:
    default:
        /* Only works with VFO Main */
        return -RIG_ENTARGET;
    }

    /* Write Memory Channel ************************* */
    /*  Clarifier TX, RX */
    if (chan->rit)
    {
        rxit = chan->rit;
        c_rit = '1';
        c_xit = '0';
    }
    else if (chan->xit)
    {
        rxit = chan->xit;
        c_rit = '0';
        c_xit = '1';
    }
    else
    {
        rxit  =  0;
        c_rit = '0';
        c_xit = '0';
    }

    /* MODE */
    c_mode = newcat_modechar(chan->mode);

    /* VFO Fixed */
    c_vfo = '0';

    /* CTCSS Tone / Sql */
    if (chan->ctcss_tone)
    {
        c_tone = '2';
        tone = chan->ctcss_tone;
    }
    else if (chan->ctcss_sql)
    {
        c_tone = '1';
        tone = chan->ctcss_sql;
    }
    else
    {
        c_tone = '0';
        tone = 0;
    }

    for (i = 0; rig->caps->ctcss_list[i] != 0; i++)
        if (tone == rig->caps->ctcss_list[i])
        {
            tone = i;

            if (tone > 49)
            {
                tone = 0;
            }

            break;
        }

    /* Repeater Shift */
    switch (chan->rptr_shift)
    {
    case RIG_RPT_SHIFT_NONE:  c_rptr_shift = '0'; break;

    case RIG_RPT_SHIFT_PLUS:  c_rptr_shift = '1'; break;

    case RIG_RPT_SHIFT_MINUS: c_rptr_shift = '2'; break;

    default: c_rptr_shift = '0';
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str),
             "MW%03d%08d%+.4d%c%c%c%c%c%02u%c%c",
             chan->channel_num, (int)chan->freq, rxit, c_rit, c_xit, c_mode, c_vfo,
             c_tone, tone, c_rptr_shift, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    /* Set Memory Channel */
    priv->question_mark_response_means_rejected = 1;
    err = newcat_set_cmd(rig);
    priv->question_mark_response_means_rejected = 0;

    if (err != RIG_OK)
    {
        return err;
    }

    /* Restore VFO ********************************** */
    if (restore_vfo)
    {
        err = newcat_vfomem_toggle(rig);
        return err;
    }

    return RIG_OK;
}


int newcat_get_channel(RIG *rig, channel_t *chan, int read_only)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    char *retval;
    char c, c2;
    int err, i;
    chan_t *chan_list;
    channel_cap_t *mem_caps = NULL;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "MR"))
    {
        return -RIG_ENAVAIL;
    }

    chan_list = rig->caps->chan_list;

    for (i = 0; i < CHANLSTSIZ && !RIG_IS_CHAN_END(chan_list[i]); i++)
    {
        if (chan->channel_num >= chan_list[i].startc &&
                chan->channel_num <= chan_list[i].endc)
        {
            mem_caps = &chan_list[i].mem_caps;
            break;
        }
    }

    /* Out of Range */
    if (!mem_caps)
    {
        return -RIG_ENAVAIL;
    }

    rig_debug(RIG_DEBUG_TRACE, "sizeof(channel_t) = %d\n", (int)sizeof(channel_t));
    rig_debug(RIG_DEBUG_TRACE, "sizeof(priv->cmd_str) = %d\n",
              (int)sizeof(priv->cmd_str));

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "MR%03d%c", chan->channel_num,
             cat_term);

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);


    /* Get Memory Channel */
    priv->question_mark_response_means_rejected = 1;
    err = newcat_get_cmd(rig);
    priv->question_mark_response_means_rejected = 0;

    if (RIG_OK != err)
    {
        if (-RIG_ERJCTED == err)
        {
            /* Invalid channel, has not been set up, make sure freq is
               0 to indicate empty channel */
            chan->freq = 0.;
            return RIG_OK;
        }

        return err;
    }

    /* ret_data string to channel_t struct :: this will destroy ret_data */

    /* rptr_shift P10 ************************ */
    retval = priv->ret_data + 25;

    switch (*retval)
    {
    case '0': chan->rptr_shift = RIG_RPT_SHIFT_NONE;  break;

    case '1': chan->rptr_shift = RIG_RPT_SHIFT_PLUS;  break;

    case '2': chan->rptr_shift = RIG_RPT_SHIFT_MINUS; break;

    default:  chan->rptr_shift = RIG_RPT_SHIFT_NONE;
    }

    *retval = '\0';

    /* CTCSS Encoding P8 ********************* */
    retval = priv->ret_data + 22;
    c = *retval;

    /* CTCSS Tone P9 ************************* */
    chan->ctcss_tone = 0;
    chan->ctcss_sql  = 0;
    retval = priv->ret_data + 23;
    i = atoi(retval);

    if (c == '1')
    {
        chan->ctcss_sql = rig->caps->ctcss_list[i];
    }
    else if (c == '2')
    {
        chan->ctcss_tone = rig->caps->ctcss_list[i];
    }

    /* vfo, mem, P7 ************************** */
    retval = priv->ret_data + 21;

    if (*retval == '1')
    {
        chan->vfo = RIG_VFO_MEM;
    }
    else
    {
        chan->vfo = RIG_VFO_CURR;
    }

    /* MODE P6 ******************************* */
    chan->width = 0;

    retval = priv->ret_data + 20;
    chan->mode = newcat_rmode(*retval);

    if (chan->mode == RIG_MODE_NONE)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: unknown mode=%c\n", __func__, *retval);
        chan->mode = RIG_MODE_LSB;
    }

    /* Clarifier TX P5 *********************** */
    retval = priv->ret_data + 19;
    c2 = *retval;

    /* Clarifier RX P4 *********************** */
    retval = priv->ret_data + 18;
    c = *retval;
    *retval = '\0';

    /* Clarifier Offset P3 ******************* */
    chan->rit = 0;
    chan->xit = 0;
    retval = priv->ret_data + 13;

    if (c == '1')
    {
        chan->rit = atoi(retval);
    }
    else if (c2 == '1')
    {
        chan->xit = atoi(retval);
    }

    *retval = '\0';

    /* Frequency P2 ************************** */
    retval = priv->ret_data + 5;
    chan->freq = atof(retval);

    if (!read_only)
    {
        // Set rig to channel values
        rig_debug(RIG_DEBUG_ERR,
                  "%s: please contact hamlib mailing list to implement this\n", __func__);
        rig_debug(RIG_DEBUG_ERR,
                  "%s: need to know if rig updates when channel read or not\n", __func__);
        return -RIG_ENIMPL;
    }

    return RIG_OK;
}


const char *newcat_get_info(RIG *rig)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    static char idbuf[129]; /* extra large static string array */

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    /* Build the command string */
    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "ID;");

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    /* Get Identification Channel */
    if (RIG_OK != newcat_get_cmd(rig))
    {
        return NULL;
    }

    priv->ret_data[6] = '\0';
    snprintf(idbuf, sizeof(idbuf), "%s", priv->ret_data);

    return idbuf;
}


/*
 * newcat_valid_command
 *
 * Determine whether or not the command is valid for the specified
 * rig.  This function should be called before sending the command
 * to the rig to make it easier to differentiate invalid and illegal
 * commands (for a rig).
 */

ncboolean newcat_valid_command(RIG *rig, char const *const command)
{
    const struct rig_caps *caps;
    int search_high;
    int search_low;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);
    rig_debug(RIG_DEBUG_TRACE, "%s %s\n", __func__, command);

    caps = rig->caps;

    if (!caps)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: Rig capabilities not valid\n", __func__);
        return FALSE;
    }

    /*
     * Determine the type of rig from the model number.  Note it is
     * possible for several model variants to exist; i.e., all the
     * FT-9000 variants.
     */

    is_ft450 = newcat_is_rig(rig, RIG_MODEL_FT450);
    is_ft891 = newcat_is_rig(rig, RIG_MODEL_FT891);
    is_ft950 = newcat_is_rig(rig, RIG_MODEL_FT950);
    is_ft991 = newcat_is_rig(rig, RIG_MODEL_FT991);
    is_ft2000 = newcat_is_rig(rig, RIG_MODEL_FT2000);
    is_ftdx9000 = newcat_is_rig(rig, RIG_MODEL_FT9000);
    is_ftdx5000 = newcat_is_rig(rig, RIG_MODEL_FTDX5000);
    is_ftdx1200 = newcat_is_rig(rig, RIG_MODEL_FTDX1200);
    is_ftdx3000 = newcat_is_rig(rig, RIG_MODEL_FTDX3000);
    is_ftdx101 = newcat_is_rig(rig, RIG_MODEL_FTDX101D);

    if (!is_ft450 && !is_ft950 && !is_ft891 && !is_ft991 && !is_ft2000
        && !is_ftdx5000 && !is_ftdx9000 && !is_ftdx1200 && !is_ftdx3000 && !is_ftdx101)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: '%s' is unknown\n", __func__, caps->model_name);
        return FALSE;
    }

    /*
     * Make sure the command is known, and then check to make sure
     * is it valid for the rig.
     */

    search_low = 0;
    search_high = valid_commands_count;

    while (search_low <= search_high)
    {
        int search_test;
        int search_index;

        search_index = (search_low + search_high) / 2;
        search_test = strcmp(valid_commands[search_index].command, command);

        if (search_test > 0)
        {
            search_high = search_index - 1;
        }
        else if (search_test < 0)
        {
            search_low = search_index + 1;
        }
        else
        {
            /*
             * The command is valid.  Now make sure it is supported by the rig.
             */
            if (is_ft450 && valid_commands[search_index].ft450)
            {
                return TRUE;
            }
            else if (is_ft891 && valid_commands[search_index].ft891)
            {
                return TRUE;
            }
            else if (is_ft950 && valid_commands[search_index].ft950)
            {
                return TRUE;
            }
            else if (is_ft991 && valid_commands[search_index].ft991)
            {
                return TRUE;
            }
            else if (is_ft2000 && valid_commands[search_index].ft2000)
            {
                return TRUE;
            }
            else if (is_ftdx5000 && valid_commands[search_index].ft5000)
            {
                return TRUE;
            }
            else if (is_ftdx9000 && valid_commands[search_index].ft9000)
            {
                return TRUE;
            }
            else if (is_ftdx1200 && valid_commands[search_index].ft1200)
            {
                return TRUE;
            }
            else if (is_ftdx3000 && valid_commands[search_index].ft3000)
            {
                return TRUE;
            }
            else if (is_ftdx101 && valid_commands[search_index].ft101)
            {
                return TRUE;
            }
            else
            {
                rig_debug(RIG_DEBUG_TRACE, "%s: '%s' command '%s' not supported\n",
                          __func__, caps->model_name, command);
                return FALSE;
            }
        }
    }

    rig_debug(RIG_DEBUG_TRACE, "%s: '%s' command '%s' not valid\n",
              __func__, caps->model_name, command);
    return FALSE;
}


ncboolean newcat_is_rig(RIG *rig, rig_model_t model)
{
    ncboolean is_rig;

    is_rig = (model == rig->caps->rig_model) ? TRUE : FALSE;

    return is_rig;
}


/*
 * newcat_set_tx_vfo does not set priv->curr_vfo
 */
int newcat_set_tx_vfo(RIG *rig, vfo_t tx_vfo)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char p1;
    char *command = "FT";

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "FT"))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig, &tx_vfo);

    if (err < 0)
    {
        return err;
    }

    switch (tx_vfo)
    {
    case RIG_VFO_A:
    case RIG_VFO_MAIN:
        p1 = '0';
        break;

    case RIG_VFO_B:
    case RIG_VFO_SUB:
        p1 = '1';
        break;

    case RIG_VFO_MEM:

        /* VFO A */
        if (priv->current_mem == NC_MEM_CHANNEL_NONE)
        {
            return RIG_OK;
        }
        else    /* Memory Channel mode */
        {
            p1 = '0';
        }

        break;

    default:
        return -RIG_EINVAL;
    }

    /* TODO: G4WJS - FT-450 only has toggle command so not sure how to
       definitively set the TX VFO (VS; doesn't seem to help
       either) */
    if (newcat_is_rig(rig, RIG_MODEL_FT950) ||
            newcat_is_rig(rig, RIG_MODEL_FT2000) ||
            newcat_is_rig(rig, RIG_MODEL_FTDX5000) ||
            newcat_is_rig(rig, RIG_MODEL_FTDX1200) ||
            newcat_is_rig(rig, RIG_MODEL_FT991) ||
            newcat_is_rig(rig, RIG_MODEL_FTDX3000))
    {
        p1 = p1 + 2;    /* use non-Toggle commands */
    }

    if (is_ftdx101)
    {
        // what other Yaeus rigs should be using this?
        // The DX101D returns FT0 when in split and not transmitting
        command = "ST";
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c%c", command, p1, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "cmd_str = %s\n", priv->cmd_str);

    /* Set TX VFO */
    return newcat_set_cmd(rig);
}


/*
 * newcat_get_tx_vfo does not set priv->curr_vfo
 */
int newcat_get_tx_vfo(RIG *rig, vfo_t *tx_vfo)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char c;
    vfo_t vfo_mode;
    char const *command = "FT";

    if (is_ftdx101)
    {
        // what other Yaeus rigs should be using this?
        // The DX101D returns FT0 when in split and not transmitting
        command = "ST";
    }

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", command, cat_term);

    /* Get TX VFO */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    c = priv->ret_data[2];

    switch (c)
    {
    case '0':
        if (rig->state.vfo_list & RIG_VFO_MAIN) { *tx_vfo = RIG_VFO_MAIN; }
        else { *tx_vfo = RIG_VFO_A; }

        rig->state.cache.split = 0;
        break;

    case '1' :
        if (rig->state.vfo_list & RIG_VFO_SUB) { *tx_vfo = RIG_VFO_SUB; }
        else { *tx_vfo = RIG_VFO_B; }

        rig->state.cache.split = 1;
        break;

    default:
        rig_debug(RIG_DEBUG_ERR, "%s: Unknown tx_vfo=%c from index 2 of %s\n", __func__,
                  c, priv->ret_data);
        return -RIG_EPROTO;
    }

    /* Check to see if RIG is in MEM mode */
    err = newcat_get_vfo_mode(rig, &vfo_mode);

    if (err != RIG_OK)
    {
        return err;
    }

    if (vfo_mode == RIG_VFO_MEM && *tx_vfo == RIG_VFO_A)
    {
        *tx_vfo = RIG_VFO_MEM;
    }

    rig_debug(RIG_DEBUG_TRACE, "%s: tx_vfo = %s\n", __func__, rig_strvfo(*tx_vfo));

    return RIG_OK;
}


int newcat_set_vfo_from_alias(RIG *rig, vfo_t *vfo)
{

    rig_debug(RIG_DEBUG_TRACE, "%s: alias vfo = %s\n", __func__, rig_strvfo(*vfo));

    switch (*vfo)
    {
    case RIG_VFO_A:
    case RIG_VFO_B:
    case RIG_VFO_MEM:
        /* passes through */
        break;

    case RIG_VFO_CURR:  /* RIG_VFO_RX == RIG_VFO_CURR */
    case RIG_VFO_VFO:
        *vfo = rig->state.current_vfo;
        break;

    case RIG_VFO_TX:

        /* set to another vfo for split or uplink */
        if (rig->state.vfo_list & RIG_VFO_MAIN)
        {
            *vfo = (rig->state.current_vfo == RIG_VFO_SUB) ? RIG_VFO_MAIN : RIG_VFO_SUB;
        }
        else
        {
            *vfo = (rig->state.current_vfo == RIG_VFO_B) ? RIG_VFO_A : RIG_VFO_B;
        }

        break;

    case RIG_VFO_MAIN:
        *vfo = RIG_VFO_MAIN;
        break;

    case RIG_VFO_SUB:
        *vfo = RIG_VFO_SUB;
        break;

    default:
        rig_debug(RIG_DEBUG_TRACE, "Unrecognized.  vfo= %s\n", rig_strvfo(*vfo));
        return -RIG_EINVAL;
    }

    return RIG_OK;
}

/*
 *  Found newcat_set_level() floating point math problem
 *  Using rigctl on FT950 I was trying to set RIG_LEVEL_COMP to 12
 *  I kept setting it to 11.  I wrote some test software and
 *  found out that 0.12 * 100 = 11 with my setup.
 *  Compiler is gcc 4.2.4, CPU is AMD X2
 *  This works somewhat but Find a better way.
 *  The newcat_get_level() seems to work correctly.
 *  Terry KJ4EED
 *
 */
int newcat_scale_float(int scale, float fval)
{
    float f;
    float fudge = 0.003;

    if ((fval + fudge) > 1.0)
    {
        f = scale * fval;
    }
    else
    {
        f = scale * (fval + fudge);
    }

    return (int) f;
}


int newcat_set_narrow(RIG *rig, vfo_t vfo, ncboolean narrow)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char c;
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "NA"))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    if (narrow == TRUE)
    {
        c = '1';
    }
    else
    {
        c = '0';
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "NA%c%c%c", main_sub_vfo, c,
             cat_term);

    rig_debug(RIG_DEBUG_TRACE, "cmd_str = %s\n", priv->cmd_str);

    return newcat_set_cmd(rig);
}


int newcat_get_narrow(RIG *rig, vfo_t vfo, ncboolean *narrow)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char c;
    char command[] = "NA";
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c%c", command, main_sub_vfo,
             cat_term);

    /* Get NAR */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    c = priv->ret_data[3];

    if (c == '1')
    {
        *narrow = TRUE;
    }
    else
    {
        *narrow = FALSE;
    }

    return RIG_OK;
}

// returns 1 if in narrow mode 0 if not, < 0 if error
// if vfo != RIG_VFO_NONE then will use NA0 or NA1 depending on vfo Main or Sub
static int get_narrow(RIG *rig, vfo_t vfo)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int narrow = 0;
    int err;

    // find out if we're in narrow or wide mode

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "NA%c%c",
             vfo == RIG_VFO_SUB ? '1' : '0', cat_term);

    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    if (sscanf(priv->ret_data, "NA%*1d%3d", &narrow) != 1)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: unable to parse width from '%s'\n", __func__,
                  priv->ret_data);
        return -RIG_EPROTO;
    }

    return narrow;
}

int newcat_set_rx_bandwidth(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    int w;
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);
    rig_debug(RIG_DEBUG_TRACE, "%s vfo=%s, mode=%s, width=%d\n", __func__,
              rig_strvfo(vfo), rig_strrmode(mode), (int)width);

    if (!newcat_valid_command(rig, "SH"))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    if (is_ft950)
    {
        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 500 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 100) { w = 3; }
            else if (width <= 200) { w = 4; }
            else if (width <= 300) { w = 5; }
            else if (width <= 400) { w = 6; }
            else if (width <= 500) { w = 7; }
            else if (width <= 800) { w = 8; }
            else if (width <= 1200) { w = 9; }
            else if (width <= 1400) { w = 10; }
            else if (width <= 1700) { w = 11; }
            else if (width <= 2000) { w = 12; }
            else { w = 13; } // 2400 Hz
            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 1800 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 200) { w = 1; }
            else if (width <= 400) { w = 2; }
            else if (width <= 600) { w = 3; }
            else if (width <= 850) { w = 4; }
            else if (width <= 1100) { w = 5; }
            else if (width <= 1350) { w = 6; }
            else if (width <= 1500) { w = 7; }
            else if (width <= 1650) { w = 8; }
            else if (width <= 1800) { w = 9; }
            else if (width <= 1950) { w = 10; }
            else if (width <= 2100) { w = 11; }
            else if (width <= 2250) { w = 12; }
            else if (width <= 2400) { w = 13; }
            else if (width <= 2450) { w = 14; }
            else if (width <= 2500) { w = 15; }
            else if (width <= 2600) { w = 16; }
            else if (width <= 2700) { w = 17; }
            else if (width <= 2800) { w = 18; }
            else if (width <= 2900) { w = 19; }
            else { w = 20; } // 3000 Hz
            break;

        case RIG_MODE_AM:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
        case RIG_MODE_FMN:
            // Set roofing filter and narrow mode
            break;

        default:
            return -RIG_EINVAL;
        } // end switch(mode)

        if ((err = set_roofing_filter_for_width(rig, vfo, width)) != RIG_OK)
        {
            return err;
        }

        switch (mode)
        {
        case RIG_MODE_AM:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
            if (width < rig_passband_normal(rig, mode))
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            else
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            return err;

        case RIG_MODE_FMN:
            return RIG_OK;
        }
    } // end is_ft950 */
    else if (is_ft891)
    {
        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 500 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 50) { w = 1; }
            else if (width <= 100) { w = 2; }
            else if (width <= 150) { w = 3; }
            else if (width <= 200) { w = 4; }
            else if (width <= 250) { w = 5; }
            else if (width <= 300) { w = 6; }
            else if (width <= 350) { w = 7; }
            else if (width <= 400) { w = 8; }
            else if (width <= 450) { w = 9; }
            else if (width <= 500) { w = 10; }
            else if (width <= 800) { w = 11; }
            else if (width <= 1200) { w = 12; }
            else if (width <= 1400) { w = 13; }
            else if (width <= 1700) { w = 14; }
            else if (width <= 2000) { w = 15; }
            else if (width <= 2400) { w = 16; }
            else { w = 17; } // 3000 Hz
            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 1800 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 200) { w = 1; }
            else if (width <= 400) { w = 2; }
            else if (width <= 600) { w = 3; }
            else if (width <= 850) { w = 4; }
            else if (width <= 1100) { w = 5; }
            else if (width <= 1350) { w = 6; }
            else if (width <= 1500) { w = 7; }
            else if (width <= 1650) { w = 8; }
            else if (width <= 1800) { w = 9; }
            else if (width <= 1950) { w = 10; }
            else if (width <= 2100) { w = 11; }
            else if (width <= 2200) { w = 12; }
            else if (width <= 2300) { w = 13; }
            else if (width <= 2400) { w = 14; }
            else if (width <= 2500) { w = 15; }
            else if (width <= 2600) { w = 16; }
            else if (width <= 2700) { w = 17; }
            else if (width <= 2800) { w = 18; }
            else if (width <= 2900) { w = 19; }
            else if (width <= 3000) { w = 20; }
            else { w = 21; } // 3000 Hz
            break;

        case RIG_MODE_AM:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
            if (width < rig_passband_normal(rig, mode))
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            else
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            return err;

        case RIG_MODE_FMN:
            break;

        default:
            return -RIG_EINVAL;
        } // end switch(mode)
    } // end is_ft891
    else if (is_ft991)
    {
        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 500 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 50) { w = 1; }
            else if (width <= 100) { w = 2; }
            else if (width <= 150) { w = 3; }
            else if (width <= 200) { w = 4; }
            else if (width <= 250) { w = 5; }
            else if (width <= 305) { w = 6; }
            else if (width <= 350) { w = 7; }
            else if (width <= 400) { w = 8; }
            else if (width <= 450) { w = 9; }
            else if (width <= 500) { w = 10; }
            else if (width <= 800) { w = 11; }
            else if (width <= 1200) { w = 12; }
            else if (width <= 1400) { w = 13; }
            else if (width <= 1700) { w = 14; }
            else if (width <= 2000) { w = 15; }
            else if (width <= 2400) { w = 16; }
            else { w = 17; } // 3000 Hz
            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 1800 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 200) { w = 1; }
            else if (width <= 400) { w = 2; }
            else if (width <= 600) { w = 3; }
            else if (width <= 850) { w = 4; }
            else if (width <= 1100) { w = 5; }
            else if (width <= 1350) { w = 6; }
            else if (width <= 1500) { w = 7; }
            else if (width <= 1650) { w = 8; }
            else if (width <= 1800) { w = 9; }
            else if (width <= 1950) { w = 10; }
            else if (width <= 2100) { w = 11; }
            else if (width <= 2200) { w = 12; }
            else if (width <= 2300) { w = 13; }
            else if (width <= 2400) { w = 14; }
            else if (width <= 2500) { w = 15; }
            else if (width <= 2600) { w = 16; }
            else if (width <= 2700) { w = 17; }
            else if (width <= 2800) { w = 18; }
            else if (width <= 2900) { w = 19; }
            else if (width <= 3000) { w = 20; }
            else { w = 21; } // 3200 Hz
            break;

        case RIG_MODE_AM: // Only 1 passband each for AM or AMN
            if (width == RIG_PASSBAND_NORMAL || width == 9000)
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            return err;

        case RIG_MODE_AMN:
            if (width == RIG_PASSBAND_NORMAL || width == 6000)
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            return err;

        case RIG_MODE_FM: // Only 1 passband each for FM or FMN
            if (width == RIG_PASSBAND_NORMAL || width == 16000)
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            return err;

        case RIG_MODE_FMN:
            if (width == RIG_PASSBAND_NORMAL || width == 9000)
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            return err;

        case RIG_MODE_C4FM:
            if (width == RIG_PASSBAND_NORMAL || width == 16000)
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            else if (width == 9000)
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            else
            {
                return -RIG_EINVAL;
            }
            return err;

        case RIG_MODE_PKTFM:
            if (width < rig_passband_normal(rig, mode))
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            else
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            return err;

        default:
            return -RIG_EINVAL;
        } // end switch(mode)
    } // end is_ft991
    else if (is_ftdx1200 || is_ftdx3000)
    {
        // FTDX 1200 and FTDX 3000 have the same set of filter choices
        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 500 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 50) { w = 1; }
            else if (width <= 100) { w = 2; }
            else if (width <= 150) { w = 3; }
            else if (width <= 200) { w = 4; }
            else if (width <= 250) { w = 5; }
            else if (width <= 300) { w = 6; }
            else if (width <= 350) { w = 7; }
            else if (width <= 400) { w = 8; }
            else if (width <= 450) { w = 9; }
            else if (width <= 500) { w = 10; }
            else if (width <= 800) { w = 11; }
            else if (width <= 1200) { w = 12; }
            else if (width <= 1400) { w = 13; }
            else if (width <= 1700) { w = 14; }
            else if (width <= 2000) { w = 15; }
            else { w = 16; } // 2400 Hz
            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 1800 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 200) {  w = 1; }
            else if (width <= 400) {  w = 2; }
            else if (width <= 600) {  w = 3; }
            else if (width <= 850) {  w = 4; }
            else if (width <= 1100) {  w = 5; }
            else if (width <= 1350) {  w = 6; }
            else if (width <= 1500) {  w = 7; }
            else if (width <= 1650) {  w = 8; }
            else if (width <= 1800) {  w = 9; }
            else if (width <= 1950) {  w = 10; }
            else if (width <= 2100) {  w = 11; }
            else if (width <= 2200) {  w = 12; }
            else if (width <= 2300) {  w = 13; }
            else if (width <= 2400) {  w = 14; }
            else if (width <= 2500) {  w = 15; }
            else if (width <= 2600) {  w = 16; }
            else if (width <= 2700) {  w = 17; }
            else if (width <= 2800) {  w = 18; }
            else if (width <= 2900) {  w = 19; }
            else if (width <= 3000) {  w = 20; }
            else if (width <= 3200) {  w = 21; }
            else if (width <= 3400) {  w = 22; }
            else if (width <= 3600) {  w = 23; }
            else if (width <= 3800) {  w = 24; }
            else { w = 25; } // 4000 Hz
            break;

        case RIG_MODE_AM:
        case RIG_MODE_AMN:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
        case RIG_MODE_FMN:
            // Set roofing filter and narrow mode
            break;

        default:
            return -RIG_EINVAL;
        } // end switch(mode)

        if ((err = set_roofing_filter_for_width(rig, vfo, width)) != RIG_OK)
        {
            return err;
        }

        switch (mode)
        {
        case RIG_MODE_AM:
        case RIG_MODE_AMN:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
        case RIG_MODE_FMN:
            if (width < rig_passband_normal(rig, mode))
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            else
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            return err;
        }
    } // end is_ftdx1200 and is_ftdx3000
    else if (is_ftdx5000)
    {
        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 500 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 50) { w = 1; }
            else if (width <= 100) { w = 2; }
            else if (width <= 150) { w = 3; }
            else if (width <= 200) { w = 4; }
            else if (width <= 250) { w = 5; }
            else if (width <= 300) { w = 6; }
            else if (width <= 350) { w = 7; }
            else if (width <= 400) { w = 8; }
            else if (width <= 450) { w = 9; }
            else if (width <= 500) { w = 10; }
            else if (width <= 800) { w = 11; }
            else if (width <= 1200) { w = 12; }
            else if (width <= 1400) { w = 13; }
            else if (width <= 1700) { w = 14; }
            else if (width <= 2000) { w = 15; }
            else { w = 16; } // 2400 Hz
            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            // Narrow mode must be chosen correctly before filter width
            err = newcat_set_narrow(rig, vfo, width <= 1800 ? TRUE : FALSE);
            if (err != RIG_OK)
            {
                return err;
            }

            if (width == RIG_PASSBAND_NORMAL) { w = 0; }
            else if (width <= 200) {  w = 1; }
            else if (width <= 400) {  w = 2; }
            else if (width <= 600) {  w = 3; }
            else if (width <= 850) {  w = 4; }
            else if (width <= 1100) {  w = 5; }
            else if (width <= 1350) {  w = 6; }
            else if (width <= 1500) {  w = 7; }
            else if (width <= 1650) {  w = 8; }
            else if (width <= 1800) {  w = 9; }
            else if (width <= 1950) {  w = 10; }
            else if (width <= 2100) {  w = 11; }
            else if (width <= 2250) {  w = 12; }
            else if (width <= 2400) {  w = 13; }
            else if (width <= 2500) {  w = 15; }
            else if (width <= 2600) {  w = 16; }
            else if (width <= 2700) {  w = 17; }
            else if (width <= 2800) {  w = 18; }
            else if (width <= 2900) {  w = 19; }
            else if (width <= 3000) {  w = 20; }
            else if (width <= 3200) {  w = 21; }
            else if (width <= 3400) {  w = 22; }
            else if (width <= 3600) {  w = 23; }
            else if (width <= 3800) {  w = 24; }
            else { w = 25; } // 4000 Hz
            break;

        case RIG_MODE_AM:
        case RIG_MODE_AMN:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
        case RIG_MODE_FMN:
            // Set roofing filter and narrow mode
            break;

        default:
            return -RIG_EINVAL;
        } // end switch(mode)

        if ((err = set_roofing_filter_for_width(rig, vfo, width)) != RIG_OK)
        {
            return err;
        }

        switch (mode)
        {
        case RIG_MODE_AM:
        case RIG_MODE_AMN:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
        case RIG_MODE_FMN:
            if (width < rig_passband_normal(rig, mode))
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            else
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            return err;
        }
    } // end is_ftdx5000
    else if (is_ftdx101)
    {
        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            if (width == RIG_PASSBAND_ROOF) { w = 0; }
            else if (width <= 50) { w = 1; }
            else if (width <= 100) { w = 2; }
            else if (width <= 150) { w = 3; }
            else if (width <= 200) { w = 4; }
            else if (width <= 250) { w = 5; }
            else if (width <= 300) { w = 6; }
            else if (width <= 350) { w = 7; }
            else if (width <= 400) { w = 8; }
            else if (width <= 450) { w = 9; }
            else if (width <= 500) { w = 10; }
            else if (width <= 600) { w = 11; }
            else if (width <= 800) { w = 12; }
            else if (width <= 1200) { w = 13; }
            else if (width <= 1400) { w = 14; }
            else if (width <= 1700) { w = 15; }
            else if (width <= 2000) { w = 16; }
            else if (width <= 2400) { w = 17; }
            else { w = 18; }
            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            if (width == RIG_PASSBAND_ROOF) { w = 0; }
            else if (width <= 300) {  w = 1; }
            else if (width <= 400) {  w = 2; }
            else if (width <= 600) {  w = 3; }
            else if (width <= 850) {  w = 4; }
            else if (width <= 1100) {  w = 5; }
            else if (width <= 1200) {  w = 6; }
            else if (width <= 1500) {  w = 7; }
            else if (width <= 1650) {  w = 8; }
            else if (width <= 1800) {  w = 9; }
            else if (width <= 1950) {  w = 10; }
            else if (width <= 2100) {  w = 11; }
            else if (width <= 2200) {  w = 12; }
            else if (width <= 2300) {  w = 13; }
            else if (width <= 2400) {  w = 14; }
            else if (width <= 2500) {  w = 15; }
            else if (width <= 2600) {  w = 16; }
            else if (width <= 2700) {  w = 17; }
            else if (width <= 2800) {  w = 18; }
            else if (width <= 2900) {  w = 19; }
            else if (width <= 3000) {  w = 20; }
            else if (width <= 3200) {  w = 21; }
            else if (width <= 3500) {  w = 22; }
            else { w = 23; } // 4000Hz
            break;

        case RIG_MODE_AM:
        case RIG_MODE_AMN:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
        case RIG_MODE_FMN:
            // Set roofing filter and narrow mode
            break;

        default:
            return -RIG_EINVAL;
        } // end switch(mode)

        if ((err = set_roofing_filter_for_width(rig, vfo, width)) != RIG_OK)
        {
            return err;
        }

        switch (mode)
        {
        case RIG_MODE_AM:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
            if (width < rig_passband_normal(rig, mode))
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            else
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            return err;

        case RIG_MODE_AMN:
        case RIG_MODE_FMN:
            return RIG_OK;
        }
    } // end is_ftdx101
    else
    {
        // FT-450, FT-2000, FTDX 9000
        // We need details on the widths here, manuals lack information.
        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            if (width <= 500) { w = 6; }
            else if (width <= 1800) { w = 16; }
            else { w = 24; }
            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            if (width <= 1800) { w = 8; }
            else if (width <= 2400) { w = 16; }
            else { w = 25; } // 3000
            break;

        case RIG_MODE_AM:
        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
            if (width < rig_passband_normal(rig, mode))
            {
                err = newcat_set_narrow(rig, vfo, TRUE);
            }
            else
            {
                err = newcat_set_narrow(rig, vfo, FALSE);
            }
            return err;

        case RIG_MODE_FMN:
            return RIG_OK;

        default:
            return -RIG_EINVAL;
        } /* end switch(mode) */

    } /* end else */

    if (is_ftdx101)
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SH%c0%02d;", main_sub_vfo, w);
    }
    else
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "SH%c%02d;", main_sub_vfo, w);
    }

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    /* Set RX Bandwidth */
    return newcat_set_cmd(rig);
}

static int set_roofing_filter(RIG *rig, vfo_t vfo, int index)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    struct newcat_priv_caps *priv_caps = (struct newcat_priv_caps *)rig->caps->priv;
    struct newcat_roofing_filter *roofing_filters;
    char main_sub_vfo = '0';
    char roofing_filter_choice = 0;
    int err;
    int i;

    rig_debug(RIG_DEBUG_TRACE, "%s: called\n", __func__);

    if (priv_caps == NULL)
    {
        return -RIG_ENAVAIL;
    }

    roofing_filters = priv_caps->roofing_filters;

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    if (!newcat_valid_command(rig, "RF"))
    {
        return -RIG_ENAVAIL;
    }

    for (i = 0; roofing_filters[i].index >= 0; i++)
    {
        struct newcat_roofing_filter *current_filter = &roofing_filters[i];
        char set_value = current_filter->set_value;

        if (set_value == 0)
        {
            continue;
        }

        roofing_filter_choice = set_value;

        if (current_filter->index == index)
        {
            break;
        }
    }

    if (roofing_filter_choice == 0)
    {
        return -RIG_EINVAL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RF%c%c%c", main_sub_vfo,
             roofing_filter_choice, cat_term);

    priv->question_mark_response_means_rejected = 1;
    err = newcat_set_cmd(rig);
    priv->question_mark_response_means_rejected = 0;

    if (RIG_OK != err)
    {
        return err;
    }

    return RIG_OK;
}

static int set_roofing_filter_for_width(RIG *rig, vfo_t vfo, int width)
{
    struct newcat_priv_caps *priv_caps = (struct newcat_priv_caps *)rig->caps->priv;
    int index = 0;
    int i;

    rig_debug(RIG_DEBUG_TRACE, "%s: called\n", __func__);

    if (priv_caps == NULL)
    {
        return -RIG_ENAVAIL;
    }

    for (i = 0; i < priv_caps->roofing_filter_count; i++)
    {
        struct newcat_roofing_filter *current_filter = &priv_caps->roofing_filters[i];
        char set_value = current_filter->set_value;

        // Skip get-only values and optional filters
        if (set_value == 0 || current_filter->optional)
        {
            continue;
        }

        // The last filter is always the narrowest
        if (current_filter->width < width)
        {
            break;
        }

        index = current_filter->index;
    }

    return set_roofing_filter(rig, vfo, index);
}

static int get_roofing_filter(RIG *rig, vfo_t vfo,
                              struct newcat_roofing_filter **roofing_filter)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    struct newcat_priv_caps *priv_caps = (struct newcat_priv_caps *)rig->caps->priv;
    struct newcat_roofing_filter *roofing_filters;
    char roofing_filter_choice;
    char main_sub_vfo = '0';
    char rf_vfo = 'X';
    int err;
    int n;
    int i;

    rig_debug(RIG_DEBUG_TRACE, "%s: called\n", __func__);

    if (priv_caps == NULL)
    {
        return -RIG_ENAVAIL;
    }

    roofing_filters = priv_caps->roofing_filters;

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "RF%c%c", main_sub_vfo,
             cat_term);

    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    n = sscanf(priv->ret_data, "RF%c%c", &rf_vfo, &roofing_filter_choice);

    if (n != 2)
    {
        rig_debug(RIG_DEBUG_ERR,
                  "%s: error parsing '%s' for vfo and roofing filter, got %d parsed\n", __func__,
                  priv->ret_data, n);
        return -RIG_EPROTO;
    }

    for (i = 0; i < priv_caps->roofing_filter_count; i++)
    {
        struct newcat_roofing_filter *current_filter = &roofing_filters[i];

        if (current_filter->get_value == roofing_filter_choice)
        {
            *roofing_filter = current_filter;
            return RIG_OK;
        }
    }

    rig_debug(RIG_DEBUG_ERR,
              "%s: Expected a valid roofing filter but got %c from '%s'\n", __func__,
              roofing_filter_choice, priv->ret_data);

    return RIG_EPROTO;
}

int newcat_get_rx_bandwidth(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t *width)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    int w;
    int sh_command_valid = 1;
    char narrow = '!';
    char cmd[] = "SH";
    char main_sub_vfo = '0';

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, cmd))
    {
        return -RIG_ENAVAIL;
    }

    err = newcat_set_vfo_from_alias(rig, &vfo);

    if (err < 0)
    {
        return err;
    }

    if (is_ft950 || is_ftdx5000)
    {
        // Some Yaesu rigs cannot query SH in modes such as AM/FM
        switch (mode)
        {
        case RIG_MODE_FM:
        case RIG_MODE_FMN:
        case RIG_MODE_PKTFM:
        case RIG_MODE_AM:
        case RIG_MODE_AMN:
        case RIG_MODE_PKTAM:
            sh_command_valid = 0;
            break;
        }
    }

    if (rig->caps->targetable_vfo & RIG_TARGETABLE_MODE)
    {
        main_sub_vfo = (RIG_VFO_B == vfo || RIG_VFO_SUB == vfo) ? '1' : '0';
    }

    if (sh_command_valid)
    {
        snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c%c", cmd, main_sub_vfo,
                 cat_term);

        err = newcat_get_cmd(rig);
        if (err != RIG_OK)
        {
            return err;
        }

        if (strlen(priv->ret_data) == 7)
        {
            if (sscanf(priv->ret_data, "SH%*1d0%3d", &w) != 1)
            {
                err = -RIG_EPROTO;
            }
        }
        else if (strlen(priv->ret_data) == 6)
        {
            if (sscanf(priv->ret_data, "SH%*1d%3d", &w) != 1)
            {
                err = -RIG_EPROTO;
            }
        }
        else
        {
            rig_debug(RIG_DEBUG_ERR, "%s: unknown SH response='%s'\n", __func__,
                      priv->ret_data);
            return -RIG_EPROTO;
        }

        if (err != RIG_OK)
        {
            rig_debug(RIG_DEBUG_ERR, "%s: unable to parse width from '%s'\n", __func__,
                      priv->ret_data);
            return -RIG_EPROTO;
        }

        rig_debug(RIG_DEBUG_TRACE, "%s: w=%d\n", __func__, w);
    }
    else
    {
        // Some Yaesu rigs cannot query filter width using SH command in modes such as AM/FM
        w = 0;
    }

    if (is_ft950)
    {
        if ((narrow = get_narrow(rig, RIG_VFO_MAIN)) < 0)
        {
            return -RIG_EPROTO;
        }

        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            switch (w)
            {
            case 0:
                *width = narrow ? 300 : 500;
                break;

            case 3: *width = 100; break;

            case 4: *width = 200; break;

            case 5: *width = 300; break;

            case 6: *width = 400; break;

            case 7: *width = 5000; break;

            case 8: *width = 800; break;

            case 9: *width = 1200; break;

            case 10: *width = 1400; break;

            case 11: *width = 1700; break;

            case 12: *width = 2000; break;

            case 13: *width = 2400; break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            switch (w)
            {
            case 0:
                *width = narrow ? 1800 : 2400;
                break;

            case  1: *width =  200; break;

            case  2: *width =  400; break;

            case  3: *width =  600; break;

            case  4: *width =  850; break;

            case  5: *width = 1100; break;

            case  6: *width = 1350; break;

            case  7: *width = 1500; break;

            case  8: *width = 1650; break;

            case  9: *width = 1800; break;

            case 10: *width = 1950; break;

            case 11: *width = 2100; break;

            case 12: *width = 2250; break;

            case 13: *width = 2400; break;

            case 14: *width = 2450; break;

            case 15: *width = 2500; break;

            case 16: *width = 2600; break;

            case 17: *width = 2700; break;

            case 18: *width = 2800; break;

            case 19: *width = 2900; break;

            case 20: *width = 3000; break;

            default:
                return -RIG_EINVAL;
            }
            break;

        case RIG_MODE_AM:
            *width = narrow ? 6000 : 9000;
            break;

        case RIG_MODE_PKTFM:
        case RIG_MODE_FM:
            *width = narrow ? 9000 : 16000;
            break;

        case RIG_MODE_FMN:
            *width = 9000;
            break;

        default:
            return -RIG_EINVAL;
        }   /* end switch(mode) */

    } /* end if is_ft950 */
    else if (is_ft891)
    {
        if ((narrow = get_narrow(rig, vfo)) < 0)
        {
            return -RIG_EPROTO;
        }

        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            switch (w)
            {
            case 0:
                if (mode == RIG_MODE_CW || mode == RIG_MODE_CWR)
                {
                    *width = narrow ? 500 : 2400;
                }
                else
                {
                    *width = narrow ? 300 : 500;
                }
                break;

            case 1: *width = 50; break;

            case 2: *width = 100; break;

            case 3: *width = 150; break;

            case 4: *width = 200; break;

            case 5: *width = 250; break;

            case 6: *width = 300; break;

            case 7: *width = 350; break;

            case 8: *width = 400; break;

            case 9: *width = 450; break;

            case 10: *width = 500; break;

            case 11: *width = 800; break;

            case 12: *width = 1200; break;

            case 13: *width = 1400; break;

            case 14: *width = 1700; break;

            case 15: *width = 2000; break;

            case 16: *width = 2400; break;

            case 17: *width = 3000; break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            switch (w)
            {
            case 0: *width = narrow ? 1500 : 2400; break;

            case  1: *width =  200; break;

            case  2: *width =  400; break;

            case  3: *width =  600; break;

            case  4: *width =  850; break;

            case  5: *width = 1100; break;

            case  6: *width = 1350; break;

            case  7: *width = 1500; break;

            case  8: *width = 1650; break;

            case  9: *width = 1800; break;

            case 10: *width = 1950; break;

            case 11: *width = 2100; break;

            case 12: *width = 2200; break;

            case 13: *width = 2300; break;

            case 14: *width = 2400; break;

            case 15: *width = 2500; break;

            case 16: *width = 2600; break;

            case 17: *width = 2700; break;

            case 18: *width = 2800; break;

            case 19: *width = 2900; break;

            case 20: *width = 3000; break;

            case 21: *width = 3200; break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_AM:
        case RIG_MODE_FMN:
            *width =  9000;
            break;

        case RIG_MODE_AMN:
            *width =  6000;
            break;

        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
            *width = 16000;
            break;

        default:
            return -RIG_EINVAL;
        }   /* end switch(mode) */

    } /* end if is_ft891 */
    else if (is_ft991)
    {
        if ((narrow = get_narrow(rig, vfo)) < 0)
        {
            return -RIG_EPROTO;
        }

        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            switch (w)
            {
            case 0:
                if (mode == RIG_MODE_CW || mode == RIG_MODE_CWR)
                {
                    *width = narrow ? 500 : 2400;
                }
                else
                {
                    *width = narrow ? 300 : 500;
                }
                break;

            case 1: *width = 50; break;

            case 2: *width = 100; break;

            case 3: *width = 150; break;

            case 4: *width = 200; break;

            case 5: *width = 250; break;

            case 6: *width = 300; break;

            case 7: *width = 350; break;

            case 8: *width = 400; break;

            case 9: *width = 450; break;

            case 10: *width = 500; break;

            case 11: *width = 800; break;

            case 12: *width = 1200; break;

            case 13: *width = 1400; break;

            case 14: *width = 1700; break;

            case 15: *width = 2000; break;

            case 16: *width = 2400; break;

            case 17: *width = 3000; break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            switch (w)
            {
            case 0: *width = narrow ? 1500 : 2400; break;

            case  1: *width =  200; break;

            case  2: *width =  400; break;

            case  3: *width =  600; break;

            case  4: *width =  850; break;

            case  5: *width = 1100; break;

            case  6: *width = 1350; break;

            case  7: *width = 1500; break;

            case  8: *width = 1650; break;

            case  9: *width = 1800; break;

            case 10: *width = 1950; break;

            case 11: *width = 2100; break;

            case 12: *width = 2200; break;

            case 13: *width = 2300; break;

            case 14: *width = 2400; break;

            case 15: *width = 2500; break;

            case 16: *width = 2600; break;

            case 17: *width = 2700; break;

            case 18: *width = 2800; break;

            case 19: *width = 2900; break;

            case 20: *width = 3000; break;

            case 21: *width = 3200; break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_AM:
        case RIG_MODE_FMN:
            *width =  9000;
            break;

        case RIG_MODE_AMN:
            *width =  6000;
            break;

        case RIG_MODE_FM:
        case RIG_MODE_C4FM:
        case RIG_MODE_PKTFM:
            *width = 16000;
            break;

        default:
            return -RIG_EINVAL;
        }   /* end switch(mode) */

    } /* end if is_ft991 */
    else if (is_ftdx1200 || is_ftdx3000)
    {
        if ((narrow = get_narrow(rig, RIG_VFO_MAIN)) < 0)
        {
            return -RIG_EPROTO;
        }

        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            switch (w)
            {
            case 0:
                *width = narrow ? 500 : 2400;
                break;

            case 1: *width = 50; break;

            case 2: *width = 100; break;

            case 3: *width = 150; break;

            case 4: *width = 200; break;

            case 5: *width = 250; break;

            case 6: *width = 300; break;

            case 7: *width = 350; break;

            case 8: *width = 400; break;

            case 9: *width = 450; break;

            case 10: *width = 500; break;

            case 11: *width = 800; break;

            case 12: *width = 1200; break;

            case 13: *width = 1400; break;

            case 14: *width = 1700; break;

            case 15: *width = 2000; break;

            case 16: *width = 2400; break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            switch (w)
            {
            case 0:
                *width = narrow ? 1500 : 2400;
                break;

            case  1: *width =  200; break;

            case  2: *width =  400; break;

            case  3: *width =  600; break;

            case  4: *width =  850; break;

            case  5: *width = 1100; break;

            case  6: *width = 1350; break;

            case  7: *width = 1500; break;

            case  8: *width = 1650; break;

            case  9: *width = 1800; break;

            case 10: *width = 1950; break;

            case 11: *width = 2100; break;

            case 12: *width = 2250; break;

            case 13: *width = 2400; break;

            case 14: *width = 2450; break;

            case 15: *width = 2500; break;

            case 16: *width = 2600; break;

            case 17: *width = 2700; break;

            case 18: *width = 2800; break;

            case 19: *width = 2900; break;

            case 20: *width = 3000; break;

            case 21: *width = 3200; break;

            case 22: *width = 3400; break;

            case 23: *width = 3600; break;

            case 24: *width = 3800; break;

            case 25: *width = 4000; break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_AM:
            *width = narrow ? 6000 : 9000;
            break;

        case RIG_MODE_PKTFM:
        case RIG_MODE_FM:
            *width = narrow ? 9000 : 16000;
            break;

        case RIG_MODE_FMN:
            *width = 9000;
            break;

        case RIG_MODE_AMN:
            *width = 6000;
            break;

        default:
            return -RIG_EINVAL;
        }   /* end switch(mode) */

    } /* end if is_ftdx1200 or is_ftdx3000 */
    else if (is_ftdx5000)
    {
        if ((narrow = get_narrow(rig, RIG_VFO_MAIN)) < 0)
        {
            return -RIG_EPROTO;
        }

        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            switch (w)
            {
            case 0:
                *width = narrow ? 500 : 2400;
                break;

            case 1: *width = 50; break;

            case 2: *width = 100; break;

            case 3: *width = 150; break;

            case 4: *width = 200; break;

            case 5: *width = 250; break;

            case 6: *width = 300; break;

            case 7: *width = 350; break;

            case 8: *width = 400; break;

            case 9: *width = 450; break;

            case 10: *width = 500; break;

            case 11: *width = 800; break;

            case 12: *width = 1200; break;

            case 13: *width = 1400; break;

            case 14: *width = 1700; break;

            case 15: *width = 2000; break;

            case 16: *width = 2400; break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            switch (w)
            {
            case 0:
                *width = narrow ? 1500 : 2400;
                break;

            case  1: *width =  200; break;

            case  2: *width =  400; break;

            case  3: *width =  600; break;

            case  4: *width =  850; break;

            case  5: *width = 1100; break;

            case  6: *width = 1350; break;

            case  7: *width = 1500; break;

            case  8: *width = 1650; break;

            case  9: *width = 1800; break;

            case 10: *width = 1950; break;

            case 11: *width = 2100; break;

            case 12: *width = 2250; break;

            case 13: *width = 2400; break;

            // 14 is not defined for FTDX 5000, but leaving here for completeness
            case 14: *width = 2400; break;

            case 15: *width = 2500; break;

            case 16: *width = 2600; break;

            case 17: *width = 2700; break;

            case 18: *width = 2800; break;

            case 19: *width = 2900; break;

            case 20: *width = 3000; break;

            case 21: *width = 3200; break;

            case 22: *width = 3400; break;

            case 23: *width = 3600; break;

            case 24: *width = 3800; break;

            case 25: *width = 4000; break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_AM:
            *width = narrow ? 6000 : 9000;
            break;

        case RIG_MODE_PKTFM:
        case RIG_MODE_FM:
            *width = narrow ? 9000 : 16000;
            break;

        case RIG_MODE_FMN:
            *width = 9000;
            break;

        case RIG_MODE_AMN:
            *width = 6000;
            break;

        default:
            return -RIG_EINVAL;
        }   /* end switch(mode) */

    } /* end if is_ftdx5000 */
    else if (is_ftdx101)
    {
        rig_debug(RIG_DEBUG_TRACE, "%s: is_ftdx101 w=%d, mode=%s\n", __func__, w,
                  rig_strrmode(mode));

        if (w == 0) // then we need to know the roofing filter
        {
            struct newcat_roofing_filter *roofing_filter;
            int err = get_roofing_filter(rig, vfo, &roofing_filter);

            if (err == RIG_OK)
            {
                *width = roofing_filter->width;
            }
        }

        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
            switch (w)
            {
            case 0: break; /* use roofing filter width */

            case 1: *width = 50; break;

            case 2: *width = 100; break;

            case 3: *width = 150; break;

            case 4: *width = 200; break;

            case 5: *width = 250; break;

            case 6: *width = 300; break;

            case 7: *width = 350; break;

            case 8: *width = 400; break;

            case 9: *width = 450;  break;

            case 10: *width = 500;  break;

            case 11: *width = 600;  break;

            case 12: *width = 800;  break;

            case 13: *width = 1200;  break;

            case 14: *width = 1400;  break;

            case 15: *width = 1700;  break;

            case 16: *width = 2000;  break;

            case 17: *width = 2400;  break;

            case 18: *width = 3000;  break;

            default: return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            switch (w)
            {
            case 0: break; /* use roofing filter width */

            case 1: *width = 300; break;

            case 2: *width = 400; break;

            case 3: *width = 600; break;

            case 4: *width = 850; break;

            case 5: *width = 1100; break;

            case 6: *width = 1200; break;

            case 7: *width = 1500; break;

            case 8: *width = 1650; break;

            case 9: *width = 1800; break;

            case 10: *width = 1950;  break;

            case 11: *width = 2100;  break;

            case 12: *width = 2200;  break;

            case 13: *width = 2300;  break;

            case 14: *width = 2400;  break;

            case 15: *width = 2500;  break;

            case 16: *width = 2600;  break;

            case 17: *width = 2700;  break;

            case 18: *width = 2800;  break;

            case 19: *width = 2900;  break;

            case 20: *width = 3000;  break;

            case 21: *width = 3200;  break;

            case 22: *width = 3500;  break;

            case 23: *width = 4000;  break;

            default:
                rig_debug(RIG_DEBUG_ERR, "%s: unknown width=%d\n", __func__, w);
                return -RIG_EINVAL;
            }

            break;

        case RIG_MODE_AM:
        case RIG_MODE_FMN:
        case RIG_MODE_PKTFMN:
            *width = 9000;
            break;

        case RIG_MODE_AMN:
            *width = 6000;
            break;

        case RIG_MODE_FM:
        case RIG_MODE_PKTFM:
            *width = 16000;
            break;

        default:
            rig_debug(RIG_DEBUG_TRACE, "%s: bad mode\n", __func__);
            return -RIG_EINVAL;
        }   /* end switch(mode) */

        rig_debug(RIG_DEBUG_TRACE, "%s: end if FTDX101D\n", __func__);
    } /* end if is_ftdx101 */
    else
    {
        /* FT450, FT2000, FT9000 */
        switch (mode)
        {
        case RIG_MODE_PKTUSB:
        case RIG_MODE_PKTLSB:
        case RIG_MODE_RTTY:
        case RIG_MODE_RTTYR:
        case RIG_MODE_CW:
        case RIG_MODE_CWR:
        case RIG_MODE_LSB:
        case RIG_MODE_USB:
            if (w < 16)
            {
                *width = rig_passband_narrow(rig, mode);
            }
            else if (w > 16)
            {
                *width = rig_passband_wide(rig, mode);
            }
            else
            {
                *width = rig_passband_normal(rig, mode);
            }
            break;

        case RIG_MODE_AM:
        case RIG_MODE_PKTFM:
        case RIG_MODE_FM:
            return RIG_OK;

        default:
            return -RIG_EINVAL;
        }   /* end switch (mode) */
    } /* end else */

    rig_debug(RIG_DEBUG_TRACE, "%s: return RIG_OK\n", __func__);
    return RIG_OK;
}


int newcat_set_faststep(RIG *rig, ncboolean fast_step)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    char c;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, "FS"))
    {
        return -RIG_ENAVAIL;
    }

    if (fast_step == TRUE)
    {
        c = '1';
    }
    else
    {
        c = '0';
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "FS%c%c", c, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "cmd_str = %s\n", priv->cmd_str);

    return newcat_set_cmd(rig);
}


int newcat_get_faststep(RIG *rig, ncboolean *fast_step)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    char c;
    char command[] = "FS";

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", command, cat_term);

    /* Get Fast Step */
    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    c = priv->ret_data[2];

    if (c == '1')
    {
        *fast_step = TRUE;
    }
    else
    {
        *fast_step = FALSE;
    }

    return RIG_OK;
}


int newcat_get_rigid(RIG *rig)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    const char *s = NULL;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    /* if first valid get */
    if (priv->rig_id == NC_RIGID_NONE)
    {
        s = newcat_get_info(rig);

        if (s != NULL)
        {
            s += 2;     /* ID0310, jump past ID */
            priv->rig_id = atoi(s);
        }
    }

    rig_debug(RIG_DEBUG_TRACE, "rig_id = %d, *s = %s\n", priv->rig_id,
              s == NULL ? "NULL" : s);

    return priv->rig_id;
}


/*
 * input:   RIG *, vfo_t *
 * output:  VFO mode: RIG_VFO_VFO for VFO A and B
 *                    RIG_VFO_MEM for VFO MEM
 * return: RIG_OK or error
 */
int newcat_get_vfo_mode(RIG *rig, vfo_t *vfo_mode)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int err;
    int offset = 0;
    char command[] = "IF";

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    /* Get VFO A Information ****************** */
    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", command, cat_term);

    if (RIG_OK != (err = newcat_get_cmd(rig)))
    {
        return err;
    }

    /* vfo, mem, P7 ************************** */
    // e.g. FT450 has 27 byte IF response, FT991 has 28 byte if response (one more byte for P2 VFO A Freq)
    // so we now check to ensure we know the length of the response
    switch (strlen(priv->ret_data))
    {
    case 27: offset = 21; priv->width_frequency = 8; break;

    case 28: offset = 22; priv->width_frequency = 9; break;

    default:
        rig_debug(RIG_DEBUG_ERR,
                  "%s: incorrect length of IF response, expected 27 or 28, got %d", __func__,
                  (int)strlen(priv->ret_data));
        return -RIG_EPROTO;
    }

    rig_debug(RIG_DEBUG_TRACE, "%s: offset=%d, width_frequency=%d\n", __func__,
              offset, priv->width_frequency);

    switch (priv->ret_data[offset])
    {
    case '0': *vfo_mode = RIG_VFO_VFO; break;

    case '1':   /* Memory */
    case '2':   /* Memory Tune */
    case '3':   /* Quick Memory Bank */
    case '4':   /* Quick Memory Bank Tune */
    default:
        *vfo_mode = RIG_VFO_MEM;
    }

    rig_debug(RIG_DEBUG_TRACE, "%s: vfo mode = %s\n", __func__,
              rig_strvfo(*vfo_mode));

    return err;
}


/*
 * Writes data and waits for response
 * input:  complete CAT command string including termination in cmd_str
 * output: complete CAT command answer string in ret_data
 * return: RIG_OK or error
 */


int newcat_vfomem_toggle(RIG *rig)
{
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    char command[] = "VM";

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (!newcat_valid_command(rig, command))
    {
        return -RIG_ENAVAIL;
    }

    /* copy set command */
    snprintf(priv->cmd_str, sizeof(priv->cmd_str), "%s%c", command, cat_term);

    rig_debug(RIG_DEBUG_TRACE, "%s: cmd_str = %s\n", __func__, priv->cmd_str);

    return newcat_set_cmd(rig);
}

/*
 * Writes a null  terminated command string from  priv->cmd_str to the
 * CAT  port and  returns a  response from  the rig  in priv->ret_data
 * which is also null terminated.
 *
 * Honors the 'retry'  capabilities field by resending  the command up
 * to 'retry' times until a valid response is received. In the special
 * cases of receiving  a valid response to a different  command or the
 * "?;" busy please wait response; the command is not resent but up to
 * 'retry' retries to receive a valid response are made.
 */
int newcat_get_cmd(RIG *rig)
{
    struct rig_state *state = &rig->state;
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int retry_count = 0;
    int rc = -RIG_EPROTO;
    int is_read_cmd = 0;

    // try to cache rapid repeats of the IF command
    // this is for WSJT-X/JTDX sequence of v/f/m/t
    // should allow rapid repeat of any call using the IF; cmd
    // Any call that changes something in the IF response should invalidate the cache
    if (strcmp(priv->cmd_str, "IF;") == 0 && priv->cache_start.tv_sec != 0)
    {
        int cache_age_ms;

        cache_age_ms = elapsed_ms(&priv->cache_start, 0);

        if (cache_age_ms < 500) // 500ms cache time
        {
            rig_debug(RIG_DEBUG_TRACE, "%s: cache hit, age=%dms\n", __func__, cache_age_ms);
            strcpy(priv->ret_data, priv->last_if_response);
            return RIG_OK;
        }

        // we drop through and do the real IF command
    }

    // any command that is read only should not expire cache
    is_read_cmd =
        strcmp(priv->cmd_str, "AG0;") == 0
        || strcmp(priv->cmd_str, "AG1;") == 0
        || strcmp(priv->cmd_str, "AN0;") == 0
        || strcmp(priv->cmd_str, "AN1;") == 0
        || strcmp(priv->cmd_str, "BP00;") == 0
        || strcmp(priv->cmd_str, "BP01;") == 0
        || strcmp(priv->cmd_str, "BP10;") == 0
        || strcmp(priv->cmd_str, "BP11;") == 0
        || strcmp(priv->cmd_str, "CN00;") == 0
        || strcmp(priv->cmd_str, "CN10;") == 0
        || strcmp(priv->cmd_str, "CO00;") == 0
        || strcmp(priv->cmd_str, "CO01;") == 0
        || strcmp(priv->cmd_str, "CO02;") == 0
        || strcmp(priv->cmd_str, "CO03;") == 0
        || strcmp(priv->cmd_str, "CO10;") == 0
        || strcmp(priv->cmd_str, "CO11;") == 0
        || strcmp(priv->cmd_str, "CO12;") == 0
        || strcmp(priv->cmd_str, "CO13;") == 0
        || strcmp(priv->cmd_str, "IS1;") == 0
        || strcmp(priv->cmd_str, "IS0;") == 0
        || strcmp(priv->cmd_str, "IS1;") == 0
        || strcmp(priv->cmd_str, "MD0;") == 0
        || strcmp(priv->cmd_str, "MD1;") == 0
        || strcmp(priv->cmd_str, "NA0;") == 0
        || strcmp(priv->cmd_str, "NA1;") == 0
        || strcmp(priv->cmd_str, "NB0;") == 0
        || strcmp(priv->cmd_str, "NB1;") == 0
        || strcmp(priv->cmd_str, "NL0;") == 0
        || strcmp(priv->cmd_str, "NL1;") == 0
        || strcmp(priv->cmd_str, "NR0;") == 0
        || strcmp(priv->cmd_str, "NR1;") == 0
        || strcmp(priv->cmd_str, "NR0;") == 0
        || strcmp(priv->cmd_str, "NR1;") == 0
        || strcmp(priv->cmd_str, "OS0;") == 0
        || strcmp(priv->cmd_str, "OS0;") == 0
        || strcmp(priv->cmd_str, "OS1;") == 0
        || strcmp(priv->cmd_str, "PA0;") == 0
        || strcmp(priv->cmd_str, "PA1;") == 0
        || strcmp(priv->cmd_str, "RA0;") == 0
        || strcmp(priv->cmd_str, "RA1;") == 0
        || strcmp(priv->cmd_str, "RF0;") == 0
        || strcmp(priv->cmd_str, "RF1;") == 0
        || strcmp(priv->cmd_str, "RL0;") == 0
        || strcmp(priv->cmd_str, "RL1;") == 0
        || strcmp(priv->cmd_str, "RM0;") == 0
        || strcmp(priv->cmd_str, "RM1;") == 0
        || strcmp(priv->cmd_str, "SM0;") == 0
        || strcmp(priv->cmd_str, "SM1;") == 0
        || strcmp(priv->cmd_str, "SQ0;") == 0
        || strcmp(priv->cmd_str, "SQ1;") == 0
        || strcmp(priv->cmd_str, "VT0;") == 0
        || strcmp(priv->cmd_str, "VT1;") == 0;

    if (priv->cmd_str[2] !=
            ';' && !is_read_cmd) // then we must be setting something so we'll invalidate the cache
    {
        rig_debug(RIG_DEBUG_TRACE, "%s: cache invalidated\n", __func__);
        priv->cache_start.tv_sec = 0;
    }


    while (rc != RIG_OK && retry_count++ <= state->rigport.retry)
    {
        if (rc != -RIG_BUSBUSY)
        {
            /* send the command */
            rig_debug(RIG_DEBUG_TRACE, "cmd_str = %s\n", priv->cmd_str);

            if (RIG_OK != (rc = write_block(&state->rigport, priv->cmd_str,
                                            strlen(priv->cmd_str))))
            {
                return rc;
            }
        }

        /* read the reply */
        if ((rc = read_string(&state->rigport, priv->ret_data, sizeof(priv->ret_data),
                              &cat_term, sizeof(cat_term))) <= 0)
        {
            continue;             /* usually a timeout - retry */
        }

        rig_debug(RIG_DEBUG_TRACE, "%s: read count = %d, ret_data = %s\n",
                  __func__, rc, priv->ret_data);
        rc = RIG_OK;              /* received something */

        /* Check that command termination is correct - alternative is
           response is longer that the buffer */
        if (cat_term  != priv->ret_data[strlen(priv->ret_data) - 1])
        {
            rig_debug(RIG_DEBUG_ERR, "%s: Command is not correctly terminated '%s'\n",
                      __func__, priv->ret_data);
            rc = -RIG_BUSBUSY;    /* don't write command again */
            /* we could decrement retry_count
               here but there is a danger of
               infinite looping so we just use up
               a retry for safety's sake */
            continue;             /* retry */
        }

        /* check for error codes */
        if (2 == strlen(priv->ret_data))
        {
            /* The following error responses  are documented for Kenwood
               but not for  Yaesu, but at least one of  them is known to
               occur  in that  the  FT-450 certainly  responds to  "IF;"
               occasionally with  "?;". The others are  harmless even of
               they do not occur as they are unambiguous. */
            switch (priv->ret_data[0])
            {
            case 'N':
                /* Command recognized by rig but invalid data entered. */
                rig_debug(RIG_DEBUG_VERBOSE, "%s: NegAck for '%s'\n", __func__, priv->cmd_str);
                return -RIG_ENAVAIL;

            case 'O':
                /* Too many characters sent without a carriage return */
                rig_debug(RIG_DEBUG_VERBOSE, "%s: Overflow for '%s'\n", __func__,
                          priv->cmd_str);
                rc = -RIG_EPROTO;
                break;            /* retry */

            case 'E':
                /* Communication error */
                rig_debug(RIG_DEBUG_VERBOSE, "%s: Communication error for '%s'\n", __func__,
                          priv->cmd_str);
                rc = -RIG_EIO;
                break;            /* retry */

            case '?':
                /* The ? response is ambiguous and undocumented by Yaesu, but for get commands it seems to
                 * indicate that the rig rejected the command because the state of the rig is not valid for the command
                 * or that the command parameter is invalid. Retrying the command does not fix the issue,
                 * as the error is caused by the an invalid combination of rig state.
                 *
                 * For example, the following cases have been observed:
                 * - MR and MC commands are rejected when referring to an _empty_ memory channel even
                 *   if the channel number is in a valid range
                 * - BC (ANF) and RL (NR) commands fail in AM/FM modes, because they are
                 *   supported only in SSB/CW/RTTY modes
                 * - MG (MICGAIN) command fails in RTTY mode, as it's a digital mode
                 *
                 * There are many more cases like these and they vary by rig model.
                 *
                 * So far, "rig busy" type situations with the ? response have not been observed for get commands.
                 */
                rig_debug(RIG_DEBUG_ERR, "%s: Command rejected by the rig: '%s'\n", __func__,
                          priv->cmd_str);
                return -RIG_ERJCTED;
            }

            continue;
        }

        /* verify that reply was to the command we sent */
        if ((priv->ret_data[0] != priv->cmd_str[0]
                || priv->ret_data[1] != priv->cmd_str[1]))
        {
            /*
             * TODO: When RIG_TRN is enabled, we can pass the string
             * to the decoder for callback. That way we don't ignore
             * any commands.
             */
            rig_debug(RIG_DEBUG_ERR, "%s: wrong reply %.2s for command %.2s\n",
                      __func__, priv->ret_data, priv->cmd_str);
            rc = -RIG_BUSBUSY;    /* retry read only */
        }
    }

    // update the cache
    if (strncmp(priv->cmd_str, "IF;", 3) == 0)
    {
        elapsed_ms(&priv->cache_start, 1);
        strcpy(priv->last_if_response, priv->ret_data);
    }

    return rc;
}

/*
 * Writes a null  terminated command string from  priv->cmd_str to the
 * CAT  port that is not expected to have a response.
 *
 * Honors the 'retry'  capabilities field by resending  the command up
 * to 'retry' times until a valid response is received. In the special
 * cases of receiving  a valid response to a different  command or the
 * "?;" busy please wait response; the command is not resent but up to
 * 'retry' retries to receive a valid response are made.
 */
int newcat_set_cmd(RIG *rig)
{
    struct rig_state *state = &rig->state;
    struct newcat_priv_data *priv = (struct newcat_priv_data *)rig->state.priv;
    int retry_count = 0;
    int rc = -RIG_EPROTO;

    /* pick a basic quick query command for verification */
    char const *const verify_cmd = RIG_MODEL_FT9000 == rig->caps->rig_model ?
                                   "AI;" : "ID;";

    while (rc != RIG_OK && retry_count++ <= state->rigport.retry)
    {
        rig_flush(&state->rigport);  /* discard any unsolicited data */
        /* send the command */
        rig_debug(RIG_DEBUG_TRACE, "cmd_str = %s\n", priv->cmd_str);

        if (RIG_OK != (rc = write_block(&state->rigport, priv->cmd_str,
                                        strlen(priv->cmd_str))))
        {
            return rc;
        }

        /* skip validation if high throughput is needed */
        if (priv->fast_set_commands == TRUE)
        {
            return RIG_OK;
        }

        /* send the verification command */
        rig_debug(RIG_DEBUG_TRACE, "cmd_str = %s\n", verify_cmd);

        if (RIG_OK != (rc = write_block(&state->rigport, verify_cmd,
                                        strlen(verify_cmd))))
        {
            return rc;
        }

        /* read the reply */
        if ((rc = read_string(&state->rigport, priv->ret_data, sizeof(priv->ret_data),
                              &cat_term, sizeof(cat_term))) <= 0)
        {
            continue;             /* usually a timeout - retry */
        }

        rig_debug(RIG_DEBUG_TRACE, "%s: read count = %d, ret_data = %s\n",
                  __func__, rc, priv->ret_data);
        rc = RIG_OK;              /* received something */

        /* check for error codes */
        if (2 == strlen(priv->ret_data))
        {
            /* The following error responses  are documented for Kenwood
               but not for  Yaesu, but at least one of  them is known to
               occur  in that  the  FT-450 certainly  responds to  "IF;"
               occasionally with  "?;". The others are  harmless even of
               they do not occur as they are unambiguous. */
            switch (priv->ret_data[0])
            {
            case 'N':
                /* Command recognized by rig but invalid data entered. */
                rig_debug(RIG_DEBUG_VERBOSE, "%s: NegAck for '%s'\n", __func__, priv->cmd_str);
                return -RIG_ENAVAIL;

            case 'O':
                /* Too many characters sent without a carriage return */
                rig_debug(RIG_DEBUG_VERBOSE, "%s: Overflow for '%s'\n", __func__,
                          priv->cmd_str);
                rc = -RIG_EPROTO;
                break;            /* retry */

            case 'E':
                /* Communication error */
                rig_debug(RIG_DEBUG_VERBOSE, "%s: Communication error for '%s'\n", __func__,
                          priv->cmd_str);
                rc = -RIG_EIO;
                break;            /* retry */

            case '?':
                /* The ? response is ambiguous and undocumented by Yaesu. For set commands it seems to indicate:
                 * 1) either that the rig is busy and the command needs to be retried
                 * 2) or that the rig rejected the command because the state of the rig is not valid for the command
                 *    or that the command parameter is invalid. Retrying the command does not fix the issue
                 *    in this case, as the error is caused by the an invalid combination of rig state.
                 *    The latter case is consistent with behaviour of get commands.
                 *
                 * For example, the following cases have been observed:
                 * - MR and MC commands are rejected when referring to an _empty_ memory channel even
                 *   if the channel number is in a valid range
                 * - BC (ANF) and RL (NR) commands fail in AM/FM modes, because they are
                 *   supported only in SSB/CW/RTTY modes
                 * - MG (MICGAIN) command fails in RTTY mode, as it's a digital mode
                 *
                 * There are many more cases like these and they vary by rig model.
                 */
                if (priv->question_mark_response_means_rejected)
                {
                    rig_debug(RIG_DEBUG_ERR, "%s: Command rejected by the rig: '%s'\n", __func__,
                              priv->cmd_str);
                    return -RIG_ERJCTED;
                }

                /* Rig busy wait please */
                rig_debug(RIG_DEBUG_WARN, "%s: Rig busy - retrying\n", __func__);

                /* read the verify command reply */
                if ((rc = read_string(&state->rigport, priv->ret_data, sizeof(priv->ret_data),
                                      &cat_term, sizeof(cat_term))) > 0)
                {
                    rig_debug(RIG_DEBUG_TRACE, "%s: read count = %d, ret_data = %s\n",
                              __func__, rc, priv->ret_data);
                    rc = RIG_OK;  /* probably recovered and read verification */
                }
                else
                {
                    /* probably a timeout */
                    rc = -RIG_BUSBUSY; /* retry */
                }

                break;
            }
        }

        if (RIG_OK == rc)
        {
            /* Check that response prefix and response termination is
               correct - alternative is response is longer that the
               buffer */
            if (strncmp(verify_cmd, priv->ret_data, strlen(verify_cmd) - 1)
                    || (cat_term != priv->ret_data[strlen(priv->ret_data) - 1]))
            {
                rig_debug(RIG_DEBUG_ERR, "%s: Unexpected verify command response '%s'\n",
                          __func__, priv->ret_data);
                rc = -RIG_BUSBUSY;
                continue;             /* retry */
            }
        }
    }

    return rc;
}

struct
{
    rmode_t mode;
    char modechar;
    ncboolean chk_width;
} static const newcat_mode_conv[] =
{
    { RIG_MODE_LSB,    '1', FALSE },
    { RIG_MODE_USB,    '2', FALSE },
    { RIG_MODE_CW,     '3', FALSE },
    { RIG_MODE_FM,     '4', TRUE },
    { RIG_MODE_AM,     '5', TRUE },
    { RIG_MODE_RTTY,   '6', FALSE },
    { RIG_MODE_CWR,    '7', FALSE },
    { RIG_MODE_PKTLSB, '8', FALSE },
    { RIG_MODE_RTTYR,  '9', FALSE },
    { RIG_MODE_PKTFM,  'A', TRUE },
    { RIG_MODE_FMN,    'B', TRUE },
    { RIG_MODE_PKTUSB, 'C', FALSE },
    { RIG_MODE_AMN,    'D', TRUE },
    { RIG_MODE_C4FM,   'E', TRUE },
    { RIG_MODE_PKTFMN, 'F', TRUE }
};

rmode_t newcat_rmode(char mode)
{
    int i;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    for (i = 0; i < sizeof(newcat_mode_conv) / sizeof(newcat_mode_conv[0]); i++)
    {
        if (newcat_mode_conv[i].modechar == mode)
        {
            rig_debug(RIG_DEBUG_TRACE, "%s: %s for %c\n", __func__,
                      rig_strrmode(newcat_mode_conv[i].mode), mode);
            return (newcat_mode_conv[i].mode);
        }
    }

    return (RIG_MODE_NONE);
}

char newcat_modechar(rmode_t rmode)
{
    int i;

    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    for (i = 0; i < sizeof(newcat_mode_conv) / sizeof(newcat_mode_conv[0]); i++)
    {
        if (newcat_mode_conv[i].mode == rmode)
        {
            rig_debug(RIG_DEBUG_TRACE, "%s: return %c for %s\n", __func__,
                      newcat_mode_conv[i].modechar, rig_strrmode(rmode));
            return (newcat_mode_conv[i].modechar);
        }
    }

    return ('0');
}

rmode_t newcat_rmode_width(RIG *rig, vfo_t vfo, char mode, pbwidth_t *width)
{
    ncboolean narrow;
    int i;
    rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __func__);

    if (width != NULL)
    {
        *width = RIG_PASSBAND_NORMAL;
    }

    for (i = 0; i < sizeof(newcat_mode_conv) / sizeof(newcat_mode_conv[0]); i++)
    {
        if (newcat_mode_conv[i].modechar == mode)
        {
            if (newcat_mode_conv[i].chk_width == TRUE && width != NULL)
            {
                if (newcat_is_rig(rig, RIG_MODEL_FT991)
                        && mode == 'E') // crude fix because 991 hangs on NA0; command while in C4FM
                {
                    rig_debug(RIG_DEBUG_TRACE, "991A & C4FM Skip newcat_get_narrow in %s\n",
                              __func__);
                }
                else
                {
                    if (newcat_get_narrow(rig, vfo, &narrow) != RIG_OK)
                    {
                        return (newcat_mode_conv[i].mode);
                    }

                    if (narrow == TRUE)
                    {
                        *width = rig_passband_narrow(rig, mode);
                    }
                    else
                    {
                        *width = rig_passband_normal(rig, mode);
                    }
                }
            }

            return (newcat_mode_conv[i].mode);
        }
    }

    rig_debug(RIG_DEBUG_VERBOSE, "%s fell out the bottom %c %s\n", __func__,
              mode, rig_strrmode(mode));

    return ('0');
}
