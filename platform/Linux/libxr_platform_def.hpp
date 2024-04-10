#include <pthread.h>
#include <semaphore.h>

typedef pthread_mutex_t libxr_mutex_handle;
typedef sem_t libxr_semaphore_handle;
typedef pthread_t libxr_thread_handle;
typedef int libxr_queue_handle;