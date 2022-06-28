// João Pedro Pacheco Silva nº 2018298731
// Rui Alexandre Vale Alves Azevedo Abreu nº 2018275587
// gcc projeto.c -pthread -Wall -o projeto

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h> 
#include <fcntl.h>
#include <sys/stat.h> 
#include <pthread.h>
#include <sys/msg.h>
#include <signal.h>
#include <errno.h>

#define PIPE_NAME "input_pipe" 
#define SHMBUFF 40
#define NORMAL_MSG 20L
#define URGENT_MSG 10L
#define DEPARTURE_TYPE 0
#define ARRIVAL_TYPE 1
#define URGENT_TYPE 2
#define REJECTED -1
#define L01 0
#define R01 1
#define L28 2
#define R28 3
#define PERCENTAGEM 50
#define NCOMMANDS 2000
//#define DEBUG //remove this line to remove debug messages
#define TESTMODE 

int ut, T, dt, L, dl, min, max, D, A;

int shmid_esta, shmid_voos, shmid_temp, fd, mqid, shmid_pistas;
sem_t *sem_esta, *sem_log, *sem_temp, *sem_sort, *sem_atualiza, *sem_inicia, *sem_esc, *sem_pistas, *sem_terminate;
	
int pid_commands, pid_cronometro, pid_torre, main_pid;

int AL, AR, DL, DR;

pthread_mutex_t mutex_lista=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_departures=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_arrivals=PTHREAD_MUTEX_INITIALIZER;

pthread_t t_pipe;
pthread_t *t_voo;
pthread_t tarefas[4];
pthread_t t_main;

FILE* log_file;
struct estatistica *esta;
struct sh_voos *flights;
int *temp;
int *pistas;

struct estatistica{
	int voos_criados;
	int aterragens;
	float espera_aterragem;
	int descolagens;
	float espera_descolagem;
	float numero_holdings;
	int numero_arrivals;
	int urgencias;
	float numero_holdings_urgencia;
	int redirecionados;
	int rejeitados;
};

struct voos{
	int init;
	int fuel;
	int eta;
	int takeoff;
	int tipo;
	char nome[7];
	int voo_id;
};

struct sh_voos{
	struct voos info;
	sem_t sem_voos;
	sem_t sinal;
};

struct mensagem{
	long mtype;
	struct voos info;
};

struct resposta{
	long mtype;
	int index;
};

typedef struct lnode *Voos;
typedef struct lnode {
	struct voos info;
	Voos next;
} List_node;

Voos lista;

typedef struct lnode2 *Thread_voos;
typedef struct lnode2 {
	int info;
	Thread_voos next;
} List_node2;

Thread_voos Ldepartures;
Thread_voos Larrivals;

int voos_criados(){
	sem_wait(sem_esta);
	int resultado= esta->voos_criados;
	sem_post(sem_esta);
	return resultado;
}

int fuel(int index){
	sem_wait(&flights[index].sem_voos);
	int resultado =flights[index].info.fuel;
	sem_post(&flights[index].sem_voos);
	return resultado;
}

int eta(int index){
	sem_wait(&flights[index].sem_voos);
	int resultado =flights[index].info.eta;
	sem_post(&flights[index].sem_voos);
	return resultado;
}

int takeoff(int index){
	sem_wait(&flights[index].sem_voos);
	int resultado =flights[index].info.takeoff;
	sem_post(&flights[index].sem_voos);
	return resultado;
}

int Tempo(){
	sem_wait(sem_temp);
	int resultado= *temp;
	sem_post(sem_temp);
	return resultado;
}

int urgente(int index){
	sem_wait(&flights[index].sem_voos);
	int resultado =flights[index].info.tipo;
	sem_post(&flights[index].sem_voos);
	if(resultado==URGENT_TYPE) return 1;
	else return 0;
}

int fim_do_combustivel(int index){
	sem_wait(&flights[index].sem_voos);
	int resultado= flights[index].info.init + flights[index].info.fuel;
	sem_post(&flights[index].sem_voos);
	return resultado;
}

int tempo_ate_chegar(int index){
	sem_wait(sem_temp);
	sem_wait(&flights[index].sem_voos);
	int resultado= flights[index].info.init + flights[index].info.eta - *temp;
	sem_post(&flights[index].sem_voos);
	sem_post(sem_temp);
	return resultado;
}

int tempo_ate_descolar(int index){
	sem_wait(sem_temp);
	sem_wait(&flights[index].sem_voos);
	int resultado= flights[index].info.takeoff - *temp;
	sem_post(&flights[index].sem_voos);
	sem_post(sem_temp);
	if(resultado<=0) resultado=0;
	return resultado;
}

int fim_da_aterragem(int index){
	sem_wait(sem_temp);
	sem_wait(&flights[index].sem_voos);
	int resultado= *temp + L;
	sem_post(&flights[index].sem_voos);
	sem_post(sem_temp);
	int extra= tempo_ate_chegar(index);
	if(extra>0) resultado+= extra;
	return resultado;
}

int fim_da_descolagem(int index){
	sem_wait(sem_temp);
	sem_wait(&flights[index].sem_voos);
	int resultado= *temp + T;
	sem_post(&flights[index].sem_voos);
	sem_post(sem_temp);
	int extra= tempo_ate_descolar(index);
	if(extra>0) resultado+= extra;
	return resultado;
}

Voos cria_lista (){
	Voos aux;
	aux = (Voos) malloc (sizeof (List_node));
	if (aux != NULL) {
		aux->info.init = -1;
		aux->info.fuel=-1;
		aux->info.eta=-1;
		aux->info.takeoff=-1;
		aux->info.tipo=-1;
		aux->info.voo_id=-1;
		aux->next = NULL;
	}
	return aux;
}

Thread_voos cria_lista_thread (){
	Thread_voos aux;
	aux = (Thread_voos) malloc (sizeof (List_node2));
	if (aux != NULL) {
		aux->info= 0;
		aux->next = NULL;
	}
	return aux;
}

void procura_lista (Voos lista, int chave, Voos *ant, Voos *actual){
	*ant = lista; *actual = lista->next;
	while ((*actual) != NULL && (*actual)->info.init < chave){
		*ant = *actual;
		*actual = (*actual)->next;
	}
	if ((*actual) != NULL && (*actual)->info.init != chave)
		*actual = NULL;
}

void procura_lista_thread_departure (Thread_voos lista, int chave, Thread_voos *ant, Thread_voos *actual){
	*ant = lista; *actual = lista->next;

	while ((*actual) != NULL){
		if(tempo_ate_descolar((*actual)->info) < tempo_ate_descolar(chave)){
			*ant = *actual;
			*actual = (*actual)->next;
		}
		else break;

	}
}

void procura_lista_thread_arrival (Thread_voos lista, int chave, Thread_voos *ant, Thread_voos *actual){
	*ant = lista; *actual = lista->next;
	if(tempo_ate_chegar(chave)>0){
		while ((*actual) != NULL){
			if(tempo_ate_chegar((*actual)->info)< tempo_ate_chegar(chave)){
				*ant = *actual;
				*actual = (*actual)->next;
			}
			else break;
		}
		while ((*actual) != NULL){
			if(tempo_ate_chegar((*actual)->info) == tempo_ate_chegar(chave) && fuel((*actual)->info) < fuel(chave)){
				*ant = *actual;
				*actual = (*actual)->next;
			}
			else break;
		}
		while ((*actual) != NULL){
			if(tempo_ate_chegar((*actual)->info) == tempo_ate_chegar(chave) && fuel((*actual)->info) == fuel(chave) && urgente(chave)){
				*ant = *actual;
				*actual = (*actual)->next;
			}
			else break;
		}
	}
	else{
		while ((*actual) != NULL){
			if(tempo_ate_chegar((*actual)->info) <= 0 && fuel((*actual)->info) < fuel(chave)){
				*ant = *actual;
				*actual = (*actual)->next;
			}
			else break;
		}
		while ((*actual) != NULL){
			if(tempo_ate_chegar((*actual)->info) <= 0 && fuel((*actual)->info) == fuel(chave) && tempo_ate_chegar((*actual)->info) < tempo_ate_chegar(chave)){
				*ant = *actual;
				*actual = (*actual)->next;
			}
			else break;
		}
	}
}

void pesquisa_lista_thread(Thread_voos lista, int chave, Thread_voos *ant, Thread_voos *actual){
	*ant = lista; *actual = lista->next;

	while ((*actual) != NULL && (*actual)->info != chave){
		*ant = *actual;
		*actual = (*actual)->next;
	}
}

void elimina_lista (Voos lista, int it){
	Voos ant;
	Voos actual;
	procura_lista (lista, it, &ant, &actual);
	if (actual != NULL) {
		ant->next = actual->next;
	}
}

void elimina_lista_thread (Thread_voos lista, int it){
	Thread_voos ant;
	Thread_voos actual;
	pesquisa_lista_thread (lista, it, &ant, &actual);
	if (actual != NULL) {
		ant->next = actual->next;
		free (actual);
	}
}

void insere_lista (Voos lista, struct voos it){
	Voos no;
	Voos ant, inutil;
	no = (Voos) malloc (sizeof (List_node));
	if (no != NULL) {
		no->info.tipo= it.tipo;
		memcpy(no->info.nome, it.nome, sizeof (it.nome));
		no->info.init = it.init;
		no->info.eta = it.eta;
		no->info.takeoff = it.takeoff;
		no->info.fuel = it.fuel;
		procura_lista (lista, it.init, &ant, &inutil);
		no->next = ant->next;
		ant->next = no;
	}
}

void insere_lista_thread_departure (Thread_voos lista, int it){
	Thread_voos no;
	Thread_voos ant, inutil;
	no = (Thread_voos) malloc (sizeof (List_node2));
	if (no != NULL) {
		no->info= it;
		procura_lista_thread_departure (lista, it, &ant, &inutil);
		no->next = ant->next;
		ant->next = no;
	}
}

void insere_lista_thread_arrival (Thread_voos lista, int it){
	Thread_voos no;
	Thread_voos ant, inutil;
	no = (Thread_voos) malloc (sizeof (List_node2));
	if (no != NULL) {
		no->info= it;
		procura_lista_thread_arrival (lista, it, &ant, &inutil);
		no->next = ant->next;
		ant->next = no;
	}
}

int lista_vazia(Voos lista){
	return (lista->next == NULL ? 1 : 0);
}

int lista_thread_vazia(Thread_voos lista){
	return (lista->next == NULL ? 1 : 0);
}

Voos destroi_lista (Voos lista)
{
	Voos temp_ptr;
	while (lista_vazia (lista) == 0) {
		temp_ptr = lista;
		lista= lista->next;
		free (temp_ptr);
	}
	free(lista);
	return NULL;
}

Thread_voos destroi_lista_thread (Thread_voos lista)
{
	Thread_voos temp_ptr;
	while (lista_thread_vazia (lista) == 0) {
		temp_ptr = lista;
		lista= lista->next;
		free (temp_ptr);
	}
	free(lista);
	return NULL;
}

char *hora(){
	time_t timer;
	struct tm *calendario;
	char momento[20];
	char *ptr;
	time(&timer);
   	calendario= localtime(&timer);
	memcpy(momento, asctime(calendario), sizeof(momento));
	momento[19]=0;
	ptr=&momento[11];
	return ptr;
}

void initialize(){
	log_file=fopen("log.txt","w");
	fclose(log_file);
	log_file=fopen("log.txt","a");
	char *ptr=hora();
	printf("%s program started\n", ptr);
	fprintf(log_file,"%s program started\n", ptr);
	FILE *file;
	if ((file=fopen("config.txt","r"))==NULL){
		printf("Cannot open config.txt file\n");
		exit(0);
	}
	#ifdef DEBUG
	printf("Aplicando as configuracoes presentes no ficheiro config.txt\n");
	#endif
	fscanf(file, "%d", &ut);
	fscanf(file, "%d, %d", &T, &dt);
	fscanf(file, "%d, %d", &L, &dl);
	fscanf(file, "%d, %d", &min, &max);
	fscanf(file, "%d", &D);
	fscanf(file, "%d", &A);
	fclose(file);

	if(!(ut && T && dt && L && dl && min && max && D && A)){
		printf("Error reading config.txt file\n");
		exit(0);
	}
	
	#ifdef DEBUG
	printf("Criando memoria partilhada\n");
	#endif
	shmid_temp = shmget(IPC_PRIVATE, 1*sizeof(int), IPC_CREAT|0700);
	if (shmid_temp < 1) exit(0);
	temp= (int*)shmat(shmid_temp, NULL, 0);
	shmid_esta = shmget(IPC_PRIVATE, sizeof(struct estatistica), IPC_CREAT|0700);
	if (shmid_esta < 1) exit(0);
	esta= (struct estatistica*)shmat(shmid_esta, NULL, 0);
	esta->voos_criados= 0;
	esta->aterragens= 0;
	esta->espera_aterragem= 0;
	esta->descolagens= 0;
	esta->espera_descolagem= 0;
	esta->numero_holdings= 0;
	esta->numero_arrivals= 0;
	esta->urgencias= 0;
	esta->numero_holdings_urgencia= 0;
	esta->redirecionados= 0;
	esta->rejeitados= 0;
	shmid_voos = shmget(IPC_PRIVATE, (A+D)*sizeof(struct sh_voos), IPC_CREAT|0700);
	if (shmid_voos < 1) exit(0);
	flights= (struct sh_voos*)shmat(shmid_voos, NULL, 0);
	shmid_pistas = shmget(IPC_PRIVATE, 4*sizeof(int), IPC_CREAT|0700);
	if (shmid_pistas < 1) exit(0);
	pistas= (int*)shmat(shmid_pistas, NULL, 0);
	for(int i=0; i<4; i++){
		pistas[i]= 1;
	}
	#ifdef DEBUG
	printf("Criando semaforos\n");
	#endif
	sem_unlink("sem_esta");
	sem_esta = sem_open("sem_esta",O_CREAT|O_EXCL,0700,1);
	sem_unlink("sem_log");
	sem_log = sem_open("sem_log",O_CREAT|O_EXCL,0700,1);
	sem_unlink("sem_temp");
	sem_temp = sem_open("sem_temp",O_CREAT|O_EXCL,0700,1);
	sem_unlink("sem_sort");
	sem_sort = sem_open("sem_sort",O_CREAT|O_EXCL,0700,0);
	sem_unlink("sem_atualiza");
	sem_atualiza = sem_open("sem_atualiza",O_CREAT|O_EXCL,0700,0);
	sem_unlink("sem_inicia");
	sem_inicia = sem_open("sem_inicia",O_CREAT|O_EXCL,0700,0);
	sem_unlink("sem_esc");
	sem_esc = sem_open("sem_esc",O_CREAT|O_EXCL,0700,0);
	sem_unlink("sem_pistas");
	sem_pistas = sem_open("sem_pistas",O_CREAT|O_EXCL,0700,1);
	sem_unlink("sem_terminate");
	sem_terminate = sem_open("sem_terminate",O_CREAT|O_EXCL,0700,0);
	for(int i=0; i<A+D; i++){
		sem_init(&flights[i].sem_voos, 1, 1);
		sem_init(&flights[i].sinal, 1, 0);
	}
	#ifdef DEBUG
	printf("Criando named pipe\n");
	#endif
	if ((mkfifo(PIPE_NAME, O_CREAT|O_EXCL|0700)<0) && (errno!= EEXIST)){
    		perror("Cannot create pipe\n");
    		exit(0);
  	}
	#ifdef DEBUG
	printf("Criando fila de mensagens\n");
	#endif
	mqid = msgget(IPC_PRIVATE, IPC_CREAT|0700);
	if (mqid < 0){
      		perror("Error creating MQ\n");
      		exit(0);
    	}
}

void terminate(){
	char *ptr;
	char commands[50];
	if(getpid()==main_pid){
		if(pthread_self()!=t_main){
			pthread_exit(NULL);
		}
		pthread_cancel(t_pipe);
		close(fd);
		#ifdef TESTMODE
		waitpid(pid_commands, NULL, 0);
		#endif

		pthread_join(t_pipe, NULL);
		fd = open(PIPE_NAME, O_RDONLY|O_NONBLOCK);
		while(read(fd, commands, sizeof(commands))==sizeof(commands)){
			sem_wait(sem_log);
			ptr=hora();
			printf("%s %s\n", ptr, commands);
			fprintf(log_file,"%s %s\n", ptr, commands);
			sem_post(sem_log);
		}
		close(fd);

		for(int i=0; i<voos_criados(); i++){
			pthread_join(t_voo[i], NULL);
		}
		
		sem_post(sem_terminate);
		waitpid(pid_torre, NULL, 0);
		sem_wait(sem_temp);
		*temp=-1;
		sem_post(sem_temp);
		waitpid(pid_cronometro, NULL, 0);
		
		sem_close(sem_esta);
		sem_unlink("sem_esta");
		sem_close(sem_log);
		sem_unlink("sem_log");
		sem_close(sem_temp);
		sem_unlink("sem_temp");
		sem_close(sem_sort);
		sem_unlink("sem_sort");
		sem_close(sem_atualiza);
		sem_unlink("sem_atualiza");
		sem_close(sem_inicia);
		sem_unlink("sem_inicia");
		sem_close(sem_esc);
		sem_unlink("sem_esc");
		sem_close(sem_pistas);
		sem_unlink("sem_pistas");
		sem_close(sem_terminate);
		sem_unlink("sem_terminate");

		for(int i=0; i<A+D; i++){
			sem_destroy(&flights[i].sem_voos);
			sem_destroy(&flights[i].sinal);
		}

		pthread_mutex_destroy(&mutex_lista);
		pthread_mutex_destroy(&mutex_departures);
		pthread_mutex_destroy(&mutex_arrivals);

		shmctl(shmid_esta, IPC_RMID, NULL);
		shmctl(shmid_voos, IPC_RMID, NULL);
		shmctl(shmid_temp, IPC_RMID, NULL);
		shmctl(shmid_pistas, IPC_RMID, NULL);

		msgctl(mqid, IPC_RMID, NULL);

		ptr=hora();
		printf("%s program concluded\n", ptr);
		fprintf(log_file,"%s program concluded", ptr); 
		fclose(log_file);
	}
	else{
		sem_wait(sem_terminate);
		for(int i=0; i<4; i++){
			pthread_cancel(tarefas[i]);
		}	
	}
	exit(0);
}

void *atualiza(){
	sigset_t set;
	sigfillset(&set);

	pthread_sigmask (SIG_BLOCK, &set, NULL);
	Thread_voos aux;
	while(1){
		sem_wait(sem_atualiza);
		pthread_mutex_lock(&mutex_arrivals);
		aux= Larrivals->next;
		while(aux!=NULL){
			sem_wait(&flights[aux->info].sem_voos);
			flights[aux->info].info.fuel--;
			sem_post(&flights[aux->info].sem_voos);
			//printf("voo %d -> %d\n", aux->info , fuel(aux->info));
			if(fuel(aux->info)==0){
				sem_post(sem_sort);
				sem_post(&flights[aux->info].sinal);
				Larrivals->info--;
			}
			else if(tempo_ate_chegar(aux->info)==0){
				#ifdef DEBUG
				printf("voo %d chegou\n", aux->info);
				#endif
				sem_post(sem_sort);
				Larrivals->info--;
			}
			aux=aux->next;
		}
		if(Larrivals->info==0) sem_post(sem_esc);
		pthread_mutex_unlock(&mutex_arrivals);
	}
	pthread_exit(NULL);
} 

void *ordena(){
	sigset_t set;
	sigfillset(&set);

	pthread_sigmask (SIG_BLOCK, &set, NULL);

	Thread_voos aux;
	int i;
	int n=0;
	int holding;
	srand(getpid());
	while(1){
		sem_wait(sem_sort);
		pthread_mutex_lock(&mutex_arrivals);
		aux= Larrivals->next;
		i=0;
		while(aux!=NULL){
			if(fuel(aux->info)==0){
				elimina_lista_thread(Larrivals, aux->info);
				break;
			}
			if(tempo_ate_chegar(aux->info)== 0 && i>5){
				holding =rand()%(max-min)+min; 
				flights[aux->info].info.eta+= holding;
				if(fuel(aux->info)<= holding) Larrivals->info--; 
				n=aux->info;
				#ifdef DEBUG
				printf("voo %d holding\n", n);
				#endif

				elimina_lista_thread(Larrivals, n);
				insere_lista_thread_arrival(Larrivals, n);
				sem_post(&flights[n].sinal);

				break;
			}
			aux=aux->next;
			i++;
		}
		aux= Larrivals->next;
		i=0;
		if(aux!=NULL){
			while(aux->next!=NULL && i<=5){
				if(tempo_ate_chegar(aux->next->info)<=0 && fuel(aux->info) > fuel(aux->next->info)){
					n=aux->next->info;
					if(aux->next->next!=NULL){
						aux=aux->next->next;
						i++;
					}
					else aux=aux->next;
					i++;
					elimina_lista_thread(Larrivals, n);
					insere_lista_thread_arrival(Larrivals, n);
					continue;
				}
				else if(tempo_ate_chegar(aux->next->info)<=0 && fuel(aux->info) == fuel(aux->next->info) && tempo_ate_chegar(aux->info)>tempo_ate_chegar(aux->next->info)){
					n=aux->next->info;
					if(aux->next->next!=NULL){
						aux=aux->next->next;
						i++;
					}
					else aux=aux->next;
					i++;
					elimina_lista_thread(Larrivals, n);
					insere_lista_thread_arrival(Larrivals, n);
					continue;
				}
				i++;
				aux=aux->next;
			}
		}
		Larrivals->info++;
		if(Larrivals->info==0) sem_post(sem_esc);
		pthread_mutex_unlock(&mutex_arrivals);	
	}
	pthread_exit(NULL);
}

void aterrar(int index, int Pista){
	sem_wait(sem_pistas);
	pistas[Pista]=0;
	sem_post(sem_pistas);

	sem_wait(&flights[index].sem_voos);
	//printf("%s %d\n",flights[index].info.nome, flights[index].info.fuel);
	flights[index].info.takeoff=Pista;
	sem_post(&flights[index].sem_voos);
	sem_post(&flights[index].sinal);
	elimina_lista_thread(Larrivals, index);
}

void descolar(int index, int Pista){
	sem_wait(sem_pistas);
	pistas[Pista]=0;
	sem_post(sem_pistas);
	
	sem_wait(&flights[index].sem_voos);
	//printf("%s %d\n",flights[index].info.nome, index);
	flights[index].info.fuel=Pista;
	sem_post(&flights[index].sem_voos);
	sem_post(&flights[index].sinal);
	elimina_lista_thread(Ldepartures, index);
}

int pista(int index){
	sem_wait(sem_pistas);
	int resultado= pistas[index];
	sem_post(sem_pistas);
	return resultado;
}

int menor_arrival(){
	Thread_voos aux= Larrivals->next;
	int i=0;
	int resultado;
	if(aux!=NULL){
		resultado=aux->info;
		aux=aux->next;
		i++;
	}
	while(aux!=NULL && i<=5){
		if (fim_do_combustivel(resultado)>fim_do_combustivel(aux->info))
			resultado=aux->info;
		aux=aux->next;
		i++;
	}
	return resultado;
}

int risco_de_holding(){
	Thread_voos aux= Larrivals->next;
	int i=0;
	while(aux!=NULL && i<6 && tempo_ate_chegar(aux->info)<=0){
		i++;
	}
	if(i>=4) return 1;
	return 0;
}
int risco_de_espera(){
	Thread_voos aux= Ldepartures->next;
	int i=0;
	while(aux!=NULL && i<7 && tempo_ate_descolar(aux->info)==0){
		i++;
	}
	if(i>=7) return 1;
	return 0;
}

int int_extra(int int_x, int index){
	int resultado = int_x-eta(index);
	if(resultado<0) return 0;
	return resultado;
}

void *escaloneamento(){
	sigset_t set;
	sigfillset(&set);

	pthread_sigmask (SIG_BLOCK, &set, NULL);

	int dep1;
	int dep2;
	int arr1;
	int arr2;
	int menor_arr;
	int arrivals_autorizados;
	int int_a=0;
	int int_d=0;
	while(1){
		sem_wait(sem_esc);
		if(int_a) int_a--;
		if(int_d) int_d--;
		if(pista(L01) && pista(R01) && pista(L28) && pista(R28)){
			pthread_mutex_lock(&mutex_arrivals);
			pthread_mutex_lock(&mutex_departures);
			if(Ldepartures->next!=NULL){
				dep1 = Ldepartures->next->info;
				
				if(Ldepartures->next->next!=NULL) dep2 = Ldepartures->next->next->info;
				else dep2=-1;
			}
			else{
				dep1=-1;
				dep2=-1;
			}
			arrivals_autorizados= 0;
			if(Larrivals->next!=NULL){
				menor_arr= menor_arrival();
				arr1= Larrivals->next->info;
				if(dep1 != -1){
					if(fim_do_combustivel(menor_arr) > fim_da_descolagem(dep1)+int_extra(int_d, dep1)){
						if(risco_de_holding()==0 || risco_de_espera()){
							if(int_d==0){
								if(tempo_ate_descolar(dep1)==0){
									if(dep2 != -1){
										if(tempo_ate_descolar(dep2)==0){
											//--------descola dep1---------
											descolar(dep1, L01);
											//-----------------------------
											int_d=dt+T;
											//--------aterra dep2----------
											descolar(dep2, R01);
											//-----------------------------
										}
										else{
											if(fim_do_combustivel(menor_arr) > fim_da_descolagem(dep2)){
												if(tempo_ate_descolar(dep2)> (dt+T)/2){
													//--------descola dep1---------
													descolar(dep1, L01);
													//-----------------------------
													int_d=dt+T;
												}
												else{
													if(tempo_ate_chegar(arr1)<=0 && int_a==0){
														if (fim_da_aterragem(arr1) < tempo_ate_descolar(dep2)) arrivals_autorizados= 1;
													}
												}	
											}
											else{
												//--------descola dep1---------
												descolar(dep1, L01);
												//-----------------------------
												int_d=dt+T;
											}
										}
									}
									else{
										//--------descola dep1---------
										descolar(dep1, L01);
										//-----------------------------
										int_d=dt+T;
									}
								}
								else{
									if(dep2 != -1){
										if(fim_do_combustivel(menor_arr) > fim_da_descolagem(dep2)){
											if(tempo_ate_descolar(dep2)<(dt+fim_da_descolagem(dep1))/2){
												if(tempo_ate_chegar(arr1)<=0 && int_a==0){
													if (2*fim_da_aterragem(arr1)/3 < tempo_ate_descolar(dep2)) arrivals_autorizados= 1;
												}
											}
											else{
												if(tempo_ate_chegar(arr1)<=0 && int_a==0){
													if (2*fim_da_aterragem(arr1)/3 < tempo_ate_descolar(dep1)) arrivals_autorizados= 1;
												}
											}	
										}
										else {
											if(tempo_ate_chegar(arr1)<=0 && int_a==0){
												if (2*fim_da_aterragem(arr1)/3 < tempo_ate_descolar(dep1)) arrivals_autorizados= 1;
											}
										}
									}
									else{
										if(tempo_ate_chegar(arr1)<=0 && int_a==0){
											if (2*fim_da_aterragem(arr1)/3 < tempo_ate_descolar(dep1)) arrivals_autorizados= 1;
										}
									}
								}
							}
							else{
								if(dep2 != -1 && tempo_ate_descolar(dep2)>0){
									if(fim_do_combustivel(menor_arr) > fim_da_descolagem(dep2)+int_extra(int_d, dep2)){
										if(tempo_ate_descolar(dep2)+int_extra(int_d, dep2)<(dt+fim_da_descolagem(dep1)+int_extra(int_d, dep1))/2){
											if(tempo_ate_chegar(arr1)<=0 && int_a==0){
												if (2*fim_da_aterragem(arr1)/3 < tempo_ate_descolar(dep2)+int_extra(int_d, dep2)) 														arrivals_autorizados= 1;
											}
										}
										else{
											if(tempo_ate_chegar(arr1)<=0 && int_a==0){
												if (2*fim_da_aterragem(arr1)/3 < tempo_ate_descolar(dep1)+int_extra(int_d, dep1)) 														arrivals_autorizados= 1;
											}
										}	
									}
									else {
										if(tempo_ate_chegar(arr1)<=0 && int_a==0){
											if (2*fim_da_aterragem(arr1)/3 < tempo_ate_descolar(dep1)+int_extra(int_d, dep1)) 													arrivals_autorizados= 1;
										}
									}
								}
								else{
									if(tempo_ate_chegar(arr1)<=0 && int_a==0){
										if (2*fim_da_aterragem(arr1)/3 < tempo_ate_descolar(dep1)+int_extra(int_d, dep1)) 												arrivals_autorizados= 1;
									}
								}

							}
						}
						else {
							arrivals_autorizados= 1;
						}
					}
					else arrivals_autorizados= 1;
				}
				else arrivals_autorizados= 1;
			
				if (arrivals_autorizados){
					if(int_a==0){
						if(tempo_ate_chegar(menor_arr)<=0){
							if(Larrivals->next->next!=NULL){
								arr2= Larrivals->next->next->info;
								if(tempo_ate_chegar(arr2)<=0){
									//--------aterra menor_arr---------
									aterrar(arr1, L28);
 									//----------------------------
									int_a=dl+L;
									//--------aterra arr2---------
									aterrar(arr2, R28); 
									//----------------------------
								}
								else {
									if(fim_do_combustivel(menor_arr)>tempo_ate_chegar(arr2)
									   && tempo_ate_chegar(arr2)< (dl+L)/2){
									}	
									else{
										//--------aterra menor_arr---------
										aterrar(menor_arr, L28);
 										//----------------------------
										int_a=dl+L;
									}
								}
							}
							else{
								//--------aterra menor_arr---------
								aterrar(menor_arr, L28);
 								//----------------------------
								int_a=dl+L;
							}
						}
						else{
							if(tempo_ate_chegar(arr1)<=0){
								if(fim_da_aterragem(arr1)+dl < fim_do_combustivel(menor_arr)){
									if(Larrivals->next->next!=NULL){
										arr2 = Larrivals->next->next->info;
										if(tempo_ate_chegar(arr2)<=0){
											//--------aterra arr1---------
											aterrar(arr1, L28);
											//----------------------------
											int_a=dl+L;
											//--------aterra arr2---------
											aterrar(arr2, R28);
											//----------------------------
								
										}
										else{
											if(fim_do_combustivel(arr1)>tempo_ate_chegar(arr2) && 												   fim_do_combustivel(menor_arr)>fim_da_aterragem(arr2)+dl && 
											   tempo_ate_chegar(arr2)< (dl+L)/2){
											}	
											else{
												//--------aterra arr1---------
												aterrar(arr1, L28);
 												//----------------------------
												int_a=dl+L;
											}
										}
									}
									else{
										//--------aterra arr1---------
										aterrar(arr1, L28);
										//----------------------------
										int_a=dl+L;
									}
								}
								else{
									if(tempo_ate_chegar(menor_arr)>=fim_do_combustivel(arr1)){
										if(Larrivals->next->next!=NULL){
											arr2 = Larrivals->next->next->info;
											if(tempo_ate_chegar(arr2)<=0){
												//--------aterra arr1---------
												aterrar(arr1, L28);
												//----------------------------
												int_a=dl+L;
												//--------aterra arr2---------
												aterrar(arr2, R28);
												//----------------------------
											}
											else{
												if(fim_do_combustivel(arr1)>tempo_ate_chegar(arr2) && 						 
												   tempo_ate_chegar(arr2)< (dl+L)/2){
												}	
												else{
													//--------aterra arr1---------
													aterrar(arr1, L28);
 													//----------------------------
													int_a=dl+L;
												}
											}
										}	
										else{	
											//--------aterra arr1---------
											aterrar(arr1, L28); 
											//----------------------------
											int_a=dl+L;
										}
									}
								}
							}
						}
					}
				}
			}
			else{
				if(int_d==0){
					if(dep1!=-1){
						if(tempo_ate_descolar(dep1)==0){
							if(dep2!=-1){
								if(tempo_ate_descolar(dep2)==0){
									//--------descola dep1---------
									descolar(dep1, L01);
									//-----------------------------
									int_d=dt+T;
									//--------aterra dep2----------
									descolar(dep2, R01);
									//-----------------------------
								}
								else{
									if(tempo_ate_descolar(dep2)>(dt+T)/2){
										//--------descola dep1---------
										descolar(dep1, L01);
										//-----------------------------
										int_d=dt+T;
									}
								}
							}
							else{
								//--------descola dep1---------
								descolar(dep1, L01);
								//-----------------------------
								int_d=dt+T;
							}
						}		
					}
				}
			}
			pthread_mutex_unlock(&mutex_departures);
			pthread_mutex_unlock(&mutex_arrivals);
		}
	}
	pthread_exit(NULL);
}

void *responde(){
	sigset_t set;
	sigfillset(&set);

	pthread_sigmask (SIG_BLOCK, &set, NULL);

	struct mensagem msg;
	struct resposta answer;
	int d=0;
	int a=0;
	while(1){
		msgrcv(mqid, &msg, sizeof(struct mensagem)-sizeof(long), -NORMAL_MSG, 0);
		#ifdef DEBUG
		printf("Numero de aterragens: %d\nNumero de descolagens: %d\n", a, d);
		printf("Mensagem de %s recebida\n", msg.info.nome);
		#endif
		

		answer.mtype= msg.info.voo_id;
		if(msg.info.tipo==DEPARTURE_TYPE ){
			if(d==D) answer.index= REJECTED;
			else{
				answer.index= a+d;
				sem_wait(&flights[a+d].sem_voos);

				memcpy(flights[a+d].info.nome, msg.info.nome, sizeof(msg.info.nome));
				flights[a+d].info.tipo= msg.info.tipo;
				flights[a+d].info.init= msg.info.init;
				flights[a+d].info.takeoff= msg.info.takeoff;

				sem_post(&flights[a+d].sem_voos);
				pthread_mutex_lock(&mutex_departures);
				insere_lista_thread_departure(Ldepartures, a+d);
				pthread_mutex_unlock(&mutex_departures);
				d++;
			}
		}
		else{
			if(a==A) answer.index= REJECTED;
			else{
				answer.index= a+d;
				sem_wait(&flights[a+d].sem_voos);

				memcpy(flights[a+d].info.nome, msg.info.nome, sizeof(msg.info.nome));
				flights[a+d].info.tipo= msg.info.tipo;
				flights[a+d].info.init= msg.info.init;
				flights[a+d].info.eta= msg.info.eta;
				flights[a+d].info.fuel= msg.info.fuel;
		
				sem_post(&flights[a+d].sem_voos);
				pthread_mutex_lock(&mutex_arrivals);
				
				insere_lista_thread_arrival(Larrivals, a+d);
				
				pthread_mutex_unlock(&mutex_arrivals);
				a++;
			}
		}

		msgsnd(mqid, &answer, sizeof(struct resposta)-sizeof(long), 0);
		#ifdef DEBUG
		printf("A torre respondeu a %s\n", msg.info.nome);
		#endif
	}
	pthread_exit(NULL);
}

void print_esta(){
	printf("Numero total de voo criados: %d\n", esta->voos_criados);
	printf("Numero total de voos que aterraram: %d\n", esta->aterragens);
	float media;
	if(esta->aterragens) media= esta->espera_aterragem / esta->aterragens;
	else media= esta->espera_aterragem;
	printf("Tempo médio de espera para aterrar: %.3f\n", media);
	printf("Numero total de voos que descolaram: %d\n", esta->descolagens);
	if(esta->descolagens) media= esta->espera_descolagem / esta->descolagens;
	else  media= esta->espera_descolagem;
	printf("Tempo medio de espera para descolar: %.3f\n", media);
	if(esta->numero_arrivals) media= esta->numero_holdings / esta->numero_arrivals;
	else media= esta->numero_holdings;
	printf("Numero medio de manobras holding por voo de aterragem: %.3f\n", media);
	if(esta->urgencias) media= esta->numero_holdings_urgencia / esta->urgencias;
	else media= esta->numero_holdings_urgencia;
	printf("Numero medio de manobras holding por voo em estado de urgencia: %.3f\n", media);
	printf("Numero de voos redirecionados: %d\n", esta->redirecionados);
	printf("Numero de voos rejeitados: %d\n", esta->rejeitados);
	sem_post(sem_esta);
}

void torre(){
	Ldepartures= cria_lista_thread();
	Larrivals= cria_lista_thread();
	pthread_create(&tarefas[0], NULL, atualiza, NULL);
	pthread_create(&tarefas[1], NULL, ordena, NULL);
	pthread_create(&tarefas[2], NULL, escaloneamento, NULL);
	pthread_create(&tarefas[3], NULL, responde, NULL);

	sigset_t set;
	sigfillset(&set);
	sigdelset(&set, SIGUSR1);
	sigdelset(&set, SIGINT);

	sigprocmask (SIG_BLOCK, &set, NULL);
	
	struct sigaction action;
	action.sa_handler= print_esta; 
	sigfillset(&action.sa_mask);              
	action.sa_flags= 0;
	sigaction(SIGUSR1, &action, NULL); 

	struct sigaction action2;
	action2.sa_handler= terminate; 
	sigfillset(&action2.sa_mask);              
	action2.sa_flags= 0;
	sigaction(SIGINT, &action2, NULL);

	while(1){
		pause();
	}
}	

void *thread_departure(void *informacao ){
	struct voos inf= *((struct voos*) informacao);
	sigset_t set;
	sigfillset(&set);

	sigprocmask (SIG_BLOCK, &set, NULL);
	
	sem_wait(sem_log);
	char *ptr=hora();
	printf("%s %s DEPARTURE STARTED\n", ptr, inf.nome);
	fprintf(log_file,"%s %s DEPARTURE STARTED\n", ptr, inf.nome);
	sem_post(sem_log);

	sem_wait(sem_esta);
	esta->voos_criados++;
	sem_post(sem_esta);

	struct mensagem msg;
	msg.info.init= inf.init;
	msg.info.takeoff= inf.takeoff;
	memcpy(msg.info.nome, inf.nome, sizeof(inf.nome));
	msg.info.tipo= inf.tipo;
	msg.info.voo_id= inf.voo_id;
	msg.mtype= NORMAL_MSG;

	
	msgsnd(mqid, &msg, sizeof(struct mensagem)-sizeof(long), 0);
	struct resposta answer;
	msgrcv(mqid, &answer, sizeof(struct resposta)-sizeof(long), inf.voo_id, 0);
	
	if(answer.index==REJECTED){
		sem_wait(sem_esta);
		esta->rejeitados++;
		sem_post(sem_esta);
		sem_wait(sem_log);
		ptr=hora();
		printf("%s %s DEPARTURE REJECTED\n", ptr, inf.nome);
		fprintf(log_file,"%s %s DEPARTURE REJECTED\n", ptr, inf.nome);
		sem_post(sem_log);
		pthread_exit(NULL);
	}

	#ifdef DEBUG
	printf("%s recebeu resposta com indice: %d\n", inf.nome, answer.index);
	sem_wait(&flights[answer.index].sem_voos);
	printf("DEPARTURE %s %d %d\n", flights[answer.index].info.nome, flights[answer.index].info.init, flights[answer.index].info.takeoff);
	sem_post(&flights[answer.index].sem_voos);
	#endif
	int espera;
	int int_d;
	while(1){
		sem_wait(&flights[answer.index].sinal);
		int_d = Tempo() + T;
		espera= Tempo() - inf.takeoff;
	
		switch (fuel(answer.index)){
			case L01:
				sem_wait(sem_log);
				ptr=hora();
				printf("%s %s DEPARTURE 01L started\n", ptr, inf.nome);
				fprintf(log_file,"%s %s DEPARTURE 01L started\n", ptr, inf.nome);
				sem_post(sem_log);

				usleep(ut*1000*(T-1));
				while(int_d<Tempo()){
				}
				
				sem_wait(sem_log);
				ptr=hora();
				printf("%s %s DEPARTURE 01L concluded\n", ptr, inf.nome);
				fprintf(log_file,"%s %s DEPARTURE 01L concluded\n", ptr, inf.nome);
				sem_post(sem_log);

				sem_wait(sem_pistas);
				pistas[fuel(answer.index)]=1;
				sem_post(sem_pistas);
				pthread_exit(NULL);

				sem_wait(sem_esta);
				esta->descolagens++;
				esta->espera_descolagem+= espera;
				sem_post(sem_esta);

				break;
			case R01:
				sem_wait(sem_log);
				ptr=hora();
				printf("%s %s DEPARTURE 01R started\n", ptr, inf.nome);
				fprintf(log_file,"%s %s DEPARTURE 01R started\n", ptr, inf.nome);
				sem_post(sem_log);

				usleep(ut*1000*(T-1));
				while(int_d<Tempo()){
				}

				sem_wait(sem_log);
				ptr=hora();
				printf("%s %s DEPARTURE 01R concluded\n", ptr, inf.nome);
				fprintf(log_file,"%s %s DEPARTURE 01R concluded\n", ptr, inf.nome);
				sem_post(sem_log);

				sem_wait(sem_pistas);
				pistas[fuel(answer.index)]=1;
				sem_post(sem_pistas);


				sem_wait(sem_esta);
				esta->descolagens++;
				esta->espera_descolagem+= espera;
				sem_post(sem_esta);				
				
				pthread_exit(NULL);
				break;
		}
	}
	pthread_exit(NULL);
}

void *thread_arrival(void *informacao ){
	struct voos inf= *((struct voos*) informacao);
	sigset_t set;
	sigfillset(&set);

	sigprocmask (SIG_BLOCK, &set, NULL);

	sem_wait(sem_log);
	char *ptr=hora();
	printf("%s %s ARRIVAL STARTED\n", ptr, inf.nome);
	fprintf(log_file,"%s %s ARRIVAL STARTED\n", ptr, inf.nome);
	sem_post(sem_log);

	sem_wait(sem_esta);
	esta->voos_criados++;
	sem_post(sem_esta);
	
	struct mensagem msg;
	msg.info.init= inf.init;
	msg.info.eta= inf.eta;
	msg.info.fuel= inf.fuel;
	msg.info.voo_id= inf.voo_id;
	memcpy(msg.info.nome, inf.nome, sizeof(inf.nome));

	if(inf.fuel<= 4+ inf.eta+ L){
		inf.tipo= URGENT_TYPE; 
		msg.info.tipo= inf.tipo;
		msg.mtype= URGENT_MSG;
		
		sem_wait(sem_log);
		ptr=hora();
		printf("%s %s EMERGENCY LANDING REQUESTED\n", ptr, inf.nome);
		fprintf(log_file,"%s %s EMERGENCY LANDING REQUESTED\n", ptr, inf.nome);
		sem_post(sem_log);
	}

	else{
		msg.info.tipo= inf.tipo;
 		msg.mtype= NORMAL_MSG;
	}

	msgsnd(mqid, &msg, sizeof(struct mensagem)-sizeof(long), 0);
	struct resposta answer;
	msgrcv(mqid, &answer, sizeof(struct resposta)-sizeof(long), inf.voo_id, 0);

	if(answer.index==REJECTED){
		sem_wait(sem_esta);
		esta->rejeitados++;
		sem_post(sem_esta);
		sem_wait(sem_log);
		ptr=hora();
		printf("%s %s DEPARTURE REJECTED\n", ptr, inf.nome);
		fprintf(log_file,"%s %s DEPARTURE REJECTED\n", ptr, inf.nome);
		sem_post(sem_log);
		pthread_exit(NULL);
	}

	sem_wait(sem_esta);
	esta->numero_arrivals++;
	sem_post(sem_esta);

	#ifdef DEBUG
	printf("%s recebeu resposta com indice: %d\n", inf.nome, answer.index);
	sem_wait(&flights[answer.index].sem_voos);
	printf("ARRIVAL %s %d %d %d\n", flights[answer.index].info.nome, flights[answer.index].info.init, flights[answer.index].info.eta, flights[answer.index].info.fuel);
	sem_post(&flights[answer.index].sem_voos);
	#endif
	int espera;
	int int_a;	
	int holding;
	while(1){
		sem_wait(&flights[answer.index].sinal);

		if(fuel(answer.index)==0){
			sem_wait(sem_esta);
			esta->redirecionados++;
			sem_post(sem_esta);
		
			sem_wait(sem_log);
			ptr=hora();
			printf("%s %s LEAVING TO OTHER AIRPORT => FUEL = 0\n", ptr, inf.nome);
			fprintf(log_file,"%s %s LEAVING TO OTHER AIRPORT => FUEL = 0\n", ptr, inf.nome);
			sem_post(sem_log);

			break;
		}
		else if(eta(answer.index) > inf.eta){
			if(fim_do_combustivel(answer.index) <= tempo_ate_chegar(answer.index)){
				sem_wait(sem_esta);
				esta->redirecionados++;
				sem_post(sem_esta);

				sem_wait(&flights[answer.index].sem_voos);
				flights[answer.index].info.fuel=0;
				sem_post(&flights[answer.index].sem_voos);

				sem_post(sem_sort);

				sem_wait(sem_log);
				ptr=hora();
				printf("%s %s LEAVING TO OTHER AIRPORT => FUEL = 0\n", ptr, inf.nome);
				fprintf(log_file,"%s %s LEAVING TO OTHER AIRPORT => FUEL = 0\n", ptr, inf.nome);
				sem_post(sem_log);

				break;
			}
			else if(flights[answer.index].info.tipo==URGENT_TYPE){
				sem_wait(sem_esta);
				esta->urgencias++;
				esta->numero_holdings_urgencia++;
				esta->numero_holdings++;
				sem_post(sem_esta);
			}
			else{
				sem_wait(sem_esta);
				esta->numero_holdings++;
				sem_post(sem_esta);
			}

			holding= eta(answer.index) - inf.eta;
			inf.eta= eta(answer.index);

			sem_wait(sem_log);
			ptr=hora();
			printf("%s %s HOLDING %d\n", ptr, inf.nome, holding);
			fprintf(log_file,"%s %s HOLDING %d\n", ptr, inf.nome, holding);
			sem_post(sem_log);

		}
		else{
			int_a = Tempo() + L;
			espera= Tempo() - inf.eta - inf.init;
			switch (takeoff(answer.index)){
				case L28:
					sem_wait(sem_log);
					ptr=hora();
					printf("%s %s LANDING 28L started\n", ptr, inf.nome);
					fprintf(log_file,"%s %s LANDING 28L started\n", ptr, inf.nome);
					sem_post(sem_log);
	
					usleep(ut*1000*(L-1));
					while(int_a<Tempo()){
					}
	
					sem_wait(sem_log);
					ptr=hora();
					printf("%s %s LANDING 28L concluded\n", ptr, inf.nome);
					fprintf(log_file,"%s %s LANDING 28L concluded\n", ptr, inf.nome);
					sem_post(sem_log);

					sem_wait(sem_pistas);
					pistas[takeoff(answer.index)]=1;
					sem_post(sem_pistas);
					
					sem_wait(sem_esta);
					esta->aterragens++;
					esta->espera_aterragem+= espera;
					sem_post(sem_esta);

					pthread_exit(NULL);
					break;
				case R28:
					sem_wait(sem_log);
					ptr=hora();
					printf("%s %s LANDING 28R started\n", ptr, inf.nome);
					fprintf(log_file,"%s %s LANDING 28R started\n", ptr, inf.nome);
					sem_post(sem_log);
	
					usleep(ut*1000*(L-1));
					while(int_a<Tempo()){
					}
	
					sem_wait(sem_log);
					ptr=hora();
					printf("%s %s LANDING 28R concluded\n", ptr, inf.nome);
					fprintf(log_file,"%s %s LANDING 28R concluded\n", ptr, inf.nome);
					sem_post(sem_log);
					
					sem_wait(sem_pistas);
					pistas[takeoff(answer.index)]=1;
					sem_post(sem_pistas);

					sem_wait(sem_esta);
					esta->aterragens++;
					esta->espera_aterragem+= espera;
					sem_post(sem_esta);
					
					pthread_exit(NULL);
					break;
			}
		}	
	}
	pthread_exit(NULL);
}
void cronometro(){
	sigset_t set;
	sigfillset(&set);
	sigprocmask (SIG_BLOCK, &set, NULL);

	while(1){
		usleep(ut*1000);
		sem_wait(sem_temp);	
		if(*temp==-1) exit(0);
		(*temp)++;
		sem_post(sem_temp);
		sem_post(sem_atualiza);
		sem_post(sem_inicia);
	}
}

void *pipe_reader(){
	sigset_t set;
	sigfillset(&set);
	pthread_sigmask (SIG_BLOCK, &set, NULL);

	int i;
	int erro;
	char comando[50];
	struct voos informacao;
	char *ptr;
	if ((fd=open(PIPE_NAME, O_RDWR)) < 0){
    		perror("Cannot open pipe for reading\n");
    		exit(0);
  	}
	fd_set read_set;
	while(1){
		erro=-1;
		informacao.init=0;
		informacao.fuel=0;
		informacao.eta=0;
		informacao.takeoff=0;
		comando[0]=0;
		FD_ZERO(&read_set);
		FD_SET(fd, &read_set);
		if(select(fd+1, &read_set, NULL, NULL, NULL)>0) read(fd, comando, sizeof(comando));

		#ifdef DEBUG
		printf("O pipe leu: %s\n",comando);
		#endif

		if(strstr(comando, "DEPARTURE ")!=NULL){
			if(strcmp(strstr(comando, "DEPARTURE "), comando)==0) {
				#ifdef DEBUG
				printf("classificado como DEPARTURE\n");
				#endif
				informacao.tipo= DEPARTURE_TYPE;
				ptr=comando;
				ptr=ptr+10;
				i=0;
				while(*ptr!=' ' && *ptr!=0 && i<6){
					if(i>1 && ('0'>*ptr || *ptr>'9')) break;
					else{
						informacao.nome[i]=*ptr;
						ptr++;
						i++;
					}
				}
				informacao.nome[i]=0;
				if( *ptr==' ' && i>=2 && informacao.nome[0]=='T' && informacao.nome[1]=='P'){
					#ifdef DEBUG
					printf("nome= %s\n",informacao.nome);
					#endif 
					if(strstr(ptr, " init: ")!=NULL){
						if(strcmp(strstr(ptr, " init: "), ptr)==0){ 
							ptr=ptr+7;
							i=0; 
							while('0'<=*ptr && *ptr<='9'){
								informacao.init*=10;
								informacao.init+= *ptr-'0';
								ptr++;
								i++;
							}
							if(*ptr==' ' && i!=0){
								sem_wait(sem_temp);
								if(informacao.init>= *temp){
									#ifdef DEBUG
									printf("init= %d\n",informacao.init);
									#endif
									if(strstr(ptr, " takeoff: ")!=NULL){
										if(strcmp(strstr(ptr, " takeoff: "), ptr)==0){
											ptr=ptr+10;
											i=0;
											while('0'<=*ptr && *ptr<='9'){
												informacao.takeoff*=10;
												informacao.takeoff+= *ptr-'0';
												ptr++;
												i++;
											}
											if((*ptr==0 || *ptr=='\n') && i!=0){
												if(informacao.takeoff>=informacao.init){
													erro=0;
													#ifdef DEBUG
													printf("takeoff= %d\n",informacao.takeoff);
													#endif
												}
											}
										}
									}
								}
								sem_post(sem_temp);
							}
						}
					}
				}
			}
		}
		if(strstr(comando, "ARRIVAL ")!=NULL){
			if(strcmp(strstr(comando, "ARRIVAL "), comando)==0){
				#ifdef DEBUG
				printf("classificado como ARRIVAL\n");
				#endif
				informacao.tipo= ARRIVAL_TYPE;
				ptr=comando;
				ptr=ptr+8;
				i=0;
				while(*ptr!=' ' && *ptr!=0 && i<6){
					if(i>1 && ('0'>*ptr || *ptr>'9')) break;
					else{
						informacao.nome[i]=*ptr;
						ptr++;
						i++;
					}
					
				}
				informacao.nome[i]=0;
				if( *ptr==' ' && i>=2 && informacao.nome[0]=='T' && informacao.nome[1]=='P'){
					#ifdef DEBUG
					printf("nome= %s\n",informacao.nome);
					#endif
					if(strstr(ptr, " init: ")!=NULL){ 
						if(strcmp(strstr(ptr, " init: "), ptr)==0){ 
							ptr=ptr+7;
							i=0; 
							while('0'<=*ptr && *ptr<='9'){
								informacao.init*=10;
								informacao.init+= *ptr-'0';
								ptr++;
								i++;
							}
							if(*ptr==' ' && i!=0){
								sem_wait(sem_temp);
								if(informacao.init>= *temp){
									#ifdef DEBUG
									printf("init= %d\n",informacao.init);
									#endif
									if(strstr(ptr, " eta: ")!=NULL){
										if(strcmp(strstr(ptr, " eta: "), ptr)==0){
											ptr=ptr+6;
											i=0;
											while('0'<=*ptr && *ptr<='9'){
												informacao.eta*=10;
												informacao.eta+= *ptr-'0';
												ptr++;
												i++;
											}
											if(*ptr==' ' && i!=0){
												#ifdef DEBUG
												printf("eta= %d\n",informacao.eta);
												#endif
												if(strstr(ptr, " fuel: ")!=NULL){
													if(strcmp(strstr(ptr, " fuel: "), ptr)==0){
														ptr=ptr+7;
														i=0;
														while('0'<=*ptr && *ptr<='9'){
															informacao.fuel*=10;
															informacao.fuel+= *ptr-'0';
															ptr++;
															i++;
														}
														if((*ptr==0 || *ptr=='\n') && i!=0){
															if(informacao.fuel>=informacao.eta + L){
																erro=0;
																#ifdef DEBUG
																printf("fuel= %d\n",informacao.fuel);
																#endif
															}
														}
													}
												}
											}
										}
									}
								}
								sem_post(sem_temp);
							}
						}
					}
				}
			}
		}

		if(erro==0 && comando[0]!=0){
			pthread_mutex_lock(&mutex_lista);
			insere_lista(lista, informacao);
			pthread_mutex_unlock(&mutex_lista);
	
			sem_wait(sem_log);
			ptr=hora();
			printf("%s NEW COMMAND => %s\n", ptr, comando);
			fprintf(log_file,"%s NEW COMMAND => %s\n", ptr, comando);
			sem_post(sem_log);
		}
		
		if(erro!=0 && comando[0]!=0){
			sem_wait(sem_log);
			ptr=hora();
			printf("%s WRONG COMMAND => %s\n", ptr, comando);
			fprintf(log_file,"%s WRONG COMMAND => %s\n", ptr, comando);
			sem_post(sem_log);
		}

	}
	pthread_exit(NULL);
}

void terminate_commands(){
	close(fd);
	exit(0);
}

int commands() {
	sigset_t set;
	sigfillset(&set);
	sigdelset(&set, SIGINT);
	sigprocmask (SIG_BLOCK, &set, NULL);
	
	struct sigaction action;
	action.sa_handler= terminate_commands; 
	sigfillset(&action.sa_mask);              
	action.sa_flags= 0;
	sigaction(SIGINT, &action, NULL); 

	srand(getpid());
	if ((fd=open(PIPE_NAME, O_WRONLY)) < 0){
    		perror("Cannot open pipe for writing\n");
    		exit(0);
  	}
	//char comando[50]= "ARRIVAL TP10 init: 20 eta: 10 fuel: 1000";
	//char comando[50]= "DEPARTURE TP10 init: 20 takeoff: 40";
	char comando[50];
	char num[5];
	int i=0;
	while(i<NCOMMANDS){
		if(rand()%100 < PERCENTAGEM){
			memcpy(comando, "ARRIVAL", 8*sizeof(char));
			strcat(comando, " TP");
			sprintf(num, "%d", rand()%1000);
			strcat(comando, num);
			strcat(comando, " init: ");
			sprintf(num, "%d", rand()%1000);
			strcat(comando, num);
			strcat(comando, " eta: ");
			sprintf(num, "%d", rand()%1000);
			strcat(comando, num);
			strcat(comando, " fuel: ");
			sprintf(num, "%d", rand()%1000);
			strcat(comando, num);
			strcat(comando, "\0");
		}
		else{
			memcpy(comando, "DEPARTURE", 10*sizeof(char));
			strcat(comando, " TP");
			sprintf(num, "%d", rand()%1000);
			strcat(comando, num);
			strcat(comando, " init: ");
			sprintf(num, "%d", rand()%1000);
			strcat(comando, num);
			strcat(comando, " takeoff: ");
			sprintf(num, "%d", rand()%1000);
			strcat(comando, num);
			strcat(comando, "\0");
		}
		write(fd, comando, 50*sizeof(char));
		i++;
		usleep(1000000);
	} 
	while(1){
	}
	close(fd);
}

void sigusr_handler(){
	kill(0,SIGUSR1);
}

int main(void) {
	main_pid= getpid();
	t_main= pthread_self();
	signal(SIGINT, SIG_DFL);
	initialize();

	struct sigaction action_print;
	action_print.sa_handler= sigusr_handler; 
	sigfillset(&action_print.sa_mask);              
	action_print.sa_flags= 0;
	sigaction(SIGTSTP, &action_print, NULL);
	
	pid_cronometro=fork();
	if(pid_cronometro==0){
		cronometro();
		exit(0);
	}
	else if (pid_cronometro==-1) {
		perror("Zombie process found\n");
		exit(0);
	}
	pid_torre=fork();
	if(pid_torre==0){
		torre();
		exit(0);
	}
	else if (pid_torre==-1){
		perror("Zombie process found\n");
		exit(0);
	}
	#ifdef TESTMODE
	pid_commands=fork();
	if(pid_commands==0){
		commands();
		exit(0);
	}
	else if (pid_commands==-1){
		perror("Zombie process found\n");
		exit(0);
	}
	#endif

	lista =cria_lista();
	pthread_create(&t_pipe, NULL, pipe_reader, NULL);
	
	sigset_t set;
	sigfillset(&set);
	sigdelset(&set, SIGINT);
	sigdelset(&set, SIGTSTP);
	sigprocmask (SIG_BLOCK, &set, NULL);
	
	struct sigaction action;
	action.sa_handler= terminate; 
	sigfillset(&action.sa_mask);
	sigdelset(&action.sa_mask, SIGTSTP);              
	action.sa_flags= 0;
	sigaction(SIGINT, &action, NULL);

	int voo_id=30;
	int i=0;
	t_voo= (pthread_t*) malloc((A+D)*sizeof(pthread_t));
	while(1){
		sem_wait(sem_inicia);
		pthread_mutex_lock(&mutex_lista);
		sem_wait(sem_temp);
		while(lista->next!= NULL && lista->next->info.init == *temp){
				if(lista->next->info.tipo== DEPARTURE_TYPE){
					lista->next->info.voo_id= voo_id;
					pthread_create(&t_voo[i], NULL, thread_departure, &lista->next->info);
					
				}
				else {
					lista->next->info.voo_id= voo_id;
					pthread_create(&t_voo[i], NULL, thread_arrival, &lista->next->info);
				}
				elimina_lista(lista, lista->next->info.init);
				voo_id++;
				i++;
		}
		sem_post(sem_temp);
		pthread_mutex_unlock(&mutex_lista);
	}
	return 0;
}