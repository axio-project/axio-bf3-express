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

#ifndef PCC_CORE_H_
#define PCC_CORE_H_

#include <doca_pcc.h>
#include <doca_dev.h>
#include <doca_error.h>

#define WAIT_TIME_DEFAULT_VALUE			(-1)				/* Wait time - default value (infinity) */
#define PCC_THREADS_NUM_DEFAULT_VALUE		(48 + 1)			/* Default Number of PCC threads, the extra one is used for communication */
#define PCC_PRINT_BUFFER_SIZE_DEFAULT_VALUE	(512 * 2048)			/* Device print buffer size - default value */
#define PCC_COREDUMP_FILE_DEFAULT_PATH		("/tmp/pcc_coredump.txt")	/* Default pathanme for device coredump file */
#define MAX_USER_ARG_SIZE			(256)				/* Maximum size of user input argument */
#define MAX_ARG_SIZE				(MAX_USER_ARG_SIZE + 1)		/* Maximum size of input argument */

#define LOG_LEVEL_CRIT				(20)				/* Critical log level */
#define LOG_LEVEL_ERROR				(30)				/* Error log level */
#define LOG_LEVEL_WARNING			(40)				/* Warning log level */
#define LOG_LEVEL_INFO				(50)				/* Info log level */
#define LOG_LEVEL_DEBUG				(60)				/* Debug log level */

/* Log level */
extern int log_level;

#define PRINT_CRIT(...) do { \
	if (log_level >= LOG_LEVEL_CRIT) \
		printf(__VA_ARGS__); \
} while(0)

#define PRINT_ERROR(...) do { \
	if (log_level >= LOG_LEVEL_ERROR) \
		printf(__VA_ARGS__); \
} while(0)

#define PRINT_WARNING(...) do { \
	if (log_level >= LOG_LEVEL_WARNING) \
		printf(__VA_ARGS__); \
} while(0)

#define PRINT_INFO(...) do { \
	if (log_level >= LOG_LEVEL_INFO) \
		printf(__VA_ARGS__); \
} while(0)

#define PRINT_DEBUG(...) do { \
	if (log_level >= LOG_LEVEL_DEBUG) \
		printf(__VA_ARGS__); \
} while(0)

/*
 * A struct that includes all needed info on device program and is initialized during linkage by DPACC.
 * Variable name should be the token passed to DPACC with --app-name parameter.
 */
extern struct doca_pcc_app *pcc_main_app;

struct pcc_config {
	char device_name[DOCA_DEVINFO_IBDEV_NAME_SIZE];  /* DOCA device name */
	int wait_time;                                   /* Wait duration */
	uint32_t pcc_threads_num;                        /* Number of PCC threads */
	uint32_t pcc_threads_list[MAX_ARG_SIZE];         /* PCC threads numbers */
	char pcc_coredump_file[MAX_ARG_SIZE];		 /* PCC coredump file pathname */
};

struct pcc_resources {
	struct doca_dev *doca_device;                    /* DOCA device */
	struct doca_pcc *doca_pcc;                       /* DOCA PCC context */
};

/*
 * Initialize the PCC application resources
 *
 * @cfg [in]: PCC application user configurations
 * @resources [in/out]: PCC resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t pcc_init(struct pcc_config *cfg, struct pcc_resources *resources);

/*
 * Destroy the PCC application resources
 *
 * @resources [in]: PCC resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t pcc_destroy(struct pcc_resources *resources);

/*
 * Register the command line parameters for the PCC application.
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_pcc_params(void);

#endif /* PCC_CORE_H_ */
