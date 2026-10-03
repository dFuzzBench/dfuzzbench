/**
 * pugixml parser - version 1.9
 * --------------------------------------------------------
 * Copyright (C) 2006-2018, by Arseny Kapoulkine (arseny.kapoulkine@gmail.com)
 * Report bugs and download new versions at http://pugixml.org/
 *
 * This library is distributed under the MIT License. See notice at the end
 * of this file.
 *
 * This work is based on the pugxml parser, which is:
 * Copyright (C) 2003, by Kristen Wegner (kristen@tima.net)
 */

#ifndef SOURCE_PUGIXML_CPP
#define SOURCE_PUGIXML_CPP

#include "pugixml.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#ifdef PUGIXML_WCHAR_MODE
#	include <wchar.h>
#endif

#ifndef PUGIXML_NO_XPATH
#	include <math.h>
#	include <float.h>
#endif

#ifndef PUGIXML_NO_STL
#	include <istream>
#	include <ostream>
#	include <string>
#endif

// For placement new
#include <new>

#ifdef _MSC_VER
#	pragma warning(push)
#	pragma warning(disable: 4127) // conditional expression is constant
#	pragma warning(disable: 4324) // structure was padded due to __declspec(align())
#	pragma warning(disable: 4702) // unreachable code
#	pragma warning(disable: 4996) // this function or variable may be unsafe
#endif

#if defined(_MSC_VER) && defined(__c2__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Wdeprecated" // this function or variable may be unsafe
#endif

#ifdef __INTEL_COMPILER
#	pragma warning(disable: 177) // function was declared but never referenced
#	pragma warning(disable: 279) // controlling expression is constant
#	pragma warning(disable: 1478 1786) // function was declared "deprecated"
#	pragma warning(disable: 1684) // conversion from pointer to same-sized integral type
#endif

#if defined(__BORLANDC__) && defined(PUGIXML_HEADER_ONLY)
#	pragma warn -8080 // symbol is declared but never used; disabling this inside push/pop bracket does not make the warning go away
#endif

#ifdef __BORLANDC__
#	pragma option push
#	pragma warn -8008 // condition is always false
#	pragma warn -8066 // unreachable code
#endif

#ifdef __SNC__
// Using diag_push/diag_pop does not disable the warnings inside templates due to a compiler bug
#	pragma diag_suppress=178 // function was declared but never referenced
#	pragma diag_suppress=237 // controlling expression is constant
#endif

#ifdef __TI_COMPILER_VERSION__
#	pragma diag_suppress 179 // function was declared but never referenced
#endif

// Inlining controls
#if defined(_MSC_VER) && _MSC_VER >= 1300
#	define PUGI__NO_INLINE __declspec(noinline)
#elif defined(__GNUC__)
#	define PUGI__NO_INLINE __attribute__((noinline))
#else
#	define PUGI__NO_INLINE
#endif

// Branch weight controls
#if defined(__GNUC__) && !defined(__c2__)
#	define PUGI__UNLIKELY(cond) __builtin_expect(cond, 0)
#else
#	define PUGI__UNLIKELY(cond) (cond)
#endif

// Simple static assertion
#define PUGI__STATIC_ASSERT(cond) { static const char condition_failed[(cond) ? 1 : -1] = {0}; (void)condition_failed[0]; }

// Digital Mars C++ bug workaround for passing char loaded from memory via stack
#ifdef __DMC__
#	define PUGI__DMC_VOLATILE volatile
#else
#	define PUGI__DMC_VOLATILE
#endif

// Integer sanitizer workaround; we only apply this for clang since gcc8 has no_sanitize but not unsigned-integer-overflow and produces "attribute directive ignored" warnings
#if defined(__clang__) && defined(__has_attribute)
#	if __has_attribute(no_sanitize)
#		define PUGI__UNSIGNED_OVERFLOW __attribute__((no_sanitize("unsigned-integer-overflow")))
#	else
#		define PUGI__UNSIGNED_OVERFLOW
#	endif
#else
#	define PUGI__UNSIGNED_OVERFLOW
#endif

// Borland C++ bug workaround for not defining ::memcpy depending on header include order (can't always use std::memcpy because some compilers don't have it at all)
#if defined(__BORLANDC__) && !defined(__MEM_H_USING_LIST)
using std::memcpy;
using std::memmove;
using std::memset;
#endif

// Some MinGW/GCC versions have headers that erroneously omit LLONG_MIN/LLONG_MAX/ULLONG_MAX definitions from limits.h in some configurations
#if defined(PUGIXML_HAS_LONG_LONG) && defined(__GNUC__) && !defined(LLONG_MAX) && !defined(LLONG_MIN) && !defined(ULLONG_MAX)
#	define LLONG_MIN (-LLONG_MAX - 1LL)
#	define LLONG_MAX __LONG_LONG_MAX__
#	define ULLONG_MAX (LLONG_MAX * 2ULL + 1ULL)
#endif

// In some environments MSVC is a compiler but the CRT lacks certain MSVC-specific features
#if defined(_MSC_VER) && !defined(__S3E__)
#	define PUGI__MSVC_CRT_VERSION _MSC_VER
#endif

// Not all platforms have snprintf; we define a wrapper that uses snprintf if possible. This only works with buffers with a known size.
#if __cplusplus >= 201103
#	define PUGI__SNPRINTF(buf, ...) snprintf(buf, sizeof(buf), __VA_ARGS__)
#elif defined(PUGI__MSVC_CRT_VERSION) && PUGI__MSVC_CRT_VERSION >= 1400
#	define PUGI__SNPRINTF(buf, ...) _snprintf_s(buf, _countof(buf), _TRUNCATE, __VA_ARGS__)
#else
#	define PUGI__SNPRINTF sprintf
#endif

// We put implementation details into an anonymous namespace in source mode, but have to keep it in non-anonymous namespace in header-only mode to prevent binary bloat.
#ifdef PUGIXML_HEADER_ONLY
#	define PUGI__NS_BEGIN namespace pugi { namespace impl {
#	define PUGI__NS_END } }
#	define PUGI__FN inline
#	define PUGI__FN_NO_INLINE inline
#else
#	if defined(_MSC_VER) && _MSC_VER < 1300 // MSVC6 seems to have an amusing bug with anonymous namespaces inside namespaces
#		define PUGI__NS_BEGIN namespace pugi { namespace impl {
#		define PUGI__NS_END } }
#	else
#		define PUGI__NS_BEGIN namespace pugi { namespace impl { namespace {
#		define PUGI__NS_END } } }
#	endif
#	define PUGI__FN
#	define PUGI__FN_NO_INLINE PUGI__NO_INLINE
#endif

// uintptr_t
#if (defined(_MSC_VER) && _MSC_VER < 1600) || (defined(__BORLANDC__) && __BORLANDC__ < 0x561)
namespace pugi
{
#	ifndef _UINTPTR_T_DEFINED
	typedef size_t uintptr_t;
#	endif

	typedef unsigned __int8 uint8_t;
	typedef unsigned __int16 uint16_t;
	typedef unsigned __int32 uint32_t;
}
#else
#	include <stdint.h>
#endif

// Memory allocation
PUGI__NS_BEGIN
	PUGI__FN void* default_allocate(size_t size)
	{
		return malloc(size);
	}

	PUGI__FN void default_deallocate(void* ptr)
	{
		free(ptr);
	}

	template <typename T>
	struct xml_memory_management_function_storage
	{
		static allocation_function allocate;
		static deallocation_function deallocate;
	};

	// Global allocation functions are stored in class statics so that in header mode linker deduplicates them
	// Without a template<> we'll get multiple definitions of the same static
	template <typename T> allocation_function xml_memory_management_function_storage<T>::allocate = default_allocate;
	template <typename T> deallocation_function xml_memory_management_function_storage<T>::deallocate = default_deallocate;

	typedef xml_memory_management_function_storage<int> xml_memory;
PUGI__NS_END

// String utilities
PUGI__NS_BEGIN
	// Get string length
	PUGI__FN size_t strlength(const char_t* s)
	{
		assert(s);

	#ifdef PUGIXML_WCHAR_MODE
		return wcslen(s);
	#else
		return strlen(s);
	#endif
	}

	// Compare two strings
	PUGI__FN bool strequal(const char_t* src, const char_t* dst)
	{
		assert(src && dst);

	#ifdef PUGIXML_WCHAR_MODE
		return wcscmp(src, dst) == 0;
	#else
		return strcmp(src, dst) == 0;
	#endif
	}

	// Compare lhs with [rhs_begin, rhs_end)
	PUGI__FN bool strequalrange(const char_t* lhs, const char_t* rhs, size_t count)
	{
		for (size_t i = 0; i < count; ++i)
			if (lhs[i] != rhs[i])
				return false;

		return lhs[count] == 0;
	}

	// Get length of wide string, even if CRT lacks wide character support
	PUGI__FN size_t strlength_wide(const wchar_t* s)
	{
		assert(s);

	#ifdef PUGIXML_WCHAR_MODE
		return wcslen(s);
	#else
		const wchar_t* end = s;
		while (*end) end++;
		return static_cast<size_t>(end - s);
	#endif
	}
PUGI__NS_END

// auto_ptr-like object for exception recovery
PUGI__NS_BEGIN
	template <typename T> struct auto_deleter
	{
		typedef void (*D)(T*);

		T* data;
		D deleter;

		auto_deleter(T* data_, D deleter_): data(data_), deleter(deleter_)
		{
		}

		~auto_deleter()
		{
			if (data) deleter(data);
		}

		T* release()
		{
			T* result = data;
			data = 0;
			return result;
		}
	};
PUGI__NS_END

#ifdef PUGIXML_COMPACT
PUGI__NS_BEGIN
	class compact_hash_table
	{
	public:
		compact_hash_table(): _items(0), _capacity(0), _count(0)
		{
		}

		void clear()
		{
			if (_items)
			{
				xml_memory::deallocate(_items);
				_items = 0;
				_capacity = 0;
				_count = 0;
			}
		}

		void* find(const void* key)
		{
			if (_capacity == 0) return 0;

			item_t* item = get_item(key);
			assert(item);
			assert(item->key == key || (item->key == 0 && item->value == 0));

			return item->value;
		}

		void insert(const void* key, void* value)
		{
			assert(_capacity != 0 && _count < _capacity - _capacity / 4);

			item_t* item = get_item(key);
			assert(item);

			if (item->key == 0)
			{
				_count++;
				item->key = key;
			}

			item->value = value;
		}

		bool reserve(size_t extra = 16)
		{
			if (_count + extra >= _capacity - _capacity / 4)
				return rehash(_count + extra);

			return true;
		}

	private:
		struct item_t
		{
			const void* key;
			void* value;
		};

		item_t* _items;
		size_t _capacity;

		size_t _count;

		bool rehash(size_t count);

		item_t* get_item(const void* key)
		{
			assert(key);
			assert(_capacity > 0);

			size_t hashmod = _capacity - 1;
			size_t bucket = hash(key) & hashmod;

			for (size_t probe = 0; probe <= hashmod; ++probe)
			{
				item_t& probe_item = _items[bucket];

				if (probe_item.key == key || probe_item.key == 0)
					return &probe_item;

				// hash collision, quadratic probing
				bucket = (bucket + probe + 1) & hashmod;
			}

			assert(false && "Hash table is full"); // unreachable
			return 0;
		}

		static PUGI__UNSIGNED_OVERFLOW unsigned int hash(const void* key)
		{
			unsigned int h = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(key));

			// MurmurHash3 32-bit finalizer
			h ^= h >> 16;
			h *= 0x85ebca6bu;
			h ^= h >> 13;
			h *= 0xc2b2ae35u;
			h ^= h >> 16;

			return h;
		}
	};

	PUGI__FN_NO_INLINE bool compact_hash_table::rehash(size_t count)
	{
		size_t capacity = 32;
		while (count >= capacity - capacity / 4)
			capacity *= 2;

		compact_hash_table rt;
		rt._capacity = capacity;
		rt._items = static_cast<item_t*>(xml_memory::allocate(sizeof(item_t) * capacity));

		if (!rt._items)
			return false;

		memset(rt._items, 0, sizeof(item_t) * capacity);

		for (size_t i = 0; i < _capacity; ++i)
			if (_items[i].key)
				rt.insert(_items[i].key, _items[i].value);

		if (_items)
			xml_memory::deallocate(_items);

		_capacity = capacity;
		_items = rt._items;

		assert(_count == rt._count);

		return true;
	}

PUGI__NS_END
#endif

PUGI__NS_BEGIN
#ifdef PUGIXML_COMPACT
	static const uintptr_t xml_memory_block_alignment = 4;
#else
	static const uintptr_t xml_memory_block_alignment = sizeof(void*);
#endif

	// extra metadata bits
	static const uintptr_t xml_memory_page_contents_shared_mask = 64;
	static const uintptr_t xml_memory_page_name_allocated_mask = 32;
	static const uintptr_t xml_memory_page_value_allocated_mask = 16;
	static const uintptr_t xml_memory_page_type_mask = 15;

	// combined masks for string uniqueness
	static const uintptr_t xml_memory_page_name_allocated_or_shared_mask = xml_memory_page_name_allocated_mask | xml_memory_page_contents_shared_mask;
	static const uintptr_t xml_memory_page_value_allocated_or_shared_mask = xml_memory_page_value_allocated_mask | xml_memory_page_contents_shared_mask;

#ifdef PUGIXML_COMPACT
	#define PUGI__GETHEADER_IMPL(object, page, flags) // unused
	#define PUGI__GETPAGE_IMPL(header) (header).get_page()
#else
	#define PUGI__GETHEADER_IMPL(object, page, flags) (((reinterpret_cast<char*>(object) - reinterpret_cast<char*>(page)) << 8) | (flags))
	// this macro casts pointers through void* to avoid 'cast increases required alignment of target type' warnings
	#define PUGI__GETPAGE_IMPL(header) static_cast<impl::xml_memory_page*>(const_cast<void*>(static_cast<const void*>(reinterpret_cast<const char*>(&header) - (header >> 8))))
#endif

	#define PUGI__GETPAGE(n) PUGI__GETPAGE_IMPL((n)->header)
	#define PUGI__NODETYPE(n) static_cast<xml_node_type>((n)->header & impl::xml_memory_page_type_mask)

	struct xml_allocator;

	struct xml_memory_page
	{
		static xml_memory_page* construct(void* memory)
		{
			xml_memory_page* result = static_cast<xml_memory_page*>(memory);

			result->allocator = 0;
			result->prev = 0;
			result->next = 0;
			result->busy_size = 0;
			result->freed_size = 0;

		#ifdef PUGIXML_COMPACT
			result->compact_string_base = 0;
			result->compact_shared_parent = 0;
			result->compact_page_marker = 0;
		#endif

			return result;
		}

		xml_allocator* allocator;

		xml_memory_page* prev;
		xml_memory_page* next;

		size_t busy_size;
		size_t freed_size;

	#ifdef PUGIXML_COMPACT
		char_t* compact_string_base;
		void* compact_shared_parent;
		uint32_t* compact_page_marker;
	#endif
	};

	static const size_t xml_memory_page_size =
	#ifdef PUGIXML_MEMORY_PAGE_SIZE
		(PUGIXML_MEMORY_PAGE_SIZE)
	#else
		32768
	#endif
		- sizeof(xml_memory_page);

	struct xml_memory_string_header
	{
		uint16_t page_offset; // offset from page->data
		uint16_t full_size; // 0 if string occupies whole page
	};

	struct xml_allocator
	{
		xml_allocator(xml_memory_page* root): _root(root), _busy_size(root->busy_size)
		{
		#ifdef PUGIXML_COMPACT
			_hash = 0;
		#endif
		}

		xml_memory_page* allocate_page(size_t data_size)
		{
			size_t size = sizeof(xml_memory_page) + data_size;

			// allocate block with some alignment, leaving memory for worst-case padding
			void* memory = xml_memory::allocate(size);
			if (!memory) return 0;

			// prepare page structure
			xml_memory_page* page = xml_memory_page::construct(memory);
			assert(page);

			page->allocator = _root->allocator;

			return page;
		}

		static void deallocate_page(xml_memory_page* page)
		{
			xml_memory::deallocate(page);
		}

		void* allocate_memory_oob(size_t size, xml_memory_page*& out_page);

		void* allocate_memory(size_t size, xml_memory_page*& out_page)
		{
			if (PUGI__UNLIKELY(_busy_size + size > xml_memory_page_size))
				return allocate_memory_oob(size, out_page);

			void* buf = reinterpret_cast<char*>(_root) + sizeof(xml_memory_page) + _busy_size;

			_busy_size += size;

			out_page = _root;

			return buf;
		}

	#ifdef PUGIXML_COMPACT
		void* allocate_object(size_t size, xml_memory_page*& out_page)
		{
			void* result = allocate_memory(size + sizeof(uint32_t), out_page);
			if (!result) return 0;

			// adjust for marker
			ptrdiff_t offset = static_cast<char*>(result) - reinterpret_cast<char*>(out_page->compact_page_marker);

			if (PUGI__UNLIKELY(static_cast<uintptr_t>(offset) >= 256 * xml_memory_block_alignment))
			{
				// insert new marker
				uint32_t* marker = static_cast<uint32_t*>(result);

				*marker = static_cast<uint32_t>(reinterpret_cast<char*>(marker) - reinterpret_cast<char*>(out_page));
				out_page->compact_page_marker = marker;

				// since we don't reuse the page space until we reallocate it, we can just pretend that we freed the marker block
				// this will make sure deallocate_memory correctly tracks the size
				out_page->freed_size += sizeof(uint32_t);

				return marker + 1;
			}
			else
			{
				// roll back uint32_t part
				_busy_size -= sizeof(uint32_t);

				return result;
			}
		}
	#else
		void* allocate_object(size_t size, xml_memory_page*& out_page)
		{
			return allocate_memory(size, out_page);
		}
	#endif

		void deallocate_memory(void* ptr, size_t size, xml_memory_page* page)
		{
			if (page == _root) page->busy_size = _busy_size;

			assert(ptr >= reinterpret_cast<char*>(page) + sizeof(xml_memory_page) && ptr < reinterpret_cast<char*>(page) + sizeof(xml_memory_page) + page->busy_size);
			(void)!ptr;

			page->freed_size += size;
			assert(page->freed_size <= page->busy_size);

			if (page->freed_size == page->busy_size)
			{
				if (page->next == 0)
				{
					assert(_root == page);

					// top page freed, just reset sizes
					page->busy_size = 0;
					page->freed_size = 0;

				#ifdef PUGIXML_COMPACT
					// reset compact state to maximize efficiency
					page->compact_string_base = 0;
					page->compact_shared_parent = 0;
					page->compact_page_marker = 0;
				#endif

					_busy_size = 0;
				}
				else
				{
					assert(_root != page);
					assert(page->prev);

					// remove from the list
					page->prev->next = page->next;
					page->next->prev = page->prev;

					// deallocate
					deallocate_page(page);
				}
			}
		}

		char_t* allocate_string(size_t length)
		{
			static const size_t max_encoded_offset = (1 << 16) * xml_memory_block_alignment;

			PUGI__STATIC_ASSERT(xml_memory_page_size <= max_encoded_offset);

			// allocate memory for string and header block
			size_t size = sizeof(xml_memory_string_header) + length * sizeof(char_t);

			// round size up to block alignment boundary
			size_t full_size = (size + (xml_memory_block_alignment - 1)) & ~(xml_memory_block_alignment - 1);

			xml_memory_page* page;
			xml_memory_string_header* header = static_cast<xml_memory_string_header*>(allocate_memory(full_size, page));

			if (!header) return 0;

			// setup header
			ptrdiff_t page_offset = reinterpret_cast<char*>(header) - reinterpret_cast<char*>(page) - sizeof(xml_memory_page);

			assert(page_offset % xml_memory_block_alignment == 0);
			assert(page_offset >= 0 && static_cast<size_t>(page_offset) < max_encoded_offset);
			header->page_offset = static_cast<uint16_t>(static_cast<size_t>(page_offset) / xml_memory_block_alignment);

			// full_size == 0 for large strings that occupy the whole page
			assert(full_size % xml_memory_block_alignment == 0);
			assert(full_size < max_encoded_offset || (page->busy_size == full_size && page_offset == 0));
			header->full_size = static_cast<uint16_t>(full_size < max_encoded_offset ? full_size / xml_memory_block_alignment : 0);

			// round-trip through void* to avoid 'cast increases required alignment of target type' warning
			// header is guaranteed a pointer-sized alignment, which should be enough for char_t
			return static_cast<char_t*>(static_cast<void*>(header + 1));
		}

		void deallocate_string(char_t* string)
		{
			// this function casts pointers through void* to avoid 'cast increases required alignment of target type' warnings
			// we're guaranteed the proper (pointer-sized) alignment on the input string if it was allocated via allocate_string

			// get header
			xml_memory_string_header* header = static_cast<xml_memory_string_header*>(static_cast<void*>(string)) - 1;
			assert(header);

			// deallocate
			size_t page_offset = sizeof(xml_memory_page) + header->page_offset * xml_memory_block_alignment;
			xml_memory_page* page = reinterpret_cast<xml_memory_page*>(static_cast<void*>(reinterpret_cast<char*>(header) - page_offset));

			// if full_size == 0 then this string occupies the whole page
			size_t full_size = header->full_size == 0 ? page->busy_size : header->full_size * xml_memory_block_alignment;

			deallocate_memory(header, full_size, page);
		}

		bool reserve()
		{
		#ifdef PUGIXML_COMPACT
			return _hash->reserve();
		#else
			return true;
		#endif
		}

		xml_memory_page* _root;
		size_t _busy_size;

	#ifdef PUGIXML_COMPACT
		compact_hash_table* _hash;
	#endif
	};

	PUGI__FN_NO_INLINE void* xml_allocator::allocate_memory_oob(size_t size, xml_memory_page*& out_page)
	{
		const size_t large_allocation_threshold = xml_memory_page_size / 4;

		xml_memory_page* page = allocate_page(size <= large_allocation_threshold ? xml_memory_page_size : size);
		out_page = page;

		if (!page) return 0;

		if (size <= large_allocation_threshold)
		{
			_root->busy_size = _busy_size;

			// insert page at the end of linked list
			page->prev = _root;
			_root->next = page;
			_root = page;

			_busy_size = size;
		}
		else
		{
			// insert page before the end of linked list, so that it is deleted as soon as possible
			// the last page is not deleted even if it's empty (see deallocate_memory)
			assert(_root->prev);

			page->prev = _root->prev;
			page->next = _root;

			_root->prev->next = page;
			_root->prev = page;

			page->busy_size = size;
		}

		return reinterpret_cast<char*>(page) + sizeof(xml_memory_page);
	}
PUGI__NS_END

#ifdef PUGIXML_COMPACT
PUGI__NS_BEGIN
	static const uintptr_t compact_alignment_log2 = 2;
	static const uintptr_t compact_alignment = 1 << compact_alignment_log2;

	class compact_header
	{
	public:
		compact_header(xml_memory_page* page, unsigned int flags)
		{
			PUGI__STATIC_ASSERT(xml_memory_block_alignment == compact_alignment);

			ptrdiff_t offset = (reinterpret_cast<char*>(this) - reinterpret_cast<char*>(page->compact_page_marker));
			assert(offset % compact_alignment == 0 && static_cast<uintptr_t>(offset) < 256 * compact_alignment);

			_page = static_cast<unsigned char>(offset >> compact_alignment_log2);
			_flags = static_cast<unsigned char>(flags);
		}

		void operator&=(uintptr_t mod)
		{
			_flags &= static_cast<unsigned char>(mod);
		}

		void operator|=(uintptr_t mod)
		{
			_flags |= static_cast<unsigned char>(mod);
		}

		uintptr_t operator&(uintptr_t mod) const
		{
			return _flags & mod;
		}

		xml_memory_page* get_page() const
		{
			// round-trip through void* to silence 'cast increases required alignment of target type' warnings
			const char* page_marker = reinterpret_cast<const char*>(this) - (_page << compact_alignment_log2);
			const char* page = page_marker - *reinterpret_cast<const uint32_t*>(static_cast<const void*>(page_marker));

			return const_cast<xml_memory_page*>(reinterpret_cast<const xml_memory_page*>(static_cast<const void*>(page)));
		}

	private:
		unsigned char _page;
		unsigned char _flags;
	};

	PUGI__FN xml_memory_page* compact_get_page(const void* object, int header_offset)
	{
		const compact_header* header = reinterpret_cast<const compact_header*>(static_cast<const char*>(object) - header_offset);

		return header->get_page();
	}

	template <int header_offset, typename T> PUGI__FN_NO_INLINE T* compact_get_value(const void* object)
	{
		return static_cast<T*>(compact_get_page(object, header_offset)->allocator->_hash->find(object));
	}

	template <int header_offset, typename T> PUGI__FN_NO_INLINE void compact_set_value(const void* object, T* value)
	{
		compact_get_page(object, header_offset)->allocator->_hash->insert(object, value);
	}

	template <typename T, int header_offset, int start = -126> class compact_pointer
	{
	public:
		compact_pointer(): _data(0)
		{
		}

		void operator=(const compact_pointer& rhs)
		{
			*this = rhs + 0;
		}

		void operator=(T* value)
		{
			if (value)
			{
				// value is guaranteed to be compact-aligned; 'this' is not
				// our decoding is based on 'this' aligned to compact alignment downwards (see operator T*)
				// so for negative offsets (e.g. -3) we need to adjust the diff by compact_alignment - 1 to
				// compensate for arithmetic shift rounding for negative values
				ptrdiff_t diff = reinterpret_cast<char*>(value) - reinterpret_cast<char*>(this);
				ptrdiff_t offset = ((diff + int(compact_alignment - 1)) >> compact_alignment_log2) - start;

				if (static_cast<uintptr_t>(offset) <= 253)
					_data = static_cast<unsigned char>(offset + 1);
				else
				{
					compact_set_value<header_offset>(this, value);

					_data = 255;
				}
			}
			else
				_data = 0;
		}

		operator T*() const
		{
			if (_data)
			{
				if (_data < 255)
				{
					uintptr_t base = reinterpret_cast<uintptr_t>(this) & ~(compact_alignment - 1);

					return reinterpret_cast<T*>(base + (_data - 1 + start) * compact_alignment);
				}
				else
					return compact_get_value<header_offset, T>(this);
			}
			else
				return 0;
		}

		T* operator->() const
		{
			return *this;
		}

	private:
		unsigned char _data;
	};

	template <typename T, int header_offset> class compact_pointer_parent
	{
	public:
		compact_pointer_parent(): _data(0)
		{
		}

		void operator=(const compact_pointer_parent& rhs)
		{
			*this = rhs + 0;
		}

		void operator=(T* value)
		{
			if (value)
			{
				// value is guaranteed to be compact-aligned; 'this' is not
				// our decoding is based on 'this' aligned to compact alignment downwards (see operator T*)
				// so for negative offsets (e.g. -3) we need to adjust the diff by compact_alignment - 1 to
				// compensate for arithmetic shift behavior for negative values
				ptrdiff_t diff = reinterpret_cast<char*>(value) - reinterpret_cast<char*>(this);
				ptrdiff_t offset = ((diff + int(compact_alignment - 1)) >> compact_alignment_log2) + 65533;

				if (static_cast<uintptr_t>(offset) <= 65533)
				{
					_data = static_cast<unsigned short>(offset + 1);
				}
				else
				{
					xml_memory_page* page = compact_get_page(this, header_offset);

					if (PUGI__UNLIKELY(page->compact_shared_parent == 0))
						page->compact_shared_parent = value;

					if (page->compact_shared_parent == value)
					{
						_data = 65534;
					}
					else
					{
						compact_set_value<header_offset>(this, value);

						_data = 65535;
					}
				}
			}
			else
			{
				_data = 0;
			}
		}

		operator T*() const
		{
			if (_data)
			{
				if (_data < 65534)
				{
					uintptr_t base = reinterpret_cast<uintptr_t>(this) & ~(compact_alignment - 1);

					return reinterpret_cast<T*>(base + (_data - 1 - 65533) * compact_alignment);
				}
				else if (_data == 65534)
					return static_cast<T*>(compact_get_page(this, header_offset)->compact_shared_parent);
				else
					return compact_get_value<header_offset, T>(this);
			}
			else
				return 0;
		}

		T* operator->() const
		{
			return *this;
		}

	private:
		uint16_t _data;
	};

	template <int header_offset, int base_offset> class compact_string
	{
	public:
		compact_string(): _data(0)
		{
		}

		void operator=(const compact_string& rhs)
		{
			*this = rhs + 0;
		}

		void operator=(char_t* value)
		{
			if (value)
			{
				xml_memory_page* page = compact_get_page(this, header_offset);

				if (PUGI__UNLIKELY(page->compact_string_base == 0))
					page->compact_string_base = value;

				ptrdiff_t offset = value - page->compact_string_base;

				if (static_cast<uintptr_t>(offset) < (65535 << 7))
				{
					// round-trip through void* to silence 'cast increases required alignment of target type' warnings
					uint16_t* base = reinterpret_cast<uint16_t*>(static_cast<void*>(reinterpret_cast<char*>(this) - base_offset));

					if (*base == 0)
					{
						*base = static_cast<uint16_t>((offset >> 7) + 1);
						_data = static_cast<unsigned char>((offset & 127) + 1);
					}
					else
					{
						ptrdiff_t remainder = offset - ((*base - 1) << 7);

						if (static_cast<uintptr_t>(remainder) <= 253)
						{
							_data = static_cast<unsigned char>(remainder + 1);
						}
						else
						{
							compact_set_value<header_offset>(this, value);

							_data = 255;
						}
					}
				}
				else
				{
					compact_set_value<header_offset>(this, value);

					_data = 255;
				}
			}
			else
			{
				_data = 0;
			}
		}

		operator char_t*() const
		{
			if (_data)
			{
				if (_data < 255)
				{
					xml_memory_page* page = compact_get_page(this, header_offset);

					// round-trip through void* to silence 'cast increases required alignment of target type' warnings
					const uint16_t* base = reinterpret_cast<const uint16_t*>(static_cast<const void*>(reinterpret_cast<const char*>(this) - base_offset));
					assert(*base);

					ptrdiff_t offset = ((*base - 1) << 7) + (_data - 1);

					return page->compact_string_base + offset;
				}
				else
				{
					return compact_get_value<header_offset, char_t>(this);
				}
			}
			else
				return 0;
		}

	private:
		unsigned char _data;
	};
PUGI__NS_END
#endif

#ifdef PUGIXML_COMPACT
namespace pugi
{
	struct xml_attribute_struct
	{
		xml_attribute_struct(impl::xml_memory_page* page): header(page, 0), namevalue_base(0)
		{
			PUGI__STATIC_ASSERT(sizeof(xml_attribute_struct) == 8);
		}

		impl::compact_header header;

		uint16_t namevalue_base;

		impl::compact_string<4, 2> name;
		impl::compact_string<5, 3> value;

		impl::compact_pointer<xml_attribute_struct, 6> prev_attribute_c;
		impl::compact_pointer<xml_attribute_struct, 7, 0> next_attribute;
	};

	struct xml_node_struct
	{
		xml_node_struct(impl::xml_memory_page* page, xml_node_type type): header(page, type), namevalue_base(0)
		{
			PUGI__STATIC_ASSERT(sizeof(xml_node_struct) == 12);
		}

		impl::compact_header header;

		uint16_t namevalue_base;

		impl::compact_string<4, 2> name;
		impl::compact_string<5, 3> value;

		impl::compact_pointer_parent<xml_node_struct, 6> parent;

		impl::compact_pointer<xml_node_struct, 8, 0> first_child;

		impl::compact_pointer<xml_node_struct,  9>    prev_sibling_c;
		impl::compact_pointer<xml_node_struct, 10, 0> next_sibling;

		impl::compact_pointer<xml_attribute_struct, 11, 0> first_attribute;
	};
}
#else
namespace pugi
{
	struct xml_attribute_struct
	{
		xml_attribute_struct(impl::xml_memory_page* page): name(0), value(0), prev_attribute_c(0), next_attribute(0)
		{
			header = PUGI__GETHEADER_IMPL(this, page, 0);
		}

		uintptr_t header;

		char_t*	name;
		char_t*	value;

		xml_attribute_struct* prev_attribute_c;
		xml_attribute_struct* next_attribute;
	};

	struct xml_node_struct
	{
		xml_node_struct(impl::xml_memory_page* page, xml_node_type type): name(0), value(0), parent(0), first_child(0), prev_sibling_c(0), next_sibling(0), first_attribute(0)
		{
			header = PUGI__GETHEADER_IMPL(this, page, type);
		}

		uintptr_t header;

		char_t* name;
		char_t* value;

		xml_node_struct* parent;

		xml_node_struct* first_child;

		xml_node_struct* prev_sibling_c;
		xml_node_struct* next_sibling;

		xml_attribute_struct* first_attribute;
	};
}
#endif

PUGI__NS_BEGIN
	struct xml_extra_buffer
	{
		char_t* buffer;
		xml_extra_buffer* next;
	};

	struct xml_document_struct: public xml_node_struct, public xml_allocator
	{
		xml_document_struct(xml_memory_page* page): xml_node_struct(page, node_document), xml_allocator(page), buffer(0), extra_buffers(0)
		{
		}

		const char_t* buffer;

		xml_extra_buffer* extra_buffers;

	#ifdef PUGIXML_COMPACT
		compact_hash_table hash;
	#endif
	};

	template <typename Object> inline xml_allocator& get_allocator(const Object* object)
	{
		assert(object);

		return *PUGI__GETPAGE(object)->allocator;
	}

	template <typename Object> inline xml_document_struct& get_document(const Object* object)
	{
		assert(object);

		return *static_cast<xml_document_struct*>(PUGI__GETPAGE(object)->allocator);
	}
PUGI__NS_END

// Low-level DOM operations
PUGI__NS_BEGIN
	inline xml_attribute_struct* allocate_attribute(xml_allocator& alloc)
	{
		xml_memory_page* page;
		void* memory = alloc.allocate_object(sizeof(xml_attribute_struct), page);
		if (!memory) return 0;

		return new (memory) xml_attribute_struct(page);
	}

	inline xml_node_struct* allocate_node(xml_allocator& alloc, xml_node_type type)
	{
		xml_memory_page* page;
		void* memory = alloc.allocate_object(sizeof(xml_node_struct), page);
		if (!memory) return 0;

		return new (memory) xml_node_struct(page, type);
	}

	inline void destroy_attribute(xml_attribute_struct* a, xml_allocator& alloc)
	{
		if (a->header & impl::xml_memory_page_name_allocated_mask)
			alloc.deallocate_string(a->name);

		if (a->header & impl::xml_memory_page_value_allocated_mask)
			alloc.deallocate_string(a->value);

		alloc.deallocate_memory(a, sizeof(xml_attribute_struct), PUGI__GETPAGE(a));
	}

	inline void destroy_node(xml_node_struct* n, xml_allocator& alloc)
	{
		if (n->header & impl::xml_memory_page_name_allocated_mask)
			alloc.deallocate_string(n->name);

		if (n->header & impl::xml_memory_page_value_allocated_mask)
			alloc.deallocate_string(n->value);

		for (xml_attribute_struct* attr = n->first_attribute; attr; )
		{
			xml_attribute_struct* next = attr->next_attribute;

			destroy_attribute(attr, alloc);

			attr = next;
		}

		for (xml_node_struct* child = n->first_child; child; )
		{
			xml_node_struct* next = child->next_sibling;

			destroy_node(child, alloc);

			child = next;
		}

		alloc.deallocate_memory(n, sizeof(xml_node_struct), PUGI__GETPAGE(n));
	}

	inline void append_node(xml_node_struct* child, xml_node_struct* node)
	{
		child->parent = node;

		xml_node_struct* head = node->first_child;

		if (head)
		{
			xml_node_struct* tail = head->prev_sibling_c;

			tail->next_sibling = child;
			child->prev_sibling_c = tail;
			head->prev_sibling_c = child;
		}
		else
		{
			node->first_child = child;
			child->prev_sibling_c = child;
		}
	}

	inline void prepend_node(xml_node_struct* child, xml_node_struct* node)
	{
		child->parent = node;

		xml_node_struct* head = node->first_child;

		if (head)
		{
			child->prev_sibling_c = head->prev_sibling_c;
			head->prev_sibling_c = child;
		}
		else
			child->prev_sibling_c = child;

		child->next_sibling = head;
		node->first_child = child;
	}

	inline void insert_node_after(xml_node_struct* child, xml_node_struct* node)
	{
		xml_node_struct* parent = node->parent;

		child->parent = parent;

		if (node->next_sibling)
			node->next_sibling->prev_sibling_c = child;
		else
			parent->first_child->prev_sibling_c = child;

		child->next_sibling = node->next_sibling;
		child->prev_sibling_c = node;

		node->next_sibling = child;
	}

	inline void insert_node_before(xml_node_struct* child, xml_node_struct* node)
	{
		xml_node_struct* parent = node->parent;

		child->parent = parent;

		if (node->prev_sibling_c->next_sibling)
			node->prev_sibling_c->next_sibling = child;
		else
			parent->first_child = child;

		child->prev_sibling_c = node->prev_sibling_c;
		child->next_sibling = node;

		node->prev_sibling_c = child;
	}

	inline void remove_node(xml_node_struct* node)
	{
		xml_node_struct* parent = node->parent;

		if (node->next_sibling)
			node->next_sibling->prev_sibling_c = node->prev_sibling_c;
		else
			parent->first_child->prev_sibling_c = node->prev_sibling_c;

		if (node->prev_sibling_c->next_sibling)
			node->prev_sibling_c->next_sibling = node->next_sibling;
		else
			parent->first_child = node->next_sibling;

		node->parent = 0;
		node->prev_sibling_c = 0;
		node->next_sibling = 0;
	}

	inline void append_attribute(xml_attribute_struct* attr, xml_node_struct* node)
	{
		xml_attribute_struct* head = node->first_attribute;

		if (head)
		{
			xml_attribute_struct* tail = head->prev_attribute_c;

			tail->next_attribute = attr;
			attr->prev_attribute_c = tail;
			head->prev_attribute_c = attr;
		}
		else
		{
			node->first_attribute = attr;
			attr->prev_attribute_c = attr;
		}
	}

	inline void prepend_attribute(xml_attribute_struct* attr, xml_node_struct* node)
	{
		xml_attribute_struct* head = node->first_attribute;

		if (head)
		{
			attr->prev_attribute_c = head->prev_attribute_c;
			head->prev_attribute_c = attr;
		}
		else
			attr->prev_attribute_c = attr;

		attr->next_attribute = head;
		node->first_attribute = attr;
	}

	inline void insert_attribute_after(xml_attribute_struct* attr, xml_attribute_struct* place, xml_node_struct* node)
	{
		if (place->next_attribute)
			place->next_attribute->prev_attribute_c = attr;
		else
			node->first_attribute->prev_attribute_c = attr;

		attr->next_attribute = place->next_attribute;
		attr->prev_attribute_c = place;
		place->next_attribute = attr;
	}

	inline void insert_attribute_before(xml_attribute_struct* attr, xml_attribute_struct* place, xml_node_struct* node)
	{
		if (place->prev_attribute_c->next_attribute)
			place->prev_attribute_c->next_attribute = attr;
		else
			node->first_attribute = attr;

		attr->prev_attribute_c = place->prev_attribute_c;
		attr->next_attribute = place;
		place->prev_attribute_c = attr;
	}

	inline void remove_attribute(xml_attribute_struct* attr, xml_node_struct* node)
	{
		if (attr->next_attribute)
			attr->next_attribute->prev_attribute_c = attr->prev_attribute_c;
		else
			node->first_attribute->prev_attribute_c = attr->prev_attribute_c;

		if (attr->prev_attribute_c->next_attribute)
			attr->prev_attribute_c->next_attribute = attr->next_attribute;
		else
			node->first_attribute = attr->next_attribute;

		attr->prev_attribute_c = 0;
		attr->next_attribute = 0;
	}

	PUGI__FN_NO_INLINE xml_node_struct* append_new_node(xml_node_struct* node, xml_allocator& alloc, xml_node_type type = node_element)
	{
		if (!alloc.reserve()) return 0;

		xml_node_struct* child = allocate_node(alloc, type);
		if (!child) return 0;

		append_node(child, node);

		return child;
	}

	PUGI__FN_NO_INLINE xml_attribute_struct* append_new_attribute(xml_node_struct* node, xml_allocator& alloc)
	{
		if (!alloc.reserve()) return 0;

		xml_attribute_struct* attr = allocate_attribute(alloc);
		if (!attr) return 0;

		append_attribute(attr, node);

		return attr;
	}
PUGI__NS_END

// Helper classes for code generation
PUGI__NS_BEGIN
	struct opt_false
	{
		enum { value = 0 };
	};

	struct opt_true
	{
		enum { value = 1 };
	};
PUGI__NS_END

// Unicode utilities
PUGI__NS_BEGIN
	inline uint16_t endian_swap(uint16_t value)
	{
		return static_cast<uint16_t>(((value & 0xff) << 8) | (value >> 8));
	}

	inline uint32_t endian_swap(uint32_t value)
	{
		return ((value & 0xff) << 24) | ((value & 0xff00) << 8) | ((value & 0xff0000) >> 8) | (value >> 24);
	}

	struct utf8_counter
	{
		typedef size_t value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			// U+0000..U+007F
			if (ch < 0x80) return result + 1;
			// U+0080..U+07FF
			else if (ch < 0x800) return result + 2;
			// U+0800..U+FFFF
			else return result + 3;
		}

		static value_type high(value_type result, uint32_t)
		{
			// U+10000..U+10FFFF
			return result + 4;
		}
	};

	struct utf8_writer
	{
		typedef uint8_t* value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			// U+0000..U+007F
			if (ch < 0x80)
			{
				*result = static_cast<uint8_t>(ch);
				return result + 1;
			}
			// U+0080..U+07FF
			else if (ch < 0x800)
			{
				result[0] = static_cast<uint8_t>(0xC0 | (ch >> 6));
				result[1] = static_cast<uint8_t>(0x80 | (ch & 0x3F));
				return result + 2;
			}
			// U+0800..U+FFFF
			else
			{
				result[0] = static_cast<uint8_t>(0xE0 | (ch >> 12));
				result[1] = static_cast<uint8_t>(0x80 | ((ch >> 6) & 0x3F));
				result[2] = static_cast<uint8_t>(0x80 | (ch & 0x3F));
				return result + 3;
			}
		}

		static value_type high(value_type result, uint32_t ch)
		{
			// U+10000..U+10FFFF
			result[0] = static_cast<uint8_t>(0xF0 | (ch >> 18));
			result[1] = static_cast<uint8_t>(0x80 | ((ch >> 12) & 0x3F));
			result[2] = static_cast<uint8_t>(0x80 | ((ch >> 6) & 0x3F));
			result[3] = static_cast<uint8_t>(0x80 | (ch & 0x3F));
			return result + 4;
		}

		static value_type any(value_type result, uint32_t ch)
		{
			return (ch < 0x10000) ? low(result, ch) : high(result, ch);
		}
	};

	struct utf16_counter
	{
		typedef size_t value_type;

		static value_type low(value_type result, uint32_t)
		{
			return result + 1;
		}

		static value_type high(value_type result, uint32_t)
		{
			return result + 2;
		}
	};

	struct utf16_writer
	{
		typedef uint16_t* value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			*result = static_cast<uint16_t>(ch);

			return result + 1;
		}

		static value_type high(value_type result, uint32_t ch)
		{
			uint32_t msh = static_cast<uint32_t>(ch - 0x10000) >> 10;
			uint32_t lsh = static_cast<uint32_t>(ch - 0x10000) & 0x3ff;

			result[0] = static_cast<uint16_t>(0xD800 + msh);
			result[1] = static_cast<uint16_t>(0xDC00 + lsh);

			return result + 2;
		}

		static value_type any(value_type result, uint32_t ch)
		{
			return (ch < 0x10000) ? low(result, ch) : high(result, ch);
		}
	};

	struct utf32_counter
	{
		typedef size_t value_type;

		static value_type low(value_type result, uint32_t)
		{
			return result + 1;
		}

		static value_type high(value_type result, uint32_t)
		{
			return result + 1;
		}
	};

	struct utf32_writer
	{
		typedef uint32_t* value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			*result = ch;

			return result + 1;
		}

		static value_type high(value_type result, uint32_t ch)
		{
			*result = ch;

			return result + 1;
		}

		static value_type any(value_type result, uint32_t ch)
		{
			*result = ch;

			return result + 1;
		}
	};

	struct latin1_writer
	{
		typedef uint8_t* value_type;

		static value_type low(value_type result, uint32_t ch)
		{
			*result = static_cast<uint8_t>(ch > 255 ? '?' : ch);

			return result + 1;
		}

		static value_type high(value_type result, uint32_t ch)
		{
			(void)ch;

			*result = '?';

			return result + 1;
		}
	};

	struct utf8_decoder
	{
		typedef uint8_t type;

		template <typename Traits> static inline typename Traits::value_type process(const uint8_t* data, size_t size, typename Traits::value_type result, Traits)
		{
			const uint8_t utf8_byte_mask = 0x3f;

			while (size)
			{
				uint8_t lead = *data;

				// 0xxxxxxx -> U+0000..U+007F
				if (lead < 0x80)
				{
					result = Traits::low(result, lead);
					data += 1;
					size -= 1;

					// process aligned single-byte (ascii) blocks
					if ((reinterpret_cast<uintptr_t>(data) & 3) == 0)
					{
						// round-trip through void* to silence 'cast increases required alignment of target type' warnings
						while (size >= 4 && (*static_cast<const uint32_t*>(static_cast<const void*>(data)) & 0x80808080) == 0)
						{
							result = Traits::low(result, data[0]);
							result = Traits::low(result, data[1]);
							result = Traits::low(result, data[2]);
							result = Traits::low(result, data[3]);
							data += 4;
							size -= 4;
						}
					}
				}
				// 110xxxxx -> U+0080..U+07FF
				else if (static_cast<unsigned int>(lead - 0xC0) < 0x20 && size >= 2 && (data[1] & 0xc0) == 0x80)
				{
					result = Traits::low(result, ((lead & ~0xC0) << 6) | (data[1] & utf8_byte_mask));
					data += 2;
					size -= 2;
				}
				// 1110xxxx -> U+0800-U+FFFF
				else if (static_cast<unsigned int>(lead - 0xE0) < 0x10 && size >= 3 && (data[1] & 0xc0) == 0x80 && (data[2] & 0xc0) == 0x80)
				{
					result = Traits::low(result, ((lead & ~0xE0) << 12) | ((data[1] & utf8_byte_mask) << 6) | (data[2] & utf8_byte_mask));
					data += 3;
					size -= 3;
				}
				// 11110xxx -> U+10000..U+10FFFF
				else if (static_cast<unsigned int>(lead - 0xF0) < 0x08 && size >= 4 && (data[1] & 0xc0) == 0x80 && (data[2] & 0xc0) == 0x80 && (data[3] & 0xc0) == 0x80)
				{
					result = Traits::high(result, ((lead & ~0xF0) << 18) | ((data[1] & utf8_byte_mask) << 12) | ((data[2] & utf8_byte_mask) << 6) | (data[3] & utf8_byte_mask));
					data += 4;
					size -= 4;
				}
				// 10xxxxxx or 11111xxx -> invalid
				else
				{
					data += 1;
					size -= 1;
				}
			}

			return result;
		}
	};

	template <typename opt_swap> struct utf16_decoder
	{
		typedef uint16_t type;

		template <typename Traits> static inline typename Traits::value_type process(const uint16_t* data, size_t size, typename Traits::value_type result, Traits)
		{
			while (size)
			{
				uint16_t lead = opt_swap::value ? endian_swap(*data) : *data;

				// U+0000..U+D7FF
				if (lead < 0xD800)
				{
					result = Traits::low(result, lead);
					data += 1;
					size -= 1;
				}
				// U+E000..U+FFFF
				else if (static_cast<unsigned int>(lead - 0xE000) < 0x2000)
				{
					result = Traits::low(result, lead);
					data += 1;
					size -= 1;
				}
				// surrogate pair lead
				else if (static_cast<unsigned int>(lead - 0xD800) < 0x400 && size >= 2)
				{
					uint16_t next = opt_swap::value ? endian_swap(data[1]) : data[1];

					if (static_cast<unsigned int>(next - 0xDC00) < 0x400)
					{
						result = Traits::high(result, 0x10000 + ((lead & 0x3ff) << 10) + (next & 0x3ff));
						data += 2;
						size -= 2;
					}
					else
					{
						data += 1;
						size -= 1;
					}
				}
				else
				{
					data += 1;
					size -= 1;
				}
			}

			return result;
		}
	};

	template <typename opt_swap> struct utf32_decoder
	{
		typedef uint32_t type;

		template <typename Traits> static inline typename Traits::value_type process(const uint32_t* data, size_t size, typename Traits::value_type result, Traits)
		{
			while (size)
			{
				uint32_t lead = opt_swap::value ? endian_swap(*data) : *data;

				// U+0000..U+FFFF
				if (lead < 0x10000)
				{
					result = Traits::low(result, lead);
					data += 1;
					size -= 1;
				}
				// U+10000..U+10FFFF
				else
				{
					result = Traits::high(result, lead);
					data += 1;
					size -= 1;
				}
			}

			return result;
		}
	};

	struct latin1_decoder
	{
		typedef uint8_t type;

		template <typename Traits> static inline typename Traits::value_type process(const uint8_t* data, size_t size, typename Traits::value_type result, Traits)
		{
			while (size)
			{
				result = Traits::low(result, *data);
				data += 1;
				size -= 1;
			}

			return result;
		}
	};

	template <size_t size> struct wchar_selector;

	template <> struct wchar_selector<2>
	{
		typedef uint16_t type;
		typedef utf16_counter counter;
		typedef utf16_writer writer;
		typedef utf16_decoder<opt_false> decoder;
	};

	template <> struct wchar_selector<4>
	{
		typedef uint32_t type;
		typedef utf32_counter counter;
		typedef utf32_writer writer;
		typedef utf32_decoder<opt_false> decoder;
	};

	typedef wchar_selector<sizeof(wchar_t)>::counter wchar_counter;
	typedef wchar_selector<sizeof(wchar_t)>::writer wchar_writer;

	struct wchar_decoder
	{
		typedef wchar_t type;

		template <typename Traits> static inline typename Traits::value_type process(const wchar_t* data, size_t size, typename Traits::value_type result, Traits traits)
		{
			typedef wchar_selector<sizeof(wchar_t)>::decoder decoder;

			return decoder::process(reinterpret_cast<const typename decoder::type*>(data), size, result, traits);
		}
	};

#ifdef PUGIXML_WCHAR_MODE
	PUGI__FN void convert_wchar_endian_swap(wchar_t* result, const wchar_t* data, size_t length)
	{
		for (size_t i = 0; i < length; ++i)
			result[i] = static_cast<wchar_t>(endian_swap(static_cast<wchar_selector<sizeof(wchar_t)>::type>(data[i])));
	}
#endif
PUGI__NS_END

PUGI__NS_BEGIN
	enum chartype_t
	{
		ct_parse_pcdata = 1,	// \0, &, \r, <
		ct_parse_attr = 2,		// \0, &, \r, ', "
		ct_parse_attr_ws = 4,	// \0, &, \r, ', ", \n, tab
		ct_space = 8,			// \r, \n, space, tab
		ct_parse_cdata = 16,	// \0, ], >, \r
		ct_parse_comment = 32,	// \0, -, >, \r
		ct_symbol = 64,			// Any symbol > 127, a-z, A-Z, 0-9, _, :, -, .
		ct_start_symbol = 128	// Any symbol > 127, a-z, A-Z, _, :
	};

	static const unsigned char chartype_table[256] =
	{
		55,  0,   0,   0,   0,   0,   0,   0,      0,   12,  12,  0,   0,   63,  0,   0,   // 0-15
		0,   0,   0,   0,   0,   0,   0,   0,      0,   0,   0,   0,   0,   0,   0,   0,   // 16-31
		8,   0,   6,   0,   0,   0,   7,   6,      0,   0,   0,   0,   0,   96,  64,  0,   // 32-47
		64,  64,  64,  64,  64,  64,  64,  64,     64,  64,  192, 0,   1,   0,   48,  0,   // 48-63
		0,   192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192, // 64-79
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 0,   0,   16,  0,   192, // 80-95
		0,   192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192, // 96-111
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 0, 0, 0, 0, 0,           // 112-127

		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192, // 128+
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
		192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192
	};

	enum chartypex_t
	{
		ctx_special_pcdata = 1,   // Any symbol >= 0 and < 32 (except \t, \r, \n), &, <, >
		ctx_special_attr = 2,     // Any symbol >= 0 and < 32 (except \t), &, <, >, "
		ctx_start_symbol = 4,	  // Any symbol > 127, a-z, A-Z, _
		ctx_digit = 8,			  // 0-9
		ctx_symbol = 16			  // Any symbol > 127, a-z, A-Z, 0-9, _, -, .
	};

	static const unsigned char chartypex_table[256] =
	{
		3,  3,  3,  3,  3,  3,  3,  3,     3,  0,  2,  3,  3,  2,  3,  3,     // 0-15
		3,  3,  3,  3,  3,  3,  3,  3,     3,  3,  3,  3,  3,  3,  3,  3,     // 16-31
		0,  0,  2,  0,  0,  0,  3,  0,     0,  0,  0,  0,  0, 16, 16,  0,     // 32-47
		24, 24, 24, 24, 24, 24, 24, 24,    24, 24, 0,  0,  3,  0,  3,  0,     // 48-63

		0,  20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,    // 64-79
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 0,  0,  0,  0,  20,    // 80-95
		0,  20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,    // 96-111
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 0,  0,  0,  0,  0,     // 112-127

		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,    // 128+
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20,    20, 20, 20, 20, 20, 20, 20, 20
	};

#ifdef PUGIXML_WCHAR_MODE
	#define PUGI__IS_CHARTYPE_IMPL(c, ct, table) ((static_cast<unsigned int>(c) < 128 ? table[static_cast<unsigned int>(c)] : table[128]) & (ct))
#else
	#define PUGI__IS_CHARTYPE_IMPL(c, ct, table) (table[static_cast<unsigned char>(c)] & (ct))
#endif

	#define PUGI__IS_CHARTYPE(c, ct) PUGI__IS_CHARTYPE_IMPL(c, ct, chartype_table)
	#define PUGI__IS_CHARTYPEX(c, ct) PUGI__IS_CHARTYPE_IMPL(c, ct, chartypex_table)

	PUGI__FN bool is_little_endian()
	{
		unsigned int ui = 1;

		return *reinterpret_cast<unsigned char*>(&ui) == 1;
	}

	PUGI__FN xml_encoding get_wchar_encoding()
	{
		PUGI__STATIC_ASSERT(sizeof(wchar_t) == 2 || sizeof(wchar_t) == 4);

		if (sizeof(wchar_t) == 2)
			return is_little_endian() ? encoding_utf16_le : encoding_utf16_be;
		else
			return is_little_endian() ? encoding_utf32_le : encoding_utf32_be;
	}

	PUGI__FN bool parse_declaration_encoding(const uint8_t* data, size_t size, const uint8_t*& out_encoding, size_t& out_length)
	{
	#define PUGI__SCANCHAR(ch) { if (offset >= size || data[offset] != ch) return false; offset++; }
	#define PUGI__SCANCHARTYPE(ct) { while (offset < size && PUGI__IS_CHARTYPE(data[offset], ct)) offset++; }

		// check if we have a non-empty XML declaration
		if (size < 6 || !((data[0] == '<') & (data[1] == '?') & (data[2] == 'x') & (data[3] == 'm') & (data[4] == 'l') && PUGI__IS_CHARTYPE(data[5], ct_space)))
			return false;

		// scan XML declaration until the encoding field
		for (size_t i = 6; i + 1 < size; ++i)
		{
			// declaration can not contain ? in quoted values
			if (data[i] == '?')
				return false;

			if (data[i] == 'e' && data[i + 1] == 'n')
			{
				size_t offset = i;

				// encoding follows the version field which can't contain 'en' so this has to be the encoding if XML is well formed
				PUGI__SCANCHAR('e'); PUGI__SCANCHAR('n'); PUGI__SCANCHAR('c'); PUGI__SCANCHAR('o');
				PUGI__SCANCHAR('d'); PUGI__SCANCHAR('i'); PUGI__SCANCHAR('n'); PUGI__SCANCHAR('g');

				// S? = S?
				PUGI__SCANCHARTYPE(ct_space);
				PUGI__SCANCHAR('=');
				PUGI__SCANCHARTYPE(ct_space);

				// the only two valid delimiters are ' and "
				uint8_t delimiter = (offset < size && data[offset] == '"') ? '"' : '\'';

				PUGI__SCANCHAR(delimiter);

				size_t start = offset;

				out_encoding = data + offset;

				PUGI__SCANCHARTYPE(ct_symbol);

				out_length = offset - start;

				PUGI__SCANCHAR(delimiter);

				return true;
			}
		}

		return false;

	#undef PUGI__SCANCHAR
	#undef PUGI__SCANCHARTYPE
	}

	PUGI__FN xml_encoding guess_buffer_encoding(const uint8_t* data, size_t size)
	{
		// skip encoding autodetection if input buffer is too small
		if (size < 4) return encoding_utf8;

		uint8_t d0 = data[0], d1 = data[1], d2 = data[2], d3 = data[3];

		// look for BOM in first few bytes
		if (d0 == 0 && d1 == 0 && d2 == 0xfe && d3 == 0xff) return encoding_utf32_be;
		if (d0 == 0xff && d1 == 0xfe && d2 == 0 && d3 == 0) return encoding_utf32_le;
		if (d0 == 0xfe && d1 == 0xff) return encoding_utf16_be;
		if (d0 == 0xff && d1 == 0xfe) return encoding_utf16_le;
		if (d0 == 0xef && d1 == 0xbb && d2 == 0xbf) return encoding_utf8;

		// look for <, <? or <?xm in various encodings
		if (d0 == 0 && d1 == 0 && d2 == 0 && d3 == 0x3c) return encoding_utf32_be;
		if (d0 == 0x3c && d1 == 0 && d2 == 0 && d3 == 0) return encoding_utf32_le;
		if (d0 == 0 && d1 == 0x3c && d2 == 0 && d3 == 0x3f) return encoding_utf16_be;
		if (d0 == 0x3c && d1 == 0 && d2 == 0x3f && d3 == 0) return encoding_utf16_le;

		// look for utf16 < followed by node name (this may fail, but is better than utf8 since it's zero terminated so early)
		if (d0 == 0 && d1 == 0x3c) return encoding_utf16_be;
		if (d0 == 0x3c && d1 == 0) return encoding_utf16_le;

		// no known BOM detected; parse declaration
		const uint8_t* enc = 0;
		size_t enc_length = 0;

		if (d0 == 0x3c && d1 == 0x3f && d2 == 0x78 && d3 == 0x6d && parse_declaration_encoding(data, size, enc, enc_length))
		{
			// iso-8859-1 (case-insensitive)
			if (enc_length == 10
				&& (enc[0] | ' ') == 'i' && (enc[1] | ' ') == 's' && (enc[2] | ' ') == 'o'
				&& enc[3] == '-' && enc[4] == '8' && enc[5] == '8' && enc[6] == '5' && enc[7] == '9'
				&& enc[8] == '-' && enc[9] == '1')
				return encoding_latin1;

			// latin1 (case-insensitive)
			if (enc_length == 6
				&& (enc[0] | ' ') == 'l' && (enc[1] | ' ') == 'a' && (enc[2] | ' ') == 't'
				&& (enc[3] | ' ') == 'i' && (enc[4] | ' ') == 'n'
				&& enc[5] == '1')
				return encoding_latin1;
		}

		return encoding_utf8;
	}

	PUGI__FN xml_encoding get_buffer_encoding(xml_encoding encoding, const void* contents, size_t size)
	{
		// replace wchar encoding with utf implementation
		if (encoding == encoding_wchar) return get_wchar_encoding();

		// replace utf16 encoding with utf16 with specific endianness
		if (encoding == encoding_utf16) return is_little_endian() ? encoding_utf16_le : encoding_utf16_be;

		// replace utf32 encoding with utf32 with specific endianness
		if (encoding == encoding_utf32) return is_little_endian() ? encoding_utf32_le : encoding_utf32_be;

		// only do autodetection if no explicit encoding is requested
		if (encoding != encoding_auto) return encoding;

		// try to guess encoding (based on XML specification, Appendix F.1)
		const uint8_t* data = static_cast<const uint8_t*>(contents);

		return guess_buffer_encoding(data, size);
	}

	PUGI__FN bool get_mutable_buffer(char_t*& out_buffer, size_t& out_length, const void* contents, size_t size, bool is_mutable)
	{
		size_t length = size / sizeof(char_t);

		if (is_mutable)
		{
			out_buffer = static_cast<char_t*>(const_cast<void*>(contents));
			out_length = length;
		}
		else
		{
			char_t* buffer = static_cast<char_t*>(xml_memory::allocate((length + 1) * sizeof(char_t)));
			if (!buffer) return false;

			if (contents)
				memcpy(buffer, contents, length * sizeof(char_t));
			else
				assert(length == 0);

			buffer[length] = 0;

			out_buffer = buffer;
			out_length = length + 1;
		}

		return true;
	}

#ifdef PUGIXML_WCHAR_MODE
	PUGI__FN bool need_endian_swap_utf(xml_encoding le, xml_encoding re)
	{
		return (le == encoding_utf16_be && re == encoding_utf16_le) || (le == encoding_utf16_le && re == encoding_utf16_be) ||
			   (le == encoding_utf32_be && re == encoding_utf32_le) || (le == encoding_utf32_le && re == encoding_utf32_be);
	}

	PUGI__FN bool convert_buffer_endian_swap(char_t*& out_buffer, size_t& out_length, const void* contents, size_t size, bool is_mutable)
	{
		const char_t* data = static_cast<const char_t*>(contents);
		size_t length = size / sizeof(char_t);

		if (is_mutable)
		{
			char_t* buffer = const_cast<char_t*>(data);

			convert_wchar_endian_swap(buffer, data, length);

			out_buffer = buffer;
			out_length = length;
		}
		else
		{
			char_t* buffer = static_cast<char_t*>(xml_memory::allocate((length + 1) * sizeof(char_t)));
			if (!buffer) return false;

			convert_wchar_endian_swap(buffer, data, length);
			buffer[length] = 0;

			out_buffer = buffer;
			out_length = length + 1;
		}

		return true;
	}

	template <typename D> PUGI__FN bool convert_buffer_generic(char_t*& out_buffer, size_t& out_length, const void* contents, size_t size, D)
	{
		const typename D::type* data = static_cast<const typename D::type*>(contents);
		size_t data_length = size / sizeof(typename D::type);

		// first pass: get length in wchar_t units
		size_t length = D::process(data, data_length, 0, wchar_counter());

		// allocate buffer of suitable length
		char_t* buffer = static_cast<char_t*>(xml_memory::allocate((length + 1) * sizeof(char_t)));
		if (!buffer) return false;

		// second pass: convert utf16 input to wchar_t
		wchar_writer::value_type obegin = reinterpret_cast<wchar_writer::value_type>(buffer);
		wchar_writer::value_type oend = D::process(data, data_length, obegin, wchar_writer());

		assert(oend == obegin + length);
		*oend = 0;

		out_buffer = buffer;
		out_length = length + 1;

		return true;
	}

	PUGI__FN bool convert_buffer(char_t*& out_buffer, size_t& out_length, xml_encoding encoding, const void* contents, size_t size, bool is_mutable)
	{
		// get native encoding
		xml_encoding wchar_encoding = get_wchar_encoding();

		// fast path: no conversion required
		if (encoding == wchar_encoding)
			return get_mutable_buffer(out_buffer, out_length, contents, size, is_mutable);

		// only endian-swapping is required
		if (need_endian_swap_utf(encoding, wchar_encoding))
