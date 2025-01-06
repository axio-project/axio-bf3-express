#include "datapath/channel_impl/soc_channel.h"

namespace nicc {

nicc_retval_t Channel_SoC::allocate_channel(const char *dev_name, uint8_t phy_port) {
    nicc_retval_t retval = NICC_SUCCESS;
    
    _huge_alloc = new HugeAlloc(kMemRegionSize, 0);    // SoC only has one NUMA node
    common_resolve_phy_port(dev_name, phy_port, kMTU, _resolve);

    return retval;
}

nicc_retval_t Channel_SoC::deallocate_channel() {
    nicc_retval_t retval = NICC_SUCCESS;
    /* ...... */
    return retval;
}

} // namespace nicc