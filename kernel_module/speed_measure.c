#include <linux/module.h>
#include <linux/platform_device.h> // Nécessaire pour le nouveau paradigme
#include <linux/gpio/consumer.h>   // Nouveau standard (gpiod)
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/ktime.h>
#include <linux/of.h>

/* --- VARIABLES GLOBALES --- */
static struct gpio_desc *desc_in, *desc_out;
static int irq_num;
static u64 T1 = 0, DT = 0;

/* --- HANDLER D'INTERRUPTION --- */
static irqreturn_t SpeedM_handler(int irq, void *dev_id) {
    u64 tme = ktime_get_ns();
    DT = tme - T1;
    T1 = tme;
    // toggle la sortie
    gpiod_set_value(desc_out, !gpiod_get_value(desc_out));
    return IRQ_HANDLED;
}

/* --- INTERFACE PROC --- */
static ssize_t myread(struct file *file, char __user *ubuf, size_t count, loff_t *ppos) {
    char buf[64];
    int len;
    if (*ppos > 0) return 0;
    len = snprintf(buf, sizeof(buf), "%llu\n", DT);
    if (copy_to_user(ubuf, buf, len)) return -EFAULT;
    *ppos += len;
    return len;
}

static const struct proc_ops myops = { .proc_read = myread };

/* --- FONCTION PROBE --- */
/* Elle est appelée par le noyau quand le Device Tree correspond au module */
static int SpeedM_probe(struct platform_device *pdev) {
    struct device *dev = &pdev->dev;
    pr_info("SpeedM_meas: Le noyau a trouve le péripherique, lancement du probe.\n");

    // 1. Récupère les GPIOs définis dans le Device Tree
    // Le préfixe "devm_" permet au noyau de libérer les ressources automatiquement en cas d'erreur.
    desc_in = devm_gpiod_get(dev, "inputcapture", GPIOD_IN);
    desc_out = devm_gpiod_get(dev, "inputcaptureLED", GPIOD_OUT_LOW);

    if (IS_ERR(desc_in) || IS_ERR(desc_out)) {
        pr_err("SpeedM_meas: Impossible de recuperer les GPIOs depuis le Device Tree.\n");
        return -ENODEV;
    }

    // 2. Configuration de l'IRQ
    irq_num = gpiod_to_irq(desc_in);
    if (request_irq(irq_num, SpeedM_handler, IRQF_TRIGGER_RISING, "SpeedM_irq", NULL)) {
        pr_err("SpeedM_meas: Erreur lors de la demande d'IRQ.\n");
        return -EBUSY;
    }

    // 3. Création du fichier dans /proc
    proc_create("SpeedM_dev", 0660, NULL, &myops);
    
    return 0; // Succès !
}

/* --- FONCTION REMOVE --- */
static void SpeedM_remove(struct platform_device *pdev) {
    free_irq(irq_num, NULL);
    remove_proc_entry("SpeedM_dev", NULL);
    pr_info("SpeedM_meas: Nettoyage termine.\n");
}

/* --- TABLE DE CORRESPONDANCE DEVICE TREE --- */
static const struct of_device_id SpeedM_dt_ids[] = {
    { .compatible = "MP_emb,sm-pulse", }, // Doit être identique au nom dans le .dts
    { }
};
MODULE_DEVICE_TABLE(of, SpeedM_dt_ids);

/* --- DÉFINITION DU PILOTE PLATFORME --- */
static struct platform_driver SpeedM_driver = {
    .probe = SpeedM_probe,
    .remove = SpeedM_remove,
    .driver = {
        .name = "SpeedM_pulse_driver",
        .of_match_table = SpeedM_dt_ids, // Lien avec le Device Tree
    },
};

/* --- MACRO MAGIQUE (Remplace module_init et module_exit) --- */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Enitstudent");
MODULE_DESCRIPTION("Module de mesure de la vitesse du moteur via GPIO (BeagleBone Black)");
MODULE_VERSION("1.0");
module_platform_driver(SpeedM_driver);

