//=====================================================================
//
// ServiceRegistry.cpp - 
//
// Last Modified: 2026/08/29 09:40:00
//
//=====================================================================
#include "ServiceRegistry.h"
#include <mutex>
#include <algorithm>

//---------------------------------------------------------------------
// MinGW 32-bit runs with emulated TLS: a thread_local object with a
// destructor runs through the CRT tls_atexit chain on thread exit,
// where the emutls per-thread storage may already have been freed
// (use-after-free, observed as a crash inside pthread_mutex_lock
// called from ~ServiceRegistry). Store the thread-local instance
// behind a pthread key instead: winpthread runs key destructors from
// its own native-TLS bookkeeping, which is unaffected by emutls.
// Other platforms (MSVC, Linux, macOS) keep the plain thread_local
// object; the guard stays 32-bit only because the ordering bug has
// only ever reproduced on the OLD i686 toolchain. Verified with
// scratch/tests/test_emutls_probe.cpp on four toolchains, each
// 3 rounds x 8 threads:
//   i686   MSYS2 gcc 15.2: emutls           -> UAF reproduced
//   i686   MSYS2 gcc 16.2: native PE TLS    -> clean
//   x86_64 MSYS2 gcc 16.2: native PE TLS    -> clean
//   x86_64 MSYS2 gcc 11.2: emutls           -> clean
// emutls alone does not trigger the bug (64-bit emutls is fine); the
// broken destruction order belongs to the old i686 winpthread cleanup
// chain. Whether emutls is used is a toolchain build-time choice (gcc
// configure --enable-emutls), not a per-compile flag: MSYS2 builds
// expose no switch (-femutls/-fno-emutls are unrecognized on both
// i686 gcc 15.2 and 16.2), and __GNUC__ is not a reliable proxy for
// the TLS model either (a custom gcc 16 configured with emutls would
// still emit it), so the workaround conservatively covers all 32-bit
// MinGW builds - it is verified harmless on the new ones. If some
// toolchain ever shows the same crash, extend the guard below.
//
// The key destructor is driven by winpthread's DllMain
// DLL_THREAD_DETACH handler, not by pthread_create bookkeeping, so it
// fires for EVERY exiting thread, including ones spawned with
// CreateThread()/std::thread (verified scratch/tests/
// test_winpthread_key.cpp). The one exception is the final thread of
// the process, which never receives DLL_THREAD_DETACH: the main
// thread's instance is dropped by the OS without a destructor run,
// so that allocation leaks at exit (no detach notice exists for the
// final thread); a leak checker needs a suppression such as
// "leak:ServiceRegistry" on this path. The process-wide registry
// from GetInstance() is different: it is a plain function-local
// static and gets destroyed normally at exit.
// The handler executes under the loader lock, so service destructors
// must not do blocking loader work (LoadLibrary etc).
//---------------------------------------------------------------------
#if defined(__MINGW32__) && !defined(__MINGW64__)
#include <pthread.h>
#define SERVICE_REGISTRY_EMUTLS_WORKAROUND 1
#endif


//---------------------------------------------------------------------
// namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// dtor: mark the registry dying first so reentrant Get()/Install()
// from service destructors are refused instead of resurrecting
// services inside a dying object
//---------------------------------------------------------------------
ServiceRegistry::~ServiceRegistry()
{
	// take the lock before flipping the flags: Get()/Install() read them
	// under the same lock, an unlocked write here would be a data race in
	// the memory-model sense even though racing with destruction is
	// already a caller error; the lock is recursive, so holding it across
	// _ClearServices() is fine
	std::lock_guard<std::recursive_mutex> lock(_lock);
	_closing = true;
	_ClearServices();
	_closed = true;
}


//---------------------------------------------------------------------
// clear
//---------------------------------------------------------------------
void ServiceRegistry::Clear()
{
	_ClearServices();
}


//---------------------------------------------------------------------
// get service ptr
//---------------------------------------------------------------------
void *ServiceRegistry::_GetService(const std::string &key) const
{
	std::lock_guard<std::recursive_mutex> lock(_lock);
	auto it = _registry.find(key);
	if (it != _registry.end()) {
		// a reserved entry (service == NULL) is still under construction
		// and must stay invisible to callers
		return it->second.service;
	}
	return nullptr;
}


//---------------------------------------------------------------------
// reserve key for a service under construction, return false if the
// key is already installed or already reserved (circular dependency)
//---------------------------------------------------------------------
bool ServiceRegistry::_ReserveService(const std::string &key)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);
	auto it = _registry.find(key);
	if (it != _registry.end()) {
		return false;
	}
	ServiceEntry entry;
	entry.service = NULL;
	entry.dtor = NULL;
	_registry[key] = entry;
	return true;
}


//---------------------------------------------------------------------
// drop an unfinished reservation, keep installed entries untouched
//---------------------------------------------------------------------
void ServiceRegistry::_ReleaseService(const std::string &key)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);
	auto it = _registry.find(key);
	if (it != _registry.end() && it->second.service == NULL) {
		_registry.erase(it);
	}
}


//---------------------------------------------------------------------
// remove service by key
//---------------------------------------------------------------------
bool ServiceRegistry::_RemoveService(const std::string &key)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);
	auto it = _registry.find(key);
	if (it == _registry.end()) {
		return false;
	}
	ServiceEntry entry = it->second;
	// remove from the table before destroying, so a destructor querying
	// this key gets NULL instead of a dying object
	_registry.erase(it);
	for (size_t i = 0; i < _key_index.size(); i++) {
		if (_key_index[i] == key) {
			_key_index.erase(_key_index.begin() + i);
			break;
		}
	}
	if (entry.service != NULL && entry.dtor != NULL) {
		try {
			entry.dtor(entry.service);
		}
		catch (...) {
		}
	}
	return true;
}


//---------------------------------------------------------------------
// install service by key, if service is NULL, remove the key
//---------------------------------------------------------------------
bool ServiceRegistry::_InstallService(const std::string &key, void *service, void (*dtor)(void*))
{
	if (service == NULL) {
		return _RemoveService(key);
	}

	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (_busy) {
		return false;
	}
	auto it = _registry.find(key);
	if (it != _registry.end()) {
		ServiceEntry old = it->second;
		if (old.service == service && old.dtor != dtor) {
			// reinstalling the very same instance with a flipped
			// ownership flag: ownership of a live entry is sticky,
			// reject the call instead of silently applying it, or
			// Clear() would delete an object the caller still owns
			// (borrowed upgraded to owned) or leak one it handed over
			// (owned downgraded to borrowed)
			return false;
		}
		it->second.service = service;
		it->second.dtor = dtor;
		if (old.service == NULL) {
			// reservation turning into a real service: construction
			// succeeded, enqueue for LIFO destruction; if push_back
			// throws, revert to the reservation so the map never holds
			// an unindexed (thus never-destroyed) service
			try {
				_key_index.push_back(key);
			}
			catch (...) {
				it->second = old;
				throw;
			}
		}
		else if (old.service != service) {
			// replace: move the key to the back of the LIFO queue first
			// (pure container ops, std::string moves cannot throw), so
			// the new instance is destroyed before the services created
			// after the original install, which it may depend on
			for (size_t i = 0; i < _key_index.size(); i++) {
				if (_key_index[i] == key) {
					std::rotate(_key_index.begin() + i,
						_key_index.begin() + i + 1, _key_index.end());
					break;
				}
			}
			if (old.dtor) {
				// the table already points to the new instance when the
				// old one is destroyed, so queries made from inside the
				// old destructor cannot see a dangling pointer
				try {
					old.dtor(old.service);
				}
				catch (...) {
				}
			}
		}
		return true;
	}
	else {
		// enqueue first: if the table insertion throws, pop the key back
		// out, so Clear() can never miss an installed service
		_key_index.push_back(key);
		try {
			ServiceEntry entry;
			entry.service = service;
			entry.dtor = dtor;
			_registry[key] = entry;
		}
		catch (...) {
			_key_index.pop_back();
			throw;
		}
		return true;
	}
}


//---------------------------------------------------------------------
// clear all services in LIFO order, call dtor if not NULL
//---------------------------------------------------------------------
void ServiceRegistry::_ClearServices()
{
	std::lock_guard<std::recursive_mutex> lock(_lock);
	// RAII guard: _busy is a nesting depth counter, so a nested Clear()
	// from a service destructor bumps it and this decrement only returns
	// it to the OUTER sweep's level; the registry refuses Get()/Install()
	// until the outermost sweep finishes. Decrementing through the guard
	// also survives exceptions, which would otherwise wedge the counter
	struct BusyGuard {
		int &depth;
		BusyGuard(int &d): depth(d) { depth++; }
		~BusyGuard() { depth--; }
	} guard(_busy);
	while (_key_index.empty() == false) {
		// swap instead of copy: copying the key could throw bad_alloc,
		// which would leak out of ~ServiceRegistry and terminate; with
		// swap the whole sweep is allocation-free
		std::string key;
		key.swap(_key_index.back());
		_key_index.pop_back();
		auto it = _registry.find(key);
		if (it == _registry.end()) {
			continue;
		}
		ServiceEntry entry = it->second;
		// remove from the table before destroying, so a destructor
		// querying this key (or any already destroyed key) gets NULL
		// instead of a dangling pointer
		_registry.erase(it);
		if (entry.service != NULL && entry.dtor != NULL) {
			// swallow destructor exceptions: one broken service must not
			// abort the sweep (leaking the rest), and must not escape
			// ~ServiceRegistry (terminate)
			try {
				entry.dtor(entry.service);
			}
			catch (...) {
			}
		}
	}
	// drop leftover reservations (a construction was in flight when
	// Clear() was called reentrantly)
	_registry.clear();
	_key_index.clear();
}


//---------------------------------------------------------------------
// GetInstance: process-wide singleton as a plain function-local
// static: constructed on first use and destroyed during the normal
// static-destruction sequence at process exit, so it does not leak.
// The _closing/_busy machinery keeps reentrant Get()/Install() from
// resurrecting services while the sweep runs. Two caveats: do not
// touch the registry from threads still running after main()
// returned, and call Clear() explicitly before exiting if services
// depend on other globals and need a controlled shutdown order
//---------------------------------------------------------------------
ServiceRegistry& ServiceRegistry::GetInstance()
{
	static ServiceRegistry instance;
	return instance;
}


//---------------------------------------------------------------------
// GetThreadLocalInstance
//---------------------------------------------------------------------
ServiceRegistry& ServiceRegistry::GetThreadLocalInstance()
{
#if !defined(SERVICE_REGISTRY_EMUTLS_WORKAROUND)
	static thread_local ServiceRegistry instance;
	return instance;
#else
	static pthread_key_t key;
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, []() {
		pthread_key_create(&key, [](void *ptr) {
			ServiceRegistry *instance = (ServiceRegistry*)ptr;
			// winpthread can invoke this destructor again with NULL if
			// pthread_setspecific() is called while key destructors are
			// running (observed with MSYS2 winpthread): tolerate it
			if (instance == NULL) return;
			// ~ServiceRegistry sets _closing under the lock and sweeps
			// the services, so reentrant Get()/Install() are refused and
			// service destructors cannot resurrect anything inside the
			// dying instance
			// no manual detach here: winpthread already stored NULL in
			// the slot before invoking this destructor, so a reentrant
			// GetThreadLocalInstance() sees an empty slot and (at worst)
			// creates a fresh instance winpthread destroys on a later
			// iteration; calling pthread_setspecific(key, NULL) here
			// would trigger the spurious NULL invocation guarded above
			delete instance;
		});
	});
	ServiceRegistry *instance = (ServiceRegistry*)pthread_getspecific(key);
	if (instance == NULL) {
		instance = new ServiceRegistry();
		pthread_setspecific(key, instance);
	}
	return *instance;
#endif
}


//---------------------------------------------------------------------
// namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);



