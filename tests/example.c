/*  This is a elementary program calling Hamlib to do some useful things.
 *
 *  Edit to specify your rig model and serial port, and baud rate
 *  before compiling.
 *  To compile:
 *      gcc -I../src -I../include -g -o example example.c sprintflst.c -lhamlib
 *      if hamlib is installed in /usr/local/...
 *
 */

#include <stdio.h>
#include <string.h>
#include <hamlib/rig.h>
#include <hamlib/riglist.h>
#include "sprintflst.h"


int main()
{
    RIG *my_rig;
    char *rig_file, *info_buf, *mm;
    freq_t freq;
    value_t rawstrength, power, strength;
    float s_meter, rig_raw2val();
    int status, retcode;
    unsigned int mwpower;
    rmode_t mode;
    pbwidth_t width;

    /* Set verbosity level */
    rig_set_debug(RIG_DEBUG_ERR);       // errors only

    /* Instantiate a rig */
    my_rig = rig_init(RIG_MODEL_DUMMY); // your rig model.

    /* Set up serial port, baud rate */
    rig_file = "/dev/ttyUSB0";        // your serial device

    strncpy(my_rig->state.rigport.pathname, rig_file, FILPATHLEN - 1);

    my_rig->state.rigport.parm.serial.rate = 57600; // your baud rate

    /* Open my rig */
    retcode = rig_open(my_rig);

    if (retcode != RIG_OK)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: rig_open failed %s\n", __func__,
                  rigerror(retcode));
        return 1;
    }

    /* Give me ID info, e.g., firmware version. */
    info_buf = (char *)rig_get_info(my_rig);

    printf("Rig_info: '%s'\n", info_buf);

    /* Note: As a general practice, we should check to see if a given
     * function is within the rig's capabilities before calling it, but
     * we are simplifying here. Also, we should check each call's returned
     * status in case of error.  (That's an inelegant way to catch an unsupported
     * operation.)
     */

    /* Main VFO frequency */
    status = rig_get_freq(my_rig, RIG_VFO_CURR, &freq);

    if (status != RIG_OK) { printf("Get freq failed?? Err=%s\n", rigerror(status)); }

    printf("VFO freq. = %.1f Hz\n", freq);

    /* Current mode */
    status = rig_get_mode(my_rig, RIG_VFO_CURR, &mode, &width);

    if (status != RIG_OK) { printf("Get mode failed?? Err=%s\n", rigerror(status)); }

    switch (mode)
    {
    case RIG_MODE_USB:
        mm = "USB";
        break;

    case RIG_MODE_LSB:
        mm = "LSB";
        break;

    case RIG_MODE_CW:
        mm = "CW";
        break;

    case RIG_MODE_CWR:
        mm = "CWR";
        break;

    case RIG_MODE_AM:
        mm = "AM";
        break;

    case RIG_MODE_FM:
        mm = "FM";
        break;

    case RIG_MODE_WFM:
        mm = "WFM";
        break;

    case RIG_MODE_RTTY:
        mm = "RTTY";
        break;

    default:
        mm = "unrecognized";
        break; /* there are more possibilities! */
    }

    printf("Current mode = 0x%lX = %s, width = %ld\n", mode, mm, width);

    /* rig power output */
    status = rig_get_level(my_rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, &power);

    if (status != RIG_OK) { rig_debug(RIG_DEBUG_ERR, "%s: error rig_get_level: %s\n", __func__, rigerror(status)); }

    printf("RF Power relative setting = %.3f (0.0 - 1.0)\n", power.f);

    /* Convert power reading to watts */
    status = rig_power2mW(my_rig, &mwpower, power.f, freq, mode);

    if (status != RIG_OK) { rig_debug(RIG_DEBUG_ERR, "%s: error rig_get_level: %s\n", __func__, rigerror(status)); }

    printf("RF Power calibrated = %.1f Watts\n", mwpower / 1000.);

    /* Raw and calibrated S-meter values */
    status = rig_get_level(my_rig, RIG_VFO_CURR, RIG_LEVEL_RAWSTR, &rawstrength);

    if (status != RIG_OK) { rig_debug(RIG_DEBUG_ERR, "%s: error rig_get_level: %s\n", __func__, rigerror(status)); }

    printf("Raw receive strength = %d\n", rawstrength.i);

    s_meter = rig_raw2val(rawstrength.i, &my_rig->caps->str_cal);

    printf("S-meter value = %.2f dB relative to S9\n", s_meter);

    /* now try using RIG_LEVEL_STRENGTH itself */
    status = rig_get_strength(my_rig, RIG_VFO_CURR, &strength);

    if (status != RIG_OK) { rig_debug(RIG_DEBUG_ERR, "%s: error rig_get_level: %s\n", __func__, rigerror(status)); }

    printf("LEVEL_STRENGTH returns %d\n", strength.i);

    const freq_range_t *range = rig_get_range(&my_rig->state.rx_range_list[0],
                                14074000, RIG_MODE_USB);

    if (status != RIG_OK) { rig_debug(RIG_DEBUG_ERR, "%s: error rig_get_ragne: %s\n", __func__, rigerror(status)); }

    if (range)
    {
        char vfolist[256];
        sprintf_vfo(vfolist, my_rig->state.vfo_list);
        printf("Range start=%"PRIfreq", end=%"PRIfreq", low_power=%d, high_power=%d, vfos=%s\n",
               range->startf, range->endf, range->low_power, range->high_power, vfolist);
    }
    else
    {
        printf("Not rx range list found\n");
    }
};
