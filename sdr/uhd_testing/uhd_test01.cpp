#include <iostream>
#include <uhd.h>
#include <uhd/device.hpp>

int main()
{
    uhd::device_addr_t hint;
    hint["type"] = "usrp1";
    uhd::device_addrs_t dev_addrs = uhd::device::find(hint);

    // uhd_string_vector_handle devs;
    // uhd_usrp_find(nullptr, &devs);
    // std::cout << "Hello world!" << std::endl;
    return 0;
}