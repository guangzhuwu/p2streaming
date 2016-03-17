#ifndef big_number_h__
#define big_number_h__

#include "p2engine/push_warning_option.hpp"
#include <cctype>
#include <algorithm>
#include <string>
#include <cstring>
#include "p2engine/pop_warning_option.hpp"

namespace p2common{

	template<size_t bit_size>
	class big_number
		: public boost::bitwise<big_number<bit_size> >
		, public boost::shiftable<big_number<bit_size> >
		, public boost::totally_ordered<big_number<bit_size> >
	{
		BOOST_STATIC_ASSERT(bit_size%8==0);
		BOOST_STATIC_ASSERT(bit_size%32==0);
		enum { byte_size = bit_size/8 };
		enum { int_size = bit_size/32 };
	public:
		static big_number get_max()
		{
			big_number ret;
			memset(ret.m_data, 0xff, byte_size);
			return ret;
		}

		static big_number get_min()
		{
			big_number ret;
			memset(ret.m_data, 0, byte_size);
			return ret;
		}

		big_number() { clear(); }
		big_number(const big_number& rhs) 
		{
			assign(rhs);
		}
		explicit big_number(const char* s)
		{
			assign(s);
		}
		explicit big_number(const void* s, size_t len)
		{
			assign(s, len);
		}
		explicit big_number(std::string const& s)
		{
			assign(s);
		}

		void assign(std::string const& s)
		{
			//BOOST_ASSERT(s.size() >= byte_size);
			int sl = int(s.size()) < byte_size ? int(s.size()) : byte_size;
			std::memcpy(m_data, &s[0], sl);
			if (sl<byte_size)
				std::memset(((char*)&m_data)+sl, 0, byte_size-sl);
		}
		void assign(const char* str) 
		{ 			
			if (!str) clear();
			else std::memcpy(m_data, str, byte_size);
		}
		void assign(const void* p, size_t len) 
		{ 			
			if (!p) clear();
			else std::memcpy(m_data, p, (std::min<size_t>)(byte_size, len));
		}
		void assign(const big_number& rhs) 
		{
			std::memcpy(m_data, rhs.m_data, sizeof(m_data));
		}

		big_number& operator=(std::string const& s){assign(s);return *this;}
		big_number& operator=(const big_number& rhs){assign(rhs);return *this;}

		void clear() { std::memset(m_data, 0, byte_size); }

		bool is_all_zeros() const
		{
			static big_number zero;
			return std::memcmp(m_data, zero.m_data, byte_size)==0;
		}

		big_number& operator<<=(int n)
		{
			BOOST_ASSERT(n >= 0);
			if (n > byte_size * 8) n = byte_size;
			int num_bytes = n / 8;
			if (num_bytes >= byte_size)
			{
				std::memset(m_data, 0, byte_size);
				return *this;
			}
			char* char_ptr=(char*)m_data;
			if (num_bytes > 0)
			{
				std::memmove(char_ptr, char_ptr + num_bytes, byte_size - num_bytes);
				std::memset(char_ptr + byte_size - num_bytes, 0, num_bytes);
				n -= num_bytes * 8;
			}
			if (n > 0)
			{
				for (int i = 0; i < byte_size - 1; ++i)
				{
					char_ptr[i] <<= n;
					char_ptr[i] |= char_ptr[i+1] >> (8 - n);
				}
			}
			return *this;
		}

		big_number& operator>>=(int n)
		{
			int num_bytes = n / 8;
			if (num_bytes >= byte_size)
			{
				std::memset(m_data, 0, byte_size);
				return *this;
			}
			char* char_ptr=(char*)m_data;
			if (num_bytes > 0)
			{
				std::memmove(char_ptr + num_bytes, char_ptr, byte_size - num_bytes);
				std::memset(char_ptr, 0, num_bytes);
				n -= num_bytes * 8;
			}
			if (n > 0)
			{
				for (int i = byte_size - 1; i > 0; --i)
				{
					char_ptr[i] >>= n;
					char_ptr[i] |= char_ptr[i-1] << (8 - n);
				}
			}
			return *this;
		}

		bool operator==(big_number const& n) const
		{
			for (int i=0;i<int_size;++i)
			{
				if (n.m_data[i]!=m_data[i])
					return false;
			}
			return true;
		}

		bool operator<(big_number const& n) const
		{
			return std::memcmp(m_data, n.m_data, byte_size)<0;
		}

		big_number operator~()
		{
			big_number ret;
			for (int i = 0; i< int_size; ++i)
				ret.m_data[i] = ~m_data[i];
			return ret;
		}

		big_number& operator &= (big_number const& n)
		{
			for (int i = 0; i< int_size; ++i)
				m_data[i] &= n.m_data[i];
			return *this;
		}

		big_number& operator |= (big_number const& n)
		{
			for (int i = 0; i< int_size; ++i)
				m_data[i] |= n.m_data[i];
			return *this;
		}

		big_number& operator ^= (big_number const& n)
		{
			for (int i = 0; i< int_size; ++i)
				m_data[i] ^= n.m_data[i];
			return *this;
		}

		static std::size_t size(){return byte_size;}

		char& operator[](int i)
		{ BOOST_ASSERT(i >= 0 && i < byte_size); return ((char*)m_data)[i]; }

		const char& operator[](int i) const
		{ BOOST_ASSERT(i >= 0 && i < byte_size); return ((const char*)m_data)[i];}

		typedef const unsigned char* const_iterator;
		typedef unsigned char* iterator;

		const_iterator begin() const { return (const_iterator)m_data; }
		const_iterator end() const { return ((const_iterator)m_data)+byte_size; }

		iterator begin() { return (iterator)m_data; }
		iterator end() { return ((iterator)m_data)+byte_size; }

		std::string to_string() const
		{ return std::string((const char*)&m_data[0], byte_size); }

	private:
		boost::uint32_t m_data[int_size];
	};

}


#endif // TORRENT_PEER_ID_HPP_INCLUDED

