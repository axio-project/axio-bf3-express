#pragma once

#include "common.h"
#include "utils/ibv_device.h"

/**
 *  \brief  root flow table of all datapath pipelines
 */
class RootTable {

 public:
   /**
    *   \brief  constructor
    *   \param  device_state  global device state
    */
   RootTable(mlx5dv_dr_domain* rx_domain, mlx5dv_dr_domain* tx_domain, mlx5dv_dr_domain* fdb_domain){
      NICC_CHECK_POINTER(rx_domain);
      NICC_CHECK_POINTER(tx_domain);
      NICC_CHECK_POINTER(fdb_domain);
    
      /**
       *  \brief  create_new_table_on_specific_domain
       *  \param  domain      the domain to create table on
       *  \param  tbl         the table to be create
       *  \param  priority    the table's priority on current domain
       *  \param  level       the table's level on this domain
       *  \param  match_mask  
       *  \return NICC_SUCCESS for succesfully creation
       */
      auto __create_table = [&](
          mlx5dv_dr_domain *domain,
          struct dr_flow_table *tbl,
          int priority, 
          int level,
          struct mlx5dv_flow_match_parameters *match_mask
      ) -> nicc_retval_t {	      
        nicc_retval_t retval = NICC_SUCCESS;
        uint8_t criteria_enable = 0x1;
        doca_error_t result;

        tbl = calloc(1, sizeof(*tbl));
        NICC_CHECK_POINTER(tbl);

        tbl->dr_table = mlx5dv_dr_table_create(domain, level);
        if(unlikely(tbl->dr_table == nullptr)){
          NICC_WARN_C("failed to create mlx5 flow table");
          retval = NICC_ERROR_HARDWARE_FAILURE;
          goto exit;
        }

        tbl->dr_matcher = mlx5dv_dr_matcher_create(tbl->dr_table, priority, criteria_enable, match_mask);
        if (tbl->dr_matcher == NULL) {
          DOCA_LOG_ERR("Failed to create matcher [%d]", errno);
          result = DOCA_ERROR_DRIVER;
          goto exit_with_error;
        }

        *tbl_out = tbl;
        return DOCA_SUCCESS;
      exit_with_error:
        if (tbl->dr_matcher)
          mlx5dv_dr_matcher_destroy(tbl->dr_matcher);
        if (tbl->dr_table)
          mlx5dv_dr_table_destroy(tbl->dr_table);
        free(tbl);
        retur

      };
       
   }
   ~RootTable = default;

 private:

   nicc_retval_t __init
};
