#include "datapath/channel_impl/soc_channel.h"

namespace nicc {

// GIDs are currently used only for RoCE. This default value works for most
// clusters, but we need a more robust GID selection method. Some observations:
//  * On physical clusters, gid_index = 0 always works (in my experience)
//  * On VM clusters (AWS/KVM), gid_index = 0 does not work, gid_index = 1 works
//  * Mellanox's `show_gids` script lists all GIDs on all NICs
static constexpr size_t kDefaultGIDIndex = 0;   // Currently, the GRH (ipv4 + udp port) is set by CPU

nicc_retval_t Channel_SoC::allocate_channel(const char *dev_name, uint8_t phy_port) {
    nicc_retval_t retval = NICC_SUCCESS;
    
    _huge_alloc = new HugeAlloc(kMemRegionSize, 0);    // SoC only has one NUMA node
    common_resolve_phy_port(dev_name, phy_port, kMTU, _resolve);
    if(unlikely(NICC_SUCCESS != (retval = __roce_resolve_phy_port()))){
        NICC_WARN_C("failed to resolve phy port: dev_name(%s), phy_port(%u), retval(%u)", dev_name, phy_port, retval);
        goto exit;
    }

exit:
    // TODO: destory if failed
    return retval;
}

nicc_retval_t Channel_SoC::__roce_resolve_phy_port() {
    nicc_retval_t retval = NICC_SUCCESS;
    struct ibv_port_attr port_attr;

    NICC_CHECK_POINTER(_resolve.ib_ctx);

    // 查询端口属性
    if (ibv_query_port(_resolve.ib_ctx, _resolve.dev_port_id, &port_attr)) {
        NICC_WARN_C("failed to query port: dev_port_id(%u), retval(%u)", _resolve.dev_port_id, retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    _resolve.port_lid = port_attr.lid;

    if (ibv_query_gid(_resolve.ib_ctx, _resolve.dev_port_id, kDefaultGIDIndex, &_resolve.gid)) {
        NICC_WARN_C("failed to query gid: dev_port_id(%u), gid_index(%u), retval(%u)", _resolve.dev_port_id, kDefaultGIDIndex, retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    // 验证 GID 是否有效（不是全0）
    uint64_t *gid_data = (uint64_t *)&_resolve.gid;
    if (gid_data[0] == 0 && gid_data[1] == 0) {
        NICC_WARN_C("invalid gid (all zeros), dev_port_id(%u), gid_index(%u)", _resolve.dev_port_id, kDefaultGIDIndex);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    // 设置 IPv4 地址（从 GID 中提取）
    memset(&_resolve.ipv4_addr_, 0, sizeof(ipaddr_t));
    _resolve.ipv4_addr_.ip = _resolve.gid.global.interface_id & 0xffffffff;

    return retval;
}

nicc_retval_t Channel_SoC::__init_verbs_structs() {
    nicc_retval_t retval = NICC_SUCCESS;
    /* ...... */
    return retval;
}

nicc_retval_t Channel_SoC::__init_ring() {
    nicc_retval_t retval = NICC_SUCCESS;
    /* ...... */
    return retval;
}

nicc_retval_t Channel_SoC::deallocate_channel() {
    nicc_retval_t retval = NICC_SUCCESS;
    /* ...... */
    return retval;
}

} // namespace nicc