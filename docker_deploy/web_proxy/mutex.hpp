#ifndef MUTEX_HPP
#define MUTEX_HPP

#include <mutex>

/** Make a mutex-synchronized subclass of any class.
 */
template <typename T, typename M = std::mutex>
class mutex_synchronized : public T, public M {
	typedef M mutex_type;
};

#endif /* MUTEX_HPP */
