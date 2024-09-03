#
# Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
#
# This software product is a proprietary product of NVIDIA CORPORATION &
# AFFILIATES (the "Company") and all right, title, and interest in and to the
# software product, including all associated intellectual property rights, are
# and shall remain exclusively with the Company.
#
# This software product is governed by the End User License Agreement
# provided with the software product.
#

sample_dependencies = [libflexio_host]
sample_dependencies += dependency('libibverbs')
sample_dependencies += dependency('libmlx5')
sample_dependencies += dependency('doca')

srcs = [
        SAMPLE_NAME + '_sample.c',
	'../' + SAMPLE_NAME + '_main.c'
]

executable(SAMPLE_NAME,
	srcs,
	link_with: dpa_dependency,
	dependencies: sample_dependencies,
)
