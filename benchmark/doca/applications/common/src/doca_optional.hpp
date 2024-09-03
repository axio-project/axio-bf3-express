/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef DOCA_OPTIONAL_HPP_
#define DOCA_OPTIONAL_HPP_

#if __cplusplus >= 201703L
#include <optional>
#else // __cplusplus < 201703L (C++98:C++14)
#include <memory>
#include <stdexcept>
#endif // __cplusplus >= 201703L

namespace doca {

#if __cplusplus >= 201703L

// When C++17 or greater is present, doca::xxx are just aliases for their std::xxx counterparts.

using bad_optional_access = std::bad_optional_access;
using nullopt_t = std::nullopt_t;
static constexpr nullopt_t nullopt = std::nullopt;

template <typename T>
using optional = std::optional<T>;

#else // __cplusplus < 201703L (C++98:C++14)

// When C++17 is not available we must implement a basic version of std::optional for ourselves.

/**
 * The exception type that will be thrown when attempting to access the value of an optional when no value is held. You
 * can avoid this by verifying the optional has a value before accessing it like you would do with a pointer.
 */
class bad_optional_access : public std::exception {};

/**
 * Tag type used to indicate an empty optional (uninitialized state).
 */
struct nullopt_t {};

/**
 * Value that can be used to construct or assign to an optional to
 */
static constexpr nullopt_t nullopt;

/**
 * Class template that manages an optional contained value. This is useful to disambiguate between for example an empty
 * value vs a value not being specified. For example if some class has a get_name method which return an empty string,
 * is the name empty, or not known, an optional tells us if a value is present then we can distinguish between these
 * cases.
 *
 * An optional does not require any dynamic memory allocations. It is sized to hold an object of its held type as part
 * of its own size.
 *
 * @tparam Element Type of the value to manage. Type must meet the requirements of Destructible:
 * https://en.cppreference.com/w/cpp/types/is_destructible. Array and reference types are not permitted.
 */
template <typename Element>
class optional final {
public:
	using value_type = Element;

	/** Destroy the optional and any held object (if any) */
	~optional() { destruct(); }

	/** Default constuction of an optional creates an optional which holds no value */
	optional() = default;

	/** Create an optional from a nullopt to create an empty optional */
	optional(const nullopt_t&): optional() {}

	/**
	 * Create an optional from a object of its held value type.
	 *
	 * Requires Element to support copy construction.
	 *
	 * @param [in] value: Value to copy as the held object of the optional.
	 */
	optional(const Element& value): m_ptr{nullptr} { assign(value); }

	/**
	 * Create an optional from a object of its held value type.
	 *
	 * Requires Element to support move construction.
	 *
	 * @param [in] value: Value to move into the held object of the optional.
	 */
	optional(Element&& value): m_ptr{nullptr} { assign(std::move(value)); }

	/**
	 * Copy constuct an optional. If the other optional holds a value the new optional will hold a value which is a
	 * copy of the other optionals held value.
	 *
	 * @param [in] other: Optional to copy from.
	 */
	optional(const optional& other): m_ptr{nullptr} {
		if (m_ptr == other.m_ptr)
			return;

		if (other.has_value())
			assign(*other);
	}

	/**
	 * Move constuct an optional. If the other optional holds a value the new optional will move assign that value
	 * from other to itself.
	 *
	 * @param [in] other: Optional to move from.
	 */
	optional(optional&& other) noexcept: m_ptr{nullptr} {
		if (m_ptr == other.m_ptr)
			return;

		if (other.has_value())
			assign(std::move(*other));
	}

	/**
	 * Copy assign a new value to an optional. If the optional already holds a value, the existing value will be
	 * destroyed first.
	 *
	 * @param [in] value Value to copy.
	 */
	optional& operator=(const Element& value) {
		assign(value);
		return *this;
	}

	/**
	 * Move assign a new value to an optional. If the optional already holds a value, the existing value will be
	 * destroyed first.
	 *
	 * @param [in] value Value to move.
	 */
	optional& operator=(Element&& value) {
		assign(std::move(value));
		return *this;
	}

	/**
	 * Copy assign an optional from another. If this optional held a value before the assignment it will be
	 * destroyed. If other is an empty optional this optional will become empty.
	 *
	 * @param [in] other Optional to copy new state from.
	 *
	 * @return A reference to this object.
	 */
	optional& operator=(const optional& other) {
		if (m_ptr == other.m_ptr)
			return *this;

		if (other.has_value())
			assign(*other);
		else
			destruct();

		return *this;
	}

	/**
	 * Move assign an optional from another. If this optional held a value before the assignment it will be
	 * destroyed. If other is an empty optional this optional will become empty. If optional held a value it's value
	 * will be transferred to this optional.
	 *
	 * @param [in] other Optional to copy new state from.
	 *
	 * @return A reference to this object.
	 */
	optional& operator=(optional&& other) noexcept {
		if (m_ptr == other.m_ptr)
			return *this;

		if (other.has_value())
			assign(std::move(*other));
		else
			destruct();

		return *this;
	}

	/**
	 * Assign an optional from a nullopt. IF this optional held a value the value will be destroyed.
	 *
	 * @param [in] ignored Ignored value, only used for tag dispatch.
	 *
	 * @return A reference to this object.
	 */
	optional& operator=(const nullopt_t& ignored) noexcept {
		destruct();
		return *this;
	}

	/**
	 * Set the state of the optional to that of a newly created (default constructed) value. Any existing object is
	 * destroyed.
	 *
	 * Requires that Element is default constructable.
	 *
	 * @return reference to the newly held value.
	 */
	Element& emplace() {
		assign(Element{});
		return *m_ptr;
	}

	/**
	 * Set the state of the optional to that of a newly created value by forwarding all the arguments to emplace to
	 * the elements constructor. Any existing object is destroyed.
	 *
	 * Requires that Element is constructable with the pack of parameters passed to emplace.
	 *
	 * @param [in] args Argument pack which is perfect-forwarded to the underlying Element constructor.
	 *
	 * @return reference to the newly held value.
	 */
	template <typename... Args>
	Element& emplace(Args&&... args) {
		assign(Element(std::forward<Args>(args)...));
		return operator*();
	}

	/**
	 * Checks if the optional currently holds a value.
	 *
	 * @return true when a value is held, false otherwise.
	 */
	bool has_value() const noexcept { return m_ptr != nullptr; }

	/**
	 * Implicit conversion to bool used to check if the optional currently holds a value.
	 *
	 * @return true when a value is held, false otherwise.
	 */
	explicit operator bool() const noexcept { return m_ptr != nullptr; }

	/**
	 * Reset the optional to the empty state. Any held object is destroyed. Calling reset for an empty optional has
	 * no effect.
	 */
	void reset() noexcept { destruct(); }

	/**
	 * Unchecked access to the the contained object.
	 *
	 * Ensuring the optional has a value before accessing it can be achieved using has_value() or value() members.
	 *
	 * @warning The behavior is undefined if this optional does not hold a value.
	 *
	 * @returns pointer to the underlying element.
	 */
	Element* operator->() noexcept { return m_ptr; }

	/**
	 * Unchecked access to the the contained object.
	 *
	 * Ensuring the optional has a value before accessing it can be achieved using has_value() or value() members.
	 *
	 * @warning The behavior is undefined if this optional does not hold a value.
	 *
	 * @returns const pointer to the underlying element.
	 */
	const Element* operator->() const noexcept { return m_ptr; }

	/**
	 * Unchecked access to the the contained object.
	 *
	 * Ensuring the optional has a value before accessing it can be achieved using has_value() or value() members.
	 *
	 * @warning The behavior is undefined if this optional does not hold a value.
	 *
	 * @returns reference to the contained object.
	 */
	Element& operator*() noexcept { return *m_ptr; }

	/**
	 * Unchecked access to the the contained object.
	 *
	 * Ensuring the optional has a value before accessing it can be achieved using has_value() or value() members.
	 *
	 * @warning The behavior is undefined if this optional does not hold a value.
	 *
	 * @returns const reference to the contained object.
	 */
	const Element& operator*() const noexcept { return *m_ptr; }

	/**
	 * Checked access to the value held by the optional.
	 *
	 * Throws bad_optional_access is no value is held
	 *
	 * @return reference to the contained object.
	 */
	Element& value() {
		if (!m_ptr)
			throw bad_optional_access();

		return *m_ptr;
	}

	/**
	 * Checked access to the value held by the optional.
	 *
	 * Throws bad_optional_access is no value is held
	 *
	 * @return const reference to the contained object.
	 */
	const Element& value() const {
		if (!m_ptr)
			throw bad_optional_access();

		return *m_ptr;
	}

private:
	Element* m_ptr{nullptr};
	std::aligned_storage_t<sizeof(Element)> m_data;

	inline void assign(const Element& value) {
		if (m_ptr)
			destruct();

		construct(value);
	}

	inline void assign(Element&& value) {
		if (m_ptr)
			destruct();

		construct(std::move(value));
	}

	inline void construct(const Element& value) { m_ptr = new (&m_data) Element(value); }

	inline void construct(Element&& value) { m_ptr = new (&m_data) Element(std::move(value)); }

	inline void destruct() {
		if (m_ptr) {
			m_ptr->~Element();
			m_ptr = nullptr;
		}
	}
};

#endif // __cplusplus >= 201703L

} // namespace doca

#endif // DOCA_OPTIONAL_HPP_
