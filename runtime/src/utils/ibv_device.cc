#include "utils/ibv_device.h"

namespace nicc {

/*!
 * \brief   Opens the device and returns the device context
 * \param   device_name [in]: device name
 * \return  device context on success and NULL otherwise
 */
struct ibv_context* utils_ibv_open_device(const char *device_name){
    struct ibv_device **dev_list = nullptr;
    struct ibv_device *dev = nullptr;
    struct ibv_context *ibv_ctx = nullptr;
    int dev_idx;

    NICC_CHECK_POINTER(device_name)

    dev_list = ibv_get_device_list(nullptr);
    if (unlikely(dev_list == nullptr)) {
        NICC_WARN("failed to get IB device list");
        goto exit;
    }

    for (dev_idx = 0; dev_list[dev_idx] != nullptr; ++dev_idx)
        if (!strcmp(ibv_get_device_name(dev_list[dev_idx]), device_name))
            break;
    
    dev = dev_list[dev_idx];
    if (unlikely(dev == nullptr)) {
        NICC_WARN("failed to find device %s", device_name);
        goto exit;
    }

    ibv_ctx = ibv_open_device(dev);
    if (unlikely(ibv_ctx == nullptr)) {
        NICC_WARN("failed to get device context of device %s", device_name);
    }
    ibv_free_device_list(dev_list);

exit:
    if(unlikely(ibv_ctx == nullptr)){
        if(unlikely(dev_list == nullptr)){
            ibv_free_device_list(dev_list);
        }
    }

    return ibv_ctx;
}

nicc_retval_t utils_create_flow_engine_domains(device_state_t &dev_state) {
    NICC_CHECK_POINTER(dev_state.ibv_ctx);
    dev_state.rx_domain = mlx5dv_dr_domain_create(dev_state.ibv_ctx, MLX5DV_DR_DOMAIN_TYPE_NIC_RX);
    NICC_CHECK_POINTER(dev_state.rx_domain);
    dev_state.tx_domain = mlx5dv_dr_domain_create(dev_state.ibv_ctx, MLX5DV_DR_DOMAIN_TYPE_NIC_TX);
    NICC_CHECK_POINTER(dev_state.tx_domain);
    dev_state.fdb_domain = mlx5dv_dr_domain_create(dev_state.ibv_ctx, MLX5DV_DR_DOMAIN_TYPE_FDB);
    NICC_CHECK_POINTER(dev_state.fdb_domain);

    return NICC_SUCCESS;
}

} // namespace nicc
