#pragma once
#include <iostream>

#include <infiniband/mlx5_api.h>
#include <infiniband/mlx5dv.h>

#include "common.h"
#include "log.h"

namespace nicc {

/*!
 * \brief   Opens the device and returns the device context
 * \param   device_name [in]: device name
 * \return  device context on success and NULL otherwise
 */
struct ibv_context* nicc_utils_ibv_open_device(const char *device_name);

} // namespace nicc
