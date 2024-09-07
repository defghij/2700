#include <complex.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>

void print_stats(int thread_count, int successes, int* results, int experiment_count);
void complex_mode();
void simple_mode();

// Purpose of this application ------------------------------------
//-----------------------------------------------------------------

// The point of this application is to demonstrate that even very
// simple operations are subject to inconsistent results when 
// there is shared mutable access to region of memory across many
// threads. 
//
// In this case, we use a 'static int shared_data' and have each
// thread increment the value. What one would expect to happen when
// reasoning about applications with in-program order operation is 
// that the value would be consistent and correct. Or even
// consistently incorrect (i.e. always 3 instead or 4). Or that
// given a sufficiently small operation (such as '+= 1') would not
// be overly susceptable to these effects.
//
// Hopefully, this program demonstrates that these assumptions 
// cannot be relied on. Thanks to modern processors, one thing we
// can rely on is that we wont get undefined partial values (i.e.
// 1 + 1 = 0xabcdef01). Instead we're simply likely to omit 
// operations. That is 1 + 1 <= 2.
//
// Ultimately, you can mess with the Configuration and Shared State
// section and run the application to see the results. If you want
// additional exposition, you can dive into the remaining
// comments below.


// Configuration and Shared State ---------------------------------
//-----------------------------------------------------------------

// Caution: program run-time is proporional to the magnitude of these
// values due to atomic contention. That is... bigger values mean 
// longer run-times. These settings seem like a good trade off.
static int max_threads = 10;
static int total_experiments = 100;

// Complex mode will do a 'total_experiments' on 1..'max_threads'
// and output statistical data on the value of 'shared_data'.
// Simple mode does one experiment over 'max_threads' and report
// the value of 'max_threads' and 'shared_data'.
static bool do_complex_mode = true;
static bool do_simple_mode = true;


// This is here to be changed! By default (0) it will use a non-threadsafe
// type for the shared state variable 'shared_data' Changing it to
// non-zero will make the shared state variable use a threadsafe
// type ('atomic_int'). This will affect the results of the experiments!
// Settings:
//   USE_ATOMIC := 0 --> static int shared_data = 0;
//   USE_ATOMIC := 1 --> static atomic_int shared_data = 0;
#define USE_ATOMICS 0         

// Declare the number of threads. It is set to 8 here. However,
// feel free to change this to 2 or whatever. If you do, you'll
// notice that the probability of incorrect results of decreases. 
// Why is this
static atomic_int thread_count = 8;        

// Expected value is just the number of threads because each 
// thread will increment the shared state _once and only once_.

// This variable will be accessed, via loads and stores,
// by every thread. The type, 'int', provides no consistency
// or coherence guarentees when accessed concurrently by
// multiple writers/readers.
#if !USE_ATOMICS 
  static int shared_data = 0;
#else
  static atomic_int shared_data = 0;
#endif


// Thread Synchronization -----------------------------------------
//-----------------------------------------------------------------

/*
 * Strictly speaking, this example does not need the thread 
 * synchronization in this section. However, by creating and
 * utilizing a thread _barrier_, a primitive that stalls execution
 * of the program until all threads reach the same point in the 
 * program, we dramatically increase the odds of witnessing
 * inconsistent results in the global state. This is used becasue
 * the operation demonstrated in this example (an increment) is only
 * three assembly instructions. The time it takes to execute these 
 * instructions is significantly less than the time it takes to
 * create a new thread and start its execution. So, essentially what
 * we are doing is moving the "start" line, to use a foot-race 
 * metaphor, to 'barrier' rather  than the 'pthread_join' function
 * call.
 */

// Atomic variable used to create a barrier for threads. This needs
// to be atomic so that the value of 'wait_lock' is:
// 1) always coherent: that is, never in an undefined state due to 
//    reading a partially written value (for example).
// 2) eventually consistent: the value will eventually reflect the
//    all the operations that took place on the data. In this case,
//    it is a trivially provided.
static atomic_int wait_lock = 0;

// Uses the above global state to ensure that all threads of 
// execution halt at the same set of instructions. Once all
// threads arrive, signaled by the condition in the while 
// construct, they proceed.
void barrier() {
  wait_lock += 1;
  while (wait_lock != thread_count) {}
}

// Primary Functions of the program -------------------------------
//-----------------------------------------------------------------

/*
 * The 'worker' thread is pretty simple:
 *  - A 'barrier' to ensure all threads start the increment at 
 *    roughly the same time
 *  - an incremement which compiles to 3 instructions,
 *  - a return due to pthread's required function signature form.
 */

// Simple worker function that mutates global state.
// Mutation of state does not occur until all thread
// have reached 'barrier()'. See in-function comment for more details.
void* worker(void* _ignored) {
  barrier();
  shared_data += 1;
  return NULL;
/*
 * The assembly for the function 'worker' (with '#define USE_ATOMICS 0') is as follows:
 *  1221:       e8 83 ff ff ff          call   11a9 <barrier>
 *  1226:       8b 05 ec 2d 00 00       mov    eax,DWORD PTR [rip+0x2dec]  # 4018 <shared_data>
 *  122c:       83 c0 01                add    eax,0x1
 *  122f:       89 05 e3 2d 00 00       mov    DWORD PTR [rip+0x2de3],eax  # 4018 <shared_data>
 *
 *  We can see that 'barrier' maps to a function call. Not suprising. The increment 
 *  operation maps to three instructions: load, add 1, store. When multiple threads
 *  leave the 'barrier' call at the same time the odds of these instructions getting
 *  interleaved on the processor is non-trivial.
 *  Imagine two threads leave the barrier at the same time,
 *
 * If we have the following order then we get a consistent value:
 *   +---+-------------------------+-------------------------+
 *   | t | THREAD ONE EXECUTION    | THREAD TWO EXECUTION    |              
 *   +---+-------------------------+-------------------------+
 *   | 0 | mov  eax, <shared_data> |                         |
 *   | 1 | add  eax,0x1            |                         |
 *   | 2 | mov  <shared_data>,eax  |                         |
 *   | 3 |                         | mov  eax, <shared_data> |
 *   | 4 |                         | add  eax,0x1            |
 *   | 5 |                         | mov  <shared_data>,eax  |
 *   +---+-------------------------+-------------------------+
 * This is consistent becasue at t = 0 we have thread one load the value
 * from the global memory and store it into the EAX register. Note that
 * threads do _not_ share registers. So all operations in register space 
 * are local to the thread. It then increments its local copy by 1 at t = 2.
 * Finally, it stores its local copy to global memory. Think of this as it
 * "publishing" the results of its local operations. At t = 3, thread
 * two can see new 'shared_data' value in global memory. It does that same
 * set of operations culminating with it updating 'shared_data' with its 
 * local modifiations. In this case 1 + 1 = 2.
 *
 * But is seems fairly unlikely that thread two is going to patiently wait
 * for thread one to do its load, add, and store before starting its one. 
 * A much more likely outcome is something like the following:
 *   +---+-------------------------+-------------------------+
 *   | t | THREAD ONE EXECUTION    | THREAD TWO EXECUTION    |              
 *   +---+-------------------------+-------------------------+
 *   | 0 | mov  eax, <shared_data> |                         | 
 *   | 1 |                         | mov  eax, <shared_data> |
 *   | 2 | add  eax,0x1            |                         |
 *   | 3 |                         | add  eax,0x1            |
 *   | 4 | mov  <shared_data>,eax  |                         |
 *   | 5 |                         | mov  <shared_data>,eax  |
 *   +---+-------------------------+-------------------------+
 *
 *  With the above outcome both threads will load the value from global
 *  memory. This this case they will both have the true condition 
 *  'shared_data == 0'. They then both increment their own copy by 1. 
 *  It is their own copy because threads do not share registers. So each
 *  thread is adding one to its own local copy. After they finish they 
 *  then store their local result to global memory. Regardless of the 
 *  order in which the two thread do their 'mov <shared_data,eax', the
 *  final value of 'shared_data' can _only_ be 1. This is because, if
 *  thread one completes its store first, then it sees 'shared_data == 1'.
 *  Then thread two will store its value to 'shared_data' overwriting the 
 *  result. In this case its is overwritten with the same value.
 *  So we end up with 'shared_data == 1' from both thread rather than 
 *  what we would like which is 'shared_data == 2'. The end result is that
 *  our threads have told us that 1 + 1 = 1. Thats not quite right.
 */
}



int main(int argc, char** argv) {
  if (do_complex_mode) { complex_mode(); }
  if (do_simple_mode)  { simple_mode();  }
}

void create_threads_and_launch_worker(int thread_count) {
  pthread_t threads[thread_count];

  // Here we loop through the threads we want and register a function that will
  // be executed at the start of the thread. That is, we are specifying that we
  // want THREAD_COUNT threads where they all _only_ execute the function 'worker'.
  for(int t = 0; t < thread_count; t++) {
    pthread_create(&threads[t], NULL, worker, NULL);
  }

  // This is where we actually spawn the threads we requested. Note that we have
  // to do this sequentially (a for loop). This means that if we didnt use a 'barrier'
  // then thread 0 could have finished before thread N even gets spawned!
  for(int t = 0; t < thread_count; t++) {
    pthread_join(threads[t], NULL);
  }
}


void simple_mode() {
  printf("\n");
  printf("Simple Mode--------------------------\n");

  create_threads_and_launch_worker(thread_count);
  printf("thread_count = %d = %d = shared_data\n", thread_count, shared_data); 

  // Reset global variables for next experiment
  wait_lock = 0;
  shared_data = 0;
}


void complex_mode() {
  printf("\n");
  printf("Complex Mode--------------------------\n");
  printf("|Thread_Count | Experiments | Failures |      Min |    Average |      Max |   Variance |   Std Dev  | \n");

  atomic_int original_thread_count = thread_count; // Save off this value so we can reset it later.

  for (thread_count = 1; thread_count <= max_threads; thread_count++) {
    int successes = 0;
    int results[total_experiments];

    for (int experiment = 0; experiment < total_experiments; experiment++) {

      create_threads_and_launch_worker(thread_count);

      // Record the final result was consistent/coherent.
      if (shared_data == thread_count) { successes += 1; }
      results[experiment] = shared_data;


      // Reset global variables for next experiment
      wait_lock = 0;
      shared_data = 0;
    }

    print_stats(thread_count, successes, results, total_experiments);
  }

  thread_count = original_thread_count; // restore thread count incase we want to do simple mode.
}


void print_stats(int thread_count, int successes, int* results, int experiment_count) {
  float average, variance, std_deviation, sum = 0, sum1 = 0;

  int min = max_threads + 1;
  int max = 0;
  for (int i = 0; i < experiment_count; i++) {
    sum = sum + results[i];
    if (results[i] > max) { max = results[i]; }
    if (results[i] < min) { min = results[i]; }
  }
  average = sum / (float) experiment_count;

  for (int i = 0; i < experiment_count ; i++) {
    sum1 = sum1 + pow((results[i] - average), 2);
  }

  variance = sum1 / (float) experiment_count;
  std_deviation = sqrt(variance);
  printf("| %10d  | %10d  | %8d | %8d | %10.2f | %8d | %10.2f | %10.2f |\n", 
                                      thread_count,
                                      total_experiments,
                                      total_experiments - successes,
                                      min,
                                      average,
                                      max,
                                      variance,
                                      std_deviation);
}
