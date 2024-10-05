#pragma once
#include <iostream>

#include <infiniband/mlx5dv.h>

#include "common.h"
#include "log.h"
#include "types.h"
#include "app_context.h"
#include "datapath/component_block.h"
#include "ctrlpath/mat.h"

namespace nicc {
/**
 * ----------------------General structure----------------------
 */ 


/**
 *  \brief  state of FlowEngine
 */
typedef struct ComponentState_FlowEngine {
    // basic state
    ComponentBaseState_t base_state;

    // state of FlowEngine
    uint8_t mock_state;
} ComponentState_FlowEngine_t;


/**
 *  \brief  descriptor of FlowEngine
 */
typedef struct ComponentDesp_FlowEngine {
    /* ========== Common fields ========== */
    // basic desriptor
    ComponentBaseDesp_t base_desp;
    
    /* ========== ComponentBlock_DPA fields ========== */
    struct mlx5dv_flow_match_parameters *tx_match_mask;
    struct mlx5dv_flow_match_parameters *rx_match_mask;
    //! \todo use correct table level and priority, below are fake values
    uint8_t table_level = 0;
    uint32_t matcher_priority = 0;

} ComponentDesp_FlowEngine_t;


/**
 *  \brief  basic state of the function register 
            into the component
 *  \note   this structure should be inherited and 
 *          implenented by each component
 */
typedef struct ComponentFuncState_FlowEngine {
    ComponentFuncBaseState_t base_state;

    struct dr_flow_table		*rx_flow_table;
	struct dr_flow_table		*tx_flow_table;
	struct dr_flow_table		*tx_flow_root_table;

	struct dr_flow_rule		*rx_rule;
	struct dr_flow_rule		*tx_rule;
	struct dr_flow_rule		*tx_root_rule;
} ComponentFuncState_FlowEngine_t;


/**
 *  \brief  matcher table implementation for flow engine
 */
class FlowMatcher_FlowEngine : public FlowMatcher {
 public:
    FlowMatcher_FlowEngine(
        flow_wildcards_t match_wc,
        struct mlx5dv_dr_table *tbl,
        int priority,
        nicc_uint8_t criteria
    ) : FlowMatcher(match_wc),
        _mlx5dv_dr_tbl(tbl),
        _mlx5dv_dr_match_parameters(nullptr),
        _mlx5dv_dr_match_priority(priority),
        _mlx5dv_dr_match_criteria(criteria)
    {   
        if(unlikely( NICC_SUCCESS != this->__create_mlx5dv_dr_matcher() )){
            NICC_WARN_C(
                "failed to create matcher on mlx5dv_dr table: "
                "mlx5dv_dr_table(%p), mlx5dv_dr_match_priority(%d), mlx5dv_dr_match_criteria(%u)",
                tbl, priority, criteria
            );
            goto exit;
        }

    exit:
        ;
    }

    ~FlowMatcher_FlowEngine() {
        // TODO: remember to destory _mlx5dv_dr_match_parameters
    }

 private:
    struct mlx5dv_dr_table *_mlx5dv_dr_tbl;
    struct mlx5dv_flow_match_parameters *_mlx5dv_dr_match_parameters;
    int _mlx5dv_dr_match_priority;
    nicc_uint8_t _mlx5dv_dr_match_criteria;

    /**
     *  \brief  create matcher on MLX5 flow engine
     *  \return NICC_SUCCESS for successfully creation
     */
    nicc_retval_t __create_mlx5dv_dr_matcher();
};


/**
 *  \brief  match-action table implementation for flow engine
 */
class FlowMAT_FlowEngine : public FlowMAT {
 public:
    /**
     *  \brief  constructor
     *  \param  level           level of this table
     *  \param  fdb_domain      mlx5dv_dr fdb domain to create this table
     */
    FlowMAT_FlowEngine(int level, struct mlx5dv_dr_domain* fdb_domain)
        :   FlowMAT(),
            _mlx5dv_dr_fdb_domain(fdb_domain),
            _mlx5dv_dr_tbl(nullptr),
            _mlx5dv_dr_tbl_level(level)
    {
        NICC_CHECK_POINTER(fdb_domain);

        if(unlikely( NICC_SUCCESS != this->__create_mlx5dv_dr_table() )){
            NICC_WARN_C(
                "failed to create table on mlx5dv_dr FDB domain: "
                "level(%d), fdb_domain(%p)",
                level, fdb_domain
            );
            goto exit;
        }

    exit:
        ;
    }

    ~FlowMAT_FlowEngine() = default;

 protected:
    /**
     *  \brief  create new macther in this table (detailed implementation)
     *  \param  flow_wildcards_t    wildcard for creating this matcher
     *  \param  priority            priority to create this matcher
     *  \param  criteria            criteria to create this matcher
     *  \param  matcher             the created matcher
     *  \return NICC_SUCCESS for successfully creation
     */
    nicc_retval_t __create_matcher(flow_wildcards_t wc, int priority, nicc_uint8_t criteria, FlowMatcher** matcher) override;

    /**
     *  \brief  destory macther in this table (detailed implementation)
     *  \param  matcher matcher to be destoried
     *  \return NICC_SUCCESS for successfully destory
     */
    virtual nicc_retval_t __destory_matcher(FlowMatcher* matcher) override;

    /**
     *  \brief  load MAT entries
     *  \param  entries list of entries to be loaded
     *  \return NICC_SUCCESS for successfully loading
     */
    nicc_retval_t __load_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len) override;

    /**
     *  \brief  add MAT entries
     *  \param  entries list of entries to be loaded
     *  \return NICC_SUCCESS for successfully loading
     */
    nicc_retval_t __unload_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len) override;

 private:
    // mlx5dv_dr structures
    struct mlx5dv_dr_domain *_mlx5dv_dr_fdb_domain;
    struct mlx5dv_dr_table *_mlx5dv_dr_tbl;
    int _mlx5dv_dr_tbl_level;

    /**
     *  \brief  create table on MLX5 flow engine
     *  \return NICC_SUCCESS for successfully creation
     */
    nicc_retval_t __create_mlx5dv_dr_table();
};


/**
 *  \brief  flow domain implementation for flow engine
 */
class FlowDomain_FlowEngine : public FlowDomain {
 public:
    FlowDomain_FlowEngine(struct mlx5dv_dr_domain* fdb_domain) 
        : FlowDomain(), _fdb_domain(fdb_domain) {}
    ~FlowDomain_FlowEngine() = default;
 
 protected:
    /**
     *  \brief  allocate new table in this domain (detailed implementation)
     *  \param  level       level of the table
     *  \param  table       created table
     *  \return NICC_SUCCESS for successfully allocation
     */
    nicc_retval_t __create_table(int level, FlowMAT** table) override;

    /**
     *  \brief  destory table in this domain (detailed implementation)
     *  \param  level       level of the table
     *  \param  table       table to be destoried
     *  \return NICC_SUCCESS for successfully destory
     */
    nicc_retval_t __detory_table(int level, FlowMAT* table) override;

 private:
    struct mlx5dv_dr_domain* _fdb_domain;
};


class ComponentBlock_FlowEngine : public ComponentBlock {
/**
 * ----------------------Public Methods----------------------
 */ 
 public:
    ComponentBlock_FlowEngine() {
        NICC_CHECK_POINTER(this->_desp = new ComponentDesp_FlowEngine_t);
        NICC_CHECK_POINTER(this->_state = new ComponentState_FlowEngine_t);
        NICC_CHECK_POINTER(this->_base_desp = &this->_desp->base_desp);
        NICC_CHECK_POINTER(this->_base_state = &this->_state->base_state);
    }
    ~ComponentBlock_FlowEngine(){};

    /**
     *  \brief  typeid of handlers register into FlowEngine
     */
    enum handler_typeid_t : appfunc_handler_typeid_t { Init = 0, Event };

    /**
     *  \brief  register a new application function into this component
     *  \param  app_func  the function to be registered into this 
     *                    component, at least contains Init
     *  \param  device_state        global device state
     *  \return NICC_SUCCESS for successful registeration
     */
    nicc_retval_t register_app_function(AppFunction *app_func, device_state_t &device_state) override;

    /*!
     *  \brief  deregister a application function
     *  \return NICC_SUCCESS for successful unregisteration
     */
    nicc_retval_t unregister_app_function() override;
/**
 * ----------------------Internel Parameters----------------------
 */
    friend class Component_FlowEngine;
 protected:
    /**
     * descriptor of the component block, recording total 
     * hardware resources allocated from the component
     */
    ComponentDesp_FlowEngine_t *_desp;

    /**
     * state of the component block, recording runtime state 
     * for rescheduling, inter-block communication channel, and MT
     */
    ComponentState_FlowEngine_t *_state;

    /**
     * basic state of the function register into the component  
     * block, using for running the function on this component block
     */
    ComponentFuncState_FlowEngine_t *_function_state = nullptr;
};


} // namespace nicc
