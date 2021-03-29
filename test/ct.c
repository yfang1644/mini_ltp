/*
 * ============================================================================
 *
 *       Filename:  ct.c
 *
 *    Description:  test create_timer
 *
 *        Version:  1.0
 *        Created:  03/29/2021 11:36:00 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */

#include <unistd.h> 
#include <stdio.h>
#include <signal.h>
#include <time.h>

void printTime()
{
    struct tm *cursystem;
    time_t tm_t;
    time(&tm_t);
    cursystem = localtime(&tm_t);
    char tszInfo[2048] ;
    sprintf(tszInfo, "%02d:%02d:%02d", 
        cursystem->tm_hour, 
        cursystem->tm_min, 
        cursystem->tm_sec);
    printf("[%s]\n",tszInfo);
}

void SignHandler(int signo)
{
    printTime();
    if(signo == SIGUSR1) {
        printf("Capture sign no : SIGUSR1\n"); 
    } else if (SIGALRM == signo) {
        //printf("Capture sign no : SIGALRM\n"); 
    } else {
        printf("Capture sign no:%d\n",signo); 
    }
}

int main(int argc, char *argv[])
{
    struct sigevent evp;  
    struct itimerspec ts;  
    timer_t timer;  
    int ret;  
    evp.sigev_value.sival_ptr = &timer;  
    evp.sigev_notify = SIGEV_SIGNAL;  
    evp.sigev_signo = SIGALRM;
    signal(evp.sigev_signo, SignHandler); 
    ret = timer_create(CLOCK_REALTIME, &evp, &timer);  
    if(ret) {
        perror("timer_create");
    } 
    ts.it_interval.tv_sec = 1;
    ts.it_interval.tv_nsec = 0;
    ts.it_value.tv_sec = 1;
    ts.it_value.tv_nsec = 0;  

    ret = timer_settime(timer, 0, &ts, NULL);  

    while(1){
        int left = sleep(5);
    }

    return 0; 
}

