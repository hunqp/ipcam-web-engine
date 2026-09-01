#include <stdio.h>
#include "basethread.h"

BaseThread::BaseThread() {
    
}

BaseThread::~BaseThread() {
    close();
}

void BaseThread::start(void) {
    if (mPId) {
        return;
    }
    mEscape = true;
    pthread_create(&mPId, nullptr, [](void* arg) -> void* {
        BaseThread *self = (BaseThread*)(arg);
        if (self->mImplOnOpened) {
            self->mImplOnOpened();
        }
        bool &envir = self->mEscape;
        if (self->mImplOnDoLoop) {
            self->mImplOnDoLoop(envir);
        }
        if (self->mImplOnClosed) {
            self->mImplOnClosed();
        }
        return NULL;
    }, 
    this);
}

void BaseThread::close(void) {
    if (!mPId) {
        return;
    }
    mEscape = false;
    pthread_join(mPId, NULL);
    mPId = 0;
}
