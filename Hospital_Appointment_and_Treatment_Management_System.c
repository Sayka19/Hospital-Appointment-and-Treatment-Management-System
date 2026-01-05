#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<time.h>
#include<pthread.h>
#include<semaphore.h>

int total_patients;
int waiting_chairs;
int max_appointments;
int number_of_doctors;

sem_t *sem_appt_response;
sem_t *sem_treatment_done;

int *call_queue;
int call_head=0,call_tail=0,call_count=0;

int *wait_queue;
int wait_head=0,wait_tail=0,waiting=0;

int *appointment_status;
int *patient_final_status;
int *doctor_treated_count;

int appointments_reserved=0;
int calls_processed=0;
int treated_patients=0;
int denied_appointments=0;
int left_no_seat=0;

int shutdown_flag=0;

pthread_mutex_t call_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t appt_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t chairs_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t treated_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

sem_t sem_call;
sem_t sem_patients;

void enqueue_call(int pid){
    call_queue[call_tail]=pid;
    call_tail=(call_tail+1)%total_patients;
    call_count++;
}

int dequeue_call(){
    if(call_count==0){
        return -1;
    }
    int pid=call_queue[call_head];
    call_head=(call_head+1)%total_patients;
    call_count--;
    return pid;
}

void enqueue_waiting(int pid){
    wait_queue[wait_tail]=pid;
    wait_tail=(wait_tail+1)%total_patients;
    waiting++;
}

int dequeue_waiting(){
    if(waiting==0){
        return -1;
    }
    int pid=wait_queue[wait_head];
    wait_head=(wait_head+1)%total_patients;
    waiting--;
    return pid;
}

//Receptionist Thread
void* receptionist_thread_fn(void* arg){
    while(1){
        sem_wait(&sem_call);
        pthread_mutex_lock(&call_mutex);
        int pid=dequeue_call();
        pthread_mutex_unlock(&call_mutex);
        if(pid==-1){
            continue;
        }
        pthread_mutex_lock(&appt_mutex);
        if(appointments_reserved<max_appointments){
            appointments_reserved++;
            appointment_status[pid]=1;
            printf("[Receptionist] Approved appointment for Patient %d (%d/%d)\n",pid,appointments_reserved,max_appointments);
        }
        else{
            appointment_status[pid]=0;
             pthread_mutex_lock(&stats_mutex);
            denied_appointments++;
             pthread_mutex_unlock(&stats_mutex);
            printf("[Receptionist] Denied appointment for Patient %d (no slots)\n",pid);
        }
        calls_processed++;
        pthread_mutex_unlock(&appt_mutex);
        sem_post(&sem_appt_response[pid]);
        if (calls_processed >= total_patients) break;
    }
    printf("[Receptionist] All calls processed. Closing.\n");
    return NULL;
}

//Doctor Thread
void* doctor_thread_fn(void* arg){
    int doctor_id=*((int*)arg);
    free(arg);
    while(1){
        sem_wait(&sem_patients);
        pthread_mutex_lock(&chairs_mutex);
        if(shutdown_flag && waiting==0){
            pthread_mutex_unlock(&chairs_mutex);
            break;
        }
        int pid=dequeue_waiting();
        pthread_mutex_unlock(&chairs_mutex);
        if(pid==-1){
            continue;
        }
        printf("[Doctor %d] Treating patient %d....\n",doctor_id,pid);
        usleep((rand()%3+1)*200000);
        pthread_mutex_lock(&treated_mutex);
        treated_patients++;
        pthread_mutex_unlock(&treated_mutex);
        doctor_treated_count[doctor_id]++;

        patient_final_status[pid]=2;
        printf("[Doctor %d] Finished treating patient %d (Total treated: %d)\n",doctor_id,pid,treated_patients);
        sem_post(&sem_treatment_done[pid]);
    }
    printf("[Doctor %d] No more patients. Exiting.\n",doctor_id);
    return NULL;
}

//Patient Thread
void*  patient_thread_fn(void* arg){
    int pid=*((int*)arg);
    free(arg);
    printf("[Patient %d] Calling for appointment.....\n",pid);
    pthread_mutex_lock(&call_mutex);
    enqueue_call(pid);
    pthread_mutex_unlock(&call_mutex);
    sem_post(&sem_call);
    sem_wait(&sem_appt_response[pid]);
    pthread_mutex_lock(&appt_mutex);
    int approved=appointment_status[pid];
    pthread_mutex_unlock(&appt_mutex);
    if(!approved){
        printf("[Patient %d] Appointment denied. Leaving.\n",pid);
        return NULL;
    }
    printf("[Patient %d] Appointment approved. Arriving at hospital.\n",pid);
    int got_seat=0;
    pthread_mutex_lock(&chairs_mutex);
    if(waiting<waiting_chairs){
        enqueue_waiting(pid);
        got_seat=1;
    }
        pthread_mutex_unlock(&chairs_mutex);
    if(got_seat){
            sem_post(&sem_patients);
        sem_wait(&sem_treatment_done[pid]);
        printf("[Patient %d] Treatment completed. Leaving.\n",pid);
    }
    else{
         pthread_mutex_lock(&stats_mutex);
        left_no_seat++;
     pthread_mutex_unlock(&stats_mutex);
        printf("[Patient %d] No waiting chairs available. Leaving\n",pid);
    }
    return NULL;
}
int main(){
    srand(time(NULL));
    printf("----------Hospital Appointment and Treatment Management System----------\n");
    printf("Enter total number of patients:");
    scanf("%d",&total_patients);
    printf("Enter number of waiting chairs:");
    scanf("%d",&waiting_chairs);
    printf("Enter maximum appointment slots:");
    scanf("%d",&max_appointments);
    printf("Enter number of doctors:");
    scanf("%d",&number_of_doctors);

    sem_appt_response=malloc((total_patients+1)*sizeof(sem_t));
    sem_treatment_done=malloc((total_patients+1)*sizeof(sem_t));
    appointment_status=malloc((total_patients+1)*sizeof(int));
    patient_final_status=malloc((total_patients+1)*sizeof(int));
    call_queue=malloc(total_patients*sizeof(int));
    wait_queue=malloc(total_patients*sizeof(int));

    for(int i=1;i<=total_patients;i++){
        sem_init(&sem_appt_response[i],0,0);
        sem_init(&sem_treatment_done[i],0,0);
        appointment_status[i]=0;
        patient_final_status[i]=-1;
    }

    sem_init(&sem_call,0,0);
    sem_init(&sem_patients,0,0);

    pthread_t receptionist_tid;
    pthread_t *doctor_tids=malloc(number_of_doctors*sizeof(pthread_t));
    pthread_t *patient_tids=malloc(total_patients*sizeof(pthread_t));

    pthread_create(&receptionist_tid, NULL, receptionist_thread_fn,NULL);
    for(int i=0;i<number_of_doctors;i++){
        int *id=malloc(sizeof(int));
        *id=i+1;
        pthread_create(&doctor_tids[i],NULL,doctor_thread_fn,id);
    }
    doctor_treated_count = malloc((number_of_doctors + 1) * sizeof(int));
       for (int i = 1; i <= number_of_doctors; i++){
    doctor_treated_count[i] = 0;
       }
//Create patients with random delay
for(int i=1;i<=total_patients;i++){
    int *pid=malloc(sizeof(int));
    *pid=i;
    pthread_create(&patient_tids[i-1],NULL,patient_thread_fn,pid);
    usleep((rand()%3)*100000);
}
    for(int i=0;i<total_patients;i++){
    pthread_join(patient_tids[i],NULL);
    }
    pthread_join(receptionist_tid,NULL);
    pthread_mutex_lock(&chairs_mutex);
    shutdown_flag=1;
    pthread_mutex_unlock(&chairs_mutex);
    for(int i=0;i<number_of_doctors;i++){
    sem_post(&sem_patients);
    }
    for(int i=0;i<number_of_doctors;i++){
    pthread_join(doctor_tids[i],NULL);
    }

    printf("\n---------------Hospital Summary Report----------------\n");
    printf("Total patients came: %d\n",total_patients);
    printf("Appointments approved: %d\n",appointments_reserved);
    printf("Appointments Denied (full slots): %d\n",denied_appointments);
    printf("Patients treated successfully: %d\n",treated_patients);
    printf("Patients treated by each doctor:\n");
    for (int i = 1; i <= number_of_doctors; i++) {
    printf("Doctor %d treated %d patients\n", i, doctor_treated_count[i]);
      }

    printf("Patients Left (no waiting chairs): %d\n",left_no_seat);
    printf("System Exiting.................");

    free(sem_appt_response);
    free(sem_treatment_done);
    free(appointment_status);
    free(patient_final_status);
    free(call_queue);
    free(wait_queue);
    free(doctor_tids);
    free(patient_tids);
    return 0;

}
