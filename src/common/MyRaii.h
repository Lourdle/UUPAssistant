#pragma once

#include <memory>

template<typename T>
struct MyUniqueBuffer : std::unique_ptr<std::remove_pointer_t<T>, void(*)(void*)>
{
	using unique_ptr_t = std::unique_ptr<std::remove_pointer_t<T>, void(*)(void*)>;

	MyUniqueBuffer(size_t size) : unique_ptr_t(static_cast<T>(malloc(size)), free) {}
	MyUniqueBuffer() : unique_ptr_t(nullptr, free) {}

	operator T() { return this->get(); }
	void reset(size_t size)
	{
		unique_ptr_t::reset(static_cast<T>(malloc(size)));
	}
};

template<typename T>
struct MyUniquePtr : std::unique_ptr<T[]>
{
	using unique_ptr_t = std::unique_ptr<T[]>;

	MyUniquePtr(size_t count) : unique_ptr_t(new T[count]) {}
	MyUniquePtr() : unique_ptr_t(nullptr) {}

	operator T* () { return this->get(); }
	void reset(size_t count)
	{
		unique_ptr_t::reset(new T[count]);
	}
};
