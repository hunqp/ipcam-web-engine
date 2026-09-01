#ifndef BASE_THREAD_H
#define BASE_THREAD_H

#include <pthread.h>
#include <functional>

class BaseThread {
public:
     BaseThread();
    ~BaseThread();

    void start(void);
    void close(void);
    void onOpened(std::function<void(void)> callback) { 
        mImplOnOpened = callback; 
    }
    void onClosed(std::function<void(void)> callback) { 
        mImplOnClosed = callback; 
    }
    void onDoLoop(std::function<void(bool&)> callback) { 
        mImplOnDoLoop = callback; 
    }
private:
    pthread_t mPId = 0;
    bool mEscape = true;
    std::function<void(void)>  mImplOnOpened = nullptr;
	std::function<void(void)>  mImplOnClosed = nullptr;
    std::function<void(bool&)> mImplOnDoLoop = nullptr;
};

#endif /* BASE_THREAD_HPP */