#ifndef ENTT_ENTITY_SNAPSHOT_HPP
#define ENTT_ENTITY_SNAPSHOT_HPP


#include <cstddef>

#include "EASTL/array.h"
#include "EASTL/utility.h"
#include "EASTL/iterator.h"
#include "EASTL/type_traits.h"
#include "EASTL/unordered_map.h"

#include "../config/config.h"
#include "entity.hpp"
#include "fwd.hpp"


namespace entt {


/**
 * @brief Utility class to create snapshots from a registry.
 *
 * A _snapshot_ can be either a dump of the entire registry or a narrower
 * selection of components of interest.<br/>
 * This type can be used in both cases if provided with a correctly configured
 * output archive.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 */
template<typename Entity, typename Allocator = EASTLAllocatorType>
class basic_snapshot {
    /*! @brief A registry is allowed to create snapshots. */
    friend class basic_registry<Entity, Allocator>;

    using traits_type = entt_traits<eastl::underlying_type_t<Entity>>;


    template<typename Component, typename Archive, typename It>
    void get(Archive &archive, std::size_t sz, It first, It last) const {
        archive(typename traits_type::entity_type(sz));

        while(first != last) {
            const auto entt = *(first++);

            if(reg->template has<Component>(entt)) {
                if constexpr(eastl::is_empty_v<Component>) {
                    archive(entt);
                } else {
                    archive(entt, reg->template get<Component>(entt));
                }
            }
        }
    }

    template<typename... Component, typename Archive, typename It, std::size_t... Indexes>
    void component(Archive &archive, It first, It last, eastl::index_sequence<Indexes...>) const {
        eastl::array<std::size_t, sizeof...(Indexes)> size{};
        auto begin = first;

        while(begin != last) {
            const auto entt = *(begin++);
            ((reg->template has<Component>(entt) ? ++size[Indexes] : size[Indexes]), ...);
        }

        (get<Component>(archive, size[Indexes], first, last), ...);
    }

public:
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;

    /**
     * @brief Constructs an instance that is bound to a given registry.
     * @param source A valid reference to a registry.
     */
    basic_snapshot(const basic_registry<entity_type,Allocator> &source) ENTT_NOEXCEPT
        : reg{&source}
    {}
    /*! @brief Default move constructor. */
    basic_snapshot(basic_snapshot &&) = default;

    /*! @brief Default move assignment operator. @return This snapshot. */
    basic_snapshot & operator=(basic_snapshot &&) = default;

    /**
     * @brief Puts aside all the entities from the underlying registry.
     *
     * Entities are serialized along with their versions. Destroyed entities are
     * taken in consideration as well by this function.
     *
     * @tparam Archive Type of output archive.
     * @param archive A valid reference to an output archive.
     * @return An object of this type to continue creating the snapshot.
     */
    template<typename Archive>
    const basic_snapshot & entities(Archive &archive) const {
        const auto sz = reg->size();
        auto first = reg->data();
        const auto last = first + sz;

        archive(typename traits_type::entity_type(sz));

        while(first != last) {
            archive(*(first++));
        }
        return *this;
    }

    /**
     * @brief Deprecated function. Currently, it does nothing.
     * @tparam Archive Type of output archive.
     * @return An object of this type to continue creating the snapshot.
     */
    template<typename Archive>
    [[deprecated("use ::entities instead, it exports now also destroyed entities")]]
    const basic_snapshot & destroyed(Archive &) const { return *this; }

    /**
     * @brief Puts aside the given components.
     *
     * Each instance is serialized together with the entity to which it belongs.
     * Entities are serialized along with their versions.
     *
     * @tparam Component Types of components to serialize.
     * @tparam Archive Type of output archive.
     * @param archive A valid reference to an output archive.
     * @return An object of this type to continue creating the snapshot.
     */
    template<typename... Component, typename Archive>
    const basic_snapshot & component(Archive &archive) const {
        (component<Component>(archive, reg->template data<Component>(), reg->template data<Component>() + reg->template size<Component>()), ...);
        return *this;
    }

    /**
     * @brief Puts aside the given components for the entities in a range.
     *
     * Each instance is serialized together with the entity to which it belongs.
     * Entities are serialized along with their versions.
     *
     * @tparam Component Types of components to serialize.
     * @tparam Archive Type of output archive.
     * @tparam It Type of input iterator.
     * @param archive A valid reference to an output archive.
     * @param first An iterator to the first element of the range to serialize.
     * @param last An iterator past the last element of the range to serialize.
     * @return An object of this type to continue creating the snapshot.
     */
    template<typename... Component, typename Archive, typename It>
    const basic_snapshot & component(Archive &archive, It first, It last) const {
        component<Component...>(archive, first, last, eastl::index_sequence_for<Component...>{});
        return *this;
    }

private:
    const basic_registry<entity_type,Allocator> *reg;
};


/**
 * @brief Utility class to restore a snapshot as a whole.
 *
 * A snapshot loader requires that the destination registry be empty and loads
 * all the data at once while keeping intact the identifiers that the entities
 * originally had.<br/>
 * An example of use is the implementation of a save/restore utility.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 */
template<typename Entity, typename Allocator = EASTLAllocatorType>
class basic_snapshot_loader {
    /*! @brief A registry is allowed to create snapshot loaders. */
    friend class basic_registry<Entity,Allocator>;

    using traits_type = entt_traits<eastl::underlying_type_t<Entity>>;


    template<typename Type, typename Archive, typename... Args>
    void assign(Archive &archive, Args... args) const {
        typename traits_type::entity_type length{};
        archive(length);

        while(length--) {
            entity_type entt{};

            if constexpr(eastl::is_empty_v<Type>) {
                archive(entt);
                const auto entity = reg->valid(entt) ? entt : reg->create(entt);
                ENTT_ASSERT(entity == entt);
                reg->template emplace<Type>(args..., entt);
            } else {
                Type instance{};
                archive(entt, instance);
                const auto entity = reg->valid(entt) ? entt : reg->create(entt);
                ENTT_ASSERT(entity == entt);
                reg->template emplace<Type>(args..., entt, eastl::as_const(instance));
            }
        }
    }

public:
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;

    /**
     * @brief Constructs an instance that is bound to a given registry.
     * @param source A valid reference to a registry.
     */
    basic_snapshot_loader(basic_registry<entity_type,Allocator> &source) ENTT_NOEXCEPT
        : reg{&source}
    {
        // restoring a snapshot as a whole requires a clean registry
        ENTT_ASSERT(reg->empty());
    }
    /*! @brief Default move constructor. */
    basic_snapshot_loader(basic_snapshot_loader &&) = default;

    /*! @brief Default move assignment operator. @return This loader. */
    basic_snapshot_loader & operator=(basic_snapshot_loader &&) = default;

    /**
     * @brief Restores entities that were in use during serialization.
     *
     * This function restores the entities that were in use during serialization
     * and gives them the versions they originally had.
     *
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A valid loader to continue restoring data.
     */
    template<typename Archive>
    const basic_snapshot_loader & entities(Archive &archive) const {
        typename traits_type::entity_type length{};

        archive(length);
        eastl::vector<entity_type,Allocator> all(length);

        for(decltype(length) pos{}; pos < length; ++pos) {
            archive(all[pos]);
        }

        reg->assign(all.cbegin(), all.cend());
        return *this;
    }

    /**
     * @brief Deprecated function. Currently, it does nothing.
     * @tparam Archive Type of input archive.
     * @return A valid loader to continue restoring data.
     */
    template<typename Archive>
    [[deprecated("use ::entities instead, it imports now also destroyed entities")]]
    const basic_snapshot_loader & destroyed(Archive &) const { return *this; }

    /**
     * @brief Restores components and assigns them to the right entities.
     *
     * The template parameter list must be exactly the same used during
     * serialization. In the event that the entity to which the component is
     * assigned doesn't exist yet, the loader will take care to create it with
     * the version it originally had.
     *
     * @tparam Component Types of components to restore.
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A valid loader to continue restoring data.
     */
    template<typename... Component, typename Archive>
    const basic_snapshot_loader & component(Archive &archive) const {
        (assign<Component>(archive), ...);
        return *this;
    }

    /**
     * @brief Destroys those entities that have no components.
     *
     * In case all the entities were serialized but only part of the components
     * was saved, it could happen that some of the entities have no components
     * once restored.<br/>
     * This functions helps to identify and destroy those entities.
     *
     * @return A valid loader to continue restoring data.
     */
    const basic_snapshot_loader & orphans() const {
        reg->orphans([this](const auto entt) {
            reg->destroy(entt);
        });

        return *this;
    }

private:
    basic_registry<entity_type,Allocator> *reg;
};


/**
 * @brief Utility class for _continuous loading_.
 *
 * A _continuous loader_ is designed to load data from a source registry to a
 * (possibly) non-empty destination. The loader can accommodate in a registry
 * more than one snapshot in a sort of _continuous loading_ that updates the
 * destination one step at a time.<br/>
 * Identifiers that entities originally had are not transferred to the target.
 * Instead, the loader maps remote identifiers to local ones while restoring a
 * snapshot.<br/>
 * An example of use is the implementation of a client-server applications with
 * the requirement of transferring somehow parts of the representation side to
 * side.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 */
template<typename Entity, typename Allocator = EASTLAllocatorType>
class basic_continuous_loader {
    using traits_type = entt_traits<eastl::underlying_type_t<Entity>>;

    void destroy(Entity entt) {
        const auto it = remloc.find(entt);

        if(it == remloc.cend()) {
            const auto local = reg->create();
            remloc.emplace(entt, eastl::make_pair(local, true));
            reg->destroy(local);
        }
    }

    void restore(Entity entt) {
        const auto it = remloc.find(entt);

        if(it == remloc.cend()) {
            const auto local = reg->create();
            remloc.emplace(entt, eastl::make_pair(local, true));
        } else {
            remloc[entt].first = reg->valid(remloc[entt].first) ? remloc[entt].first : reg->create();
            // set the dirty flag
            remloc[entt].second = true;
        }
    }

    template<typename Container>
    auto update(int, Container &container)
    -> decltype(typename Container::mapped_type{}, void()) {
        // map like container
        Container other;

        for(auto &&pair: container) {
            using first_type = eastl::remove_const_t<typename eastl::decay_t<decltype(pair)>::first_type>;
            using second_type = typename eastl::decay_t<decltype(pair)>::second_type;

            if constexpr(eastl::is_same_v<first_type, entity_type> && eastl::is_same_v<second_type, entity_type>) {
                other.emplace(map(pair.first), map(pair.second));
            } else if constexpr(eastl::is_same_v<first_type, entity_type>) {
                other.emplace(map(pair.first), eastl::move(pair.second));
            } else {
                static_assert(eastl::is_same_v<second_type, entity_type>);
                other.emplace(eastl::move(pair.first), map(pair.second));
            }
        }

        eastl::swap(container, other);
    }

    template<typename Container>
    auto update(char, Container &container)
    -> decltype(typename Container::value_type{}, void()) {
        // vector like container
        static_assert(eastl::is_same_v<typename Container::value_type, entity_type>);

        for(auto &&entt: container) {
            entt = map(entt);
        }
    }

    template<typename Other, typename Type, typename Member>
    void update([[maybe_unused]] Other &instance, [[maybe_unused]] Member Type:: *member) {
        if constexpr(!eastl::is_same_v<Other, Type>) {
            return;
        } else if constexpr(eastl::is_same_v<Member, entity_type>) {
            instance.*member = map(instance.*member);
        } else {
            // maybe a container? let's try...
            update(0, instance.*member);
        }
    }

    template<typename Component>
    void remove_if_exists() {
        for(auto &&ref: remloc) {
            const auto local = ref.second.first;

            if(reg->valid(local)) {
                reg->template remove_if_exists<Component>(local);
            }
        }
    }

    template<typename Other, typename Archive, typename... Type, typename... Member>
    void assign(Archive &archive, [[maybe_unused]] Member Type:: *... member) {
        typename traits_type::entity_type length{};
        archive(length);

        while(length--) {
            entity_type entt{};

            if constexpr(eastl::is_empty_v<Other>) {
                archive(entt);
                restore(entt);
                reg->template emplace_or_replace<Other>(map(entt));
            } else {
                Other instance{};
                archive(entt, instance);
                (update(instance, member), ...);
                restore(entt);
                reg->template emplace_or_replace<Other>(map(entt), eastl::as_const(instance));
            }
        }
    }

public:
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /**
     * @brief Constructs an instance that is bound to a given registry.
     * @param source A valid reference to a registry.
     */
    basic_continuous_loader(basic_registry<entity_type,Allocator> &source) ENTT_NOEXCEPT
        : reg{&source}
    {}

    /*! @brief Default move constructor. */
    basic_continuous_loader(basic_continuous_loader &&) = default;

    /*! @brief Default move assignment operator. @return This loader. */
    basic_continuous_loader & operator=(basic_continuous_loader &&) = default;

    /**
     * @brief Restores entities that were in use during serialization.
     *
     * This function restores the entities that were in use during serialization
     * and creates local counterparts for them if required.
     *
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A non-const reference to this loader.
     */
    template<typename Archive>
    basic_continuous_loader & entities(Archive &archive) {
        typename traits_type::entity_type length{};
        entity_type entt{};

        archive(length);

        for(decltype(length) pos{}; pos < length; ++pos) {
            archive(entt);

            if(const auto entity = (to_integral(entt) & traits_type::entity_mask); entity == pos) {
                restore(entt);
            } else {
                destroy(entt);
            }
        }
        return *this;
    }

    /**
     * @brief Deprecated function. Currently, it does nothing.
     * @tparam Archive Type of input archive.
     * @return A non-const reference to this loader.
     */
    template<typename Archive>
    [[deprecated("use ::entities instead, it imports now also destroyed entities")]]
    basic_continuous_loader & destroyed(Archive &) { return *this; }

    /**
     * @brief Restores components and assigns them to the right entities.
     *
     * The template parameter list must be exactly the same used during
     * serialization. In the event that the entity to which the component is
     * assigned doesn't exist yet, the loader will take care to create a local
     * counterpart for it.<br/>
     * Members can be either data members of type entity_type or containers of
     * entities. In both cases, the loader will visit them and update the
     * entities by replacing each one with its local counterpart.
     *
     * @tparam Component Type of component to restore.
     * @tparam Archive Type of input archive.
     * @tparam Type Types of components to update with local counterparts.
     * @tparam Member Types of members to update with their local counterparts.
     * @param archive A valid reference to an input archive.
     * @param member Members to update with their local counterparts.
     * @return A non-const reference to this loader.
     */
    template<typename... Component, typename Archive, typename... Type, typename... Member>
    basic_continuous_loader & component(Archive &archive, Member Type:: *... member) {
        (remove_if_exists<Component>(), ...);
        (assign<Component>(archive, member...), ...);
        return *this;
    }

    /**
     * @brief Helps to purge entities that no longer have a conterpart.
     *
     * Users should invoke this member function after restoring each snapshot,
     * unless they know exactly what they are doing.
     *
     * @return A non-const reference to this loader.
     */
    basic_continuous_loader & shrink() {
        auto it = remloc.begin();

        while(it != remloc.cend()) {
            const auto local = it->second.first;
            bool &dirty = it->second.second;

            if(dirty) {
                dirty = false;
                ++it;
            } else {
                if(reg->valid(local)) {
                    reg->destroy(local);
                }

                it = remloc.erase(it);
            }
        }

        return *this;
    }

    /**
     * @brief Destroys those entities that have no components.
     *
     * In case all the entities were serialized but only part of the components
     * was saved, it could happen that some of the entities have no components
     * once restored.<br/>
     * This functions helps to identify and destroy those entities.
     *
     * @return A non-const reference to this loader.
     */
    basic_continuous_loader & orphans() {
        reg->orphans([this](const auto entt) {
            reg->destroy(entt);
        });

        return *this;
    }

    /**
     * @brief Tests if a loader knows about a given entity.
     * @param entt An entity identifier.
     * @return True if `entity` is managed by the loader, false otherwise.
     */
    bool contains(entity_type entt) const ENTT_NOEXCEPT {
        return (remloc.find(entt) != remloc.cend());
    }

    /*! @copydoc contains */
    [[deprecated("use ::contains instead")]]
    bool has(entity_type entt) const ENTT_NOEXCEPT {
        return contains(entt);
    }

    /**
     * @brief Returns the identifier to which an entity refers.
     * @param entt An entity identifier.
     * @return The local identifier if any, the null entity otherwise.
     */
    entity_type map(entity_type entt) const ENTT_NOEXCEPT {
        const auto it = remloc.find(entt);
        entity_type other = null;

        if(it != remloc.cend()) {
            other = it->second.first;
        }

        return other;
    }

private:
    eastl::unordered_map<entity_type, eastl::pair<entity_type, bool>,Allocator> remloc;
    basic_registry<entity_type,Allocator> *reg;
};


}


#endif
