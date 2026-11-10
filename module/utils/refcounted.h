//=====================================================================
//
// refcounted.h - Intrusive reference counted base class
// skywind3000 (at) gmail.com, 2019
//
// Last Modified: 2019/01/28 15:51:00
//
// A intrusive reference counted object is one that contains
// its own reference count and manages its own lifetime.
// 
// Why this over std::shared_ptr?
//
// - Intrusive: the refcount lives inside the object, so there is no
//   separate control block allocation and no extra pointer indirection.
// - Cache friendly: fewer allocations and tighter data improve
//   performance on hot code paths in a renderer/engine.
// - Deterministic: ownership rules stay explicit (Acquire/Release), and
//   SharedPtr is a thin RAII wrapper with predictable behavior.
// - Thread-safe refs: atomic refcounting without the heavier machinery
//   that shared_ptr often drags in (weak counts, custom deleters, etc.).
//
// Use it when you need lightweight, high-frequency shared ownership and 
// the objects already agree to carry their own lifetime metadata.
//
//=====================================================================
#ifndef _REFERENCED_COUNTED_H_
#define _REFERENCED_COUNTED_H_

#include <stddef.h>
#include <assert.h>
#include <atomic>
#include <type_traits>
#include <utility>

#ifndef __cplusplus
#error "This header requires C++"
#endif


namespace System {

//---------------------------------------------------------------------
// IReferencedCounted
//---------------------------------------------------------------------
class IReferenceCounted {
public:
	IReferenceCounted(): _DebugName(nullptr), _ReferenceCounter(1) {}

	void Acquire() const {
		_ReferenceCounter.fetch_add(1, std::memory_order_acquire);
	}

	void Release() const {
		const int previous = _ReferenceCounter.fetch_sub(1, std::memory_order_acq_rel);
		assert(previous > 0);
		if (previous == 1) {
			delete this;
		}
	}

	int GetReferenceCount() const {
		return _ReferenceCounter.load(std::memory_order_acquire);
	}

	const char *GetDebugName() const {
		return _DebugName;
	}

	void SetDebugName(const char *name) {
		_DebugName = name;
	}

protected:
	IReferenceCounted(const IReferenceCounted&) = delete;
	IReferenceCounted& operator=(const IReferenceCounted&) = delete;
	IReferenceCounted(IReferenceCounted&&) = delete;
	IReferenceCounted& operator=(IReferenceCounted&&) = delete;

	virtual ~IReferenceCounted() {
		assert(_ReferenceCounter.load(std::memory_order_acquire) == 0);
	}
private:
	const char *_DebugName;
	mutable std::atomic<int> _ReferenceCounter;
};



//---------------------------------------------------------------------
// SharedPtr for IReferenceCounted objects
//
// - Caller owns the ref, e.g., just new'ed, so use Adopt:
//
//     SharedPtr<T> sp = SharedPtr<T>::Adopt(new T()); 
//     assert(sp->GetReferenceCount() == 1);
//
// - Caller does not own the ref, so normal ctor:
//
//     SharedPtr<T> sp2(raw_ptr);   // refcount +1 here
//     assert(sp2->GetReferenceCount() == 2); 
//
// - Copying shares ownership and AddRefs:
//
//     SharedPtr<T> sp3 = sp2;      // copy ctor, AddRef
//     assert(sp3->GetReferenceCount() == 3);
//
// - Moving takes ownership:
//
//     SharedPtr<T> sp4 = std::move(sp3); // move ctor, take ownership
//     assert(sp4->GetReferenceCount() == 3);
//
//---------------------------------------------------------------------
template <typename T>
class SharedPtr final {
private:
	T* _ptr;
	template <typename> friend class SharedPtr;

	// general ctor: 
	// - adopted is true: use immediately after new when caller already owns the ref
	// - adopted is false: use when the raw pointer is still owned elsewhere
	explicit SharedPtr(T* ptr, bool adopted): _ptr(ptr) {
		if (!adopted) { if (_ptr) _ptr->Acquire(); }
	}

public:
	static_assert(std::is_base_of<IReferenceCounted, T>::value, "SharedPtr requires IReferenceCounted derived type");
	// Note: individual SharedPtr instances are not thread-safe; synchronize concurrent mutation of the same SharedPtr.

	// dtor: Release the owned reference
	~SharedPtr() { if (_ptr) _ptr->Release(); }

	// Default ctor: null pointer
	SharedPtr(): _ptr(nullptr) {}

	// nullptr ctor for clarity
	explicit SharedPtr(std::nullptr_t): _ptr(nullptr) {}

	// AddRef-taking ctor: use when the raw pointer is still owned elsewhere
	explicit SharedPtr(T* ptr): SharedPtr(ptr, false) {}

	// Adopt: caller already owns the ref (e.g., freshly new)
	// usage: SharedPtr<T> sp = SharedPtr<T>::Adopt(new T());
	static SharedPtr<T> Adopt(T* ptr) {
		return SharedPtr<T>(ptr, true);
	}

	// Copying shares ownership and AddRefs the target
	SharedPtr(const SharedPtr<T>& other): _ptr(nullptr) {
		Reset(other._ptr);
	}

	// Converting copy ctor for compatible types
	template <typename U, typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
	SharedPtr(const SharedPtr<U>& other): _ptr(nullptr) {
		Reset(other.Get());
	}

	// Move ctor: takes ownership
	SharedPtr(SharedPtr<T>&& other) noexcept: _ptr(other._ptr) {
		other._ptr = nullptr;
	}

	// Converting move ctor for compatible types
	template <typename U, typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
	SharedPtr(SharedPtr<U>&& other) noexcept: _ptr(other._ptr) {
		other._ptr = nullptr;
	}

	// Assignment operators
	SharedPtr<T>& operator = (const SharedPtr<T>& other) {
		if (this != &other) {
			Reset(other._ptr);
		}
		return *this;
	}

	// Converting copy assignment for compatible types
	template <typename U, typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
	SharedPtr<T>& operator = (const SharedPtr<U>& other) {
		Reset(other.Get());
		return *this;
	}

	// Move assignment: takes ownership
	SharedPtr<T>& operator = (SharedPtr<T>&& other) noexcept {
		if (this != &other) {
			if (_ptr) _ptr->Release();
			_ptr = other._ptr;
			other._ptr = nullptr;
		}
		return *this;
	}

	// Converting move assignment for compatible types
	template <typename U, typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
	SharedPtr<T>& operator = (SharedPtr<U>&& other) noexcept {
		if (reinterpret_cast<void*>(this) != reinterpret_cast<void*>(&other)) {
			if (_ptr) _ptr->Release();
			_ptr = other._ptr;
			other._ptr = nullptr;
		}
		return *this;
	}

	// Assignment from raw pointer
	SharedPtr<T>& operator = (T *ptr) {
		Reset(ptr);
		return *this;
	}

	// Assignment from nullptr
	SharedPtr<T>& operator = (std::nullptr_t) {
		Reset(nullptr);
		return *this;
	}

	// Reset to a new raw pointer, AddRefs the new one
	void Reset(T* ptr) {
		if (_ptr != ptr) {
			if (ptr) ptr->Acquire();
			if (_ptr) _ptr->Release();
			_ptr = ptr;
		}
	}

	// Reset to a new raw pointer, assumes ownership (no AddRef)
	void Swap(SharedPtr<T>& other) noexcept {
		std::swap(_ptr, other._ptr);
	}

	T* operator -> () { return _ptr; }
	const T* operator -> () const { return _ptr; }

	T& operator * () { return *_ptr; }
	const T& operator * () const { return *_ptr; }

	T* Get() { return _ptr; }
	const T* Get() const { return _ptr; }

	int GetReferenceCount() const {
		if (_ptr) return _ptr->GetReferenceCount();
		return 0;
	}

	bool Available() const {
		return (_ptr != nullptr);
	}

	// Equality comparisons
	template <typename U>
	bool operator == (const SharedPtr<U>& other) const {
		return _ptr == other.Get();
	}

	bool operator == (std::nullptr_t) const {
		return _ptr == nullptr;
	}

	template <typename U>
	bool operator != (const SharedPtr<U>& other) const {
		return _ptr != other.Get();
	}

	bool operator != (std::nullptr_t) const {
		return _ptr != nullptr;
	}
};


};


#endif



