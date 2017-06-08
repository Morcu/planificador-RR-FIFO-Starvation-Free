#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"
#include "queue.h"

long hungry = 0L;

TCB* scheduler();
void activator();
void timer_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N]; 
/* Current running thread */
static TCB* running;
static int current = 0;
/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

//Variable para la cola
struct queue* colaBaja;
struct queue* colaAlta;

/* Initialize the thread library */
void init_mythreadlib(int priority,void (*fun_addr)()) {

//Generamos la cola
	colaBaja = queue_new () ;
	colaAlta = queue_new () ;


  int i;  
  t_state[0].function= fun_addr;
  t_state[0].state = INIT;
  t_state[0].priority = priority;
  t_state[0].ticks = QUANTUM_TICKS;
  
  
 
  if(getcontext(&t_state[0].run_env) == -1){
    perror("getcontext in my_thread_create");
    exit(5);
  }	
  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }
  t_state[0].tid = 0;
  running = &t_state[0];
  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */ 
int mythread_create (void (*fun_addr)(),int priority)
{
  int i;
  
  if (!init) { init_mythreadlib(priority, fun_addr); init=1;}
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;
  if (i == N) return(-1);
  if(getcontext(&t_state[i].run_env) == -1){
    perror("getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
//Añadimos el cuanto
  t_state[i].ticks= QUANTUM_TICKS;

  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;	
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr, 1);  

  if(priority==LOW_PRIORITY){
 
  //Encolamos los siguientes elementos 
    enqueue(colaBaja, &t_state[i]);
  }

  if(priority==HIGH_PRIORITY){

  //Encolamos los siguientes elementos 
    enqueue(colaAlta, &t_state[i]);
  }


  return i;
} /****** End my_thread_create() ******/




/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
  int tid = mythread_gettid();	
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) {
  int tid = mythread_gettid();	
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  //if (!init) { init_mythreadlib(); init=1;}
  return current;
}

/* Timer interrupt  */
void timer_interrupt(int sig)
{
//obtener el id del thread en ejecucion
  int tid = mythread_gettid();	

  //Si las ambas colas tienen elementos o si la colaBaja tiene elementos y se esta ejecutando un hilo de HIGH_PRIORITY
  if((queue_empty(colaAlta)!=1 && queue_empty(colaBaja)!=1) || (queue_empty(colaBaja)!=1 && t_state[current].priority == 1) ){
    //Incrementamos hungry
    hungry++;
	//printf("****************HUNGRY********************");
    //Si hungry ha llegado a STARVATION
    if(hungry==STARVATION){

      //Bucle que pasa la cola de baja prioridad a la de alta
      do{
        //Se encola en la cola alta lo que se desencola de la baja
        TCB* intermedio = dequeue(colaBaja);
        printf("***THREAD %d PROMOTED TO HIGH PRIORITY QUEUE\n", intermedio->tid);
        enqueue(colaAlta,intermedio);
      }while(queue_empty(colaBaja)!=1);

	hungry =0L;

    }

  }

//Comprobar la prioridad del porceso actual
  if(t_state[tid].priority == LOW_PRIORITY){

        //Restamos 1 al cuanto actual
      int QuantumA = t_state[tid].ticks;
      QuantumA--;
	    t_state[tid].ticks = QuantumA;
      	
      //Si el cuanto ha llegado a 0 pasamos a ejecutar el siguiente
      if(QuantumA==0 || queue_empty(colaAlta)!=1){
	    	//printf("Cambio \n");
		    t_state[tid].ticks = QUANTUM_TICKS;
	    
      	//Encolar
        //Si el estado del proceso no es FREE encolo el proceso 
        if (t_state[current].state!=FREE){
        enqueue(colaBaja,&t_state[current]);
        }
	
		    TCB* sigiente = scheduler();
		    printf("*** SWAPCONTEXT FROM < %d > to < %d >\n",t_state[tid].tid, sigiente->tid);		
		    activator(sigiente);

      }   

    }else{
  }
	


} 



/* Scheduler: returns the next thread to be executed */
TCB* scheduler(){

	TCB* dev;

  if(queue_empty(colaAlta)!=1){//Si la colaAlta no esta vacia devolver el primer elemento
		dev = dequeue(colaAlta);
    return dev;

  }else if(queue_empty(colaBaja)!=1){//Si colaBaja no vacia devolver el primer elemento
		dev = dequeue(colaBaja);
	
    
    return dev;
	}else if(queue_empty(colaAlta)==1 && queue_empty(colaBaja)==1){
		printf("FINISH\n");
		exit(1);
	}
	
TCB* nulo = NULL;
return nulo;  

}

/* Activator */
void activator(TCB* next){

	int tid = mythread_gettid();
	current = next->tid;
	
	//Cambio de contexto

	swapcontext(&t_state[tid].run_env, &(next->run_env));	
  
}

/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();	
  printf("*** THREAD %d FINISHED\n", tid);	
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp); 
 

  int colaBajaVacia =queue_empty(colaBaja);

  TCB* next = scheduler();

//Si la cola no esta vacia imprimo el cambio sino no
	if(colaBajaVacia!=1){
		printf("*** THREAD < %d > FINISHED : SET CONTEXT OF < %d >\n",t_state[tid].tid, next->tid);		
	}
  activator(next);


}



