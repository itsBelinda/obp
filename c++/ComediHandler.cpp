/**
 * @file        ComediHandler.cpp
 * @brief       The implementation file of the ComediHandler
 * @author      Belinda Kneubühler belinda.kneubuehler@gmail.com
 * @date        2020-08-18
 * @author      Bernd Porr mail@berndporr.me.uk
 * @date        2005-2017
 * @author      Matthias H. Hennig hennig@cn.stir.ac.uk
 * @date        2003
 * @copyright   GNU General Public License v2.0
 *
 * @details
 * This class is based on a previous implementation by Bernd Porr and Matthias H. Henning.
 */
#include <cstdlib>
#include "common.h"
#include "ComediHandler.h"


// TODO: make singleton
/**
 * The constructor of the ComediHandler object.
 *
 * Initialises the hardware. If the hardware is not connected, the initialisation fails and the program finishes. *
 */
ComediHandler::ComediHandler():
    adChannel(0) {

    PLOG_VERBOSE << "ComediHandler started";
    const char *filename = COMEDI_DEV_PATH;

    /* open the device */
    if ((dev = comedi_open(filename)) == 0) {
        comedi_perror(filename);
        exit(1);
    }

    // do not produce NAN for out of range behaviour
    comedi_set_global_oor_behavior(COMEDI_OOR_NUMBER);

    maxdata = comedi_get_maxdata(dev, COMEDI_SUB_DEVICE, COMEDI_SUB_DEVICE);
    crange = comedi_get_range(dev, COMEDI_SUB_DEVICE, COMEDI_SUB_DEVICE, COMEDI_RANGE_ID);
    numChannels = comedi_get_n_channels(dev, COMEDI_SUB_DEVICE);

    PLOG_VERBOSE << "maxdata: " << maxdata;
    PLOG_VERBOSE << "crange min: " << crange->min << " max: " << crange->max;
    PLOG_VERBOSE << "num channels: " << numChannels;

    if (numChannels < COMEDI_NUM_CHANNEL) {
        PLOG_ERROR << "Number of available device channels (" << numChannels << ") smaller than used ("
                   << COMEDI_NUM_CHANNEL << ")";
        exit(-1);
    } else {
        numChannels = COMEDI_NUM_CHANNEL;
    }

    chanlist = new unsigned[numChannels];

    /* Set up channel list */
    for (int i = 0; i < numChannels; i++)
        chanlist[i] = CR_PACK(i, COMEDI_RANGE_ID, AREF_GROUND);

    int ret = comedi_get_cmd_generic_timed(dev, COMEDI_SUB_DEVICE, &comediCommand, numChannels,
                                           (int) (1e9 / (SAMPLING_RATE)));

    if (ret < 0) {
        PLOG_ERROR << "comedi_get_cmd_generic_timed failed";
        exit(-1);
    }

    /* Modify parts of the command */
    comediCommand.chanlist = chanlist;
    comediCommand.stop_src = TRIG_NONE;
    comediCommand.stop_arg = 0;

    /* comedi_command_test() tests a command to see if the
     * trigger sources and arguments are valid for the subdevice.
     * If a trigger source is invalid, it will be logically ANDed
     * with valid values (trigger sources are actually bitmasks),
     * which may or may not result in a valid trigger source.
     * If an argument is invalid, it will be adjusted to the
     * nearest valid value.  In this way, for many commands, you
     * can test it multiple times until it passes.  Typically,
     * if you can't get a valid command in two tests, the original
     * command wasn't specified very well. */
    ret = comedi_command_test(dev, &comediCommand);
    PLOG_INFO << "first test returned " << ret;
    if (ret < 0) {
        PLOG_ERROR << "Comedi test command failed";
        comedi_perror("comedi_command_test");
        exit(-1);
    }

    ret = comedi_command_test(dev, &comediCommand);
    PLOG_INFO << "second test returned " << ret;
    if (ret < 0) {
        PLOG_ERROR << "Comedi test command failed";
        comedi_perror("comedi_command_test");
        exit(-1);
    }

    // the timing is done channel by channel
    // this means that the actual sampling rate is divided by
    // number of channels
    if ((comediCommand.convert_src == TRIG_TIMER) && (comediCommand.convert_arg)) {
        sampling_rate = (((double) 1E9 / comediCommand.convert_arg) / numChannels);
    }
    PLOG_VERBOSE << "sampling rate (channel by channel): " << sampling_rate;

    // the timing is done scan by scan (all channels at once)
    // the sampling rate is equivalent of the scan_begin_arg
    if ((comediCommand.scan_begin_src == TRIG_TIMER) && (comediCommand.scan_begin_arg)) {
        sampling_rate = (double) 1E9 / comediCommand.scan_begin_arg;
    }
    PLOG_VERBOSE << "sampling rate (all channels): " << sampling_rate;

    /* start the command */
    ret = comedi_command(dev, &comediCommand);
    if (ret < 0) {
        PLOG_ERROR << "Comedi command failed";
        comedi_perror("comedi_command");
        exit(1);
    }

    int subdev_flags = comedi_get_subdevice_flags(dev, COMEDI_SUB_DEVICE);

    if ((sigmaBoard = subdev_flags & SDF_LSAMPL))
    {
        readSize = sizeof(lsampl_t) * numChannels;
    }
    else {
        PLOG_WARNING << "Detected device is not a sigma board, ADC resolution might not be sufficient.";
        readSize = sizeof(sampl_t) * numChannels;
    }

};

/**
 * Gets the sampling rate from the device.
 * @return The sampling rate.
 */
double ComediHandler::getSamplingRate() {
    return sampling_rate;
}

/**
 * Checks the buffer, if there is anything in it.
 * @return The number of samples in the buffer.
 */
int ComediHandler::getBufferContents() {
    return comedi_get_buffer_contents(dev, COMEDI_SUB_DEVICE);
}

/**
 * Reads and returns the raw data sample directly.
 * @return The raw data sample.
 */
int ComediHandler::getRawSample() {
    return readRawSample();
}

/**
 * Reads a raw data sample and transforms it into voltage. This method should not be called if there is no data in
 * the buffer.
 * @return The data sample in voltage.
 */
double ComediHandler::getVoltageSample(){
    return comedi_to_phys(readRawSample(), crange, maxdata);
}

/**
 * Reads one raw sample from the buffer. This method should not be called if there is no data in
 * the buffer.
 * @return The raw ADC value read from the buffer.
 */
int ComediHandler::readRawSample() {
    unsigned char buffer[readSize];
    if (read(comedi_fileno(dev), buffer, readSize) == 0) {
        PLOG_ERROR << "Error reading from device: end of acquisition!";
        exit(-1);
    }

    int v = 0;
    if (sigmaBoard) {
        v = ((lsampl_t *) buffer)[adChannel];
    } else {
        v = ((sampl_t *) buffer)[adChannel];
    }
    return v;
}

