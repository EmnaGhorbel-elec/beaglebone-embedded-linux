// ============================================================
// pwm_main_v2.c  –  BeagleBone Black  PWM error measurement
// Compile: gcc pwm_main_v2.c -o pwm_test -lm
// Usage:   ./pwm_test          → CSV uniquement
//          ./pwm_test --save   → CSV + PNG
// ============================================================
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <signal.h>

// ── Paramètres PWM ──────────────────────────────────────────
#define T_INIT      10000000UL   // 10 ms en ns
#define T_STEP       5000000UL   // pas de 5 ms
#define T_MAX      100000000UL   // 100 ms max
#define D_INIT       5000000UL   // duty cycle 5 ms
#define MAX_SAMPLES      1000

// ── Fichiers de sortie ───────────────────────────────────────
#define LOG_FILE        "pwm_error_log.csv"
#define PNG_FILE        "pwm_error_curve.png"
#define GNUPLOT_SCRIPT  "plot_pwm.gp"

// ── Structure d'un échantillon ───────────────────────────────
typedef struct {
    int    step;
    long   T_sent_us;
    long   T_read_us;
    long   error_us;
    double error_pct;    // erreur relative en %
} Sample;

// ── Globals ──────────────────────────────────────────────────
static char     T_char[32];
static uint32_t T = T_INIT;
static int      fd;
static Sample   samples[MAX_SAMPLES];
static int      n_samples = 0;
static volatile int running = 1;

// ── Terminal raw ─────────────────────────────────────────────
static struct termios old_tio;

static void tty_raw(void)
{
    struct termios new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN]  = 0;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

static void tty_restore(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
}

static char kbhit(void)
{
    char c = 0;
    read(STDIN_FILENO, &c, 1);
    return c;
}

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

// ── Lien symbolique ──────────────────────────────────────────
static void create_link(void)
{
    const char *target = "/sys/class/pwm/pwmchip0/pwm0";
    int test = symlink(target, "PWM1A");
    if (test == -1 && errno == EEXIST)
        puts("Lien symbolique déjà existant");
}

// ── Init PWM ─────────────────────────────────────────────────
static void init_PWM(void)
{
    fd = open("PWM1A/period", O_WRONLY);
    if (fd < 0) { perror("open period"); return; }
    snprintf(T_char, sizeof(T_char), "%lu", (unsigned long)T);
    write(fd, T_char, strlen(T_char));
    close(fd);

    fd = open("PWM1A/duty_cycle", O_WRONLY);
    if (fd < 0) { perror("open duty_cycle"); return; }
    snprintf(T_char, sizeof(T_char), "%lu", (unsigned long)D_INIT);
    write(fd, T_char, strlen(T_char));
    close(fd);

    fd = open("PWM1A/enable", O_WRONLY);
    if (fd < 0) { perror("open enable"); return; }
    write(fd, "1", 1);
    close(fd);
}

static void set_period(uint32_t period_ns)
{
    fd = open("PWM1A/period", O_WRONLY);
    if (fd < 0) { perror("open period"); return; }
    snprintf(T_char, sizeof(T_char), "%lu", (unsigned long)period_ns);
    write(fd, T_char, strlen(T_char));
    close(fd);
}

static long read_measured_period_ns(void)
{
    char buf[32] = {0};
    fd = open("/proc/SpeedM_dev", O_RDONLY);
    if (fd < 0) { perror("open /proc/SpeedM_dev"); return -1; }
    lseek(fd, 0, SEEK_SET);
    read(fd, buf, sizeof(buf) - 1);
    close(fd);
    return atol(buf);
}

static void disable_PWM(void)
{
    fd = open("PWM1A/enable", O_WRONLY);
    if (fd < 0) return;
    write(fd, "0", 1);
    close(fd);
}

// ── Sauvegarde CSV ───────────────────────────────────────────
// Colonnes : step, T_envoye_us, T_lu_us, erreur_us, erreur_pct
static void save_csv(void)
{
    FILE *fp = fopen(LOG_FILE, "w");
    if (!fp) { perror("fopen CSV"); return; }

    fprintf(fp, "step,T_envoye_us,T_lu_us,erreur_us,erreur_pct\n");
    for (int i = 0; i < n_samples; i++)
        fprintf(fp, "%d,%ld,%ld,%ld,%.4f\n",
                samples[i].step,
                samples[i].T_sent_us,
                samples[i].T_read_us,
                samples[i].error_us,
                samples[i].error_pct);
    fclose(fp);
    printf("CSV sauvegardé : %s\n", LOG_FILE);
}

// ── Script gnuplot amélioré ───────────────────────────────────
static void generate_gnuplot_script(void)
{
    FILE *fp = fopen(GNUPLOT_SCRIPT, "w");
    if (!fp) { perror("fopen gnuplot script"); return; }

    fprintf(fp,
        /* ── Terminal PNG haute résolution ─────────────────── */
        "set terminal pngcairo size 1400,900 enhanced font 'Verdana,11'\n"
        "set output '%s'\n"
        "set datafile separator ','\n"
        "\n"
        /* ── Style global ────────────────────────────────────── */
        "set style line 1  lc rgb '#1a6faf' lw 2.5 pt 7  ps 0.9\n"
        "set style line 2  lc rgb '#d64e12' lw 2.5 pt 5  ps 0.9\n"
        "set style line 3  lc rgb '#28a745' lw 2.5 pt 9  ps 0.9\n"
        "set style line 10 lc rgb '#aaaaaa' lw 1   dt 2\n"
        "\n"
        "set grid ls 10\n"
        "set border lw 1.2\n"
        "set key box lw 0.5 spacing 1.3 font ',10'\n"
        "\n"
        /* ── Multiplot 3 lignes ──────────────────────────────── */
        "set multiplot layout 3,1 \\\n"
        "    title 'Analyse Erreur PWM – BeagleBone Black' \\\n"
        "    font ',13'\n"
        "\n"
        /* ════════════════════════════════════════════════════
         * Graphe 1 : T_envoyé vs T_lu (µs)
         * ════════════════════════════════════════════════════ */
        "set title 'Période envoyée vs période mesurée'\n"
        "set xlabel 'Étape'\n"
        "set ylabel 'Période (µs)'\n"
        "set key top left\n"
        "plot '%s' using 1:2 with linespoints ls 1 title 'T_{envoyé}', \\\n"
        "     '%s' using 1:3 with linespoints ls 2 title 'T_{lu}'\n"
        "\n"
        /* ════════════════════════════════════════════════════
         * Graphe 2 : Erreur absolue (µs)
         * ════════════════════════════════════════════════════ */
        "set title 'Erreur absolue = T_{envoyé} − T_{lu}'\n"
        "set xlabel 'Étape'\n"
        "set ylabel 'Erreur (µs)'\n"
        "set key top left\n"
        "set yrange [*:*]\n"
        /* zone de tolérance ±500 µs en gris clair */
        "set object 1 rect from graph 0,0.5-500.0/yrange to graph 1,0.5+500.0/yrange \\\n"
        "    fc rgb '#e8f5e9' fs solid 0.5 noborder behind\n"
        "plot '%s' using 1:4 with linespoints ls 3 title 'Erreur (µs)', \\\n"
        "     0 with lines ls 10 notitle\n"
        "\n"
        /* ════════════════════════════════════════════════════
         * Graphe 3 : Erreur relative (%)
         * ════════════════════════════════════════════════════ */
        "set title 'Erreur relative = (T_{envoyé} − T_{lu}) / T_{envoyé} × 100'\n"
        "set xlabel 'Étape'\n"
        "set ylabel 'Erreur (%%)'\n"
        "set key top left\n"
        "set yrange [*:*]\n"
        "set format y '%%.2f %%%%'\n"
        "plot '%s' using 1:5 with linespoints lc rgb '#8e44ad' lw 2.5 pt 11 ps 0.9 \\\n"
        "     title 'Erreur (%%)', \\\n"
        "     0 with lines ls 10 notitle\n"
        "set format y\n"
        "\n"
        "unset multiplot\n",
        PNG_FILE,
        LOG_FILE, LOG_FILE,
        LOG_FILE,
        LOG_FILE);

    fclose(fp);
}

// ── Lancer gnuplot ───────────────────────────────────────────
static void run_gnuplot(void)
{
    save_csv();
    if (n_samples == 0) { puts("Aucune donnée."); return; }

    if (system("which gnuplot > /dev/null 2>&1") != 0) {
        puts("gnuplot non trouvé — transférez le CSV sur votre PC host.");
        return;
    }

    generate_gnuplot_script();

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "gnuplot %s", GNUPLOT_SCRIPT);
    system(cmd);
    printf("PNG sauvegardé : %s\n", PNG_FILE);
}

// ── Usage ────────────────────────────────────────────────────
static void print_usage(const char *prog)
{
    printf("Usage : %s [--save | --help]\n", prog);
    printf("  (aucune option)  : mesure + CSV\n");
    printf("  --save           : mesure + CSV + PNG\n");
}

// ════════════════════════════════════════════════════════════
// MAIN
// ════════════════════════════════════════════════════════════
int main(int argc, char *argv[])
{
    int mode_save = 0;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--save") == 0) mode_save = 1;
        else if (strcmp(argv[i], "--help") == 0) { print_usage(argv[0]); return 0; }
        else { fprintf(stderr, "Argument inconnu : %s\n", argv[i]); return 1; }
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    create_link();
    init_PWM();
    tty_raw();

    puts("══════════════════════════════════════════════════════════════════");
    puts("        BBB PWM Error Monitor  –  appuyez 'q' pour quitter");
    puts("══════════════════════════════════════════════════════════════════");
    printf("%-5s  %-14s  %-12s  %-12s  %s\n",
           "Étape","T_envoyé(µs)","T_lu(µs)","Erreur(µs)","Erreur(%)");
    puts("──────────────────────────────────────────────────────────────────");

    while (running && kbhit() != 'q' && n_samples < MAX_SAMPLES)
    {
        // 1. Nouvelle période
        if ((T += T_STEP) > T_MAX) T = T_INIT;

        // 2. Appliquer au PWM
        set_period(T);

        // 3. Stabilisation
        usleep(100000);

        // 4. Lire la période mesurée
        long T_read_ns = read_measured_period_ns();
        if (T_read_ns < 0) { sleep(1); continue; }

        // 5. Calculs
        long   T_sent_us = (long)(T          / 1000);
        long   T_read_us = (long)(T_read_ns   / 1000);
        long   err_us    = T_sent_us - T_read_us;

        // Erreur en % = (T_envoyé - T_lu) / T_envoyé × 100
        double err_pct = (T_sent_us > 0)
                         ? (100.0 * err_us / T_sent_us)
                         : 0.0;

        // 6. Stocker
        samples[n_samples].step      = n_samples;
        samples[n_samples].T_sent_us = T_sent_us;
        samples[n_samples].T_read_us = T_read_us;
        samples[n_samples].error_us  = err_us;
        samples[n_samples].error_pct = err_pct;
        n_samples++;

        // 7. Console
        printf("%-5d  %-14ld  %-12ld  %+-12ld  %+.2f%%\n",
               n_samples-1, T_sent_us, T_read_us, err_us, err_pct);
        fflush(stdout);

        sleep(1);
    }

    tty_restore();
    puts("\n──────────────────────────────────────────────────────────────────");
    disable_PWM();
    printf("Total échantillons : %d\n", n_samples);

    if (mode_save)
        run_gnuplot();
    else
        save_csv();

    return EXIT_SUCCESS;
}
