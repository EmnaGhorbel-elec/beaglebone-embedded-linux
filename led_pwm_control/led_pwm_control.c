#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>

char kbhit(void);

#define PATH_ADC    "/sys/bus/iio/devices/iio:device0/in_voltage3_raw"
#define PATH_PERIOD "/sys/class/pwm/pwmchip0/pwm0/period"
#define PATH_DUTY   "/sys/class/pwm/pwmchip0/pwm0/duty_cycle"
#define PATH_ENABLE "/sys/class/pwm/pwmchip0/pwm0/enable"
#define PATH_EXPORT "/sys/class/pwm/pwmchip0/export"
#define PATH_PROC   "/proc/SpeedM_dev"

#define PERIOD_MIN  2000000    //  2ms = 500 Hz
#define PERIOD_MAX  50000000   // 50ms =  20 Hz

pthread_cond_t  cv0  = PTHREAD_COND_INITIALIZER;
pthread_cond_t  cv1  = PTHREAD_COND_INITIALIZER;
pthread_cond_t  cv2  = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

timer_t timerid;
struct sigevent sev;
struct itimerspec trigger;

volatile int running = 1;
uint64_t adc_val = 0;

// --- UTILITY ---
void write_sysfs(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd != -1) { write(fd, value, strlen(value)); close(fd); }
    else perror(path);
}

int read_adc(void) {
    char buf[16] = {0};
    int fd = open(PATH_ADC, O_RDONLY);
    if (fd < 0) { perror(PATH_ADC); return 0; }
    read(fd, buf, sizeof(buf) - 1);
    close(fd);
    return atoi(buf);
}

uint64_t read_proc(void) {
    char buf[32] = {0};
    int fd = open(PATH_PROC, O_RDONLY);
    if (fd < 0) { perror(PATH_PROC); return 0; }
    read(fd, buf, sizeof(buf) - 1);
    close(fd);
    return atoll(buf);
}

// --- TIMER CALLBACK ---
void thread_timer100(union sigval sv) {
    pthread_mutex_lock(&lock);
    pthread_cond_signal(&cv0);
    pthread_mutex_unlock(&lock);
}

// --- THREAD 0: tick ---
void *thread0(void *v) {
    while(running) {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&cv0, &lock);
        pthread_mutex_unlock(&lock);
        if(!running) break;

        printf("[T0] tick -> ");

        pthread_mutex_lock(&lock);
        pthread_cond_signal(&cv1);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

// --- THREAD 1: ADC read ---
void *thread1(void *v) {
    while(running) {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&cv1, &lock);
        pthread_mutex_unlock(&lock);
        if(!running) break;

        adc_val = (uint64_t)read_adc();
        printf("[ADC] %llu -> ", adc_val);

        pthread_mutex_lock(&lock);
        pthread_cond_signal(&cv2);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

// --- THREAD 2: PWM period control + speed readback ---
void *thread2(void *v) {
    char buf_period[32], buf_duty[32];
    uint32_t period = 0, duty = 0;
    uint64_t DT = 0;
    double freq = 0.0, rpm = 0.0;

    while(running) {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&cv2, &lock);
        pthread_mutex_unlock(&lock);
        if(!running) break;

        // Map ADC 0-4095 -> PERIOD_MIN to PERIOD_MAX
        period = PERIOD_MIN +
                 (uint32_t)((adc_val * (uint64_t)(PERIOD_MAX - PERIOD_MIN)) >> 12);
        duty = period / 2; // 50% duty = clean square wave for IRQ

        // Must disable before changing period on BBB
        write_sysfs(PATH_ENABLE, "0");
        snprintf(buf_period, sizeof(buf_period), "%u", period);
        snprintf(buf_duty,   sizeof(buf_duty),   "%u", duty);
        write_sysfs(PATH_PERIOD, buf_period);
        write_sysfs(PATH_DUTY,   buf_duty);
        write_sysfs(PATH_ENABLE, "1");

        // Read DT from kernel module
        DT = read_proc();
        if (DT > 0) {
            freq = 1e9 / (double)DT;
            rpm  = freq * 60.0;
        }

        printf("[PWM] %.1f ms | [DT] %llu ns | [FREQ] %.1f Hz | [RPM] %.0f\n",
               period / 1000000.0f, DT, freq, rpm);
        fflush(stdout);
    }
    return NULL;
}

// --- TIMER INIT ---
void init_timer(void) {
    memset(&sev,     0, sizeof(struct sigevent));
    memset(&trigger, 0, sizeof(struct itimerspec));
    sev.sigev_notify          = SIGEV_THREAD;
    sev.sigev_notify_function = &thread_timer100;
    timer_create(CLOCK_REALTIME, &sev, &timerid);
    trigger.it_value.tv_nsec    = 100000000;
    trigger.it_interval.tv_nsec = 100000000;
    timer_settime(timerid, 0, &trigger, NULL);
}

// --- MAIN ---
int main(void) {
    pthread_t t0, t1, t2;
    char buf[32];

    // Export and init PWM
    write_sysfs(PATH_EXPORT, "0");
    usleep(300000);
    write_sysfs(PATH_ENABLE, "0");
    snprintf(buf, sizeof(buf), "%u", PERIOD_MIN);
    write_sysfs(PATH_PERIOD, buf);
    snprintf(buf, sizeof(buf), "%u", PERIOD_MIN / 2);
    write_sysfs(PATH_DUTY, buf);
    write_sysfs(PATH_ENABLE, "1");

    printf("============================================\n");
    printf("  Pot(P9_38) -> ADC -> PWM period (P9_22)\n");
    printf("  P9_22 --wire--> P9_11 -> Kernel IRQ\n");
    printf("  LED on P8_10 toggles on each pulse\n");
    printf("  Turn pot to change speed. 's' to stop.\n");
    printf("============================================\n");
    fflush(stdout);

    pthread_create(&t0, NULL, thread0, NULL);
    pthread_create(&t1, NULL, thread1, NULL);
    pthread_create(&t2, NULL, thread2, NULL);

    init_timer();

    char key = 0;
    while((key = kbhit()) != 's' && key != 'q')
        usleep(10000);

    printf("\nStopping...\n");
    running = 0;
    timer_delete(timerid);

    pthread_mutex_lock(&lock);
    pthread_cond_broadcast(&cv0);
    pthread_cond_broadcast(&cv1);
    pthread_cond_broadcast(&cv2);
    pthread_mutex_unlock(&lock);

    pthread_join(t0, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    write_sysfs(PATH_DUTY,   "0");
    write_sysfs(PATH_ENABLE, "0");
    printf("Clean stop done.\n");
    return 0;
}
