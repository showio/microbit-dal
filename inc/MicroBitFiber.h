/**
  * Definitions for the MicroBit Fiber scheduler.
  *
  * This lightweight, non-preemptive scheduler provides a simple threading mechanism for two main purposes:
  *
  * 1) To provide a clean abstraction for application languages to use when building async behaviour (callbacks).
  * 2) To provide ISR decoupling for Messagebus events generted in an ISR context.
  */
  
#ifndef MICROBIT_FIBER_H
#define MICROBIT_FIBER_H

#include "mbed.h"
#include "MicroBitMessageBus.h"

// Typical stack size of each fiber. 
// A physical stack of anything less than 1024 will likely hit overflow issues during ISR/mBed calls.
// However, as we're running a co=operative fiber scheduler, the size of the stack at the point of
// context switching is normally *very* small (circa 64 bytes!). Also, as we're likely to have many short lived threads
// being used, be actually perform a stack duplication on context switch, which keeps the RAM footprint of a fiber 
// down to a minimum, without constraining what can be done insode a fiber context.
//
// TODO: Consider a split mode scheduler, that monitors used stack size, and maintains a dedicated, persistent
// stack for any long lived fibers with large stacks.
//
#define FIBER_STACK_SIZE        64
#define FIBER_TICK_PERIOD_MS    6
#define CORTEX_M0_STACK_BASE    (0x20004000 - 4)

#define MICROBIT_FLAG_DATA_READ 	    0x01 

#define MICROBIT_FIBER_FLAG_FOB             0x01 
#define MICROBIT_FIBER_FLAG_PARENT          0x02 
#define MICROBIT_FIBER_FLAG_CHILD           0x04 

/**
  *  Thread Context for an ARM Cortex M0 core.
  * 
  * This is probably overkill, but the ARMCC compiler uses a lot register optimisation
  * in its calling conventions, so better safe than sorry. :-)
  * 
  * TODO: Check with ARM guys to see is they have suggestions to optimize this context block
  */
struct Cortex_M0_TCB
{
    uint32_t R0;
    uint32_t R1;
    uint32_t R2;
    uint32_t R3;
    uint32_t R4;
    uint32_t R5;
    uint32_t R6;
    uint32_t R7;
    uint32_t R8;
    uint32_t R9;
    uint32_t R10;
    uint32_t R11;
    uint32_t R12;
    uint32_t SP;
    uint32_t LR;
};

/**
  * Representation of a single Fiber
  */

struct Fiber
{
    uint32_t stack_top, stack_bottom;   // Address of this Fiber's stack. Stack is heap allocated, and full descending.
    Cortex_M0_TCB tcb;                  // Thread context when last scheduled out.
    uint32_t context;                   // Context specific information. 
    uint32_t flags;                     // Information about this fiber.
    Fiber **queue;                      // The queue this fiber is stored on.
    Fiber *next, *prev;                 // Position of this Fiber on the run queues.
};


/**
  * Initialises the Fiber scheduler. 
  * Creates a Fiber context around the calling thread, and adds it to the run queue as the current thread.
  *
  * This function must be called once only from the main thread, and before any other Fiber operation.
  */
void scheduler_init();

/**
  * Exit point for all fibers.
  * Any fiber reaching the end of its entry function will return here  for recycling.
  */
void release_fiber(void);

/**
  * Exit point for parameterised fibers.
  * A wrapper around release_fiber() to enable transparent operaiton.
  */
void release_fiber(void *param);

/**
 * Launches a fiber
  */
void launch_new_fiber() 
#ifdef __GCC__
    __attribute__((naked))
#endif
;

/**
 * Creates a new Fiber, and launches it.
  *
  * @param entry_fn The function the new Fiber will begin execution in.
  * @param completion_fn The function called when the thread completes execution of entry_fn.  
  * @return The new Fiber.
  */
Fiber *create_fiber(void (*entry_fn)(void), void (*completion_fn)(void) = release_fiber);

/**
 * Launches a paramaterised fiber
  */
void launch_new_fiber_param() 
#ifdef __GCC__
    __attribute__((naked))
#endif
;
/**
 * Creates a new parameterised Fiber, and launches it.
  *
  * @param entry_fn The function the new Fiber will begin execution in.
  * @param param an untyped parameter passed into the entry_fn anf completion_fn.
  * @param completion_fn The function called when the thread completes execution of entry_fn.  
  * @return The new Fiber.
  */
Fiber *create_fiber(void (*entry_fn)(void *), void *param, void (*completion_fn)(void *) = release_fiber);



/**
  * Calls the Fiber scheduler.
  * The calling Fiber will likely be blocked, and control given to another waiting fiber.
  * Call this to yield control of the processor when you have nothing more to do.
  */
void schedule();

/**
  * Blocks the calling thread for the given period of time.
  * The calling thread will be immediatley descheduled, and placed onto a 
  * wait queue until the requested amount of time has elapsed. 
  * 
  * n.b. the fiber will not be be made runnable until after the elasped time, but there
  * are no guarantees precisely when the fiber will next be scheduled.
  *
  * @param t The period of time to sleep, in milliseconds.
  */
void fiber_sleep(unsigned long t);

/**
  * Timer callback. Called from interrupt context, once every FIBER_TICK_PERIOD_MS milliseconds.
  * Simply checks to determine if any fibers blocked on the sleep queue need to be woken up 
  * and made runnable.
  */
void scheduler_tick();

/**
  * Blocks the calling thread until the specified event is raised.
  * The calling thread will be immediatley descheduled, and placed onto a 
  * wait queue until the requested event is received.
  * 
  * n.b. the fiber will not be be made runnable until after the event is raised, but there
  * are no guarantees precisely when the fiber will next be scheduled.
  *
  * @param id The ID field of the event to listen for (e.g. MICROBIT_ID_BUTTON_A)
  * @param value The VALUE of the event to listen for (e.g. MICROBIT_BUTTON_EVT_CLICK)
  */
void fiber_wait_for_event(uint16_t id, uint16_t value);

/**
  * Executes the given function asynchronously.  
  * 
  * Fibers are often used to run event handlers, however many of these event handlers are very simple functions
  * that complete very quickly, bringing unecessary RAM overhead.
  *
  * This function takes a snapshot of the current processor context, then attempts to optimistically call the given function directly. 
  * We only create an additional fiber if that function performs a block operation. 
  *
  * @param entry_fn The function to execute.
  */
void fork_on_block(void (*entry_fn)(void));

/**
  * Executes the given function asynchronously.  
  * 
  * Fibers are often used to run event handlers, however many of these event handlers are very simple functions
  * that complete very quickly, bringing unecessary RAM. overhead 
  *
  * This function takes a snapshot of the current fiber context, then attempt to optimistically call the given function directly. 
  * We only create an additional fiber if that function performs a block operation. 
  *
  * @param entry_fn The function to execute.
  * @param param an untyped parameter passed into the entry_fn anf completion_fn.
  */
void fork_on_block(void (*entry_fn)(void *), void *param);

/**
  * Resizes the stack allocation of the current fiber if necessary to hold the system stack.
  *
  * If the stack allocaiton is large enough to hold the current system stack, then this function does nothing.
  * Otherwise, the the current allocation of the fiber is freed, and a larger block is allocated.
  */
inline void verify_stack_size(Fiber *f);


/**
  * Event callback. Called from the message bus whenever an event is raised. 
  * Checks to determine if any fibers blocked on the wait queue need to be woken up 
  * and made runnable due to the event.
  */
void scheduler_event(MicroBitEvent evt);


/**
  * Utility function to add the currenty running fiber to the given queue. 
  * Perform a simple add at the head, to avoid complexity,
  * Queues are normally very short, so maintaining a doubly linked, sorted list typically outweighs the cost of
  * brute force searching.
  *
  * @param f The fiber to add to the queue
  * @param queue The run queue to add the fiber to.
  */
void queue_fiber(Fiber *f, Fiber **queue);

/**
  * Utility function to the given fiber from whichever queue it is currently stored on. 
  * @param f the fiber to remove.
  */
void dequeue_fiber(Fiber *f);

/**
  * IDLE task.
  * Only scheduled for execution when the runqueue is empty.
  * Performs a procressor sleep operation, then returns to the scheduler - most likely after a timer interrupt.
  */
void idle_task();

/**
  * Determines if the processor is executing in interrupt context.
  * @return true if any the processor is currently executing any interrupt service routine. False otherwise.
  */
inline int inInterruptContext()
{
    return (((int)__get_IPSR()) & 0x003F) > 0;
}

/**
  * Assembler Context switch routing.
  * Defined in CortexContextSwitch.s
  */
extern "C" void swap_context(Cortex_M0_TCB *from, Cortex_M0_TCB *to, uint32_t from_stack, uint32_t to_stack);
extern "C" void save_context(Cortex_M0_TCB *tcb, uint32_t stack);
extern "C" void save_register_context(Cortex_M0_TCB *tcb);
extern "C" void restore_register_context(Cortex_M0_TCB *tcb);

/**
  * Time since power on. Measured in milliseconds.
  * When stored as an unsigned long, this gives us approx 50 days between rollover, which is ample. :-)
  */
extern unsigned long ticks;

/**
  * This variable is used to prioritise the systems' idle fibre to execute essential tasks.
  */
extern uint8_t fiber_flags;

#endif