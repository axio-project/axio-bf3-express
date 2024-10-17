#include "datapath/component_block.h"

namespace nicc {


nicc_retval_t ComponentBlock::create_table(int level, FlowMAT** table){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(this->_base_domain);
    retval = this->_base_domain->create_table(level, table);

    return retval;
}


nicc_retval_t ComponentBlock::destory_table(FlowMAT* table){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(this->_base_domain);
    retval = this->_base_domain->destory_table(table);

    return retval;
}


} // namespace nicc
