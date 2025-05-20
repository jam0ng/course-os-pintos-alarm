#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

// block된 스레드 관리용 리스트(추가)
static struct list sleeping_list;

// 다음에 깨워질 가장 빠른 시간을 저장하는 변수(추가)
static int64_t next_tick_to_awake;


/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
bool thread_report_latency;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);



/* alram clock 개선용 함수 생성 공간 */

int64_t get_next_tick_to_awake(void){
  return next_tick_to_awake;
}

void updata_next_tick_to_awake(int64_t ticks)
{
  // 깨워야 할 스레드 중 가장 작은 tick을 갖도록 업데이트 함
  next_tick_to_awake = (next_tick_to_awake > ticks) ? ticks : next_tick_to_awake;
}

void thread_sleep(int64_t ticks)
{
  struct thread* curr;

  enum intr_level old_level;
  old_level = intr_disable(); // 이 라인 이후의 과정에서는 인터럽트를 허용하지 않음
  // 대신 나중에 다시 그 허용하기 위해 이전 정보를 old_level에 저장함

  curr = thread_current(); // 현재의 thread 주소를 가져옴
  ASSERT(curr != idle_thread);  // 현재의 thread가 idle_thread가 아니라면 아래 코드 진행 맞다면 종료,  ASSERT(인자); 인자가 참이면 진행 거짓이면 종료
  
  // 만약 thread_sleep 함수 인자로 주어진 ticks가 현재 시점보다 전이라면 어떻게 되는 것인가?
  updata_next_tick_to_awake(curr-> wake_tick = ticks);  // 현재의 thread의 wake_tick에 인자로 들어온 ticks를 저장후 next_tick_to_awake를 업데이트 한다.
  // sleeping_list에 현재 스레드의 elem을 맨 뒤에 삽입
  list_push_back(&sleeping_list, &(curr -> elem));

  thread_block(); // 현재 스레드를 block
  
  intr_set_level(old_level); // 다시 interrupt를 허용함 + 과저 정보 복구

}

/* list 안에 있는  ASSERT (list != NULL); 이 명령어는 NULL이라면 종료해라 라는 뜻이고, 포인터로 NULL 값을 받는 다면 문제가 생기기 때문에 그거 방지하기 위한 코드임 */

void thread_awake(int64_t ticks)
{
  struct list_elem *eptr = list_begin(&sleeping_list); // sleeping_list의 head 다음 첫 노드의 주소를 eptr에 저장

  while (eptr != list_end(&sleeping_list)){ // sleeping_list를 전부 순환할 때까지(tail 노드를 만날 때까지) while 문 반복
    struct thread *tptr = list_entry(eptr, struct thread, elem); // elem* eptr를 포함하는 struct thread를 찾아 주소로 변환 
    if (tptr-> wake_tick <= ticks){                              // 현재 ticks 보다 작거나 같은 wake_tick을 가지고 있는 thread를 찾는다면
      eptr = list_remove(eptr);                                  // sleeping_list에서 삭제하고 다음 노드를 앞으로 당김
      thread_unblock(tptr);                                      // thread unblock 하기
    }   
    else{
      if (tptr -> wake_tick < next_tick_to_awake){
        updata_next_tick_to_awake(tptr->wake_tick);   // 다음으로 올 수 있는 빠른 wake_tick을 next_tick_to_awake로 업데이트
      }

      eptr = list_next(eptr);                         // 현재 깨어날 thread가 아닌 thread를 넘김 
    }
  }

}

/*-----------------------------------------------------------------*/


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  // block된 스레드 관리용 list를 초기화
  list_init(&sleeping_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* 
  tid_t thread_create
  새로운 커널 스레드를 생성하고 초기화한 뒤 ready_list에 넣는다.
  스레드 함수 실행을 위한 스택 프레임들을 설정하고, thread_unblock()으로 준비시킨다.
  스레드 생성 이후 즉시 실행될 수도 있고, 큐에서 대기할 수도 있다.
*/
tid_t thread_create (const char *name, int priority, thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}

/* 
  void thread_block 
  현재 실행 중인 스레드를 BLOCKED 상태로 전환하고, CPU를 양도한다.
  BLOCKED 상태의 스레드는 외부에서 thread_unblock()으로 깨워야 다시 실행될 수 있다.
  이 함수는 반드시 인터럽트가 꺼진 상태에서 호출되어야 하며, schedule()을 통해 context switch가 일어난다.
*/
void thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* 
  void thread_unblock
  BLOCKED 상태의 스레드를 READY 상태로 전환시키고, ready_list에 삽입한다.
  이렇게 하면 스레드가 이후 스케줄러에 의해 실행될 수 있게 된다.
  주의: 이 함수는 호출 당시 스레드가 BLOCKED 상태임을 보장해야 하며, 현재 실행 중인 스레드를 preempt하지는 않는다.
*/
void thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* 
  void thread_exit
  현재 스레드를 종료시키고 스케줄러에게 CPU를 양도한다.
  all_list에서 제거되고, 상태를 DYING으로 설정한 후 schedule()을 호출한다.
  이후 thread_schedule_tail()에서 해당 스레드의 메모리를 정리한다.
*/
void thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* 
  void thread_yield
  현재 스레드를 READY 상태로 바꾸고 ready_list에 다시 삽입한 뒤, 스케줄러에게 CPU를 양도한다.
  스레드는 이후 다시 선택될 수 있다.
  인터럽트가 켜진 상태에서 호출되어야 하며, idle_thread는 큐에 넣지 않는다.
*/
void thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);  

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* 
  static struct thread * next_thread_to_run
  ready_list에서 다음 실행할 스레드를 반환한다.
  만약 ready_list가 비어 있다면, idle_thread를 반환하여 CPU가 놀지 않게 한다.
  반납된 스레드는 바로 실행될 수 있는 상태여야 하며, ready_list는 FIFO 방식으로 관리된다.
*/
static struct thread * next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* 
  void thread_schedule_tail
  스레드 전환이 완료된 직후 호출되는 후처리 함수다.
  새로 전환된 스레드를 RUNNING 상태로 만들고, 이전 스레드가 DYING 상태라면 메모리를 해제한다.
  첫 context switch에서도 호출되며, 사용자 프로세스의 주소 공간도 활성화한다.
*/
void thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* 
  static void schedule
  현재 스레드의 상태가 BLOCKED, READY, DYING 중 하나가 되었을 때 호출된다.
  ready_list에서 다음 실행할 스레드를 선택하고, switch_threads()를 통해 context switch를 수행한다.
  반드시 인터럽트가 비활성화된 상태에서 호출되어야 하며, thread_schedule_tail()로 후처리가 이어진다.
*/
static void schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);