#ifndef ENTT_ENTITY_RUNTIME_VIEW_HPP
#define ENTT_ENTITY_RUNTIME_VIEW_HPP


#include <cassert>
#include "EASTL/iterator.h"
#include "EASTL/vector.h"
#include "EASTL/utility.h"
#include "EASTL/algorithm.h"
#include "EASTL/type_traits.h"
#include "../config/config.h"
#include "sparse_set.hpp"
#include "fwd.hpp"


namespace entt {


/**
 * @brief Runtime view.
 *
 * Runtime views iterate over those entities that have at least all the given
 * components in their bags. During initialization, a runtime view looks at the
 * number of entities available for each component and picks up a reference to
 * the smallest set of candidate entities in order to get a performance boost
 * when iterate.<br/>
 * Order of elements during iterations are highly dependent on the order of the
 * underlying data structures. See sparse_set and its specializations for more
 * details.
 *
 * @b Important
 *
 * Iterators aren't invalidated if:
 *
 * * New instances of the given components are created and assigned to entities.
 * * The entity currently pointed is modified (as an example, if one of the
 *   given components is removed from the entity to which the iterator points).
 * * The entity currently pointed is destroyed.
 *
 * In all the other cases, modifying the pools of the given components in any
 * way invalidates all the iterators and using them results in undefined
 * behavior.
 *
 * @note
 * Views share references to the underlying data structures of the registry that
 * generated them. Therefore any change to the entities and to the components
 * made by means of the registry are immediately reflected by the views, unless
 * a pool was missing when the view was built (in this case, the view won't
 * have a valid reference and won't be updated accordingly).
 *
 * @warning
 * Lifetime of a view must not overcome that of the registry that generated it.
 * In any other case, attempting to use a view results in undefined behavior.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 */
template<typename Entity, typename Allocator = EASTLAllocatorType>
class basic_runtime_view {
    /*! @brief A registry is allowed to create views. */
    friend class basic_registry<Entity,Allocator>;

    using underlying_iterator = typename sparse_set<Entity, Allocator>::iterator;

    class view_iterator final {
        friend class basic_runtime_view<Entity>;

        using direct_type = eastl::vector<const sparse_set<Entity,Allocator> *,Allocator>;

        view_iterator(const direct_type &all, underlying_iterator curr) ENTT_NOEXCEPT
            : pools{&all},
              it{curr}
        {
            if(it != (*pools)[0]->end() && !valid()) {
                ++(*this);
            }
        }

        bool valid() const {
            return eastl::all_of(pools->begin()++, pools->end(), [entt = *it](const auto *curr) {
                return curr->contains(entt);
            });
        }

    public:
        using difference_type = typename underlying_iterator::difference_type;
        using value_type = typename underlying_iterator::value_type;
        using pointer = typename underlying_iterator::pointer;
        using reference = typename underlying_iterator::reference;
        using iterator_category = eastl::bidirectional_iterator_tag;

        view_iterator() ENTT_NOEXCEPT = default;

        view_iterator & operator++() {
            while(++it != (*pools)[0]->end() && !valid());
            return *this;
        }

        view_iterator operator++(int) {
            view_iterator orig = *this;
            return operator++(), orig;
        }

        view_iterator & operator--() ENTT_NOEXCEPT {
            while(--it != (*pools)[0]->begin() && !valid());
            return *this;
        }

        view_iterator operator--(int) ENTT_NOEXCEPT {
            view_iterator orig = *this;
            return operator--(), orig;
        }

        bool operator==(const view_iterator &other) const ENTT_NOEXCEPT {
            return other.it == it;
        }

        bool operator!=(const view_iterator &other) const ENTT_NOEXCEPT {
            return !(*this == other);
        }

        pointer operator->() const {
            return it.operator->();
        }

        reference operator*() const {
            return *operator->();
        }

    private:
        const direct_type *pools;
        underlying_iterator it;
    };

    basic_runtime_view(eastl::vector<const sparse_set<Entity,Allocator> *,Allocator> others) ENTT_NOEXCEPT
        : pools{eastl::move(others)}
    {
        const auto it = eastl::min_element(pools.begin(), pools.end(), [](const auto *lhs, const auto *rhs) {
            return (!lhs && rhs) || (lhs && rhs && lhs->size() < rhs->size());
        });

        // brings the best candidate (if any) on front of the vector
        eastl::rotate(pools.begin(), it, pools.end());
    }

    bool valid() const {
        return !pools.empty() && pools.front();
    }

public:
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief Unsigned integer type. */
    using size_type = std::size_t;
    /*! @brief Input iterator type. */
    using iterator = view_iterator;

    /**
     * @brief Estimates the number of entities that have the given components.
     * @return Estimated number of entities that have the given components.
     */
    size_type size() const {
        return valid() ? pools.front()->size() : size_type{};
    }

    /**
     * @brief Checks if the view is definitely empty.
     * @return True if the view is definitely empty, false otherwise.
     */
    bool empty() const {
        return !valid() || pools.front()->empty();
    }

    /**
     * @brief Returns an iterator to the first entity that has the given
     * components.
     *
     * The returned iterator points to the first entity that has the given
     * components. If the view is empty, the returned iterator will be equal to
     * `end()`.
     *
     * @note
     * Input iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the first entity that has the given components.
     */
    iterator begin() const {
        iterator it{};

        if(valid()) {
            it = { pools, pools[0]->begin() };
        }

        return it;
    }

    /**
     * @brief Returns an iterator that is past the last entity that has the
     * given components.
     *
     * The returned iterator points to the entity following the last entity that
     * has the given components. Attempting to dereference the returned iterator
     * results in undefined behavior.
     *
     * @note
     * Input iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the entity following the last entity that has the
     * given components.
     */
    iterator end() const {
        iterator it{};

        if(valid()) {
            it = { pools, pools[0]->end() };
        }

        return it;
    }

    /**
     * @brief Checks if a view contains an entity.
     * @param entt A valid entity identifier.
     * @return True if the view contains the given entity, false otherwise.
     */
    bool contains(const entity_type entt) const {
        return valid() && eastl::all_of(pools.cbegin(), pools.cend(), [entt](const auto *view) {
            return view->find(entt) != view->end();
        });
    }

    /**
     * @brief Iterates entities and applies the given function object to them.
     *
     * The function object is invoked for each entity. It is provided only with
     * the entity itself. To get the components, users can use the registry with
     * which the view was built.<br/>
     * The signature of the function should be equivalent to the following:
     *
     * @code{.cpp}
     * void(const entity_type);
     * @endcode
     *
     * @tparam Func Type of the function object to invoke.
     * @param func A valid function object.
     */
    template<typename Func>
    void each(Func func) const {
        for(const auto entity: *this) {
            func(entity);
        }
    }

private:
    eastl::vector<const sparse_set<Entity,Allocator> *,Allocator> pools;
};


}


#endif
