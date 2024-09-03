/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef IPSEC_CTX_H_
#define IPSEC_CTX_H_

#include <doca_dev.h>
#include <doca_ipsec.h>
#include <doca_flow.h>

#include <dpdk_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SOCKET_PATH_NAME (108)			/* Maximum socket file name length */
#define MAX_FILE_NAME (255)				/* Maximum file name length */
#define MAX_NB_RULES (1024)				/* Maximal number of rules */
#define MAX_KEY_LEN (32)				/* Maximal GCM key size is 256bit==32B */
#define ENCRYPT_DUMMY_ID ((MAX_NB_RULES * 2) + 1)	/* Dummy resource ID for encrypt pipe creation */
#define DECRYPT_DUMMY_ID ((MAX_NB_RULES * 2) + 2)	/* Dummy resource ID for decrypt pipe creation */
#define NUM_OF_SYNDROMES (4)				/* Number of bad syndromes */


/* SA attrs struct */
struct ipsec_security_gw_sa_attrs {
	enum doca_ipsec_icv_length icv_length;		/* ICV length */
	enum doca_encryption_key_type key_type;		/* Key type */
	uint8_t enc_key_data[MAX_KEY_LEN];		/* Policy encryption key */
	uint32_t salt;					/* Key Salt */
	enum doca_ipsec_direction direction;		/* Rule direction */
};

/* will hold an entry of a bad syndrome and its last counter */
struct bad_syndrome_entry {
	struct doca_flow_pipe_entry *entry;	/* DOCA Flow entry */
	uint32_t previous_stats;		/* last query stats */
};

/* decryption rule struct */
struct decrypt_rule {
	enum doca_flow_l3_type l3_type;		/* IP type */
	union {
		doca_be32_t dst_ip4;		/* destination IPv4 */
		doca_be32_t dst_ip6[4];		/* destination IPv6 */
	};
	doca_be32_t esp_spi;			/* ipsec session parameter index */
	enum doca_flow_l3_type inner_l3_type;	/* inner IP type */

	struct doca_ipsec_sa *sa;
	struct ipsec_security_gw_sa_attrs sa_attrs;
	struct bad_syndrome_entry entries[NUM_OF_SYNDROMES];
};

/* IPv4 addresses struct */
struct ipsec_security_gw_ip4 {
	doca_be32_t src_ip;		/* source IPv4 */
	doca_be32_t dst_ip;		/* destination IPv4 */
};

/* IPv6 addresses struct */
struct ipsec_security_gw_ip6 {
	doca_be32_t src_ip[4];		/* source IPv6 */
	doca_be32_t dst_ip[4];		/* destination IPv6 */
};

/* encryption rule struct */
struct encrypt_rule {
	enum doca_flow_l3_type l3_type;			/* l3 type */
	enum doca_flow_l4_type_ext protocol;		/* protocol */
	union {
		struct ipsec_security_gw_ip4 ip4;	/* IPv4 addresses */
		struct ipsec_security_gw_ip6 ip6;	/* IPv6 addresses */
	};
	int src_port;					/* source port */
	int dst_port;					/* destination port */
	enum doca_flow_l3_type encap_l3_type;		/* encap l3 type */
	union {
		doca_be32_t encap_dst_ip4;		/* encap destination IPv4 */
		doca_be32_t encap_dst_ip6[4];		/* encap destination IPv6 */
	};
	doca_be32_t esp_spi;				/* ipsec session parameter index */
	uint32_t current_sn;				/* current sequence number */

	struct doca_ipsec_sa *sa;
	struct ipsec_security_gw_sa_attrs sa_attrs;
};

/* all the pipes that is used for encrypt packets */
struct encrypt_pipes {
	struct doca_flow_pipe *egress_ip_classifier;	/* egress IP classifier */
	struct doca_flow_pipe *ipv4_encrypt_pipe;	/* encryption action pipe for ipv4 traffic */
	struct doca_flow_pipe *ipv6_encrypt_pipe;	/* encryption action pipe for ipv6 traffic */
	struct doca_flow_pipe *ipv4_tcp_pipe;		/* 5-tuple ipv4 tcp match pipe */
	struct doca_flow_pipe *ipv4_udp_pipe;		/* 5-tuple ipv4 udp match pipe */
	struct doca_flow_pipe *ipv6_tcp_pipe;		/* 5-tuple ipv6 tcp match pipe */
	struct doca_flow_pipe *ipv6_udp_pipe;		/* 5-tuple ipv6 udp match pipe */
	struct doca_flow_pipe *ipv6_src_tcp_pipe;	/* src ipv6 tcp match pipe */
	struct doca_flow_pipe *ipv6_src_udp_pipe;	/* src ipv6 udp match pipe */
};

/* all the pipes that is used for decrypt packets */
struct decrypt_pipes {
	struct doca_flow_pipe *decrypt_ipv4_pipe;	/* decrypt ipv4 pipe */
	struct doca_flow_pipe *decrypt_ipv6_pipe;	/* decrypt ipv6 pipe */
	struct doca_flow_pipe *syndrome_ipv4_pipe;	/* match on ipsec syndrome pipe for ipv4 packets */
	struct doca_flow_pipe *syndrome_ipv6_pipe;	/* match on ipsec syndrome pipe for ipv6 packets */
	struct doca_flow_pipe *bad_syndrome_ipv4_pipe;	/* match on ipsec bad syndrome for ipv4 packets */
	struct doca_flow_pipe *bad_syndrome_ipv6_pipe;	/* match on ipsec bad syndrome for ipv6 packets */
};

/* Application rules arrays {encryption, decryption}*/
struct ipsec_security_gw_rules {
	struct encrypt_rule *encrypt_rules;			/* Encryption rules array */
	struct decrypt_rule *decrypt_rules;			/* Decryption rules array */
	int nb_encrypted_rules;					/* Encryption rules array size */
	int nb_decrypted_rules;					/* Decryption rules array size */
	int nb_rules;						/* Total number of rules, will be used to indicate
								 * which crypto index is the next one.
								 */
	struct doca_ipsec_sa *dummy_encrypt_sa;			/* Encryption dummy SA */
	struct doca_ipsec_sa *dummy_decrypt_sa;			/* Encryption dummy SA */
};

/* IPsec Security Gateway modes */
enum ipsec_security_gw_mode {
	IPSEC_SECURITY_GW_TUNNEL,		/* ipsec tunnel mode */
	IPSEC_SECURITY_GW_TRANSPORT,		/* ipsec transport mode */
	IPSEC_SECURITY_GW_UDP_TRANSPORT,		/* ipsec transport mode over UDP */
};

/* IPsec Security Gateway flow modes */
enum ipsec_security_gw_flow_mode {
	IPSEC_SECURITY_GW_VNF,		/* DOCA Flow vnf mode */
	IPSEC_SECURITY_GW_SWITCH,	/* DOCA Flow switch mode */
};

/* IPsec Security Gateway ESP offload */
enum ipsec_security_gw_esp_offload {
	IPSEC_SECURITY_GW_ESP_OFFLOAD_BOTH,	/* HW offload for both encap and decap */
	IPSEC_SECURITY_GW_ESP_OFFLOAD_ENCAP,	/* HW offload for encap, decap in SW */
	IPSEC_SECURITY_GW_ESP_OFFLOAD_DECAP,	/* HW offload for decap, encap in SW */
	IPSEC_SECURITY_GW_ESP_OFFLOAD_NONE,	/* encap and decap both done in SW */
};

/* IPsec Security Gateway device information */
struct ipsec_security_gw_dev_info {
	char pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	/* PCI address */
	char iface_name[DOCA_DEVINFO_IFACE_NAME_SIZE];	/* interface name */
	bool open_by_pci;				/* true if user sent PCI address */
	bool open_by_name;				/* true if user sent interface name */
	struct doca_dev *doca_dev;			/* DOCA device */
	bool has_device;				/* true if the user sent PCI address or interface name */
};

/* IPsec Security Gateway DOCA objects */
struct ipsec_security_gw_doca_objects {
	struct ipsec_security_gw_dev_info secured_dev;	/* DOCA device for secured network */
	struct ipsec_security_gw_dev_info unsecured_dev;/* DOCA device for unsecured network */
	struct doca_pe *doca_pe;			/* DOCA IPSEC pe */
	struct doca_ipsec *full_offload_ctx;		/* DOCA IPSEC full offload context */
	struct doca_ipsec *crypto_offload_ctx;		/* DOCA IPSEC crypto offload context */
	struct doca_ctx *doca_ctx;			/* DOCA IPSEC as context */
};

/* IPsec Security Gateway DOCA socket context */
struct ipsec_security_gw_socket_ctx {
	int fd;						/* Socket file descriptor */
	int connfd;					/* Connection file descriptor */
	char socket_path[MAX_SOCKET_PATH_NAME];		/* Socket file path */
	bool socket_conf;				/* If IPC mode is enabled */
};

/* IPsec Security Gateway configuration structure */
struct ipsec_security_gw_config {
	bool sw_sn_inc_enable;				/* true for doing sn increment in software */
	bool sw_antireplay;				/* true for doing anti-replay in software */
	enum ipsec_security_gw_mode mode;		/* application mode */
	enum ipsec_security_gw_flow_mode flow_mode;	/* DOCA Flow mode */
	enum ipsec_security_gw_esp_offload offload;	/* ESP offload */
	uint64_t sn_initial;				/* set the initial sequence number */
	char json_path[MAX_FILE_NAME];			/* Path to the JSON file with rules */
	struct rte_hash *ip6_table;			/* IPV6 addresses hash table */
	struct application_dpdk_config *dpdk_config;	/* DPDK configuration struct */
	struct decrypt_pipes decrypt_pipes;		/* Decryption DOCA flow pipes */
	struct encrypt_pipes encrypt_pipes;		/* Encryption DOCA flow pipes */
	struct ipsec_security_gw_rules app_rules;	/* Application encryption/decryption rules */
	struct ipsec_security_gw_doca_objects objects;	/* Application DOCA objects */
	struct ipsec_security_gw_socket_ctx socket_ctx;	/* Application DOCA socket context */
	uint8_t nb_cores;				/* number of cores to DPDK -l flag */
};

/*
 * Open DOCA devices according to the pci-address input and probe dpdk ports
 *
 * @app_cfg [in/out]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_init_devices(struct ipsec_security_gw_config *app_cfg);

/*
 * Start ipsec contexts.
 * this function should be called only after all ctx were created using
 * ipsec_security_gw_ipsec_ctx_create for better performace.
 *
 * @app_cfg [in]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_start_ipsec_ctxs(struct ipsec_security_gw_config *app_cfg);

/*
 * Create and start all the resources for IPSec task
 *
 * @app_cfg [in]: application configuration structure
 * @offload [in]: offload type if current ctx.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_ipsec_ctx_create(struct ipsec_security_gw_config *app_cfg, enum doca_ipsec_sa_offload offload);

/*
 * Destroy all the resources of the ipsec context
 *
 * @app_cfg [in]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_ipsec_destroy(const struct ipsec_security_gw_config *app_cfg);
/*
 * Send create SA task to DOCA ipsec library
 *
 * @sa_attrs [in]: SA attributes structure
 * @cfg [in]: application configuration structure
 * @sa [out]: created crypto sa object
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_create_ipsec_sa(struct ipsec_security_gw_sa_attrs *sa_attrs, struct ipsec_security_gw_config *cfg,
					struct doca_ipsec_sa **sa);


/*
 * Send destroy SA task to DOCA ipsec library
 *
 * @app_cfg [in]: application configuration structure
 * @sa [in]: sa object to destroy
 * @is_full_offload [in]: whether current SA uses full offload or crypto one - in order to understand which bulk this SA was allocate from.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_destroy_ipsec_sa(struct ipsec_security_gw_config *app_cfg, struct doca_ipsec_sa *sa, bool is_full_offload);
/*
 * Get dpdk port ID and check if its encryption port or decryption, based on
 * user PCI input and DOCA device devinfo
 *
 * @app_cfg [in]: application configuration structure
 * @port_id [in]: port ID
 * @idx [out]: index for ports array - 0 for secured network index and 1 for unsecured
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
find_port_action_type_vnf(const struct ipsec_security_gw_config *app_cfg, int port_id, int *idx);

/*
 * Get dpdk port ID and check if its encryption port or decryption, by checking if the port is representor
 * representor port is the unsecured port
 *
 * @port_id [in]: port ID
 * @idx [out]: index for ports array - 0 for secured network index and 1 for unsecured
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
find_port_action_type_switch(int port_id, int *idx);

/*
 * Destroy the created SAs for the received policies
 *
 * @app_cfg [in]: application configuration structure
 */
void ipsec_security_gw_destroy_sas(struct ipsec_security_gw_config *app_cfg);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IPSEC_CTX_H_ */
