//************************************ bs::framework - Copyright 2018 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//

#pragma once
#include <cstdint>
#include "core/forward_decls.h"

namespace se
{
	/** @addtogroup General
	 *  @{
	 */

	 /** Wrapper around an enum that allows simple use of bitwise logic operations. */
	template<typename Enum, typename Storage = uint32_t>
	class Flags
	{
	public:
		using InternalType = Storage;

		Flags() = default;

		Flags(Enum value)
		{
			mBits = static_cast<Storage>(value);
		}

		Flags(const Flags<Enum, Storage>& value)
		{
			mBits = value.mBits;
		}

		explicit Flags(Storage bits)
		{
			mBits = bits;
		}

		/** Checks whether all of the provided bits are set */
		bool isSet(Enum value) const
		{
			return (mBits & static_cast<Storage>(value)) == static_cast<Storage>(value);
		}

		/** Checks whether any of the provided bits are set */
		bool isSetAny(Enum value) const
		{
			return (mBits & static_cast<Storage>(value)) != 0;
		}

		/** Checks whether any of the provided bits are set */
		bool isSetAny(const Flags<Enum, Storage>& value) const
		{
			return (mBits & value.mBits) != 0;
		}

		/** Activates all of the provided bits. */
		Flags<Enum, Storage>& set(Enum value)
		{
			mBits |= static_cast<Storage>(value);

			return *this;
		}

		/** Deactivates all of the provided bits. */
		void unset(Enum value)
		{
			mBits &= ~static_cast<Storage>(value);
		}

		bool operator==(Enum rhs) const
		{
			return mBits == static_cast<Storage>(rhs);
		}

		bool operator==(const Flags<Enum, Storage>& rhs) const
		{
			return mBits == rhs.mBits;
		}

		bool operator==(bool rhs) const
		{
			return ((bool)*this) == rhs;
		}

		bool operator!=(Enum rhs) const
		{
			return mBits != static_cast<Storage>(rhs);
		}

		bool operator!=(const Flags<Enum, Storage>& rhs) const
		{
			return mBits != rhs.mBits;
		}

		Flags<Enum, Storage>& operator= (Enum rhs)
		{
			mBits = static_cast<Storage>(rhs);

			return *this;
		}

		Flags<Enum, Storage>& operator= (const Flags<Enum, Storage>& rhs)
		{
			mBits = rhs.mBits;

			return *this;
		}

		Flags<Enum, Storage>& operator|= (Enum rhs)
		{
			mBits |= static_cast<Storage>(rhs);

			return *this;
		}

		Flags<Enum, Storage>& operator|= (const Flags<Enum, Storage>& rhs)
		{
			mBits |= rhs.mBits;

			return *this;
		}

		Flags<Enum, Storage> operator| (Enum rhs) const
		{
			Flags<Enum, Storage> out(*this);
			out |= rhs;

			return out;
		}

		Flags<Enum, Storage> operator| (const Flags<Enum, Storage>& rhs) const
		{
			Flags<Enum, Storage> out(*this);
			out |= rhs;

			return out;
		}

		Flags<Enum, Storage>& operator&= (Enum rhs)
		{
			mBits &= static_cast<Storage>(rhs);

			return *this;
		}

		Flags<Enum, Storage>& operator&= (const Flags<Enum, Storage>& rhs)
		{
			mBits &= rhs.mBits;

			return *this;
		}

		Flags<Enum, Storage> operator& (Enum rhs) const
		{
			Flags<Enum, Storage> out = *this;
			out.mBits &= static_cast<Storage>(rhs);

			return out;
		}

		Flags<Enum, Storage> operator& (const Flags<Enum, Storage>& rhs) const
		{
			Flags<Enum, Storage> out = *this;
			out.mBits &= rhs.mBits;

			return out;
		}

		Flags<Enum, Storage>& operator^= (Enum rhs)
		{
			mBits ^= static_cast<Storage>(rhs);

			return *this;
		}

		Flags<Enum, Storage>& operator^= (const Flags<Enum, Storage>& rhs)
		{
			mBits ^= rhs.mBits;

			return *this;
		}

		Flags<Enum, Storage> operator^ (Enum rhs) const
		{
			Flags<Enum, Storage> out = *this;
			out.mBits ^= static_cast<Storage>(rhs);

			return out;
		}

		Flags<Enum, Storage> operator^ (const Flags<Enum, Storage>& rhs) const
		{
			Flags<Enum, Storage> out = *this;
			out.mBits ^= rhs.mBits;

			return out;
		}

		Flags<Enum, Storage> operator~ () const
		{
			Flags<Enum, Storage> out;
			out.mBits = (Storage)~mBits;

			return out;
		}

		operator bool() const
		{
			return mBits ? true : false;
		}

		operator uint8_t() const
		{
			return static_cast<uint8_t>(mBits);
		}

		operator uint16_t() const
		{
			return static_cast<uint16_t>(mBits);
		}

		operator uint32_t() const
		{
			return static_cast<uint32_t>(mBits);
		}

		friend Flags<Enum, Storage> operator&(Enum a, Flags<Enum, Storage>& b)
		{
			Flags<Enum, Storage> out;
			out.mBits = a & b.mBits;
			return out;
		}

	private:
		InternalType mBits{ 0 };
	};

	/** Defines global operators for a Flags<Enum, Storage> implementation. */
#define SE_FLAGS_OPERATORS(Enum) BS_FLAGS_OPERATORS_EXT(Enum, uint32_t)

/** Defines global operators for a Flags<Enum, Storage> implementation. */
#define BS_FLAGS_OPERATORS_EXT(Enum, Storage)																   \
		inline Flags<Enum, Storage> operator|(Enum a, Enum b) { Flags<Enum, Storage> r(a); r |= b; return r; } \
		inline Flags<Enum, Storage> operator&(Enum a, Enum b) { Flags<Enum, Storage> r(a); r &= b; return r; } \
		inline Flags<Enum, Storage> operator~(Enum a) { return ~Flags<Enum, Storage>(a); }

	/** @} */
}

/** @cond STDLIB */

namespace eastl
{
	/** Hash value generator for Flags<Enum, Storage>. */
	template<class Enum, class Storage>
	struct hash<se::Flags<Enum, Storage>>
	{
		size_t operator()(const se::Flags<Enum, Storage>& key) const
		{
			return (Storage)key;
		}
	};
}

/** @endcond */