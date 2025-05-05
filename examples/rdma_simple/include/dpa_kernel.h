#ifndef DPA_USER_H_
#define DPA_USER_H_

#include <stddef.h>

// #include <libflexio-libc/stdio.h>
#include <libflexio-libc/string.h>
#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_err.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <dpaintrin.h>

#include "dpa_queue.h"

/*
 * Get next data buffer entry
 *
 * @dt_ctx [in]: Data transfer context
 * @dt_idx_mask [in]: Data transfer segment index mask
 * @log_dt_entry_sz [in]: Log of data transfer entry size
 * @return: Data buffer entry
 */
static void *
get_next_dte(struct dt_ctx_t *dt_ctx, uint32_t dt_idx_mask, uint32_t log_dt_entry_sz)
{
	uint32_t mask = ((dt_ctx->tx_buff_idx++ & dt_idx_mask) << log_dt_entry_sz);
	char *buff_p =  (char *) dt_ctx->sq_tx_buff;

	return buff_p + mask;
}

/*
 * Get next SQE from the SQ ring
 *
 * @sq_ctx [in]: SQ context
 * @sq_idx_mask [in]: SQ index mask
 * @return: pointer to next SQE
 */
static void *
get_next_sqe(struct sq_ctx_t *sq_ctx, uint32_t sq_idx_mask)
{
	return &sq_ctx->sq_ring[sq_ctx->sq_wqe_seg_idx++ & sq_idx_mask];
}

#endif
