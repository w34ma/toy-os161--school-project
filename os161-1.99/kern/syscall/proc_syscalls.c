#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>

#include "opt-A2.h"
#include "opt-A3.h"
#include <synch.h>
#include <synchprobs.h>
#include <vm.h>
#include <vfs.h>
#include <kern/wait.h>
#include <mips/trapframe.h>
#include <limits.h>
#include <kern/fcntl.h>
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */
#if OPT_A2
extern struct proc_table *arr;
extern struct lock *proc_table_lk;

int sys_fork(pid_t *pid, struct trapframe *t) {
    struct proc *proc = proc_create_runprogram("[child]");
    proc->parent = curproc->pid;

    // Copy address space
    int err1 = as_copy(curproc->p_addrspace,&proc->p_addrspace);
    if (err1) {
        return err1;
    }

    struct trapframe *trap = kmalloc(sizeof(struct trapframe));
    memcpy(trap,t,sizeof(struct trapframe));//copy trapframe
    int err2 = thread_fork("new_thread", proc, enter_forked_process, trap, 0);//create new_thread in process p
    if(err2) {
        return err2;
    }
    *pid = proc->pid;
    return 0;
}
#endif

void sys__exit(int exitcode) {
  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;
#if OPT_A2
    lock_acquire(proc_table_lk);

    // If not orphaned, then record exit code.
    if (arr[p->pid].p->parent != 0) {
       arr[p->pid].exit_code = _MKWAIT_EXIT(exitcode);
    } else {
       arr[p->pid].exit_code = -1;
    }

    arr[p->pid].p = NULL;
    cv_destroy(arr[p->pid].pid_cv);
    arr[p->pid].pid_cv = NULL;

    for (int i = __PID_MIN; i < __PID_MAX; i++) {
       // If child is still alive, orphan it, otherwise, free its PID
       if (arr[i].p && arr[i].p->parent == curproc->pid) {
          if (arr[i].p) {
             arr[i].p->parent = 0;
          } else {
             arr[i].exit_code = -1;
          }
       }
    }

    // If parent is still alive, notify that we're dying.
    if (arr[p->parent].p)
       cv_signal(arr[p->parent].pid_cv, proc_table_lk);

    lock_release(proc_table_lk);
#endif
    
  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
   *retval = curproc->pid;
    return (0);
#else
   *retval = 1;
    return(0);
#endif
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

#if OPT_A2
  //exitstatus will be 0 means not terminate yet, and be the exit_code if the process with given pid terminate
  exitstatus = 0;

  if (arr[pid].p == NULL && arr[pid].exit_code == -1){
     //the suituation where the pid argument named a nonexistent process
     return ESRCH;
  }
  
  if(arr[pid].p != NULL&& arr[pid].p->parent != curproc->pid){ return ECHILD;}
  
  if(status == NULL){return EFAULT;}

  lock_acquire(proc_table_lk);
  while (arr[pid].exit_code == -1) {
     cv_wait(arr[curproc->pid].pid_cv, proc_table_lk);
  }

  KASSERT(arr[pid].exit_code != -1 && arr[pid].p == NULL);
  exitstatus = arr[pid].exit_code;

  lock_release(proc_table_lk);
#endif

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
 //  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#if OPT_A2
//char **args
int sys_execv(char* name,char **args){
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint,stackptr;
    int result;
    //open the file
    result = vfs_open(name,O_RDONLY,0,&v);
    if(result){
       return result;
    }

    /*copy arguments from user space into kernel buffer*/
    
    char **kargv = NULL;
    
    //count args
    int count_args = 0;
    for(int i = 0; args[i] != NULL; i++){
        count_args++;
    }
    
    //allocate space for args,include program name at beginning and NUll at the end
    kargv = kmalloc((count_args+1)*sizeof(char*));
    //if(kargv==NULL){
    //   return ENOMEM;
    //}
    //copy the program name to the first position
    char* space_for_name = kmalloc((strlen(name)+1)*sizeof(char));
    //if(space_for_name){
    //   return ENOMEM;
    //}
    //int copyinstr(const_userptr_t usersrc, char *dest, size_t len, size_t *got);
    copyinstr((const_userptr_t)name,space_for_name,(size_t)strlen(name)+1,NULL);
    //allocate space for arguments and copied into kernel
    for(int j = 0; args[j] != NULL;j++){
        kargv[j] = kmalloc((strlen(args[j]) + 1) * sizeof(char));
       // if(kargv[j]==NULL){
       //    return ENOMEM;
       // }
        copyinstr((const_userptr_t)args[j],kargv[j],strlen(args[j])+1,NULL);
    }
    kargv[count_args]= NULL;
    //
    as_destroy(curproc->p_addrspace);//replace the entire program, destroy the address space
    
    curproc_setas(NULL);
    //we should be a new process
    KASSERT(curproc_getas() == NULL);
    //create a new address space
    as = as_create();
    if(as == NULL){
       vfs_close(v);
       return ENOMEM;
    }
    //switch to it and activate it
    curproc_setas(as);
    as_activate();
    //load the executable
    result = load_elf(v,&entrypoint);
    if(result){
       //p_addrspace will go away when curproc is destroyed
       vfs_close(v);
       return result;
    }
    //done with the file now
    vfs_close(v);
    //define the user stack in the address space
    result = as_define_stack(as,&stackptr);
    if(result){
        //p_adddrspace will go away when curproc is destroyed
       return result;
    }
    
    /*copy the arguments from kernel buffer into user stack*/
    
    //fill kargv[i] with real user space pointer first and then copy
    int count = 0;
    size_t topstack = stackptr;
    char** location = kmalloc((count_args+1)*sizeof(char*));
    //if(location == NULL){
    //   return ENOMEM;
   // }
    location[count_args] = NULL;

    for(int m = count_args-1 ; m >= 0; m--){
        int len = strlen(kargv[m])+1;
        count = count + len;
        stackptr = stackptr - len;
        location[m]=(char *)stackptr;
        copyoutstr(kargv[m],(userptr_t)stackptr,(size_t)len,NULL);
    }
    //round up
    stackptr = topstack - count - ((topstack - count)% 8);
    //continue to copy pointer to user stack
    for(int n = count_args;n>=0;n--){
        stackptr = stackptr - 4;
        copyout(&location[n],(userptr_t)stackptr,(size_t)4);
    }
    
    //free memory
    for(int z = 0;z < count_args;z++){
        kfree(kargv[z]);
    }
    kfree(kargv);
    kfree(space_for_name);
    
    //enter_new_process(int argc, userptr_t argv, vaddr_t stack, vaddr_t entry)
    enter_new_process(count_args,(userptr_t) stackptr, stackptr, entrypoint);
    //enter_new_process(0,NULL,stackptr,entrypoint);
    panic("enter_new_process returned\n");
    return EINVAL;
}

#endif

#if OPT_A3
void error__exit(int exitcode) {
    struct addrspace *as;
    struct proc *p = curproc;
    /* for now, just include this to keep the compiler from complaining about
     an unused variable */
    (void)exitcode;
    lock_acquire(proc_table_lk);
    
    // If not orphaned, then record exit code.
    if (arr[p->pid].p->parent != 0) {
        arr[p->pid].exit_code = _MKWAIT_SIG(exitcode);
    } else {
        arr[p->pid].exit_code = -1;
    }
    
    arr[p->pid].p = NULL;
    cv_destroy(arr[p->pid].pid_cv);
    arr[p->pid].pid_cv = NULL;
    
    for (int i = __PID_MIN; i < __PID_MAX; i++) {
        // If child is still alive, orphan it, otherwise, free its PID
        if (arr[i].p && arr[i].p->parent == curproc->pid) {
            if (arr[i].p) {
                arr[i].p->parent = 0;
            } else {
                arr[i].exit_code = -1;
            }
        }
    }
    
    // If parent is still alive, notify that we're dying.
    if (arr[p->parent].p)
        cv_signal(arr[p->parent].pid_cv, proc_table_lk);
    
    lock_release(proc_table_lk);
    
    DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);
    
    KASSERT(curproc->p_addrspace != NULL);
    as_deactivate();
    /*
     * clear p_addrspace before calling as_destroy. Otherwise if
     * as_destroy sleeps (which is quite possible) when we
     * come back we'll be calling as_activate on a
     * half-destroyed address space. This tends to be
     * messily fatal.
     */
    as = curproc_setas(NULL);
    as_destroy(as);
    
    /* detach this thread from its process */
    /* note: curproc cannot be used after this call */
    proc_remthread(curthread);
    
    /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
    proc_destroy(p);
    
    thread_exit();
    /* thread_exit() does not return, so we should never get here */
    panic("return from thread_exit in sys_exit\n");
}
#endif

