#ifndef DARTS_H_
#define DARTS_H_

#include <cstdio>
#include <exception>
#include <new>

#define DARTS_INT_TO_STR(value) #value
#define DARTS_LINE_TO_STR(line) DARTS_INT_TO_STR(line)
#define DARTS_LINE_STR DARTS_LINE_TO_STR(__LINE__)
#define DARTS_THROW(msg) throw Darts::Details::Exception( \
	__FILE__ ":" DARTS_LINE_STR ": exception: " msg)

namespace Darts {
namespace Details {

//
// Basic types.
//

typedef char char_type;
typedef unsigned char uchar_type;
typedef int value_type;
typedef unsigned int id_type;

//
// Element of double-array.
//

class DoubleArrayUnit
{
public:
	DoubleArrayUnit() : unit_() {}

	bool has_leaf() const
	{ return ((unit_ >> 8) & 1) == 1; }
	value_type value() const
	{ return static_cast<value_type>(unit_ & ((1U << 31) - 1)); }
	id_type label() const
	{ return unit_ & ((1U << 31) | 0xFF); }
	id_type offset() const
	{ return (unit_ >> 10) << ((unit_ & (1U << 9)) >> 6); }

private:
	id_type unit_;

	// Copyable.
};

//
// Exception.
//

class Exception : public std::exception
{
public:
	explicit Exception(const char *msg = NULL) throw() : msg_(msg) {}
	Exception(const Exception &rhs) throw() : msg_(rhs.msg_) {}
	virtual ~Exception() throw() {}

	virtual const char *what() const throw()
	{ return (msg_ != NULL) ? msg_ : ""; }

private:
	const char *msg_;

	// Disallows operator=.
	Exception &operator=(const Exception &);
};

}  // namespace Details

//
// Double-array implementation.
//

template <typename, typename, typename T, typename>
class DoubleArrayImpl
{
public:
	typedef T value_type;
	typedef Details::char_type key_type;
	typedef value_type result_type;  // for compatibility.

	struct result_pair_type
	{
		value_type value;
		std::size_t length;
	};

	DoubleArrayImpl() : size_(0), array_(NULL), buf_(NULL) {}
	virtual ~DoubleArrayImpl() { clear(); }

	inline void set_result(value_type *result,
		value_type value, std::size_t) const;
	inline void set_result(result_pair_type *result,
		value_type value, std::size_t length) const;

	void set_array(const void *ptr, std::size_t size = 0);
	const void *array() const { return array_; }

	void clear();

	std::size_t unit_size() const { return sizeof(unit_type); }
	std::size_t size() const { return size_; }
	std::size_t total_size() const { return unit_size() * size(); }

	std::size_t nonzero_size() const { return size(); }

	int build(std::size_t num_keys, const key_type * const *keys,
		const std::size_t *lengths = NULL, const value_type *values = NULL,
		int (*progress_func)(std::size_t, std::size_t) = NULL);

	int open(const char *file_name, const char *mode = "rb",
		std::size_t offset = 0, std::size_t size = 0);
	int save(const char *file_name, const char *mode = "wb",
		std::size_t offset = 0) const;

	template <class U>
	inline void exactMatchSearch(const key_type *key, U &result,
		std::size_t length = 0, std::size_t node_pos = 0) const;
	template <class U>
	inline U exactMatchSearch(const key_type *key, std::size_t length = 0,
		std::size_t node_pos = 0) const;

	template <class U>
	inline std::size_t commonPrefixSearch(const key_type *key, U *results,
		std::size_t max_num_results, std::size_t length = 0,
		std::size_t node_pos = 0) const;

	inline value_type traverse(const key_type *key, std::size_t &node_pos,
		std::size_t &key_pos, std::size_t length = 0) const;

private:
	typedef Details::uchar_type uchar_type;
	typedef Details::id_type id_type;
	typedef Details::DoubleArrayUnit unit_type;

	std::size_t size_;
	const unit_type *array_;
	unit_type *buf_;

	// Disallows copies.
	DoubleArrayImpl(const DoubleArrayImpl &);
	DoubleArrayImpl &operator=(const DoubleArrayImpl &);
};

//
// Basic double-array.
//

typedef DoubleArrayImpl<void, void, int, void> DoubleArray;

//
// Member functions of DoubleArrayImpl (except build()).
//

template <typename A, typename B, typename T, typename C>
inline void DoubleArrayImpl<A, B, T, C>::set_result(value_type *result,
	value_type value, std::size_t) const
{
	*result = value;
}

template <typename A, typename B, typename T, typename C>
inline void DoubleArrayImpl<A, B, T, C>::set_result(result_pair_type *result,
	value_type value, std::size_t length) const
{
	result->value = value;
	result->length = length;
}

template <typename A, typename B, typename T, typename C>
void DoubleArrayImpl<A, B, T, C>::set_array(const void *ptr,
	std::size_t size)
{
	clear();

	array_ = static_cast<const unit_type *>(ptr);
	size_ = size;
}

template <typename A, typename B, typename T, typename C>
void DoubleArrayImpl<A, B, T, C>::clear()
{
	size_ = 0;
	array_ = NULL;
	if (buf_ != NULL)
	{
		delete[] buf_;
		buf_ = NULL;
	}
}

template <typename A, typename B, typename T, typename C>
int DoubleArrayImpl<A, B, T, C>::open(const char *file_name,
	const char *mode, std::size_t offset, std::size_t size)
{
#ifdef _MSC_VER
	std::FILE *file;
	if (::fopen_s(&file, file_name, mode) != 0)
		return -1;
#else
	std::FILE *file = std::fopen(file_name, mode);
	if (file == NULL)
		return -1;
#endif

	if (size == 0)
	{
		if (std::fseek(file, 0, SEEK_END) != 0)
		{
			std::fclose(file);
			return -1;
		}
		size = std::ftell(file) - offset;
	}

	if (std::fseek(file, offset, SEEK_SET) != 0)
	{
		std::fclose(file);
		return -1;
	}

	size /= unit_size();
	unit_type *buf;
	try
	{
		buf = new unit_type[size];
	}
	catch (const std::bad_alloc &)
	{
		std::fclose(file);
		DARTS_THROW("failed to open double-array: std::bad_alloc");
	}

	if (std::fread(buf, unit_size(), size, file) != size)
	{
		std::fclose(file);
		delete[] buf;
		return -1;
	}
	std::fclose(file);

	clear();

	size_ = size;
	array_ = buf;
	buf_ = buf;

	return 0;
}

template <typename A, typename B, typename T, typename C>
int DoubleArrayImpl<A, B, T, C>::save(const char *file_name,
	const char *mode, std::size_t) const
{
	if (size() == 0)
		return -1;

#ifdef _MSC_VER
	std::FILE *file;
	if (::fopen_s(&file, file_name, mode) != 0)
		return -1;
#else
	std::FILE *file = std::fopen(file_name, mode);
	if (file == NULL)
		return -1;
#endif

	if (std::fwrite(array_, unit_size(), size(), file) != size())
	{
		std::fclose(file);
		return -1;
	}

	std::fclose(file);
	return 0;
}

template <typename A, typename B, typename T, typename C>
template <typename U>
inline void DoubleArrayImpl<A, B, T, C>::exactMatchSearch(const key_type *key,
	U &result, std::size_t length, std::size_t node_pos) const
{
	result = exactMatchSearch<U>(key, length, node_pos);
}

template <typename A, typename B, typename T, typename C>
template <typename U>
inline U DoubleArrayImpl<A, B, T, C>::exactMatchSearch(const key_type *key,
	std::size_t length, std::size_t node_pos) const
{
	U result;
	set_result(&result, static_cast<value_type>(-1), 0);

	unit_type unit = array_[node_pos];
	if (length != 0)
	{
		for (std::size_t i = 0; i < length; ++i)
		{
			node_pos ^= unit.offset() ^ static_cast<uchar_type>(key[i]);
			unit = array_[node_pos];
			if (unit.label() != static_cast<uchar_type>(key[i]))
				return result;
		}
	}
	else
	{
		for ( ; key[length] != '\0'; ++length)
		{
			node_pos ^= unit.offset() ^ static_cast<uchar_type>(key[length]);
			unit = array_[node_pos];
			if (unit.label() != static_cast<uchar_type>(key[length]))
				return result;
		}
	}

	if (!unit.has_leaf())
		return result;

	unit = array_[node_pos ^ unit.offset()];
	set_result(&result, static_cast<value_type>(unit.value()), length);
	return result;
}

template <typename A, typename B, typename T, typename C>
template <typename U>
inline std::size_t DoubleArrayImpl<A, B, T, C>::commonPrefixSearch(
	const key_type *key, U *results, std::size_t max_num_results,
	std::size_t length, std::size_t node_pos) const
{
	std::size_t num_results = 0;

	unit_type unit = array_[node_pos];
	node_pos ^= unit.offset();
	if (length != 0)
	{
		for (std::size_t i = 0; i < length; ++i)
		{
			node_pos ^= static_cast<uchar_type>(key[i]);
			unit = array_[node_pos];
			if (unit.label() != static_cast<uchar_type>(key[i]))
				return num_results;

			node_pos ^= unit.offset();
			if (unit.has_leaf())
			{
				if (num_results < max_num_results)
				{
					set_result(&results[num_results], static_cast<value_type>(
						array_[node_pos].value()), i + 1);
				}
				++num_results;
			}
		}
	}
	else
	{
		for ( ; key[length] != '\0'; ++length)
		{
			node_pos ^= static_cast<uchar_type>(key[length]);
			unit = array_[node_pos];
			if (unit.label() != static_cast<uchar_type>(key[length]))
				return num_results;

			node_pos ^= unit.offset();
			if (unit.has_leaf())
			{
				if (num_results < max_num_results)
				{
					set_result(&results[num_results], static_cast<value_type>(
						array_[node_pos].value()), length + 1);
				}
				++num_results;
			}
		}
	}

	return num_results;
}

template <typename A, typename B, typename T, typename C>
inline typename DoubleArrayImpl<A, B, T, C>::value_type
DoubleArrayImpl<A, B, T, C>::traverse(const key_type *key,
	std::size_t &node_pos, std::size_t &key_pos, std::size_t length) const
{
	id_type id = static_cast<id_type>(node_pos);
	unit_type unit = array_[id];

	if (length != 0)
	{
		for ( ; key_pos < length; ++key_pos)
		{
			id ^= unit.offset() ^ static_cast<uchar_type>(key[key_pos]);
			unit = array_[id];
			if (unit.label() != static_cast<uchar_type>(key[key_pos]))
				return static_cast<value_type>(-2);
			node_pos = id;
		}
	}
	else
	{
		for ( ; key[key_pos] != '\0'; ++key_pos)
		{
			id ^= unit.offset() ^ static_cast<uchar_type>(key[key_pos]);
			unit = array_[id];
			if (unit.label() != static_cast<uchar_type>(key[key_pos]))
				return static_cast<value_type>(-2);
			node_pos = id;
		}
	}

	if (!unit.has_leaf())
		return static_cast<value_type>(-1);

	unit = array_[id ^ unit.offset()];
	return static_cast<value_type>(unit.value());
}

namespace Details {

//
// Memory management of array.
//

template <typename T>
class AutoArray
{
public:
	explicit AutoArray(T *array = NULL) : array_(array) {}
	~AutoArray() { clear(); }

	const T &operator[](std::size_t id) const { return array_[id]; }
	T &operator[](std::size_t id) { return array_[id]; }

	bool empty() const { return array_ == NULL; }

	void clear();
	void swap(AutoArray *array);
	void reset(T *array = NULL) { AutoArray(array).swap(this); }

private:
	T *array_;

	// Disallows copies.
	AutoArray(const AutoArray &);
	AutoArray &operator=(const AutoArray &);
};

template <typename T>
void AutoArray<T>::clear()
{
	if (array_ != NULL)
	{
		delete[] array_;
		array_ = NULL;
	}
}

template <typename T>
void AutoArray<T>::swap(AutoArray *array)
{
	T *temp;
	temp = array_;
	array_ = array->array_;
	array->array_ = temp;
}

//
// Memory management of resizable array.
//

template <typename T>
class AutoPool
{
public:
	AutoPool() : buf_(), size_(0), capacity_(0) {}
	~AutoPool() { clear(); }

	const T &operator[](std::size_t id) const;
	T &operator[](std::size_t id);

	bool empty() const { return size_ == 0; }
	std::size_t size() const { return size_; }

	void clear();

	void push_back(const T &value) { append(value); }
	void pop_back();

	void append();
	void append(const T &value);

	void resize(std::size_t size);
	void resize(std::size_t size, const T &value);

	void reserve(std::size_t size);

private:
	AutoArray<char> buf_;
	std::size_t size_;
	std::size_t capacity_;

	// Disallows copies.
	AutoPool(const AutoPool &);
	AutoPool &operator=(const AutoPool &);

	void resize_buf(std::size_t size);
};

template <typename T>
const T &AutoPool<T>::operator[](std::size_t id) const
{
	return *(reinterpret_cast<const T *>(&buf_[0]) + id);
}

template <typename T>
T &AutoPool<T>::operator[](std::size_t id)
{
	return *(reinterpret_cast<T *>(&buf_[0]) + id);
}

template <typename T>
void AutoPool<T>::clear()
{
	resize(0);

	buf_.clear();
	size_ = 0;
	capacity_ = 0;
}

template <typename T>
void AutoPool<T>::pop_back()
{
	(*this)[--size_].~T();
}

template <typename T>
void AutoPool<T>::append()
{
	if (size_ == capacity_)
		resize_buf(size_ + 1);
	new(&(*this)[size_++]) T;
}

template <typename T>
void AutoPool<T>::append(const T &value)
{
	if (size_ == capacity_)
		resize_buf(size_ + 1);
	new(&(*this)[size_++]) T(value);
}

template <typename T>
void AutoPool<T>::resize(std::size_t size)
{
	while (size_ > size)
		(*this)[--size_].~T();

	if (size > capacity_)
		resize_buf(size);

	while (size_ < size)
		new(&(*this)[size_++]) T;
}

template <typename T>
void AutoPool<T>::resize(std::size_t size, const T &value)
{
	while (size_ > size)
		(*this)[--size_].~T();

	if (size > capacity_)
		resize_buf(size);

	while (size_ < size)
		new(&(*this)[size_++]) T(value);
}

template <typename T>
void AutoPool<T>::reserve(std::size_t size)
{
	if (size > capacity_)
		resize_buf(size);
}

template <typename T>
void AutoPool<T>::resize_buf(std::size_t size)
{
	std::size_t capacity;
	if (size >= capacity_ * 2)
		capacity = size;
	else
	{
		capacity = 1;
		while (capacity < size)
			capacity <<= 1;
	}

	AutoArray<char> buf;
	try
	{
		buf.reset(new char[sizeof(T) * capacity]);
	}
	catch (const std::bad_alloc &)
	{
		DARTS_THROW("failed to resize pool: std::bad_alloc");
	}

	if (size_ > 0)
	{
		T *src = reinterpret_cast<T *>(&buf_[0]);
		T *dest = reinterpret_cast<T *>(&buf[0]);
		for (std::size_t i = 0; i < size_; ++i)
		{
			new(&dest[i]) T(src[i]);
			src[i].~T();
		}
	}

	buf_.swap(&buf);
	capacity_ = capacity;
}

//
// Memory management of stack.
//

template <typename T>
class AutoStack
{
public:
	AutoStack() : pool_() {}
	~AutoStack() { clear(); }

	const T &top() const { return pool_[size() - 1]; }
	T &top() { return pool_[size() - 1]; }

	bool empty() const { return pool_.empty(); }
	std::size_t size() const { return pool_.size(); }

	void push(const T &value) { pool_.push_back(value); }
	void pop() { pool_.pop_back(); }

	void clear() { pool_.clear(); }

private:
	AutoPool<T> pool_;

	// Disallows copies.
	AutoStack(const AutoStack &);
	AutoStack &operator=(const AutoStack &);
};

//
// Succinct bit vector.
//

class BitVector
{
public:
	BitVector() : units_(), ranks_(), num_ones_(0), size_(0) {}
	~BitVector() { clear(); }

	bool operator[](std::size_t id) const;

	id_type rank(std::size_t id) const;

	void set(std::size_t id, bool bit);

	bool empty() const { return units_.empty(); }
	std::size_t num_ones() const { return num_ones_; }
	std::size_t size() const { return size_; }

	void append();
	void build();

	void clear();

private:
	enum { UNIT_SIZE = sizeof(id_type) * 8 };

	AutoPool<id_type> units_;
	AutoArray<id_type> ranks_;
	std::size_t num_ones_;
	std::size_t size_;

	// Disallows copies.
	BitVector(const BitVector &);
	BitVector &operator=(const BitVector &);

	static id_type pop_count(id_type unit);
};

inline bool BitVector::operator[](std::size_t id) const
{
	return (units_[id / UNIT_SIZE] >> (id % UNIT_SIZE) & 1) == 1;
}

inline id_type BitVector::rank(std::size_t id) const
{
	std::size_t unit_id = id / UNIT_SIZE;
	return ranks_[unit_id] + pop_count(units_[unit_id]
		& (~0U >> (UNIT_SIZE - (id % UNIT_SIZE) - 1)));
}

inline void BitVector::set(std::size_t id, bool bit)
{
	if (bit)
		units_[id / UNIT_SIZE] |= 1U << (id % UNIT_SIZE);
	else
		units_[id / UNIT_SIZE] &= ~(1U << (id % UNIT_SIZE));
}

inline void BitVector::append()
{
	if ((size_ % UNIT_SIZE) == 0)
		units_.append(0);
	++size_;
}

inline void BitVector::build()
{
	try
	{
		ranks_.reset(new id_type[units_.size()]);
	}
	catch (const std::bad_alloc &)
	{
		DARTS_THROW("failed to build rank index: std::bad_alloc");
	}

	num_ones_ = 0;
	for (std::size_t i = 0; i < units_.size(); ++i)
	{
		ranks_[i] = num_ones_;
		num_ones_ += pop_count(units_[i]);
	}
}

inline void BitVector::clear()
{
	units_.clear();
	ranks_.clear();
}

inline id_type BitVector::pop_count(id_type unit)
{
	unit = ((unit & 0xAAAAAAAA) >> 1) + (unit & 0x55555555);
	unit = ((unit & 0xCCCCCCCC) >> 2) + (unit & 0x33333333);
	unit = ((unit >> 4) + unit) & 0x0F0F0F0F;
	unit += unit >> 8;
	unit += unit >> 16;
	return unit & 0xFF;
}

//
// Node of Directed Acyclic Word Graph (DAWG).
//

class DawgNode
{
public:
	DawgNode() : child_(0), sibling_(0), label_('\0'),
		is_state_(false), has_sibling_(false) {}

	void set_child(id_type child) { child_ = child; }
	void set_sibling(id_type sibling) { sibling_ = sibling; }
	void set_value(value_type value) { child_ = value; }
	void set_label(uchar_type label) { label_ = label; }
	void set_is_state(bool is_state) { is_state_ = is_state; }
	void set_has_sibling(bool has_sibling) { has_sibling_ = has_sibling; }

	id_type child() const { return child_; }
	id_type sibling() const { return sibling_; }
	value_type value() const { return static_cast<value_type>(child_); }
	uchar_type label() const { return label_; }
	bool is_state() const { return is_state_; }
	bool has_sibling() const { return has_sibling_; }

	id_type unit() const;

private:
	id_type child_;
	id_type sibling_;
	uchar_type label_;
	bool is_state_;
	bool has_sibling_;

	// Copyable.
};

inline id_type DawgNode::unit() const
{
	if (label_ == '\0')
		return (child_ << 1) | (has_sibling_ ? 1 : 0);
	return (child_ << 2) | (is_state_ ? 2 : 0) | (has_sibling_ ? 1 : 0);
}

//
// Fixed unit of Directed Acyclic Word Graph (DAWG).
//

class DawgUnit
{
public:
	DawgUnit(id_type unit = 0) : unit_(unit) {}
	DawgUnit(const DawgUnit &unit) : unit_(unit.unit_) {}

	DawgUnit &operator=(id_type unit) { unit_ = unit; return *this; }

	id_type unit() const { return unit_; }

	id_type child() const { return unit_ >> 2; }
	bool has_sibling() const { return (unit_ & 1) == 1; }
	value_type value() const { return static_cast<value_type>(unit_ >> 1); }
	bool is_state() const { return (unit_ & 2) == 2; }

private:
	id_type unit_;

	// Copyable.
};

//
// Directed Acyclic Word Graph (DAWG) builder.
//

class DawgBuilder
{
public:
	DawgBuilder() : nodes_(), units_(), labels_(), is_intersections_(),
		table_(), node_stack_(), recycle_bin_(), num_states_(0) {}
	~DawgBuilder() { clear(); }

	id_type root() const { return 0; }

	id_type child(id_type id) const { return units_[id].child(); }
	id_type sibling(id_type id) const
	{ return units_[id].has_sibling() ? id + 1 : 0; }
	int value(id_type id) const { return units_[id].value(); }

	bool is_leaf(id_type id) const { return label(id) == '\0'; }
	uchar_type label(id_type id) const { return labels_[id]; }

	bool is_intersection(id_type id) const { return is_intersections_[id]; }
	id_type intersection_id(id_type id) const
	{ return is_intersections_.rank(id) - 1; }

	std::size_t num_intersections() const
	{ return is_intersections_.num_ones(); }

	std::size_t size() const { return units_.size(); }

	void init();
	void finish();

	void insert(const char *key, std::size_t length, value_type value);

	void clear();

private:
	enum { INITIAL_TABLE_SIZE = 1 << 10 };

	AutoPool<DawgNode> nodes_;
	AutoPool<DawgUnit> units_;
	AutoPool<uchar_type> labels_;
	BitVector is_intersections_;
	AutoPool<id_type> table_;
	AutoStack<id_type> node_stack_;
	AutoStack<id_type> recycle_bin_;
	std::size_t num_states_;

	// Disallows copies.
	DawgBuilder(const DawgBuilder &);
	DawgBuilder &operator=(const DawgBuilder &);

	void flush(id_type id);

	void expand_table();

	id_type find_unit(id_type id, id_type *hash_id) const;
	id_type find_node(id_type node_id, id_type *hash_id) const;

	bool are_equal(id_type node_id, id_type unit_id) const;

	id_type hash_unit(id_type id) const;
	id_type hash_node(id_type id) const;

	id_type append_node();
	id_type append_unit();

	void free_node(id_type id);

	static id_type hash(id_type key);
};

inline void DawgBuilder::init()
{
	table_.resize(INITIAL_TABLE_SIZE, 0);

	append_node();
	append_unit();

	num_states_ = 1;

	nodes_[0].set_label(0xFF);
	node_stack_.push(0);
}

inline void DawgBuilder::finish()
{
	flush(0);

	units_[0] = nodes_[0].unit();
	labels_[0] = nodes_[0].label();

	nodes_.clear();
	table_.clear();
	node_stack_.clear();
	recycle_bin_.clear();

	is_intersections_.build();
}

inline void DawgBuilder::insert(const char *key, std::size_t length,
	value_type value)
{
	if (value < 0)
		DARTS_THROW("failed to insert key: negative value");
	else if (length == 0)
		DARTS_THROW("failed to insert key: zero-length key");

	id_type id = 0;
	std::size_t key_pos = 0;

	for ( ; key_pos <= length; ++key_pos)
	{
		id_type child_id = nodes_[id].child();
		if (child_id == 0)
			break;

		uchar_type key_label = static_cast<uchar_type>(
			(key_pos < length) ? key[key_pos] : '\0');
		uchar_type unit_label = nodes_[child_id].label();

		if (key_label < unit_label)
			DARTS_THROW("failed to insert key: wrong key order");
		else if (key_label > unit_label)
		{
			nodes_[child_id].set_has_sibling(true);
			flush(child_id);
			break;
		}

		id = child_id;
	}

	if (key_pos > length)
		return;

	for ( ; key_pos <= length; ++key_pos)
	{
		uchar_type key_label = static_cast<uchar_type>(
			(key_pos < length) ? key[key_pos] : '\0');
		id_type child_id = append_node();

		if (nodes_[id].child() == 0)
			nodes_[child_id].set_is_state(true);
		nodes_[child_id].set_sibling(nodes_[id].child());
		nodes_[child_id].set_label(key_label);
		nodes_[id].set_child(child_id);
		node_stack_.push(child_id);

		id = child_id;
	}
	nodes_[id].set_value(value);
}

inline void DawgBuilder::clear()
{
	nodes_.clear();
	units_.clear();
	labels_.clear();
	is_intersections_.clear();
	table_.clear();
	node_stack_.clear();
	recycle_bin_.clear();
	num_states_ = 0;
}

inline void DawgBuilder::flush(id_type id)
{
	while (node_stack_.top() != id)
	{
		id_type node_id = node_stack_.top();
		node_stack_.pop();

		if (num_states_ >= table_.size() - (table_.size() >> 2))
			expand_table();

		id_type num_siblings = 0;
		for (id_type i = node_id; i != 0; i = nodes_[i].sibling())
			++num_siblings;

		id_type hash_id;
		id_type match_id = find_node(node_id, &hash_id);
		if (match_id != 0)
			is_intersections_.set(match_id, true);
		else
		{
			id_type unit_id = 0;
			for (id_type i = 0; i < num_siblings; ++i)
				unit_id = append_unit();
			for (id_type i = node_id; i != 0; i = nodes_[i].sibling())
			{
				units_[unit_id] = nodes_[i].unit();
				labels_[unit_id] = nodes_[i].label();
				--unit_id;
			}
			match_id = unit_id + 1;
			table_[hash_id] = match_id;
			++num_states_;
		}

		for (id_type i = node_id, next; i != 0; i = next)
		{
			next = nodes_[i].sibling();
			free_node(i);
		}

		nodes_[node_stack_.top()].set_child(match_id);
	}
	node_stack_.pop();
}

inline void DawgBuilder::expand_table()
{
	std::size_t table_size = table_.size() << 1;
	table_.clear();
	table_.resize(table_size, 0);

	for (std::size_t i = 1; i < units_.size(); ++i)
	{
		id_type id = static_cast<id_type>(i);
		if (labels_[id] == '\0' || units_[id].is_state())
		{
			id_type hash_id;
			find_unit(id, &hash_id);
			table_[hash_id] = id;
		}
	}
}

inline id_type DawgBuilder::find_unit(id_type id, id_type *hash_id) const
{
	*hash_id = hash_unit(id) % table_.size();
	for ( ; ; *hash_id = (*hash_id + 1) % table_.size())
	{
		id_type unit_id = table_[*hash_id];
		if (unit_id == 0)
			break;

		// There must not be the same unit.
	}
	return 0;
}

inline id_type DawgBuilder::find_node(id_type node_id, id_type *hash_id) const
{
	*hash_id = hash_node(node_id) % table_.size();
	for ( ; ; *hash_id = (*hash_id + 1) % table_.size())
	{
		id_type unit_id = table_[*hash_id];
		if (unit_id == 0)
			break;

		if (are_equal(node_id, unit_id))
			return unit_id;
	}
	return 0;
}

inline bool DawgBuilder::are_equal(id_type node_id, id_type unit_id) const
{
	for (id_type i = nodes_[node_id].sibling(); i != 0;
		i = nodes_[i].sibling())
	{
		if (units_[unit_id].has_sibling() == false)
			return false;
		++unit_id;
	}
	if (units_[unit_id].has_sibling() == true)
		return false;

	for (id_type i = node_id; i != 0; i = nodes_[i].sibling(), --unit_id)
	{
		if (nodes_[i].unit() != units_[unit_id].unit() ||
			nodes_[i].label() != labels_[unit_id])
			return false;
	}
	return true;
}

inline id_type DawgBuilder::hash_unit(id_type id) const
{
	id_type hash_value = 0;
	for ( ; id != 0; ++id)
	{
		id_type unit = units_[id].unit();
		uchar_type label = labels_[id];
		hash_value ^= hash((label << 24) ^ unit);

		if (units_[id].has_sibling() == false)
			break;
	}
	return hash_value;
}

inline id_type DawgBuilder::hash_node(id_type id) const
{
	id_type hash_value = 0;
	for ( ; id != 0; id = nodes_[id].sibling())
	{
		id_type unit = nodes_[id].unit();
		uchar_type label = nodes_[id].label();
		hash_value ^= hash((label << 24) ^ unit);
	}
	return hash_value;
}

inline id_type DawgBuilder::append_unit()
{
	is_intersections_.append();
	units_.append();
	labels_.append();

	return static_cast<id_type>(is_intersections_.size() - 1);
}

inline id_type DawgBuilder::append_node()
{
	id_type id;
	if (recycle_bin_.empty())
	{
		id = static_cast<id_type>(nodes_.size());
		nodes_.append();
	}
	else
	{
		id = recycle_bin_.top();
		nodes_[id] = DawgNode();
		recycle_bin_.pop();
	}
	return id;
}

inline void DawgBuilder::free_node(id_type id)
{
	recycle_bin_.push(id);
}

// 32-bit mix function.
// http://www.concentric.net/~Ttwang/tech/inthash.htm
inline id_type DawgBuilder::hash(id_type key)
{
	key = ~key + (key << 15);  // key = (key << 15) - key - 1;
	key = key ^ (key >> 12);
	key = key + (key << 2);
	key = key ^ (key >> 4);
	key = key * 2057;  // key = (key + (key << 3)) + (key << 11);
	key = key ^ (key >> 16);
	return key;
}

//
// Unit of double-array builder.
//

class DoubleArrayBuilderUnit
{
public:
	DoubleArrayBuilderUnit() : unit_(0) {}

	void set_has_leaf(bool has_leaf);
	void set_value(value_type value) { unit_ = value | (1U << 31); }
	void set_label(uchar_type label) { unit_ = (unit_ & ~0xFFU) | label; }
	void set_offset(id_type offset);

private:
	id_type unit_;

	// Copyable.
};

inline void DoubleArrayBuilderUnit::set_has_leaf(bool has_leaf)
{
	if (has_leaf)
		unit_ |= 1U << 8;
	else
		unit_ &= ~(1U << 8);
}

inline void DoubleArrayBuilderUnit::set_offset(id_type offset)
{
	if (offset >= 1U << 29)
		DARTS_THROW("failed to modify unit: too large offset");

	unit_ &= (1U << 31) | (1U << 8) | 0xFF;
	if (offset < 1U << 21)
		unit_ |= (offset << 10);
	else
		unit_ |= (offset << 2) | (1U << 9);
}

//
// Extra unit of double-array builder.
//

class DoubleArrayBuilderExtraUnit
{
public:
	DoubleArrayBuilderExtraUnit() : prev_(0), next_(0),
		is_fixed_(false), is_used_(false) {}

	void set_prev(id_type prev) { prev_ = prev; }
	void set_next(id_type next) { next_ = next; }
	void set_is_fixed(bool is_fixed) { is_fixed_ = is_fixed; }
	void set_is_used(bool is_used) { is_used_ = is_used; }

	id_type prev() const { return prev_; }
	id_type next() const { return next_; }
	bool is_fixed() const { return is_fixed_; }
	bool is_used() const { return is_used_; }

private:
	id_type prev_;
	id_type next_;
	bool is_fixed_;
	bool is_used_;

	// Copyable.
};

//
// DAWG -> double-array converter.
//

class DoubleArrayBuilder
{
public:
	DoubleArrayBuilder() : units_(), extras_(), labels_(),
		table_(), extras_head_(0) {}
	~DoubleArrayBuilder() { clear(); }

	void build(const DawgBuilder &dawg);
	void copy(std::size_t *size_ptr, DoubleArrayUnit **buf_ptr) const;

	void clear();

private:
	enum { BLOCK_SIZE = 256 };
	enum { NUM_EXTRA_BLOCKS = 16 };
	enum { NUM_EXTRAS = BLOCK_SIZE * NUM_EXTRA_BLOCKS };

	enum { UPPER_MASK = 0xFF << 21 };
	enum { LOWER_MASK = 0xFF };

	typedef DoubleArrayBuilderUnit unit_type;
	typedef DoubleArrayBuilderExtraUnit extra_type;

	AutoPool<unit_type> units_;
	AutoArray<extra_type> extras_;
	AutoPool<uchar_type> labels_;
	AutoArray<id_type> table_;
	id_type extras_head_;

	// Disallows copies.
	DoubleArrayBuilder(const DoubleArrayBuilder &);
	DoubleArrayBuilder &operator=(const DoubleArrayBuilder &);

	std::size_t num_blocks() const { return units_.size() / BLOCK_SIZE; }

	const extra_type &extras(id_type id) const
	{ return extras_[id % NUM_EXTRAS]; }
	extra_type &extras(id_type id) { return extras_[id % NUM_EXTRAS]; }

	bool build_double_array(const DawgBuilder &dawg,
		id_type dawg_id, id_type dic_id);

	id_type arrange_children(const DawgBuilder &dawg,
		id_type dawg_id, id_type dic_id);

	id_type find_valid_offset(id_type id) const;
	bool is_valid_offset(id_type id, id_type offset) const;

	void reserve_id(id_type id);
	void expand_units();

	void fix_all_blocks();
	void fix_block(id_type block_id);
};

inline void DoubleArrayBuilder::build(const DawgBuilder &dawg)
{
	std::size_t num_units = 1;
	while (num_units < dawg.size())
		num_units <<= 1;
	units_.reserve(num_units);

	table_.reset(new id_type[dawg.num_intersections()]);
	for (std::size_t i = 0; i < dawg.num_intersections(); ++i)
		table_[i] = 0;

	extras_.reset(new extra_type[NUM_EXTRAS]);

	reserve_id(0);
	extras(0).set_is_used(true);
	units_[0].set_offset(1);
	units_[0].set_label('\0');

	if (dawg.child(dawg.root()) != 0)
		build_double_array(dawg, dawg.root(), 0);

	fix_all_blocks();

	extras_.clear();
	labels_.clear();
	table_.clear();
}

inline void DoubleArrayBuilder::copy(std::size_t *size_ptr,
	DoubleArrayUnit **buf_ptr) const
{
	if (size_ptr != NULL)
		*size_ptr = units_.size();
	if (buf_ptr != NULL)
	{
		*buf_ptr = new DoubleArrayUnit[units_.size()];
		unit_type *units = reinterpret_cast<unit_type *>(*buf_ptr);
		for (std::size_t i = 0; i < units_.size(); ++i)
			units[i] = units_[i];
	}
}

inline void DoubleArrayBuilder::clear()
{
	units_.clear();
	extras_.clear();
	labels_.clear();
	table_.clear();
	extras_head_ = 0;
}

inline bool DoubleArrayBuilder::build_double_array(const DawgBuilder &dawg,
	id_type dawg_id, id_type dic_id)
{
	if (dawg.is_leaf(dawg_id))
		return true;

	id_type dawg_child_id = dawg.child(dawg_id);
	if (dawg.is_intersection(dawg_child_id))
	{
		id_type intersection_id = dawg.intersection_id(dawg_child_id);
		id_type offset = table_[intersection_id];
		if (offset != 0)
		{
			offset ^= dic_id;
			if (!(offset & UPPER_MASK) || !(offset & LOWER_MASK))
			{
				if (dawg.is_leaf(dawg_child_id))
					units_[dic_id].set_has_leaf(true);
				units_[dic_id].set_offset(offset);
				return true;
			}
		}
	}

	id_type offset = arrange_children(dawg, dawg_id, dic_id);
	if (offset == 0)
		return false;

	if (dawg.is_intersection(dawg_child_id))
		table_[dawg.intersection_id(dawg_child_id)] = offset;

	do
	{
		id_type dic_child_id = offset ^ dawg.label(dawg_child_id);
		if (!build_double_array(dawg, dawg_child_id, dic_child_id))
			return false;
		dawg_child_id = dawg.sibling(dawg_child_id);
	} while (dawg_child_id != 0);

	return true;
}

inline id_type DoubleArrayBuilder::arrange_children(const DawgBuilder &dawg,
	id_type dawg_id, id_type dic_id)
{
	labels_.clear();

	id_type dawg_child_id = dawg.child(dawg_id);
	while (dawg_child_id != 0)
	{
		labels_.append(dawg.label(dawg_child_id));
		dawg_child_id = dawg.sibling(dawg_child_id);
	}

	id_type offset = find_valid_offset(dic_id);
	units_[dic_id].set_offset(dic_id ^ offset);

	dawg_child_id = dawg.child(dawg_id);
	for (std::size_t i = 0; i < labels_.size(); ++i)
	{
		id_type dic_child_id = offset ^ labels_[i];
		reserve_id(dic_child_id);

		if (dawg.is_leaf(dawg_child_id))
		{
			units_[dic_id].set_has_leaf(true);
			units_[dic_child_id].set_value(dawg.value(dawg_child_id));
		}
		else
			units_[dic_child_id].set_label(labels_[i]);

		dawg_child_id = dawg.sibling(dawg_child_id);
	}
	extras(offset).set_is_used(true);

	return offset;
}

inline id_type DoubleArrayBuilder::find_valid_offset(id_type id) const
{
	if (extras_head_ >= units_.size())
		return units_.size() | (id & LOWER_MASK);

	id_type unfixed_id = extras_head_;
	do
	{
		id_type offset = unfixed_id ^ labels_[0];
		if (is_valid_offset(id, offset))
			return offset;
		unfixed_id = extras(unfixed_id).next();
	} while (unfixed_id != extras_head_);

	return units_.size() | (id & LOWER_MASK);
}

inline bool DoubleArrayBuilder::is_valid_offset(id_type id,
	id_type offset) const
{
	if (extras(offset).is_used())
		return false;

	id_type rel_offset = id ^ offset;
	if ((rel_offset & LOWER_MASK) && (rel_offset & UPPER_MASK))
		return false;

	for (std::size_t i = 1; i < labels_.size(); ++i)
	{
		if (extras(offset ^ labels_[i]).is_fixed())
			return false;
	}

	return true;
}

inline void DoubleArrayBuilder::reserve_id(id_type id)
{
	if (id >= units_.size())
		expand_units();

	if (id == extras_head_)
	{
		extras_head_ = extras(id).next();
		if (extras_head_ == id)
			extras_head_ = units_.size();
	}
	extras(extras(id).prev()).set_next(extras(id).next());
	extras(extras(id).next()).set_prev(extras(id).prev());
	extras(id).set_is_fixed(true);
}

inline void DoubleArrayBuilder::expand_units()
{
	id_type src_num_units = units_.size();
	id_type src_num_blocks = num_blocks();

	id_type dest_num_units = src_num_units + BLOCK_SIZE;
	id_type dest_num_blocks = src_num_blocks + 1;

	if (dest_num_blocks > NUM_EXTRA_BLOCKS)
		fix_block(src_num_blocks - NUM_EXTRA_BLOCKS);

	units_.resize(dest_num_units);

	if (dest_num_blocks > NUM_EXTRA_BLOCKS)
	{
		for (std::size_t id = src_num_units; id < dest_num_units; ++id)
		{
			extras(id).set_is_used(false);
			extras(id).set_is_fixed(false);
		}
	}

	for (id_type i = src_num_units + 1; i < dest_num_units; ++i)
	{
		extras(i - 1).set_next(i);
		extras(i).set_prev(i - 1);
	}

	extras(src_num_units).set_prev(dest_num_units - 1);
	extras(dest_num_units - 1).set_next(src_num_units);

	extras(src_num_units).set_prev(extras(extras_head_).prev());
	extras(dest_num_units - 1).set_next(extras_head_);

	extras(extras(extras_head_).prev()).set_next(src_num_units);
	extras(extras_head_).set_prev(dest_num_units - 1);
}

inline void DoubleArrayBuilder::fix_all_blocks()
{
	id_type begin = 0;
	if (num_blocks() > NUM_EXTRA_BLOCKS)
		begin = num_blocks() - NUM_EXTRA_BLOCKS;
	id_type end = num_blocks();

	for (id_type block_id = begin; block_id != end; ++block_id)
		fix_block(block_id);
}

inline void DoubleArrayBuilder::fix_block(id_type block_id)
{
	id_type begin = block_id * BLOCK_SIZE;
	id_type end = begin + BLOCK_SIZE;

	id_type unused_offset = 0;
	for (id_type offset = begin; offset != end; ++offset)
	{
		if (!extras(offset).is_used())
		{
			unused_offset = offset;
			break;
		}
	}

	for (id_type id = begin; id != end; ++id)
	{
		if (!extras(id).is_fixed())
		{
			reserve_id(id);
			units_[id].set_label(static_cast<uchar_type>(id ^ unused_offset));
		}
	}
}

}  // namespace Details

//
// Member function build() of DoubleArrayImpl.
//

template <typename A, typename B, typename T, typename C>
int DoubleArrayImpl<A, B, T, C>::build(std::size_t num_keys,
	const key_type * const *keys, const std::size_t *lengths,
	const value_type *values, int (*progress_func)(std::size_t, std::size_t))
{
	Details::DawgBuilder dawg_builder;

	dawg_builder.init();
	for (std::size_t i = 0; i < num_keys; ++i)
	{
		std::size_t length = 0;
		if (lengths != NULL)
			length = lengths[i];
		else
		{
			while (keys[i][length] != '\0')
				++length;
		}

		dawg_builder.insert(keys[i], length,
			(values != NULL) ? static_cast<int>(values[i]) : i);

		if (progress_func != NULL)
			progress_func(i + 1, num_keys + 1);
	}
	dawg_builder.finish();

	std::size_t size = 0;
	unit_type *buf = NULL;

	Details::DoubleArrayBuilder double_array_builder;
	double_array_builder.build(dawg_builder);

	dawg_builder.clear();
	double_array_builder.copy(&size, &buf);

	clear();

	size_ = size;
	array_ = buf;
	buf_ = buf;

	if (progress_func != NULL)
		progress_func(num_keys + 1, num_keys + 1);

	return 0;
}

}  // namespace Darts

#undef DARTS_INT_TO_STR
#undef DARTS_LINE_TO_STR
#undef DARTS_LINE_STR
#undef DARTS_THROW

#endif  // DARTS_H_