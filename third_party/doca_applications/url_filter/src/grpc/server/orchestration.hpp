/*
 * Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef ORCHESTRATION_H_
#define ORCHESTRATION_H_

#include <grpcpp/grpcpp.h>

#include "common.grpc.pb.h"

class DocaOrchestrationImpl : public DocaOrchestration::Service {
	public:
		/*
		* Checks the server is healthy (active and responsive).
		*
		* @context [in]: Ignored. gRPC forced argument with info on the server
		* @request [in]: For future use, currently ignored
		* @response [in]: For future use, currently ignored
		* @return: gRPC:Status:OK if server is healthy and error status otherwise
		*/
		grpc::Status HealthCheck(grpc::ServerContext *context, const HealthCheckReq *request,
			HealthCheckResp *response) override;

		/*
		* Destroys the server.
		*
		* @context [in]: Ignored. gRPC forced argument with info on the server
		* @request [in]: For future use, currently ignored
		* @response [in]: For future use, currently ignored
		* @return: gRPC:Status:OK if server is starting to teardown and error status otherwise
		*/
		grpc::Status Destroy(grpc::ServerContext *context, const DestroyReq *request,
			DestroyResp *response) override;
};

#endif /* ORCHESTRATION_H_ */
