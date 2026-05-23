#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <uhd/usrp/multi_usrp.hpp>

using namespace std::chrono_literals;

static const int samps_per_buff = (1 << 20);

static uhd::usrp::multi_usrp::sptr usrp;
static uhd::rx_streamer::sptr rx_stream;
static std::vector<float *> buffs;

int main(int argc, char **argv)
{
    usrp = uhd::usrp::multi_usrp::make("name,product=B200,type=b200,uhd");
    if (!usrp)
    {
        return 0;
    }

    usrp->set_rx_rate(56e6);
    usrp->set_rx_freq(913.1e6, 0);
    usrp->set_rx_gain(40.0);
    usrp->set_rx_bandwidth(56e6, 0);
    usrp->set_rx_antenna("RX2", 0);
    // usrp->set_rx_spp(samps_per_buff / 2);

    uhd::stream_args_t stream_args("fc32", "fc32");
    stream_args.channels = {0};
    // stream_args.args["spp"] = "4096";
    rx_stream = usrp->get_rx_stream(stream_args);
    if (!rx_stream)
    {
        return -1;
    }

    buffs.resize(rx_stream->get_num_channels());
    for (int i = 0; i < buffs.size(); i++)
    {
        buffs[i] = new float[samps_per_buff];
    }

    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.num_samps = samps_per_buff;
    stream_cmd.stream_now = true;
    rx_stream->issue_stream_cmd(stream_cmd);

    uhd::rx_metadata_t md;
    size_t num_rx_samps;
    while (true)
    {
        num_rx_samps = rx_stream->recv(buffs, samps_per_buff, md);
        std::cout << "Got " << num_rx_samps << " samples" << std::endl;

        std::this_thread::sleep_for(100ms);
    }

    return 0;
}
