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

#ifndef SERVER_H_
#define SERVER_H_

#include <grpcpp/grpcpp.h>

#include "url_filter.grpc.pb.h"

class UrlFilterImpl : public URLFilter::Service {
	public:
		/*
		* Adds the given gRPC stream as a listener to log messages, this function does not return
		* until the server shuts down.
		*
		* @context [in]: Ignored. gRPC forced argument with info on the server
		* @request [in]: For future use, currently ignored
		* @writer [in]: gRPC stream for passing log records to the subscribed client
		* @return: gRPC:Status:OK on success and DOCA_ERROR error status otherwise
		*/
		grpc::Status Subscribe(grpc::ServerContext *context, const SubscribeReq *request,
			grpc::ServerWriter<LogRecord> *writer) override;

		/*
		* Create new databases.
		*
		* @context [in]: Ignored. gRPC forced argument with info on the server
		* @request [in]: Create request
		* @response [in]: For future use, currently ignored
		* @return: gRPC:Status:OK on success and DOCA_ERROR error status otherwise
		*/
		grpc::Status Create(grpc::ServerContext* context, const CreateReq *request,
			CreateResp *response) override;

		/*
		* Add new filter to the database.
		*
		* @context [in]: Ignored. gRPC forced argument with info on the server
		* @request [in]: Add request
		* @response [in]: For future use, currently ignored
		* @return: gRPC:Status:OK on success and DOCA_ERROR error status otherwise
		*/
		grpc::Status Add(grpc::ServerContext *context, const FilterRule *request,
			AddResp *response) override;

		/*
		* Commit current database to file
		*
		* @context [in]: Ignored. gRPC forced argument with info on the server
		* @request [in]: Commit request
		* @response [in]: For future use, currently ignored
		* @return: gRPC:Status:OK on success and DOCA_ERROR error status otherwise
		*/
		grpc::Status Commit(grpc::ServerContext *context, const CommitReq *request,
			CommitResp *response) override;

		/*
		* Quit the application
		*
		* @context [in]: Ignored. gRPC forced argument with info on the server
		* @request [in]: Quit request
		* @response [in]: For future use, currently ignored
		* @return: gRPC:Status:OK on success and DOCA_ERROR error status otherwise
		*/
		grpc::Status Quit(grpc::ServerContext *context, const QuitReq *request,
			QuitResp *response) override;
};

#endif /* SERVER_H_ */
