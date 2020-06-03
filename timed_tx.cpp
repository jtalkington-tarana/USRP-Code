#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/timer/timer.hpp>
#include <chrono>
#include <complex>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <time.h>

namespace po = boost::program_options;
namespace bt = boost::timer;

static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

void setup_time_source(const uhd::usrp::multi_usrp::sptr& usrp) {

    try {
        std::cout << "Waiting for GPS lock.." << std::flush;
        while (!usrp->get_mboard_sensor("gps_locked",0).to_bool() && !stop_signal_called) {
            std::cout << "." << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        std::cout << "done." << std::endl;

        usrp->set_sync_source("gpsdo", "gpsdo");
    } catch (...) {
        std::cout << " no gpsdo found, using external reference." << std::endl;
        usrp->set_time_source("external");
        usrp->set_clock_source("external");
    }

    std::cout << "Waiting for reference clock lock.." << std::flush;
    while(!usrp->get_mboard_sensor("ref_locked", 0).to_bool() && !stop_signal_called) {
        std::cout << "." << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    std::cout << "done." << std::endl;

    usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
}

typedef std::complex<int8_t> samp_type;

typedef struct test_data {
    uhd::usrp::multi_usrp::sptr usrp;
    uhd::tx_streamer::sptr tx_stream;
    double frame_offset;
    size_t buff_size;
    samp_type* buff;
} test_data_t;


/*
 * Start the transmit at a specific time.
 *
 * This *always* results in a transmit start at a random time.
 */
void xmit_tx_metadata(const test_data_t& td) {
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = false;
    md.has_time_spec = true;

    uhd::time_spec_t start_offset(2.0);
    md.time_spec = td.usrp->get_time_last_pps().get_full_secs() + start_offset + uhd::time_spec_t(td.frame_offset);

    while (not stop_signal_called) {
        td.tx_stream->send(td.buff, td.buff_size, md, 4.0);
        md.has_time_spec = false;
        md.start_of_burst = false;
    }
}

/*
 * Start the transmit immediately after setting the time on the USRP.
 *
 * This consistently starts the transmit at a predictable time. However, the
 * offset must be set to at least two seconds in the future, or the uhd driver
 * reports (L) missed timing.
 */
void xmit_pps_edge(const test_data_t& td) {
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = false;
    md.has_time_spec = true;

    // must offset by 17us to meet timing. It seems to start at
    // full_secs - (some factor of 5ms) - 17us
    // This consistently gets us started at the correct offset from the 5ms
    // frame start.
    uhd::time_spec_t start_offset(2, 0.000017);
    md.time_spec = start_offset + uhd::time_spec_t(td.frame_offset);

    td.usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
    while (not stop_signal_called) {

        td.tx_stream->send(td.buff, td.buff_size, md, 4.0);
        md.has_time_spec = false;
        md.start_of_burst = false;
    }

}

/*
 * Display burst timing information.
 *
 * Transmits the waveform with 2 second intervals between. We should expect the
 * cycle to take ~2 seconds + 5ms for the waveform.
 */
void xmit_burst_timing(const test_data_t& td) {
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = true;
    md.has_time_spec = true;

    // Ignore the frame offset here, since we are only interested in how long
    // the xmit takes.
    md.time_spec = 2.0;

    while (not stop_signal_called) {

        bt::cpu_timer timer;
        td.usrp->set_time_now(0.0);
        td.tx_stream->send(td.buff, td.buff_size, md, 4.0);
        std::cout << timer.format(6, "%w") << std::endl;
    }
}

/*
 * Display transmit time info.
 *
 * Waits 2 seconds for the first transmit, then continuously sends data. We
 * should expect the first xmit to return at ~2s+5ms, and the remaining to take
 * ~5ms. 
 *
 * This seems to always result in (S) sequence errors.
 */
void xmit_fast_timing(const test_data_t& td) {
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = false;
    md.has_time_spec = true;


    uhd::time_spec_t start_offset(2, 0.000017);
    md.time_spec = start_offset + td.frame_offset;

    size_t i;
    bt::cpu_timer total_timer;
    td.usrp->set_time_now(0.0);
    for (i = 0; i < 50; ++i) {
        if (stop_signal_called) {
            break;
        }
        bt::cpu_timer timer;
        td.tx_stream->send(td.buff, td.buff_size, md, 4.0);
        std::cout << timer.format(6, "%w") << std::endl;
        md.has_time_spec = false;
        md.start_of_burst = false;
    }

    std::cout << "Time spent: " << total_timer.format(6, "%w") << std::endl;
    std::cout << "Expected time: " << std::setprecision(6) << 2 + (0.005 * i) << std::endl;
}

int UHD_SAFE_MAIN(int argc, char* argv[])
{
    double frame_offset;
    std::string test_method;

    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("frame-offset", po::value<double>(&frame_offset)->default_value(0.0), "offset from 5ms frame start (fractional seconds)")
        ("test-method", po::value<std::string>(&test_method)->default_value("tx_metadata"), "set the test transmit method (tx_metadata, pps_edge, burst_timing, fast_timing)")
        ("help", "print the help message")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cerr << desc << std::endl;
        return 0;
    }

    std::signal(SIGINT, &sig_int_handler);

    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(uhd::device_addr_t(""));

	setup_time_source(usrp);

    usrp->set_tx_rate(double(44.8e6));

    uhd::tune_request_t tune_request(double(3600e6));
    usrp->set_tx_freq(tune_request);

    usrp->set_tx_gain(double(40));

	std::this_thread::sleep_for(std::chrono::seconds(1));

    // Check LO Lock detect
    std::vector<std::string> sensor_names;
    sensor_names = usrp->get_tx_sensor_names(0);
    if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked")
        != sensor_names.end()) {
        uhd::sensor_value_t lo_locked = usrp->get_tx_sensor("lo_locked", 0);
        std::cout << boost::format("Checking TX: %s ...") % lo_locked.to_pp_string()
                  << std::endl;
        UHD_ASSERT_THROW(lo_locked.to_bool());
    }


    std::ifstream infile("W_WSE_5ms_P50_S0sc8.dat", std::ifstream::binary);
    infile.seekg(0, infile.end);
    size_t buff_size = infile.tellg() / sizeof(samp_type);
    std::vector<samp_type> buff(buff_size);
    infile.seekg(0, infile.beg);
    infile.read((char*)&buff.front(), buff_size * sizeof(samp_type));
    infile.close();

    // create a transmit streamer
	std::vector<size_t> channel_nums;
    uhd::stream_args_t stream_args("sc8", "sc8");
    channel_nums.push_back(boost::lexical_cast<size_t>(0));
    stream_args.channels             = channel_nums;
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);

    test_data_t td{ usrp, tx_stream, frame_offset, buff_size, &buff.front() };

    if (test_method.compare("tx_metadata") == 0) {
        xmit_tx_metadata(td);
    } else if (test_method.compare("pps_edge") == 0) {
        xmit_pps_edge(td);
    } else if (test_method.compare("burst_timing") == 0) {
        xmit_burst_timing(td);
    } else if (test_method.compare("fast_timing") == 0) {
        xmit_fast_timing(td);
    } else {
        std::cerr << "ERROR: --test-method must be one of `tx_metadata`, `pps_edge`, or `burst_timing`" << std::endl;
        return 1;
    }



    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    return EXIT_SUCCESS;
}
