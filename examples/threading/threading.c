#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    printf("Thread started, sleeping %d ms before obtaining mutex\n",thread_func_args->wait_to_obtain_ms);
    usleep(thread_func_args->wait_to_obtain_ms*1000);
    printf("Thread attempting to obtain mutex\n");
    pthread_mutex_lock(thread_func_args->mutex);
    printf("Thread obtained mutex, sleeping %d ms before releasing mutex\n",thread_func_args->wait_to_release_ms);
    usleep(thread_func_args->wait_to_release_ms*1000);
    printf("Thread releasing mutex\n");
    pthread_mutex_unlock(thread_func_args->mutex);
    printf("Thread exiting\n");
    thread_func_args->thread_complete_success = true;

    return (void*)thread_func_args;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    struct thread_data * th_data;

    th_data = (struct thread_data*) malloc(sizeof(struct thread_data));

    if (th_data == NULL)
    {
        return false;
    }

    th_data->thread_complete_success = false;
    th_data->mutex = mutex;
    th_data->wait_to_obtain_ms = wait_to_obtain_ms;
    th_data->wait_to_release_ms = wait_to_release_ms;

    /* Create the thread */
    if(pthread_create(thread, NULL, threadfunc, (void*)th_data) != 0)
    {
        free(th_data);
        return false;
    }
    else
    {
        return true;
    }
}