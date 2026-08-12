#ifndef TYPEID_H
#define TYPEID_H

#include <string_view>
#include <type_traits>

#include <ink/ink_base.hpp>

/**
 * @file TypeId.h
 * @brief Type identity and human-readable type names without RTTI.
 *
 * Release builds compile with `-fno-rtti`, so `typeid` and `std::type_index`
 * are unavailable — the usual way a dependency-injection container keys its
 * services. These two utilities replace them at zero runtime cost.
 */
namespace warp {

namespace detail {

/**
 * One object per distinct T, whose *address* is the identity.
 *
 * `static constexpr` data members are implicitly `inline` since C++17, so
 * every translation unit that instantiates TypeTag<T> refers to the same
 * object and therefore agrees on the address — which is exactly the property
 * a type key needs, and the reason this cannot be a plain local static.
 */
template <typename T>
struct TypeTag { static constexpr char value = 0; };

} // namespace detail

/** Stable, process-wide unique key for T. Cheaper than RTTI: it is a constant. */
template <typename T>
constexpr const void* typeKey() noexcept
{
    return &detail::TypeTag<std::remove_cvref_t<T>>::value;
}

/**
 * @brief Compile-time demangled name of T, for diagnostics.
 *
 * Parsed out of the compiler's own signature macro, which embeds the deduced
 * template argument. Without this a missing-dependency error could only say
 * "some service is missing" — with it, it names the type.
 */
template <typename T>
constexpr std::string_view typeName() noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    constexpr std::string_view sig = __PRETTY_FUNCTION__;
    constexpr std::string_view key = "T = ";

    const auto keyPos = sig.find(key);
    if (keyPos == std::string_view::npos)
        return "<unknown>";

    const auto from = keyPos + key.size();

    // GCC renders "[with T = Foo; ...]", Clang "[T = Foo]" - accept either.
    auto to = sig.find(';', from);
    if (to == std::string_view::npos)
        to = sig.find(']', from);
    if (to == std::string_view::npos)
        return "<unknown>";

    return sig.substr(from, to - from);
#else
    return "<unknown>";
#endif
}

} // namespace warp

#endif // TYPEID_H
