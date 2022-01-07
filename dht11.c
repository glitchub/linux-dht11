// Stand-alone dht11 driver for linux.
// Install linux headers then 'make'.
// Start with "sudo insmod dht11.ko gpio=X" (default gpio is 4)

// Access with "cat /dev/dht11", returns "HHH TTT", where HHH is the relative
// humidity in tenths of a percent, TTT is temperature in tenths of a degree.
// Or returns a read error if DHT11 device is not functional.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

static int gpio=4;                              // module parameter
module_param(gpio, int, 0660);

// DHT11 has a single I/O pin, pulled up to VCC.
// To initiate, force the pin high for 18 mS then tri-state.
// DHT11 response is:
//      80 uS low
//      80 uS high
//      50 uS low (captured as nominal width)
//      For forty bits:
//          28uS high = 0 or 70 uS high = 1 (relative to nominal)
//          50 uS low

// Handle GPIO edges. Normally in state 0 and edges are ignored.  Thread sets
// start 1 somewhere in the middle of the initial 80 uS low.
volatile u8 data[5];    // ISR received data
volatile int state = 0; // ISR state
static irqreturn_t doirq(int irq, void *unused)
{
    static u64 edge = 0;        // time of last edge
    static u32 bit = 0;         // current bit position 0 - 39
    static u64 nominal = 0;     // nominal 50 uS width
    static unsigned long flags;

    local_irq_save(flags);
    if (state)
    {
        u64 now = ktime_get_ns();
        switch(state)
        {
            case 1:
                // wait for initial 80 uS high
                if (gpio_get_value(gpio)) state = 2;
                break;

            case 2:
                // initial 50uS low, remember edge
                edge = now;
                bit = 0;
                state = 3;
                break;

            case 3:
                if (gpio_get_value(gpio))
                {
                    // data bit rising edge
                    if (bit == 0) nominal = now - edge; // remember nominal 50uS
                    edge = now;
                } else
                {
                    // data bit falling edge
                    data[bit/8] = (data[bit/8] << 1) | ((now - edge) > nominal);
                    if (++bit >= 40) state = 0; // done, notify thread
                }
                break;
        }
    }
    local_irq_restore(flags);
    return IRQ_HANDLED;
}

// Thread polls DHT11 every 2 seconds and tracks temperature and humidity
int valid, temperature, humidity;           // results (default 0)
struct mutex mutex;                         // arbitrate with doread
static int dothread(void *pv)
{
    int await = 0;                          // number of 100mS intervals to next poll
    int final;                              // terminal ISR state

    while (!kthread_should_stop())
    {
        msleep(100);                        // about every 100 mS
        if (--await > 0) continue;          // spin until 2 seconds

        // Prime the ISR
        state = 0;                          // make sure interrupt is ignored

        // Trigger DHT11
        gpio_direction_output(gpio, 0);     // set gpio low
        mdelay(18);                         // DHT11 needs min 18mS to signal a startup
        gpio_direction_input(gpio);         // change back to an input
        udelay(40);                         // let it start to respond

        state = 1;                          // enable ISR response
        msleep(25);                         // give it time
        final = state;                      // remember final state
        state = 0;                          // disable ISR response

        if (final == 0 &&
            data[1] <= 9 &&
            data[3] <= 9 &&
            ((data[0] + data[1] + data[2] + data[3]) & 255) == data[4])
        {
            mutex_lock(&mutex);
            valid = 5;
            humidity = (data[0] * 10) + data[1];
            temperature = (data[2] * 10) + data[3];
            mutex_unlock(&mutex);
        }
        else
        {
            if (valid) valid--;
            pr_err("dht11: error, final = %d data = %02X %02X %02X %02X %02X\n", final, data[0], data[1], data[2], data[3], data[4]);
        }
        await = 2000/100;                   // another 2 seconds
    }
    return 0;
}

#define MAXS 32 // longest string to read

// open file and assign string space
static int doopen(struct inode *inode, struct file *filp)
{
    filp->private_data = kmalloc(MAXS, GFP_KERNEL);
    if (!filp->private_data) return -ENOMEM;
    *(char *)filp->private_data = 0;
    return 0;
}

// read current humidity and temperature
static ssize_t doread(struct file *filp, char __user *buf, size_t length, loff_t *ofs)
{
    ssize_t n;

    if (*ofs)
    {
        // remainder from last read
        n = strlen(filp->private_data) - *ofs;
        if (n <= 0) return 0;
    } else
    {
        if (!valid) return -EINVAL;
        mutex_lock(&mutex);
        n = sprintf(filp->private_data, "%d %d\n", humidity, temperature) + 1; // never more than MAXS bytes
        mutex_unlock(&mutex);
    }
    if (n > length) n = length;
    n -= copy_to_user(buf, filp->private_data + *ofs, n);
    *ofs += n;
    return n;
}

// close file and free string
static int doclose(struct inode *inode, struct file *filp)
{
    kfree(filp->private_data);
    return 0;
}

static struct file_operations fops =
{
    .owner = THIS_MODULE,
    .read = doread,
    .open = doopen,
    .release = doclose,
};

static struct miscdevice miscdev =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "dht11",
    .fops = &fops,
    .mode = 0444 // world readable
};

int irq;                     // gpio IRQ number
struct task_struct *thread;  // poll thread

static int __init doinit(void)
{
    pr_info("dht11: installing on gpio %d\n", gpio);

    if (gpio_request(gpio, "dht11") < 0)
    {
        pr_err("dht11: gpio_request failed\n");
        return -EINVAL;
    }

    mutex_init(&mutex);

    if (misc_register(&miscdev))
    {
        pr_err("dht11: misc_register failed\n");
        rmgpio:
        gpio_free(gpio);
        return -EINVAL;
    }

    irq = gpio_to_irq(gpio);
    if (irq <= 0 || request_irq(irq, doirq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "dht11", NULL) < 0)
    {
        pr_err("dht11: request_irq %d failed\n", irq);
        rmmisc:
        misc_deregister(&miscdev);
        goto rmgpio;
    }

    thread = kthread_run(dothread, NULL, "dht11");
    if (!thread)
    {
        pr_err("dht11: kthread_create failed\n");
        goto rmmisc;
    }

    return 0;
}

static void __exit doexit(void)
{
    kthread_stop(thread);
    free_irq(irq, NULL);
    misc_deregister(&miscdev);
    gpio_free(gpio);
}

module_init(doinit);
module_exit(doexit);
MODULE_LICENSE("GPL");
