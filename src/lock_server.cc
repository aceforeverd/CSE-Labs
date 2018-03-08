// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server(): nacquire (0)
{
    stat_table.clear();
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
    lock_protocol::status ret = lock_protocol::OK;
    printf("stat request from clt %d\n", clt);
    r = nacquire;
    return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
    lock_protocol::status ret = lock_protocol::OK;
    // Your lab4 code goes here
    pthread_mutex_lock(&mutex);

    printf("acquire request from clt %d\n", clt);
    while (is_locked(lid)) {
        pthread_cond_wait(&cond, &mutex);
    }
    stat_table[lid] = LOCKED;

    pthread_mutex_unlock(&mutex);
    return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
    lock_protocol::status ret = lock_protocol::OK;
    // Your lab4 code goes here
    pthread_mutex_lock(&mutex);

    printf("release request from clt %d\n", clt);
    if (is_locked(lid)) {
        // stat_table[lid] = FREE;
        stat_table.erase(lid);
        pthread_cond_signal(&cond);
    } else {
        ret = lock_protocol::OK;
    }

    pthread_mutex_unlock(&mutex);
    return ret;
}

bool lock_server::is_locked(lock_protocol::lockid_t lid) {
    std::map<lock_protocol::lockid_t, lock_stat_t>::iterator it;
    it = stat_table.find(lid);
    return (it != stat_table.end() && it->second == LOCKED);
}
