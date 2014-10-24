#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>

/* 
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The globalCatMouseSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
//static struct semaphore *globalCatMouseSem;

static volatile int limit; //set limit to 2*number of bowls to interrupt
static volatile int count; //count the number of eating cat or mouse
static volatile int num_bowl;//record the number of bowls

static struct lock *lk; //lock
static struct cv *cat_wait; //conditon variable for cat
static struct cv *mouse_wait;//conditon variable for mouse
static volatile char *bowl_arr;//array of bowl to imply whether it can be used,'-' no one use,'c' cat use, 'm' mouse use

static volatile char flag; //suggest which turn should be, 'M' for mouse, 'C' for cat
static volatile int finish;//count the number of the cat or mouse who has finished
static volatile int cat_waiting;//count the number of cat who are waiting to eat
static volatile int mouse_waiting;//count the number of mouse who are waiting to eat

/* 
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 * 
 * parameters: the number of bowls
 */
void
catmouse_sync_init(int bowls)
{
  /* replace this default implementation with your own implementation of catmouse_sync_init */

  (void)bowls; /* keep the compiler from complaining about unused parameters */
 // globalCatMouseSem = sem_create("globalCatMouseSem",1);
 // if (globalCatMouseSem == NULL) {
 //   panic("could not create global CatMouse synchronization semaphore");
 // }
    num_bowl = bowls;
    flag = '\0';
    limit = 2*bowls;

    lk = lock_create("lk");
    
    cat_wait = cv_create("cat_wait");
    mouse_wait = cv_create("mouse_wait");

    bowl_arr = kmalloc(bowls * sizeof(char));
    
    for(int i =0; i<bowls;i++){
        bowl_arr[i]='-';
    }
    if(cat_wait == NULL){
      panic("could not create global cat wait condition variable");
    }
    if(mouse_wait ==NULL){
      panic("could not create global mouse wait condition variable");
    }
    if(lk ==NULL){
      panic("could not create global lock");
    }
    if(bowl_arr ==NULL){
      panic("could not create bowl array");
    }
    return;
}

/* 
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finished.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
  /* replace this default implementation with your own implementation of catmouse_sync_cleanup */
    (void)bowls; /* keep the compiler from complaining about unused parameters */
  //KASSERT(globalCatMouseSem != NULL);
  //sem_destroy(globalCatMouseSem);
    KASSERT(lk != NULL);
    KASSERT(cat_wait != NULL);
    KASSERT(mouse_wait != NULL);
    KASSERT(bowl_arr != NULL);

    lock_destroy(lk);
    cv_destroy(cat_wait);
    cv_destroy(mouse_wait);
    kfree((void *)bowl_arr);
}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

static void printbowls() {
   for (int i = 0; i < num_bowl; i++) {
      kprintf("%c", bowl_arr[i]);
   }
   kprintf("\n");
}

void
cat_before_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of cat_before_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  // KASSERT(globalCatMouseSem != NULL);
  // P(globalCatMouseSem);
  KASSERT(lk != NULL);
  KASSERT(cat_wait != NULL);
  KASSERT(bowl_arr != NULL);
    
  lock_acquire(lk);
  if(flag == '\0'){flag = 'C';}
  //kprintf("cat came\n");
  //printbowls();
  cat_waiting++;

  while(bowl_arr[bowl-1] != '-' || flag =='M'||finish >= limit){
       cv_wait(cat_wait,lk);
  }
  cat_waiting--;
  //kprintf("cat eating\n");
  //kprintf("%c\n",flag);
  count++;
  finish++;
  bowl_arr[bowl-1] = 'c';
  //printbowls();
  lock_release(lk);
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of cat_after_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  // KASSERT(globalCatMouseSem != NULL);
  // V(globalCatMouseSem);
  KASSERT(lk !=NULL);
  KASSERT(mouse_wait != NULL);
  KASSERT(cat_wait != NULL);
  KASSERT(bowl_arr != NULL);
  
  lock_acquire(lk);
  bowl_arr[bowl-1]='-';
  count--;
  if(count == 0 && mouse_waiting > 0){
     //kprintf("switching turns to mice\n");
     flag = 'M';
     finish = 0;
     cv_broadcast(mouse_wait,lk);
  } else {
     if (count == 0 && cat_waiting == 0 && mouse_waiting == 0) {
        //kprintf("switching turns to nobody\n");
        flag = '\0';
        finish = 0;
     }else{
     if(mouse_waiting == 0 && finish >= limit)  {
        finish = 0;
     }
        cv_broadcast(cat_wait, lk);
     }
  }
   lock_release(lk);
}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of mouse_before_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  //KASSERT(globalCatMouseSem != NULL);
    
  //P(globalCatMouseSem);
  KASSERT(lk != NULL);
  KASSERT(mouse_wait != NULL);
  KASSERT(bowl_arr != NULL);

  lock_acquire(lk);
  if(flag == '\0') { flag = 'M';}
  printbowls();
  kprintf("mouse came\n");
  mouse_waiting++;
  while(bowl_arr[bowl-1] != '-' || flag=='C'||finish >= limit){
     /*if (bowl_arr[bowl-1] != '-')
        kprintf("mouse waiting because bowl is occupied\n");
     if (flag == 'C')
        kprintf("mouse waiting because cat's turn\n");
     if (finish >= limit)  
        kprintf("mouse waiting because reached mouse limit\n");*/
        cv_wait(mouse_wait,lk);
  }
  mouse_waiting--;
  kprintf("mouse eating\n");
  kprintf("%c\n",flag);
  count++;
  finish++;
  bowl_arr[bowl-1] = 'm';
  lock_release(lk);
}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of mouse_after_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  //KASSERT(globalCatMouseSem != NULL);
  //V(globalCatMouseSem);
  KASSERT(lk != NULL);
  KASSERT(cat_wait != NULL);
  KASSERT(mouse_wait != NULL);
  KASSERT(bowl_arr != NULL);

  lock_acquire(lk);
  bowl_arr[bowl-1]='-';
  count--;
  if(count == 0 && cat_waiting > 0){
     kprintf("switching turns to cats\n");
     flag = 'C';
     finish = 0;
     cv_broadcast(cat_wait,lk);
  } else {
     if (count == 0 && cat_waiting == 0 && mouse_waiting == 0) {
      kprintf("switching turns to nobody\n");
           finish = 0;
           flag = '\0';
     }else{
     if(cat_waiting == 0 && finish >= limit)  {
        finish = 0;
     }
     kprintf("waking up all the mice\n");
          cv_broadcast(mouse_wait, lk);
     }
  }
     lock_release(lk);
}
