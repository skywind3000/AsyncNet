//=====================================================================
//
// ServiceRegistry.h - type-keyed service locator (service registry)
//
// Purpose:
//
// Stores at most one instance per C++ type and hands it out on
// demand, replacing hand-rolled singletons and global pointers.
//
// Two built-in scopes are provided:
//   - GetInstance():          process-wide registry
//   - GetThreadLocalInstance(): per-thread private registry
//
// Key properties:
//   - lazy creation: Get<T>() constructs T on first use (via
//     ServiceTraits<T>::Create) and returns the same instance after
//   - LIFO destruction: Clear() destroys services in reverse
//     creation order, so a service's dependencies are still alive
//     while its destructor runs
//   - ownership control: Install() can take ownership or just
//     borrow the caller's instance
//   - thread safe; a service's constructor may Get() other services
//     reentrantly (circular dependencies are detected and rejected)
//
// Simple usage:
//
//     // plain service: default constructed on first Get()
//     struct Logger { void Log(const char *msg); };
//
//     // service needing the registry: specialize ServiceTraits
//     struct Config {
//         explicit Config(ServiceRegistry &reg);
//     };
//     template<> struct ServiceTraits<Config> {
//         static Config* Create(ServiceRegistry &reg) {
//             return new Config(reg);
//         }
//     };
//
//     ServiceRegistry &reg = ServiceRegistry::GetInstance();
//     reg.Get<Logger>().Log("hello");     // created on first use
//     Logger *p = reg.Query<Logger>();    // NULL if not installed
//
//     // external instance: ownership=false (default) borrows it,
//     // ownership=true makes the registry delete it on Clear()
//     reg.Install<Config>(new Config(reg), true);
//
//     // convenience helpers for the two built-in scopes: the bare
//     // name is the process-wide default, ThreadLocal the per-thread
//     // variant (mirrors GetInstance/GetThreadLocalInstance)
//     GetService<Logger>().Log("process-wide");
//     GetThreadLocalService<Logger>().Log("per-thread");
//
// Self-contained: depends on the C++ standard library only.
//
//=====================================================================
#ifndef _SERVICE_REGISTRY_H_
#define _SERVICE_REGISTRY_H_

#include <stddef.h>

#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>
#include <stdexcept>

#ifndef __cplusplus
#error "This header requires C++"
#endif

#ifndef NAMESPACE_BEGIN
#define NAMESPACE_BEGIN(name) namespace name {
#endif

#ifndef NAMESPACE_END
#define NAMESPACE_END(name) }
#endif


//---------------------------------------------------------------------
// namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// pre-declare
//---------------------------------------------------------------------
class ServiceRegistry;


//---------------------------------------------------------------------
// ServiceTraits - default traits for service class
//---------------------------------------------------------------------
template <typename T>
struct ServiceTraits {
	static T* Create(ServiceRegistry&) { return new T(); }
};


//---------------------------------------------------------------------
// ServiceRegistry
//---------------------------------------------------------------------
class ServiceRegistry final
{
public:
	// non-virtual on purpose: the class is final, nobody can delete
	// it through a base pointer, so a vptr would be pure overhead
	~ServiceRegistry();
	explicit ServiceRegistry(): _busy(0), _closing(false),
		_closed(false), _ptr(NULL) {}

	// non-copyable and non-movable: the registry is a service locator
	// whose address is held by its users (along with the raw service
	// pointers they cached), moving would silently empty the source and
	// invite lock-ordering deadlocks between two registries
	ServiceRegistry(ServiceRegistry &&) = delete;
	ServiceRegistry& operator=(ServiceRegistry &&) = delete;

	ServiceRegistry(const ServiceRegistry &) = delete;
	ServiceRegistry& operator=(const ServiceRegistry &) = delete;

public:

	// get service by type T, create if not exist
	// if create failed, throw std::runtime_error
	//
	// T's constructor may Get()/Query() other services through the same
	// registry (they will be created on demand and destroyed before T by
	// the LIFO order of Clear()); getting T itself from T's constructor
	// is a circular dependency and is rejected with std::runtime_error
	// instead of recursing to stack overflow
	//
	// T's constructor runs while the registry lock is held: every other
	// call on this registry (from any thread) blocks until it returns,
	// so a slow constructor stalls them all, and the constructor must
	// never wait for another thread that uses the same registry, or
	// both will deadlock; the same constraint applies to destructors
	// invoked by Clear() or by an Install() replacement
	//
	// the returned reference is valid until the service is destroyed
	// (by Clear(), by an Install() replacement, or with the registry):
	// once this call returns, another thread's Clear()/Install() can
	// invalidate it at any moment, so concurrent users must not hold
	// the reference across such calls (Query again and check NULL)
	template <typename T>
	T& Get() {
		const std::string &key = KeyOf<T>();
		std::lock_guard<std::recursive_mutex> lock(_lock);
		void *ptr = _GetService(key);
		if (ptr != NULL) return *(T*)ptr;
		if (_busy || _closing) {
			throw std::runtime_error("ServiceRegistry is busy/closing");
		}
		if (_closed) {
			throw std::runtime_error("ServiceRegistry is closed");
		}
		// reserve the key before construction, so that a reentrant Get<T>()
		// from inside T's constructor can be detected as circular dependency
		if (_ReserveService(key) == false) {
			throw std::runtime_error("ServiceRegistry: circular service dependency");
		}
		T *obj = NULL;
		try {
			obj = ServiceTraits<T>::Create(*this);
			if (obj == NULL) {
				throw std::runtime_error("ServiceRegistry: service create failed");
			}
			if (_InstallService(key, (void*)obj, Deleter<T>) == false) {
				// registry refused the entry (closing): destroy the object
				// locally to avoid leaking it
				Deleter<T>((void*)obj);
				obj = NULL;
				throw std::runtime_error("ServiceRegistry is busy");
			}
		}
		catch (...) {
			// the object was created but never installed (or installing
			// threw): destroy it locally, then roll back the reservation
			if (obj != NULL) Deleter<T>((void*)obj);
			_ReleaseService(key);
			throw;
		}
		return *obj;
	}

	// install service instance, ownership=true means ServiceRegistry 
	// will delete it when Clear() is called or ServiceRegistry is destroyed
	// installing over an existing service replaces (and destroys) it if
	// the old one is owned by the registry
	//
	// reinstalling the same pointer is an idempotent no-op while the
	// ownership flag stays the same, but flipping it (borrowed -> owned
	// or the reverse) is rejected with false: ownership of a live entry
	// is sticky, so Clear() can never delete an object the caller still
	// owns, nor leak one it handed over
	template <typename T>
	bool Install(T* obj, bool ownership = false) {
		const std::string &key = KeyOf<T>();
		std::lock_guard<std::recursive_mutex> lock(_lock);
		if (_busy || _closing) return false;
		if (_closed) return false;
		return _InstallService(key, (void*)obj, ownership? Deleter<T> : NULL);
	}

	// query service by type T, return NULL if not exist
	// this function is thread safe and can be called during closing/busy
	// (a service being destroyed by Clear() is already removed from the
	// table, so its own destructor sees NULL instead of a dangling this)
	//
	// the returned pointer is only valid while the entry is installed:
	// another thread's Clear()/Install() can invalidate it right after
	// the call returns
	template <typename T>
	T* Query() const {
		const std::string &key = KeyOf<T>();
		std::lock_guard<std::recursive_mutex> lock(_lock);
		void *ptr = _GetService(key);
		return (ptr == NULL)? NULL : (T*)ptr;
	}

	// prewarm service, create it if not exist
	template <typename T> void Prewarm() { Get<T>(); }

	// destroy all services in LIFO order; destructors run while the
	// registry lock is held (see Get() for the blocking constraints)
	void Clear();

	// get/set arbitrary user pointer, not used by the registry itself
	inline void *Ptr() { return _ptr; }
	inline const void *Ptr() const { return _ptr; }
	inline void Ptr(void *ptr) { _ptr = ptr; }

public:

	// process-wide singleton instance: constructed on first use and
	// destroyed during the normal static-destruction sequence at
	// process exit (no leak). Do not touch it from threads still
	// running after main() returned, and call Clear() explicitly
	// before exiting if its services depend on other globals and need
	// a controlled shutdown order
	static ServiceRegistry& GetInstance();

	// per-thread instance: services created in a thread are private to
	// it and are destroyed when that thread exits; during that teardown
	// reentrant Get()/Install() on the dying registry are refused
	// (_closing/_busy) instead of resurrecting services (on MinGW32 the
	// instance is kept in a pthread key as an emulated-TLS workaround:
	// there a reentrant GetThreadLocalInstance() during teardown sees an
	// empty slot and creates a fresh registry that winpthread destroys
	// on a later destructor iteration, and the main-thread instance is
	// released by the OS at process exit without running
	// ~ServiceRegistry, leaking its allocation (the final thread never
	// receives DLL_THREAD_DETACH, the memory is reclaimed by the OS)
	// — call Clear() before exiting if main-thread
	// services need deterministic destruction)
	static ServiceRegistry& GetThreadLocalInstance();

protected:
	// deleter signature must match ServiceEntry::dtor (void*), so the
	// template casts back to T* before deleting
	template <typename T> static void Deleter(void *service) { delete (T*)service; }
	template <typename T> static const std::string& KeyOf() {
	#if defined(__GNUC__) || defined(__clang__)
		static const std::string key = std::string(".svc:") + __PRETTY_FUNCTION__;
	#elif defined(_MSC_VER)
		static const std::string key = std::string(".svc:") + __FUNCSIG__;
	#elif SERVICE_REGISTRY_RTTI
		static const std::string key = std::string(".svc:") + typeid(T).name();
	#else
		static const int sid = 0;
		static const std::string key = std::string(".svc:") + std::to_string((size_t)&sid);
	#endif
		return key;
	}

protected:

	// lookup installed service by key, return NULL if not installed or
	// only reserved (still under construction)
	void *_GetService(const std::string &key) const;

	// reserve the key for a service under construction, return false if
	// the key is already installed or reserved (circular dependency)
	bool _ReserveService(const std::string &key);

	// drop an unfinished reservation, keep installed entries untouched
	void _ReleaseService(const std::string &key);

	bool _RemoveService(const std::string &key);
	bool _InstallService(const std::string &key, void *service, void (*dtor)(void*));
	void _ClearServices();

private:
	struct ServiceEntry { void *service; void (*dtor)(void *); };

	std::unordered_map<std::string, ServiceEntry> _registry;
	std::vector<std::string> _key_index;

	// reentrancy depth counter: nonzero while Clear() is destroying
	// services; a nested Clear() called from a service destructor
	// increments it again, and the registry only becomes usable once
	// the OUTERMOST sweep returns (each level is decremented by an
	// RAII guard, so exceptions cannot wedge the counter)
	int _busy;

	// set as soon as destruction starts (destructor or the thread-local
	// key deleter): Get()/Install() are refused, which stops reentrant
	// calls from resurrecting services inside a dying registry
	bool _closing;

	// set once destruction has completed, Get()/Install() are refused
	bool _closed;

	// arbitrary user ptr
	void *_ptr;

	// single recursive lock: recursive so a service constructor/destructor
	// can call Get()/Query()/Install() on the same registry reentrantly,
	// Clear() takes the same lock so it can never interleave with Get()
	mutable std::recursive_mutex _lock;
};


//---------------------------------------------------------------------
// inline functions
//---------------------------------------------------------------------

// get a service from the process-wide registry, create if not exist
// if create failed, throw std::runtime_error
// this is the default scope; for per-thread services use
// GetThreadLocalService() instead
template <typename T> inline T& GetService() {
	ServiceRegistry &reg = ServiceRegistry::GetInstance();
	return reg.Get<T>();
}

// get a service from the calling thread's private registry, create
// if not exist; if create failed, throw std::runtime_error
template <typename T> inline T& GetThreadLocalService() {
	ServiceRegistry &reg = ServiceRegistry::GetThreadLocalInstance();
	return reg.Get<T>();
}

// query the process-wide registry for a service of type T, return
// NULL if not exist
// this function is thread safe and can be called during busy
template <typename T> inline T* QueryService() {
	ServiceRegistry &reg = ServiceRegistry::GetInstance();
	return reg.Query<T>();
}

// query the calling thread's private registry, return NULL if the
// service does not exist
// this function is thread safe and can be called during busy
template <typename T> inline T* QueryThreadLocalService() {
	ServiceRegistry &reg = ServiceRegistry::GetThreadLocalInstance();
	return reg.Query<T>();
}


//---------------------------------------------------------------------
// namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);


#endif



