#include "ctrlpath/mat.h"

namespace nicc {

nicc_retval_t FlowMAT::create_matcher(flow_wildcards_t wc, int priority, FlowMatcher** matcher){
    nicc_retval_t retval = NICC_SUCCESS;
    std::vector<flow_entry_t*>* _entry_list = nullptr;

    NICC_CHECK_POINTER(matcher);

    // check whether the pattern already exit
    if(unlikely(this->_entry_map.count(wc) > 0)){
        NICC_WARN_C("failed to create matcher, match pattern already exist");
        retval = NICC_ERROR_DUPLICATED;
        goto exit;
    }

    retval = this->__create_matcher(wc, priority, matcher);
    NICC_CHECK_POINTER(*matcher);

    if(likely(retval == NICC_SUCCESS)){
        NICC_CHECK_POINTER(_entry_list = new std::vector<flow_entry_t*>());
        this->_entry_map.insert({ **matcher, _entry_list });
    }

exit:
    return retval;
}

nicc_retval_t FlowMAT::detory_matcher(FlowMatcher* matcher){
    nicc_retval_t retval = NICC_SUCCESS;
    std::vector<flow_entry_t*>* _entry_list = nullptr;

    NICC_CHECK_POINTER(matcher);

    // check whether the pattern already exit
    if(unlikely(this->_entry_map.count(*matcher) == 0)){
        NICC_WARN_C("failed to destory matcher, no pattern with specified value exist");
        retval = NICC_ERROR_DUPLICATED;
        goto exit;
    }

    retval = this->__destory_matcher(matcher);
    if(likely(retval == NICC_SUCCESS)){
        NICC_CHECK_POINTER(_entry_list = this->_entry_map[*matcher]);
        delete _entry_list;
        this->_entry_map.erase(*matcher);
    }

exit:
    return retval;
}


nicc_retval_t FlowMAT::load_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len){
    nicc_retval_t retval = NICC_SUCCESS;
    flow_hash_t hash;
    uint64_t i;
    std::vector<flow_entry_t*>* _entry_list = nullptr;

    NICC_CHECK_POINTER(entries);
    NICC_CHECK_BOUND(len, NICC_UINT64_MAX);

    if(unlikely(this->_entry_map.count(matcher) == 0)){
        NICC_WARN_C(
            "failed to load entry, no matcher with specified pattern founded in this table: "
            "FlowMAT(%p)", this
        );
        retval = NICC_ERROR_NOT_FOUND;
        goto exit;
    }
    NICC_CHECK_POINTER(_entry_list = this->_entry_map[matcher]);

    retval = this->__load_entries(matcher, entries, len);

    if(likely(retval == NICC_SUCCESS)){
        for(i=0; i<len; i++){
            hash.cal(entries[i].match, entries[i].action);
            entries[i].hash = hash;
            _entry_list->push_back( &(entries[i]) );
        }
    }

exit:
    return retval;
}


nicc_retval_t FlowMAT::unload_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len){
    nicc_retval_t retval = NICC_SUCCESS;
    nicc_uint64_t i = 0;
    std::vector<flow_entry_t*>* entry_list;

    NICC_CHECK_POINTER(entries);
    NICC_CHECK_BOUND(len, NICC_UINT64_MAX);

    // check whether the match pattern exists
    if(unlikely(this->_entry_map.count(matcher) == 0)){
        NICC_WARN_C(
            "failed to remove entry, no matcher with specified pattern founded in this table: "
            "FlowMAT(%p)", this
        );
        retval = NICC_ERROR_NOT_FOUND;
        goto exit;
    }
    NICC_CHECK_POINTER(entry_list = this->_entry_map[matcher]);

    retval = this->__unload_entries(matcher, entries, len);

    if(likely(retval == NICC_SUCCESS)){
        // TODO: this could be slow, need to be optimized
        for(i=0; i<len; i++){
            entry_list->erase(std::remove(entry_list->begin(), entry_list->end(), &(entries[i])), entry_list->end());
        }
    }

exit:
    return retval;
}


nicc_retval_t FlowDomain::create_table(int level, FlowMAT** table){
    nicc_retval_t retval = NICC_SUCCESS;
    
    NICC_CHECK_POINTER(table);

    retval = this->__create_table(level, table);

    if(likely(retval == NICC_SUCCESS)){
        this->_table_map.insert({ level, *table });
    }

exit:
    return retval;
}


nicc_retval_t FlowDomain::destory_table(FlowMAT* table){
    nicc_retval_t retval = NICC_SUCCESS;
    typename std::multimap<int, FlowMAT*>::iterator map_iter;

    NICC_CHECK_POINTER(table);

   for(map_iter=this->_table_map.begin(); map_iter!=this->_table_map.end(); map_iter++){
        if(map_iter->second == table){
            retval = this->__detory_table(map_iter->first, table);
            if(likely(retval == NICC_SUCCESS)){ 
                this->_table_map.erase(map_iter); 
            } else {
                NICC_WARN_C(
                    "failed to destory table, error occurs while deleting: "
                    "table(%p), FlowDomain(%p)",
                    table, this
                );
            }
            goto exit;
        }
   }

    NICC_WARN_C(
        "failed to destory table, no table with specified pointer in this domain: "
        "table(%p), FlowDomain(%p)",
        table, this
    );
    retval = NICC_ERROR_NOT_FOUND;

exit:
    return retval;
}


} // namespace nicc
