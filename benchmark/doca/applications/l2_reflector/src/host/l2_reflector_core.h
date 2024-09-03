/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#ifndef L2_REFLECTOR_CORE_H_
#define L2_REFLECTOR_CORE_H_

#include <doca_error.h>
#include <infiniband/mlx5dv.h>
#include <libflexio/flexio.h>

#include <doca_dev.h>

#include <../common/l2_reflector_common.h>


/* Source mac address to match packets in */
#define SRC_MAC (0x001122334455)

struct mlx5_ifc_dr_match_spec_bits {
	uint8_t smac_47_16[0x20];

	uint8_t smac_15_0[0x10];
	uint8_t ethertype[0x10];

	uint8_t dmac_47_16[0x20];

	uint8_t dmac_15_0[0x10];
	uint8_t first_prio[0x3];
	uint8_t first_cfi[0x1];
	uint8_t first_vid[0xc];

	uint8_t ip_protocol[0x8];
	uint8_t ip_dscp[0x6];
	uint8_t ip_ecn[0x2];
	uint8_t cvlan_tag[0x1];
	uint8_t svlan_tag[0x1];
	uint8_t frag[0x1];
	uint8_t ip_version[0x4];
	uint8_t tcp_flags[0x9];

	uint8_t tcp_sport[0x10];
	uint8_t tcp_dport[0x10];

	uint8_t reserved_at_c0[0x18];
	uint8_t ip_ttl_hoplimit[0x8];

	uint8_t udp_sport[0x10];
	uint8_t udp_dport[0x10];

	uint8_t src_ip_127_96[0x20];

	uint8_t src_ip_95_64[0x20];

	uint8_t src_ip_63_32[0x20];

	uint8_t src_ip_31_0[0x20];

	uint8_t dst_ip_127_96[0x20];

	uint8_t dst_ip_95_64[0x20];

	uint8_t dst_ip_63_32[0x20];

	uint8_t dst_ip_31_0[0x20];
};

struct dr_flow_table {
	struct mlx5dv_dr_table		*dr_table;		/* DR table in the domain at specific level */
	struct mlx5dv_dr_matcher	*dr_matcher;		/* DR matcher object in the table. One matcher per table */
};

struct dr_flow_rule {
	struct mlx5dv_dr_action		*dr_action;		/* Rule action */
	struct mlx5dv_dr_rule		*dr_rule;		/* Steering rule */
};

/* L2 Reflector configuration structure */
struct l2_reflector_config {
	char 			device_name[DOCA_DEVINFO_IBDEV_NAME_SIZE];	/* IB device name */
	struct l2_reflector_data	*dev_data;		/* device data */

	/* IB Verbs resources */
	struct ibv_context		*ibv_ctx;		/* IB device context */
	struct ibv_pd			*pd;			/* Protection domain */

	/* FlexIO resources */
	flexio_uintptr_t		dev_data_daddr;		/* Data address accessible by the device */
	struct flexio_process		*flexio_process;	/* FlexIO process */
	struct flexio_uar		*flexio_uar;		/* FlexIO UAR */
	struct flexio_event_handler	*event_handler;		/* Event handler on device */

	struct app_transfer_cq		rq_cq_transf;
	struct app_transfer_cq		sq_cq_transf;

	struct flexio_mkey		*rqd_mkey;
	struct app_transfer_wq		rq_transf;

	struct flexio_mkey		*sqd_mkey;
	struct app_transfer_wq		sq_transf;

	struct flexio_cq		*flexio_rq_cq_ptr;	/* FlexIO RQ CQ */
	struct flexio_cq		*flexio_sq_cq_ptr;	/* FlexIO SQ CQ */
	struct flexio_rq		*flexio_rq_ptr;		/* FlexIO RQ */
	struct flexio_sq		*flexio_sq_ptr;		/* FlexIO SQ */

	/* mlx5dv direct rules resources, used for steering rules */
	struct mlx5dv_dr_domain		*rx_domain;
	struct mlx5dv_dr_domain		*fdb_domain;

	struct dr_flow_table		*rx_flow_table;
	struct dr_flow_table		*tx_flow_table;
	struct dr_flow_table		*tx_flow_root_table;

	struct dr_flow_rule		*rx_rule;
	struct dr_flow_rule		*tx_rule;
	struct dr_flow_rule		*tx_root_rule;

};

/*
 * Open IB device context and allocate PD
 *
 * @app_cfg [in]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t l2_reflector_setup_ibv_device(struct l2_reflector_config *app_cfg);

/*
 * Allocate FlexIO process and device resources
 *
 * @app_cfg [in]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t l2_reflector_setup_device(struct l2_reflector_config *app_cfg);

/*
 * Allocate device memory and WQs
 *
 * @app_cfg [in]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t l2_reflector_allocate_device_resources(struct l2_reflector_config *app_cfg);

/*
 * Create steering rule that sends all packets to the device
 *
 * @app_cfg [in]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t l2_reflector_create_steering_rule_rx(struct l2_reflector_config *app_cfg);

/*
 * Create steering rule that sends all packets back to wire
 *
 * @app_cfg [in]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t l2_reflector_create_steering_rule_tx(struct l2_reflector_config *app_cfg);

/*
 * Destroy WQs and free device memory
 *
 * @app_cfg [in]: application configuration structure
 */
void l2_reflector_device_resources_destroy(struct l2_reflector_config *app_cfg);

/*
 * Destroy steering rule resources
 *
 * @app_cfg [in]: application configuration structure
 */
void l2_reflector_steering_rules_destroy(struct l2_reflector_config *app_cfg);

/*
 * Destroy FlexIO process and device resources
 *
 * @app_cfg [in]: application configuration structure
 */
void l2_reflector_device_destroy(struct l2_reflector_config *app_cfg);

/*
 * Destroy IB device context
 *
 * @app_cfg [in]: application configuration structure
 */
void l2_reflector_ibv_device_destroy(struct l2_reflector_config *app_cfg);

/*
 * L2 Reflector destroy
 *
 * @app_cfg [in]: application configuration structure
 */
void l2_reflector_destroy(struct l2_reflector_config *app_cfg);

/*
 * Register the command line parameters for the IPS application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_l2_reflector_params(void);

#endif
