#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <string.h>
#include <sys/time.h>

#define TICK 10 //tick(ms)
#define QUANTUM 250 //quantum(ms)

typedef struct process_info {
	int process_num; //프로세스의 고유 번호
	pid_t pid; //프로세스의 PID
	int running; //CPU나 IO에서 할당받은 횟수 (몇 TICK동안 할당받고 있는지)

	int cpu_burst; //CPU BURST (10~1000, 10단위로 증가)
	int io_burst; //IO BURST (10~1000, 10단위로 증가)
	int left_cb; //남아 있는 CPU BURST
	int left_io; //남아 있는 IO BURST

	int start; //프로세스 시작 시점 (완료된 후 다시 BURST 값이 발생하면 다시 기록)
	int finished; //프로세스 종료 시점
	int Turnaround; //TurnAround Time (finished - start)
	int WaitingTime; //Waiting Time (TurnAround Time - CPU BURST - IO BURST)
	int ResponseTime; //Response Time (처음으로 할당받은 시점)
}PI;

typedef struct node {
	PI process;
	struct node* next;
	struct node* prev;
}NODE;

typedef struct queue {
	NODE* head;
	NODE* tail;
	int size;
}Q;

typedef struct msg_buff {
	long type;
	PI msg_pinfo;
}MSG;

typedef struct awt {
	int sum_Turnaround; //TurnAround Time들의 합
	int sum_Waiting; //Waiting Time들의 합
	int sum_Response; //Response Time들의 합
	int count_DoneProcess; //완료된 프로세스의 개수
	int context_switching; //Context Switching 횟수
}AWT;

void init(Q*);
void enqueue(Q*, PI); 
PI dequeue(Q*);
void print(Q*, char);
void clear(Q*);

void scheduling();

int process_count = 1; //프로세스 고유번호를 위한 전역변수
int tick_count = 0; //몇 TICK이 지났는지 기록하기 위한 전역변수
Q Run_Queue; //부모 프로세스가 관리하게 될 Run Queue 
Q Wait_Queue; //부모 프로세스가 관리하게 될 Wait Queue
AWT Report; //레포트를 쓸 때 필요하게 될 정보들을 담을 구조체 (Average Waiting Time 등을 구할 때 필요)

int main(){
	pid_t pid;

	init(&Run_Queue);	
	init(&Wait_Queue);
	
	MSG message;
	int msg_id;
	key_t key = 1234;
	msg_id = msgget(key, IPC_CREAT | 0666);
	if (msg_id == -1){
		fprintf(stderr, "msgget() failed\n");
		exit(1);
	}
	////////////////////////////////////////////////
	pid_t first_pid = getpid();
	
	for (int i=0; i<10; i++){
		if (first_pid == getpid()){
			pid = fork();
		}			
		if (first_pid == getpid()){
			process_count++;
		}
	}
	
	if (pid < 0){
		fprintf(stderr, "fork failed\n");
		exit(1);
	}
	
	else if (pid == 0){
		PI my_info;

		struct timeval time_rand;
		gettimeofday(&time_rand, NULL);
		srand(time_rand.tv_usec);
		int random;

		my_info.pid = getpid();
		my_info.process_num = process_count;
		random = (rand() % 100) + 1;
		my_info.cpu_burst = random * 10; //10ms 단위로 10ms~1000ms 사이의 CPU burst 생성

		if (rand() % 3){
			random = (rand() % 100) + 1; //10ms 단위로 10ms~1000ms 사이의 IO burst 생성
			my_info.io_burst = random * 10;
		}
		else{
			my_info.io_burst = 0;
		}

		my_info.left_cb = my_info.cpu_burst;
		my_info.left_io = my_info.io_burst;

		my_info.start = -1;
		my_info.finished = -1;
		my_info.Turnaround = -1;
		my_info.running = 0;
		my_info.WaitingTime = -1;
		my_info.ResponseTime = -1;

		message.type = process_count;
		message.msg_pinfo = my_info;
		
		if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
			fprintf(stderr, "msgsnd() failed\n");
			exit(1);
		} 
		///////////////////////////////////////////////////////////////

		while(1){
			if (msgrcv(msg_id, &message, sizeof(message) - sizeof(message.type), (my_info.process_num+10), IPC_NOWAIT) == -1){
				continue;
			}
			my_info.left_cb = message.msg_pinfo.left_cb;
			my_info.left_io = message.msg_pinfo.left_io;
			
			if (my_info.left_cb == 0 && my_info.left_io == 0){
				random = (rand() % 100) + 1;
				my_info.cpu_burst = random * 10; //10ms 단위로 10ms~1000ms 사이의 CPU burst 생성

				if (rand() % 3){
					random = (rand() % 100) + 1; //10ms 단위로 10ms~1000ms 사이의 IO burst 생성
					my_info.io_burst = random * 10;	
				}
				else{
					my_info.io_burst = 0;
				}
			
				my_info.left_cb = my_info.cpu_burst;
				my_info.left_io = my_info.io_burst;

				my_info.running = 0;

				message.msg_pinfo = my_info;
				message.type = my_info.process_num;

				if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
					fprintf(stderr, "msgsnd() failed\n");
					exit(1);
				}				
			}
		}
	}

	else{
		printf("             PID  CPU  IO  START <- Created Process\n");
		for (int i=0; i<10; i++){
			while(1){
				if (msgrcv(msg_id, &message, sizeof(message) - sizeof(message.type), i+1, IPC_NOWAIT) != -1){
					break;
				}
			}
			message.msg_pinfo.start = 0;
			enqueue(&Run_Queue, message.msg_pinfo);
			printf("[Process%2d] %4d %4d %4d   %d\n", message.msg_pinfo.process_num, message.msg_pinfo.pid, 
			message.msg_pinfo.cpu_burst, message.msg_pinfo.io_burst, message.msg_pinfo.start);
		}
		print(&Run_Queue, 'R');
		printf("\n");

		Report.sum_Turnaround = 0;
		Report.sum_Waiting = 0;
		Report.sum_Response = 0;
		Report.count_DoneProcess = 0;
		Report.context_switching = 0;

		/////////////////////////////////////////////////////
		struct sigaction sa;
		struct itimerval itv;

		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = &scheduling;
		sigaction(SIGVTALRM, &sa, NULL);

		itv.it_value.tv_sec = 0;
		itv.it_value.tv_usec = TICK*1000;

		itv.it_interval.tv_sec = 0;
		itv.it_interval.tv_usec = TICK*1000;

		setitimer(ITIMER_VIRTUAL, &itv, NULL);

		while(1);
		////////////////////////////////////////////////////
	}

    return 0;
}

void scheduling(){

	MSG message;
	int msg_id;
	key_t key = 1234;
	msg_id = msgget(key, IPC_CREAT | 0666);

	struct timeval time_rand;
	gettimeofday(&time_rand, NULL);
	srand(time_rand.tv_usec);

	tick_count++;

	if (tick_count > 1000){
		msgctl(msg_id, IPC_RMID, 0);

		NODE* tmp = Run_Queue.head;
		for (int i=0; i<Run_Queue.size; i++){
			printf("%d\n", tmp->process.pid);
			kill(tmp->process.pid, SIGKILL);
			printf("Process%2d was Killed\n", tmp->process.process_num);
			tmp = tmp->next;
		}
		clear(&Run_Queue);
		tmp = Wait_Queue.head;
		for (int j=0; j<Wait_Queue.size; j++){
			printf("%d\n", tmp->process.pid);
			kill(tmp->process.pid, SIGKILL);
			printf("Process%2d was Killed\n", tmp->process.process_num);
			tmp = tmp->next;
		}
		clear(&Wait_Queue);

		for (int i=3; i>0; i--){
			printf("Program will be finished %d\n", i);
			sleep(1);
		}
		kill(getpid(), SIGKILL);
	}
	
	printf("[%d] ", tick_count);

	if (Run_Queue.size > 0){
		if (Run_Queue.head->process.ResponseTime == -1){
			Run_Queue.head->process.ResponseTime = (tick_count-1)*10 - Run_Queue.head->process.start;
		}
		Run_Queue.head->process.left_cb -= TICK;
		Run_Queue.head->process.running++;
		printf("Process %d(%d) is Running (%d Left) ||", Run_Queue.head->process.process_num, Run_Queue.head->process.pid, Run_Queue.head->process.left_cb);
	}
	else{
		printf("None ||");
	}

	if (Wait_Queue.size > 0){
		Wait_Queue.head->process.left_io -= TICK;
		Wait_Queue.head->process.running++;
		printf(" Process %d(%d) is Waiting (%d Left)\n", Wait_Queue.head->process.process_num, Wait_Queue.head->process.pid, Wait_Queue.head->process.left_io);
	}
	else{
		printf(" None\n");
	}
	
	if (Run_Queue.size > 0){
		if (Run_Queue.head->process.left_cb == 0){ //left CPU Burst =< Quantum //CPU COMPLETED!
			if (Run_Queue.head->process.left_io > 0){ //CPU COMPLETED! & IO LEFT!	
				Run_Queue.head->process.running = 0;
				message.type = Run_Queue.head->process.process_num + 10;
				message.msg_pinfo = dequeue(&Run_Queue);

				if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
					fprintf(stderr, "msgsnd() failed\n");
					exit(1);
				}

				enqueue(&Wait_Queue, message.msg_pinfo);
				printf("\n[*CPU COMPLETE] Process %d: CPU Done, IO Left <GO FROM CPU TO IO>\n", message.msg_pinfo.process_num);
			}
			else{ //CPU COMPLETED! & IO COMPLETED!
				Run_Queue.head->process.finished = tick_count*10;
				Run_Queue.head->process.Turnaround = Run_Queue.head->process.finished - Run_Queue.head->process.start;

				PI target = dequeue(&Run_Queue);
				target.WaitingTime = target.Turnaround - target.cpu_burst - target.io_burst;

				Report.count_DoneProcess += 1;
				Report.sum_Turnaround += target.Turnaround;
				Report.sum_Waiting += target.WaitingTime;
				Report.sum_Response += target.ResponseTime;				

				printf("\n==========================================================================\n");
				printf("       NUM  PID  CPU   IO\n");
				printf("[Done] %2d  %4d %4d %4d \n(Turnaround Time: %d, Waiting Time: %d, Response Time: %d)\n", target.process_num, target.pid, target.cpu_burst, target.io_burst, target.Turnaround, target.WaitingTime, target.ResponseTime);
				printf("{Average Turnaround Time: %d} {Average Waiting Time: %d} {Average Response Time: %d} (Count: %d)\n<Context Switching has occured %d times so far!>\n", Report.sum_Turnaround/Report.count_DoneProcess, Report.sum_Waiting/Report.count_DoneProcess, Report.sum_Response/Report.count_DoneProcess, Report.count_DoneProcess, Report.context_switching);
				printf("==========================================================================\n");		
				///////////////////////////////////////////////////////////////////////////////////
				message.msg_pinfo = target;
				message.type = target.process_num + 10;

				if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
					fprintf(stderr, "msgsnd() failed\n");
					exit(1);
				}

				message.type = target.process_num;
				
				while(1){
					if (msgrcv(msg_id, &message, sizeof(message) - sizeof(message.type), target.process_num, IPC_NOWAIT) != -1){
						break;
					}				
				}

				target = message.msg_pinfo;
				target.start = tick_count*10;

				enqueue(&Run_Queue, target);

				printf("             PID  CPU   IO  START <- Created Process\n");
				printf("[Process%2d] %4d %4d %4d   %d\n\n", message.msg_pinfo.process_num, message.msg_pinfo.pid, message.msg_pinfo.cpu_burst, message.msg_pinfo.io_burst, target.start);
			}
			Report.context_switching++;
			print(&Run_Queue, 'R');
			print(&Wait_Queue, 'W');
			printf("\n");
		} 

		else if (Run_Queue.head->process.running >= (QUANTUM/TICK)){ //left CPU Burst > Quantum //CPU LEFT!
			
			Run_Queue.head->process.running = 0;
			message.type = Run_Queue.head->process.process_num + 10;

			if (Run_Queue.head->process.left_io > 0){ //CPU LEFT & IO LEFT
				message.msg_pinfo = dequeue(&Run_Queue);
				if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
					fprintf(stderr, "msgsnd() failed\n");
					exit(1);
				}					
				int random = rand() % 2;

				if (random){//random == 1 : go to Run_Q
					enqueue(&Run_Queue, message.msg_pinfo);
					printf("\n[*QUANTUM COMPLETE] Process %d: CPU Left, IO Left <GO FROM CPU TO (random) CPU>\n", message.msg_pinfo.process_num);
				}
				else{//random == 0 : go to Wait_Q
					enqueue(&Wait_Queue, message.msg_pinfo);
					printf("\n[*QUANTUM COMPLETE] Process %d: CPU Left, IO Left <GO FROM CPU TO (random) IO>\n", message.msg_pinfo.process_num);
				}
			}
			else{ //CPU LEFT & IO DONE
				message.msg_pinfo = dequeue(&Run_Queue);
				if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
					fprintf(stderr, "msgsnd() failed\n");
					exit(1);
				}	
				enqueue(&Run_Queue, message.msg_pinfo);
				printf("\n[*QUANTUM COMPLETE] Process %d: CPU Left, IO Done <GO FROM CPU TO CPU>\n", message.msg_pinfo.process_num);
			}
			Report.context_switching++;
			print(&Run_Queue, 'R');
			print(&Wait_Queue, 'W');
			printf("\n");	
		}
	}

	if (Wait_Queue.size > 0){
		if (Wait_Queue.head->process.left_io == 0){ //left IO Burst =< Quantum //IO COMPLETED!
			if (Wait_Queue.head->process.left_cb > 0){ //CPU LEFT! & IO COMPLETED!	
				Wait_Queue.head->process.running = 0;
				message.type = Wait_Queue.head->process.process_num + 10;
				message.msg_pinfo = dequeue(&Wait_Queue);

				if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
					fprintf(stderr, "msgsnd() failed\n");
					exit(1);
				}

				enqueue(&Run_Queue, message.msg_pinfo);
				printf("\n[*IO COMPLETE] Process %d: CPU Left, IO Done <GO FROM IO TO CPU>\n", message.msg_pinfo.process_num);
			}
			else{ //CPU COMPLETED! & IO COMPLETED!
				Wait_Queue.head->process.finished = tick_count*10;
				Wait_Queue.head->process.Turnaround = Wait_Queue.head->process.finished - Wait_Queue.head->process.start;

				PI target = dequeue(&Wait_Queue);
				target.WaitingTime = target.Turnaround - target.cpu_burst - target.io_burst;

				Report.count_DoneProcess += 1;
				Report.sum_Turnaround += target.Turnaround;
				Report.sum_Waiting += target.WaitingTime;
				Report.sum_Response += target.ResponseTime;

				printf("\n==========================================================================\n");
				printf("       NUM  PID  CPU   IO\n");
				printf("[Done] %2d  %4d %4d %4d \n(Turnaround Time: %d, Waiting Time: %d, Response Time: %d)\n", target.process_num, target.pid, target.cpu_burst, target.io_burst, target.Turnaround, target.WaitingTime, target.ResponseTime);
				printf("{Average Turnaround Time: %d} {Average Waiting Time: %d} {Average Response Time: %d} (Count: %d)\n<Context Switching has occured %d times so far!>\n", Report.sum_Turnaround/Report.count_DoneProcess, Report.sum_Waiting/Report.count_DoneProcess, Report.sum_Response/Report.count_DoneProcess, Report.count_DoneProcess, Report.context_switching);
				printf("==========================================================================\n");		
			
				///////////////////////////////////////////////////////////////////////////////////
				message.msg_pinfo = target;
				message.type = target.process_num + 10;

				if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
					fprintf(stderr, "msgsnd() failed\n");
					exit(1);
				}

				message.type = target.process_num;

				while(1){
					if (msgrcv(msg_id, &message, sizeof(message) - sizeof(message.type), target.process_num, 0) != -1){
						break;
					}
				}

				target = message.msg_pinfo;
				target.start = tick_count*10;

				enqueue(&Run_Queue, target);

				printf("             PID  CPU   IO  START <- Created Process\n");
				printf("[Process%2d] %4d %4d %4d   %d\n\n", message.msg_pinfo.process_num, message.msg_pinfo.pid, message.msg_pinfo.cpu_burst, message.msg_pinfo.io_burst, target.start);
			}
			Report.context_switching++;
			print(&Run_Queue, 'R');
			print(&Wait_Queue, 'W');
			printf("\n");				
		} 

		else if (Wait_Queue.head->process.running >= (QUANTUM/TICK)){ //left IO Burst > Quantum //IO LEFT!
			
			Wait_Queue.head->process.running = 0;
			message.type = Wait_Queue.head->process.process_num + 10;

			if (Wait_Queue.head->process.left_cb > 0){ //CPU LEFT & IO LEFT
				message.msg_pinfo = dequeue(&Wait_Queue);
				if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
					fprintf(stderr, "msgsnd() failed\n");
					exit(1);
				}		
				int random = rand() % 2;

				if (!random){//random == 0 : go to Run_Q
					enqueue(&Run_Queue, message.msg_pinfo);
					printf("\n[*QUANTUM COMPLETE] Process %d: CPU Left, IO Left <GO FROM IO TO (random) CPU>\n", message.msg_pinfo.process_num);
				}
				else{//random == 1 : go to Wait_Q
					enqueue(&Wait_Queue, message.msg_pinfo);
					printf("\n[*QUANTUM COMPLETE] Process %d: CPU Left, IO Left <GO FROM IO TO (random) IO>\n", message.msg_pinfo.process_num);
				}
			}
			else{ //CPU DONE & IO LEFT
				message.msg_pinfo = dequeue(&Wait_Queue);
				if (msgsnd(msg_id, (void*)&message, sizeof(message) - sizeof(message.type), 0) == -1){
					fprintf(stderr, "msgsnd() failed\n");
					exit(1);
				}			
				enqueue(&Wait_Queue, message.msg_pinfo);
				printf("\n[*QUANTUM COMPLETE] Process %d: CPU Done, IO Left <GO FROM IO TO IO>\n", message.msg_pinfo.process_num);
			}
			Report.context_switching++;
			print(&Run_Queue, 'R');
			print(&Wait_Queue, 'W');
			printf("\n");	
		}
	}
}

void init(Q* md) {
	md->head = NULL;
	md->tail = NULL;
	md->size = 0;
}
void enqueue(Q* md, PI pcs) {

	NODE* new = (NODE*)malloc(sizeof(NODE));
	new->process = pcs;
	new->prev = NULL;
	new->next = NULL;

	if (md->size == 0) { //처음 add하는 거라면
		md->tail = new;
		md->head = new;
	}
	else {
		new->prev = md->tail;
		md->tail->next = new;
		md->tail = new;
	}
	md->size++;
}
PI dequeue(Q* md) {
    if (md->size == 0){
        printf("  [Error] Nothing to delete\n");
        //return NULL;
    }
	PI dequeue_process = md->head->process; //뽑아낼 데이터를 따로 저장
	NODE* target = md->head;

	if (md->size == 1) { //마지막 data를 delete하는 거라면
		md->head = NULL;
		md->tail = NULL;
	}
	else {
		md->head->next->prev = md->head->prev;
		md->head = md->head->next;
	}

	free(target);
	md->size--;

	return dequeue_process;
}
void print(Q* md, char qt) {
	if (qt == 'R'){
		printf("RUN QUEUE");
	}
	else{
		printf("WAIT QUEUE");
	}
	int sz = md->size;

	for (int i = 0; i < sz; i++) {
		PI tmp = dequeue(md);
		printf("<%d(%d,%d)", tmp.process_num, tmp.left_cb, tmp.left_io);
		enqueue(md, tmp);
	}

	printf("\n");
}
void clear(Q* md) {
	int sz = md->size;
	for (int i = 0; i < sz; i++) { //data의 수만큼 dequeue
		dequeue(md);
	}
	printf("(Queue) Clear Finished\n");
}