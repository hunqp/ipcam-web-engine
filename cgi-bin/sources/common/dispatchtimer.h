#ifndef DISPATCHTIMER_H
#define DISPATCHTIMER_H

#include <string>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <functional>
#include <unordered_map>

class DispatchTimer {
	typedef std::function<void(void)> fp_t;

    struct TimerManagement {
        int32_t counters;
        fp_t op;
    };

public:
	 DispatchTimer();
	~DispatchTimer();

	void dispatch(std::string uuid, int32_t ms, const fp_t &op);
    void remove(std::string uuid);
	int  totalsPending();
	void removePending();

private:
	static void* dispatchTimerThreadHandler(void* arg);

private:
    pthread_t mThread = 0;
    std::unordered_map<std::string, TimerManagement> mQueue = {};

	pthread_cond_t mWait = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mLock = PTHREAD_MUTEX_INITIALIZER;

    bool bEscaped = false;
};

#endif /* DISPATCHTIMER_HPP */
