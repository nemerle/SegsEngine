#ifndef ENTT_SIGNAL_DELEGATE_HPP
#define ENTT_SIGNAL_DELEGATE_HPP



#include "EASTL/tuple.h"
#include "EASTL/utility.h"
#include "EASTL/algorithm.h"
#include "EASTL/functional.h"
#include "EASTL/type_traits.h"

#include <cstring>
#include "../config/config.h"


namespace entt {


/**
 * @cond TURN_OFF_DOXYGEN
 * Internal details not to be documented.
 */


namespace internal {


template<typename Ret, typename... Args>
auto function_pointer(Ret(*)(Args...)) -> Ret(*)(Args...);


template<typename Ret, typename Type, typename... Args, typename Other>
auto function_pointer(Ret(*)(Type, Args...), Other &&) -> Ret(*)(Args...);


template<typename Class, typename Ret, typename... Args, typename... Other>
auto function_pointer(Ret(Class:: *)(Args...), Other &&...) -> Ret(*)(Args...);


template<typename Class, typename Ret, typename... Args, typename... Other>
auto function_pointer(Ret(Class:: *)(Args...) const, Other &&...) -> Ret(*)(Args...);


template<typename Class, typename Type, typename... Other>
auto function_pointer(Type Class:: *, Other &&...) -> Type(*)();


template<typename... Type>
using function_pointer_t = decltype(internal::function_pointer(eastl::declval<Type>()...));


template<typename... Class, typename Ret, typename... Args>
constexpr auto index_sequence_for(Ret(*)(Args...)) {
    return eastl::index_sequence_for<Class..., Args...>{};
}


}


/**
 * Internal details not to be documented.
 * @endcond TURN_OFF_DOXYGEN
 */


/*! @brief Used to wrap a function or a member of a specified type. */
template<auto>
struct connect_arg_t {};


/*! @brief Constant of type connect_arg_t used to disambiguate calls. */
template<auto Func>
inline constexpr connect_arg_t<Func> connect_arg{};


/**
 * @brief Basic delegate implementation.
 *
 * Primary template isn't defined on purpose. All the specializations give a
 * compile-time error unless the template parameter is a function type.
 */
template<typename>
class delegate;


/**
 * @brief Utility class to use to send around functions and members.
 *
 * Unmanaged delegate for function pointers and members. Users of this class are
 * in charge of disconnecting instances before deleting them.
 *
 * A delegate can be used as a general purpose invoker without memory overhead
 * for free functions possibly with payloads and bound or unbound members.
 *
 * @tparam Ret Return type of a function type.
 * @tparam Args Types of arguments of a function type.
 */
template<typename Ret, typename... Args>
class delegate<Ret(Args...)> {
    using proto_fn_type = Ret(const void *, Args...);

    template<auto Candidate, std::size_t... Index>
    auto wrap(eastl::index_sequence<Index...>) ENTT_NOEXCEPT {
        return [](const void *, Args... args) -> Ret {
            const auto arguments = eastl::forward_as_tuple(eastl::forward<Args>(args)...);
            return Ret(eastl::invoke(Candidate, eastl::forward<eastl::tuple_element_t<Index, eastl::tuple<Args...>>>(eastl::get<Index>(arguments))...));
        };
    }

    template<auto Candidate, typename Type, std::size_t... Index>
    auto wrap(Type &, eastl::index_sequence<Index...>) ENTT_NOEXCEPT {
        return [](const void *payload, Args... args) -> Ret {
            const auto arguments = eastl::forward_as_tuple(eastl::forward<Args>(args)...);
            Type *curr = static_cast<Type *>(const_cast<eastl::conditional_t<eastl::is_const_v<Type>, const void *, void *>>(payload));
            return Ret(eastl::invoke(Candidate, *curr, eastl::forward<eastl::tuple_element_t<Index, eastl::tuple<Args...>>>(eastl::get<Index>(arguments))...));
        };
    }

    template<auto Candidate, typename Type, std::size_t... Index>
    auto wrap(Type *, eastl::index_sequence<Index...>) ENTT_NOEXCEPT {
        return [](const void *payload, Args... args) -> Ret {
            const auto arguments = eastl::forward_as_tuple(eastl::forward<Args>(args)...);
            Type *curr = static_cast<Type *>(const_cast<eastl::conditional_t<eastl::is_const_v<Type>, const void *, void *>>(payload));
            return Ret(eastl::invoke(Candidate, curr, eastl::forward<eastl::tuple_element_t<Index, eastl::tuple<Args...>>>(eastl::get<Index>(arguments))...));
        };
    }

public:
    /*! @brief Function type of the delegate. */
    using function_type = Ret(Args...);

    /*! @brief Default constructor. */
    delegate() ENTT_NOEXCEPT
        : fn{nullptr}, data{nullptr}
    {}

    /**
     * @brief Constructs a delegate and connects a free function or an unbound
     * member.
     * @tparam Candidate Function or member to connect to the delegate.
     */
    template<auto Candidate>
    delegate(connect_arg_t<Candidate>) ENTT_NOEXCEPT
        : delegate{}
    {
        connect<Candidate>();
    }

    /**
     * @brief Constructs a delegate and connects a free function with payload or
     * a bound member.
     * @tparam Candidate Function or member to connect to the delegate.
     * @tparam Type Type of class or type of payload.
     * @param value_or_instance A valid object that fits the purpose.
     */
    template<auto Candidate, typename Type>
    delegate(connect_arg_t<Candidate>, Type &&value_or_instance) ENTT_NOEXCEPT
        : delegate{}
    {
        connect<Candidate>(eastl::forward<Type>(value_or_instance));
    }

    /**
     * @brief Connects a free function or an unbound member to a delegate.
     * @tparam Candidate Function or member to connect to the delegate.
     */
    template<auto Candidate>
    void connect() ENTT_NOEXCEPT {
        data = nullptr;

        if constexpr(eastl::is_invocable_r_v<Ret, decltype(Candidate), Args...>) {
            fn = [](const void *, Args... args) -> Ret {
                return Ret(eastl::invoke(Candidate, eastl::forward<Args>(args)...));
            };
        } else if constexpr(eastl::is_member_pointer_v<decltype(Candidate)>) {
            fn = wrap<Candidate>(internal::index_sequence_for<eastl::tuple_element_t<0, eastl::tuple<Args...>>>(internal::function_pointer_t<decltype(Candidate)>{}));
        } else {
            fn = wrap<Candidate>(internal::index_sequence_for(internal::function_pointer_t<decltype(Candidate)>{}));
        }
    }

    /**
     * @brief Connects a free function with payload or a bound member to a
     * delegate.
     *
     * The delegate isn't responsible for the connected object or the payload.
     * Users must always guarantee that the lifetime of the instance overcomes
     * the one  of the delegate.<br/>
     * When used to connect a free function with payload, its signature must be
     * such that the instance is the first argument before the ones used to
     * define the delegate itself.
     *
     * @tparam Candidate Function or member to connect to the delegate.
     * @tparam Type Type of class or type of payload.
     * @param value_or_instance A valid reference that fits the purpose.
     */
    template<auto Candidate, typename Type>
    void connect(Type &value_or_instance) ENTT_NOEXCEPT {
        data = &value_or_instance;

        if constexpr(eastl::is_invocable_r_v<Ret, decltype(Candidate), Type &, Args...>) {
            fn = [](const void *payload, Args... args) -> Ret {
                Type *curr = static_cast<Type *>(const_cast<eastl::conditional_t<eastl::is_const_v<Type>, const void *, void *>>(payload));
                return Ret(eastl::invoke(Candidate, *curr, eastl::forward<Args>(args)...));
            };
        } else {
            fn = wrap<Candidate>(value_or_instance, internal::index_sequence_for(internal::function_pointer_t<decltype(Candidate), Type>{}));
        }
    }

    /**
     * @brief Connects a free function with payload or a bound member to a
     * delegate.
     *
     * @sa connect(Type &)
     *
     * @tparam Candidate Function or member to connect to the delegate.
     * @tparam Type Type of class or type of payload.
     * @param value_or_instance A valid pointer that fits the purpose.
     */
    template<auto Candidate, typename Type>
    void connect(Type *value_or_instance) ENTT_NOEXCEPT {
        data = value_or_instance;

        if constexpr(eastl::is_invocable_r_v<Ret, decltype(Candidate), Type *, Args...>) {
            fn = [](const void *payload, Args... args) -> Ret {
                Type *curr = static_cast<Type *>(const_cast<eastl::conditional_t<eastl::is_const_v<Type>, const void *, void *>>(payload));
                return Ret(eastl::invoke(Candidate, curr, eastl::forward<Args>(args)...));
            };
        } else {
            fn = wrap<Candidate>(value_or_instance, internal::index_sequence_for(internal::function_pointer_t<decltype(Candidate), Type>{}));
        }
    }

    /**
     * @brief Resets a delegate.
     *
     * After a reset, a delegate cannot be invoked anymore.
     */
    void reset() ENTT_NOEXCEPT {
        fn = nullptr;
        data = nullptr;
    }

    /**
     * @brief Returns the instance or the payload linked to a delegate, if any.
     * @return An opaque pointer to the underlying data.
     */
    const void * instance() const ENTT_NOEXCEPT {
        return data;
    }

    /**
     * @brief Triggers a delegate.
     *
     * The delegate invokes the underlying function and returns the result.
     *
     * @warning
     * Attempting to trigger an invalid delegate results in undefined
     * behavior.<br/>
     * An assertion will abort the execution at runtime in debug mode if the
     * delegate has not yet been set.
     *
     * @param args Arguments to use to invoke the underlying function.
     * @return The value returned by the underlying function.
     */
    Ret operator()(Args... args) const {
        ENTT_ASSERT(fn);
        return fn(data, eastl::forward<Args>(args)...);
    }

    /**
     * @brief Checks whether a delegate actually stores a listener.
     * @return False if the delegate is empty, true otherwise.
     */
    explicit operator bool() const ENTT_NOEXCEPT {
        // no need to test also data
        return !(fn == nullptr);
    }

    /**
     * @brief Compares the contents of two delegates.
     * @param other Delegate with which to compare.
     * @return False if the two contents differ, true otherwise.
     */
    bool operator==(const delegate<Ret(Args...)> &other) const ENTT_NOEXCEPT {
        return fn == other.fn && data == other.data;
    }

private:
    proto_fn_type *fn;
    const void *data;
};


/**
 * @brief Compares the contents of two delegates.
 * @tparam Ret Return type of a function type.
 * @tparam Args Types of arguments of a function type.
 * @param lhs A valid delegate object.
 * @param rhs A valid delegate object.
 * @return True if the two contents differ, false otherwise.
 */
template<typename Ret, typename... Args>
bool operator!=(const delegate<Ret(Args...)> &lhs, const delegate<Ret(Args...)> &rhs) ENTT_NOEXCEPT {
    return !(lhs == rhs);
}


/**
 * @brief Deduction guide.
 * @tparam Candidate Function or member to connect to the delegate.
 */
template<auto Candidate>
delegate(connect_arg_t<Candidate>) ENTT_NOEXCEPT
-> delegate<eastl::remove_pointer_t<internal::function_pointer_t<decltype(Candidate)>>>;


/**
 * @brief Deduction guide.
 * @tparam Candidate Function or member to connect to the delegate.
 * @tparam Type Type of class or type of payload.
 */
template<auto Candidate, typename Type>
delegate(connect_arg_t<Candidate>, Type &&) ENTT_NOEXCEPT
-> delegate<eastl::remove_pointer_t<internal::function_pointer_t<decltype(Candidate), Type>>>;


}


#endif
