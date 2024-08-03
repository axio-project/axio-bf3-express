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

#ifndef DOCA_EXCEPTION_HPP_
#define DOCA_EXCEPTION_HPP_

#include <stdexcept>
#include <string>
#include <type_traits>

#include <doca_error.h>

namespace doca {

/**
 * An exception type for use by doca applications. It provides data that is useful both to end users to understand what
 * went wrong and some details for developers to help them understand what the code was doing when the exception was
 * generated.
 */
class exception : public std::runtime_error {
public:
	/**
	 * Description of a source code location
	 */
	struct source_loc {
		const char* file = nullptr;	/**< file name */
		const char* function = nullptr; /**< function name */
		uint32_t line = 0;		/**< line number */
	};

	/** Destroy an exception object */
	~exception() = default;

	/** Default constructor is disabled. Use one of the following constructors instead. */
	exception() = delete;

	/**
	 * Create an exception object with a doca_error_t and a source location.
	 *
	 * The message will be automatically generated as the result of doca_get_error_string for the given
	 * doca_error_t. Use this when the error code is deemed enough information for the user.
	 *
	 * @param [in] error: Error code describing the type of error encountered.
	 * @param [in] loc: Description of where in the source code this error should guide the user / developer to, to
	 * understand the error. Typically the line of code that generated the error, but sometimes it might be better
	 * to catch, modify and re-throw an exception from a particularly generic function to the code that called the
	 * generic function to give the user a better hint.
	 */
	exception(doca_error_t error, source_loc loc)
		: runtime_error{doca_get_error_string(error)}, m_error{error}, m_loc{loc} {}

	/**
	 * Create an exception object with a doca_error_t, message and source location.
	 *
	 * Sometimes you want to give a more specific / helpful error to the user, for example DOCA_ERROR_NOT_FOUND is
	 * great in that it tells us something was not found, but the message could tell them which resource was not
	 * found. So in cases like that provide a message to help people understand the error more clearly.
	 *
	 * @param [in] error: Error code describing the type of error encountered.
	 * @param [in] message: Pointer to a null terminated message to present to the user. Value is copied internally
	 * so it is not required to be kept in scope.
	 * @param [in] loc: Description of where in the source code this error should guide the user / developer to, to
	 * understand the error. Typically the line of code that generated the error, but sometimes it might be better
	 * to catch, modify and re-throw an exception from a particularly generic function to the code that called the
	 * generic function to give the user a better hint.
	 */
	exception(doca_error_t error, const char* message, source_loc loc)
		: runtime_error{message}, m_error{error}, m_loc{loc} {}

	/**
	 * Create an exception object with a doca_error_t, message and source location.
	 *
	 * Sometimes you want to give a more specific / helpful error to the user, for example DOCA_ERROR_NOT_FOUND is
	 * great in that it tells us something was not found, but the message could tell them which resource was not
	 * found. So in cases like that provide a message to help people understand the error more clearly.
	 *
	 * @param [in] error: Error code describing the type of error encountered.
	 * @param [in] message: Message to present to the user.
	 * @param [in] loc: Description of where in the source code this error should guide the user / developer to, to
	 * understand the error. Typically the line of code that generated the error, but sometimes it might be better
	 * to catch, modify and re-throw an exception from a particularly generic function to the code that called the
	 * generic function to give the user a better hint.
	 */
	exception(doca_error_t error, const std::string& message, source_loc loc)
		: exception(error, message.c_str(), loc) {}

	/**
	 * Create a new exception object with a copy of the data in another exception object.
	 *
	 * @param [in] other: Object to copy values from.
	 */
	exception(const exception& other) noexcept = default;

	/**
	 * Create a new exception object by transferring the data from another object into this one.
	 *
	 * @param [in] other: Object to move values from.
	 */
	exception(exception&& other) noexcept(std::is_nothrow_move_constructible<std::runtime_error>::value) = default;

	/**
	 * Make an exception look like a copy of another exception object.
	 *
	 * @param [in] other: Object to copy values from.
	 */
	exception& operator=(const exception& other) noexcept = default;

	/**
	 * Transfer data from one exception to another.
	 *
	 * @param [in] other: Object to move values from.
	 */
	exception&
	operator=(exception&& other) noexcept(std::is_nothrow_move_constructible<std::runtime_error>::value) = default;

	/**
	 * Get the error code.
	 *
	 * @return
	 * Error code.
	 */
	doca_error_t error_code() const noexcept { return m_error; }

	/**
	 * Get the error message.
	 *
	 * @return
	 * Error message.
	 */
	const char* message() const noexcept { return what(); }

	/**
	 * Get the error location.
	 *
	 * @return
	 * Error location.
	 */
	const source_loc& location() const noexcept { return m_loc; }

private:
	doca_error_t m_error;
	source_loc m_loc;
};

/**
 * Macro to populate a source location description using the C file, function and line macros.
 *
 * C++20 users should prefer to use std::source_location
 */
#define CURRENT_SOURCE_LOCATION() \
	doca::exception::source_loc { __FILE__, __FUNCTION__, __LINE__ }

} // namespace doca

#endif /* DOCA_EXCEPTION_HPP_ */
