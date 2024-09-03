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

#ifndef DOCA_UNIQUE_PTR_HPP_
#define DOCA_UNIQUE_PTR_HPP_

#include <memory>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_buf_pool.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_mmap.h>

/*
 * Notes to developers:
 *
 * What is this, why would I use it?
 *
 *     doca-unique_ptr adds a partial specialization of std::unique_ptr that uses class template to delegate to an
 *     overloaded deleter function (doca_deleter). This makes it easy to manage the lifetime of a C object in a RAII
 *     fashion so that is will be safely deleted in any C++ context, this frees you from the burden of writing duplicate
 *     free calls, the doca::unique_ptr will assure that the object cannot be leaked by going out of scope even in the
 *     case of an exception being thrown, or a class failing to fully construct.
 *
 *     See:
 *         https://isocpp.org/wiki/faq/exceptions#selfcleaning-members
 *
 * How do I teach doca::unique_ptr to support a new type?
 *
 *     Add an overload of doca_deleter to teach unique_ptr how to destroy your type. The overload does not need to exist
 *     in this file, just just need to make sure the compiler sees the overload before you try to use a
 *     doca::unique_ptr. The deleter must exist in the same namespace as the type to be destroyed as per ADL rules, so
 *     for C types this will always be the global namespace.
 *
 *     For example you could do something like this in any other file:
 *
 *     void doca_deleter(my_thing *thing) {
 *         destroy_my_thing(thing);
 *     }
 *
 *     See:
 *         https://en.cppreference.com/w/cpp/language/overload_resolution
 *         https://en.cppreference.com/w/cpp/language/adl
 */

/**
 * Deleter for doca_buf.
 *
 * @param [in] buf:
 * Object to destroy.
 */
static inline void
doca_deleter(doca_buf* buf) noexcept {
	doca_buf_dec_refcount(buf, nullptr);
}

/**
 * Deleter for doca_buf_inventory.
 *
 * @param [in] inventory:
 * Object to destroy.
 */
static inline void
doca_deleter(doca_buf_inventory* inventory) noexcept {
	doca_buf_inventory_destroy(inventory);
}

/**
 * Deleter for doca_buf_pool.
 *
 * @param [in] pool:
 * Object to destroy.
 */
static inline void
doca_deleter(doca_buf_pool* pool) noexcept {
	doca_buf_pool_destroy(pool);
}

/**
 * Deleter for doca_dev.
 *
 * @param [in] dev:
 * Object to destroy.
 */
static inline void
doca_deleter(doca_dev* dev) noexcept {
	doca_dev_close(dev);
}

/**
 * Deleter for doca_devinfo list.
 *
 * @param [in] list:
 * List to destroy.
 */
static inline void
doca_deleter(doca_devinfo** list) noexcept {
	doca_devinfo_destroy_list(list);
}

/**
 * Deleter for doca_dev_rep.
 *
 * @param [in] devinfo:
 * Object to destroy.
 */
static inline void
doca_deleter(doca_dev_rep* rep) {
	if (rep != nullptr) {
		doca_dev_rep_close(rep);
	}
}

/**
 * Deleter for doca_devinfo_rep list.
 *
 * @param [in] list:
 * List to destroy.
 */
static inline void
doca_deleter(doca_devinfo_rep** list) {
	doca_devinfo_rep_destroy_list(list);
}

/**
 * Deleter for doca_mmap.
 *
 * @param [in] mmap:
 * Object to destroy.
 */
static inline void
doca_deleter(doca_mmap* mmap) noexcept {
	doca_mmap_destroy(mmap);
}

namespace doca {

/**
 * Generic deleter class template for C types. Remember to register an overload of doca_deleter
 */
template <typename DocaCType>
struct delegated_deleter {
	/**
	 * Delete callback functor
	 *
	 * @parma [in] obj:
	 * The object to delete.
	 */
	void operator()(DocaCType* obj) { doca_deleter(obj); }
};

/**
 * Partial specialization of unique_ptr, avoids you having to repeat the deleter type in every unique_ptr variable /
 * argument. Writing
 *     doca::unique_ptr<doca_buf> my_buf;
 * Is much nicer than writing
 *     std::unique_ptr<doca_buf, void(*)(doca_buf*)> my_buff;
 *
 * Then to populate the variable its even uglier without the class template, consider writing:
 *     std::unique_ptr<doca_buf, void(*)(doca_buf*)> my_buff{
 *             ptr_to_doca_buf, [](doca_buf* buf){doca_buf_dec_refcount(buf, nullptr); }};
 * Instead of the nicer:
 *     doca::unique_ptr<doca_buf> my_buf{ptr_to_doca_buf};
 */
template <typename DocaCType>
using unique_ptr = std::unique_ptr<DocaCType, delegated_deleter<DocaCType>>;

} // namespace doca

#endif /* DOCA_UNIQUE_PTR_HPP_ */
