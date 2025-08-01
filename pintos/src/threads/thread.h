#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
   
   // struct thread에 wake_tick 추가 (스레드별 깨울 시점)
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          // 스레드 고유 식별자 (TID), 시스템 내 유일한 ID이다.
    enum thread_status status;          // 현재 스레드의 상태 (e.g., RUNNING, READY, BLOCKED, DYING).
    char name[16];                      // 스레드의 이름, 디버깅 용도로 사용. 최대 15자 + NULL 문자.
    uint8_t *stack;                     // 커널 스택 포인터. 스레드 구조체 바로 위에 존재하는 커널 스택을 가리킴.
    int priority;                       // 스레드의 우선순위 값. 기본 스케줄링 또는 MLFQS에서 사용됨.
    struct list_elem allelem;           // all_list에 삽입될 때 사용하는 리스트 요소 (모든 스레드를 위한 리스트).
    
    // 일어날 시간을 담는 변수 (추가)
    int64_t wake_tick;

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              // ready_list나 semaphore 대기 리스트 등 다양한 큐에 사용되는 요소.

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  // 사용자 프로세스일 경우 페이지 디렉터리 포인터.
#endif

    /* Owned by thread.c. */
    unsigned magic;                     // 스택 오버플로우 검출용 매직 넘버. 이 값이 손상되면 에러로 감지됨.
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;
extern bool thread_report_latency;

int64_t get_next_tick_to_awake(void);
void updata_next_tick_to_awake(int64_t ticks);
void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
