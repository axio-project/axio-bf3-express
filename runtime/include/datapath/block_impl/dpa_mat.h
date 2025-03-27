// \todo delete this file
#pragma once
#include <doca_error.h>
#include <infiniband/mlx5dv.h>
#include <libflexio/flexio.h>
#include "mlx5/mlx5_ifc.h"

// \brief mlx5_ifc_dr_match_spec_bits has been defined in mlx5/mlx5_ifc.h
// struct mlx5_ifc_dr_match_spec_bits {
// 	uint8_t smac_47_16[0x20];

// 	uint8_t smac_15_0[0x10];
// 	uint8_t ethertype[0x10];

// 	uint8_t dmac_47_16[0x20];

// 	uint8_t dmac_15_0[0x10];
// 	uint8_t first_prio[0x3];
// 	uint8_t first_cfi[0x1];
// 	uint8_t first_vid[0xc];

// 	uint8_t ip_protocol[0x8];
// 	uint8_t ip_dscp[0x6];
// 	uint8_t ip_ecn[0x2];
// 	uint8_t cvlan_tag[0x1];
// 	uint8_t svlan_tag[0x1];
// 	uint8_t frag[0x1];
// 	uint8_t ip_version[0x4];
// 	uint8_t tcp_flags[0x9];

// 	uint8_t tcp_sport[0x10];
// 	uint8_t tcp_dport[0x10];

// 	uint8_t reserved_at_c0[0x18];
// 	uint8_t ip_ttl_hoplimit[0x8];

// 	uint8_t udp_sport[0x10];
// 	uint8_t udp_dport[0x10];

// 	uint8_t src_ip_127_96[0x20];

// 	uint8_t src_ip_95_64[0x20];

// 	uint8_t src_ip_63_32[0x20];

// 	uint8_t src_ip_31_0[0x20];

// 	uint8_t dst_ip_127_96[0x20];

// 	uint8_t dst_ip_95_64[0x20];

// 	uint8_t dst_ip_63_32[0x20];

// 	uint8_t dst_ip_31_0[0x20];
// };  

struct dr_flow_table {
	struct mlx5dv_dr_table *dr_table;     /* DR table in the domain at specific level */
	struct mlx5dv_dr_matcher *dr_matcher; /* DR matcher object in the table. One matcher per table */
};

struct dr_flow_rule {
	struct mlx5dv_dr_action *dr_action; /* Rule action */
	struct mlx5dv_dr_rule *dr_rule;	    /* Steering rule */
};

static doca_error_t __create_flow_table(struct mlx5dv_dr_domain *domain, 
                                        int level,
                                        int priority,
                                        struct mlx5dv_flow_match_parameters *match_mask,
                                        struct dr_flow_table **tbl_out){
    uint8_t criteria_enable = 0x1; /* Criteria enabled  */
    struct dr_flow_table *tbl;
    doca_error_t result;

    tbl = (struct dr_flow_table *)calloc(1, sizeof(*tbl));
    if (tbl == NULL) {
		// DOCA_LOG_ERR("Failed to allocate memory for dr table");
		return DOCA_ERROR_NO_MEMORY;
	}

    tbl->dr_table = mlx5dv_dr_table_create(domain, level);
	if (tbl->dr_table == NULL) {
		// DOCA_LOG_ERR("Failed to create table [%d]", errno);
		result = DOCA_ERROR_DRIVER;
		goto exit_with_error;
	}

	tbl->dr_matcher = mlx5dv_dr_matcher_create(tbl->dr_table, priority, criteria_enable, match_mask);
	if (tbl->dr_matcher == NULL) {
		// DOCA_LOG_ERR("Failed to create matcher [%d]", errno);
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
	return result;
}