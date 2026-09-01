#include "dispatchtimer.h"

DispatchTimer::DispatchTimer() {
    pthread_create(&mThread, NULL, DispatchTimer::dispatchTimerThreadHandler, this);
}

DispatchTimer::~DispatchTimer() {
    pthread_mutex_lock(&mLock);
    bEscaped = true;
    pthread_cond_signal(&mWait);
    pthread_mutex_unlock(&mLock);

    pthread_join(mThread, NULL);

    pthread_mutex_destroy(&mLock);
    pthread_cond_destroy(&mWait);
}

void DispatchTimer::dispatch(std::string uuid, int32_t ms, const fp_t &op) {
    pthread_mutex_lock(&mLock);

    mQueue[std::move(uuid)] = TimerManagement{ms * 1000, op};

    pthread_cond_signal(&mWait);
    pthread_mutex_unlock(&mLock);
}

void DispatchTimer::remove(std::string uuid) {
    pthread_mutex_lock(&mLock);
    mQueue.erase(uuid);
    pthread_mutex_unlock(&mLock);
}

int  DispatchTimer::totalsPending() {
    pthread_mutex_lock(&mLock);
    int size = mQueue.size();
    pthread_mutex_unlock(&mLock);
    return size;
}

void DispatchTimer::removePending() {
    pthread_mutex_lock(&mLock);
    mQueue = {};
    pthread_mutex_unlock(&mLock);
}

void* DispatchTimer::dispatchTimerThreadHandler(void* arg) {
    constexpr int unit = 50 * 1000; /* Polling each 50 milliseconds */
    DispatchTimer* me = static_cast<DispatchTimer*>(arg);

    while (true) {
        pthread_mutex_lock(&me->mLock);

        while (!me->bEscaped && me->mQueue.empty()) {
            pthread_cond_wait(&me->mWait, &me->mLock);
        }

        if (me->bEscaped) {
            pthread_mutex_unlock(&me->mLock);
            break;
        }

        for (auto it = me->mQueue.begin(); it != me->mQueue.end(); ) {
            if (it->second.counters > unit) {
                it->second.counters -= unit;
                ++it;
            }
            else {
                auto fn = it->second.op;
                it = me->mQueue.erase(it);

                pthread_mutex_unlock(&me->mLock);
                fn();
                pthread_mutex_lock(&me->mLock);
            }
        }

        pthread_mutex_unlock(&me->mLock);
        usleep(unit);
    }

    return NULL;
}