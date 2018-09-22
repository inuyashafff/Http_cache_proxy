#ifndef CACHE_HPP
#define CACHE_HPP

#include <vector>
#include <mutex>

/** Cache
 */
template <typename K, typename V, typename Hasher = std::hash<K> >
class Cache {
	/** Every Entry of cache include one key and one
	    value. The mutex is for multi-thread programming.
	 */
	struct Entry {
		std::mutex mtx;
		K key;
		V value;
	};
	/** The arrary of entries.
	 */
	std::vector<Entry> vec;
	/** Use hasher to transfer the key to an integer 
	    fitting the array's size.
	 */
	Hasher hasher;
	/** Use key to get Entry. Only be used within class Cache
	 */
	Entry& getEntry(const K& key)
	{
		return vec[hasher(key) % vec.size()];
	}

public:
	/** The inner class Accessor is for getting 
	    and setting cache thread-safe. We need 
	    to lock the mutex every time we access 
	    cache.
	*/
	class Accessor {
	private:
		/** We need the cache to get and set entries.
		 */
		Cache& cache;
		Entry& entry;
		const K& key;
		std::lock_guard<std::mutex> locker;

	public:
		/** Initialize the accessor by key and cache.
		    Get the entry according to key and cache.
		    Initialize the locker by entry's mutex.
		 */
		Accessor(Cache& cache, const K& key)
			: cache(cache)
			, entry(cache.getEntry(key))
			, key(key)
			, locker(entry.mtx)
		{
		}
		std::pair<const K&, V&> get()
		{
			return { entry.key, entry.value };
		}
		void set(V value)
		{
			entry.key = key;
			entry.value = std::move(value);
		}
	};
	/** Initialize cache with the array's size.*/
	Cache(size_t size) : vec(size) {}
};

#endif /* CACHE_HPP */
