#pragma once
#include <iostream>

#include "mlx5/mlx5_api.h"
#include "mlx5/mlx5dv.h"

#include "common.h"
#include "log.h"

namespace nicc {


typedef struct device_state {
    struct ibv_context *ibv_ctx;
    const char *device_name;
    
    // device-wise flow engine domain
    struct mlx5dv_dr_domain *rx_domain;
    struct mlx5dv_dr_domain *tx_domain;
    struct mlx5dv_dr_domain *fdb_domain;
} device_state_t;


/*!
 * \brief   Opens the device and returns the device context
 * \param   device_name [in]: device name
 * \return  device context on success and NULL otherwise
 */
struct ibv_context* utils_ibv_open_device(const char *device_name);

/**
 * \brief  Create flow engine domains
 * \param  dev_state [in]: device state
 * \return NICC_SUCCESS on success and NICC_ERROR otherwise
 */
nicc_retval_t utils_create_flow_engine_domains(device_state_t &dev_state);

} // namespace nicc
