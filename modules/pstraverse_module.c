#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/fs.h> 
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alp");
MODULE_DESCRIPTION("A simple example Linux module.");
MODULE_VERSION("0.01");

struct task_struct *task;
struct task_struct *task_child;
struct list_head *list;
struct pid *pid_struct;


static int user_input = 3;
module_param(user_input, int, S_IRUGO);
MODULE_PARM_DESC(user_input, "The PID of a process.");

static int __init pstraverse_module_init(void) {
    printk(KERN_INFO "Loading module. Desired PID recieved: %d\n", user_input);

    pid_struct = find_get_pid(user_input);
    task = pid_task(pid_struct, PIDTYPE_PID);

    for_each_process(task) {
        printk(KERN_INFO "\nParent PID: %d Process: %s", task->pid, task->comm);
          list_for_each(list, &task->children) {
            task_child = list_entry(list, struct task_struct, sibling);
            printk(KERN_INFO "\n Child of %s[%d] Pid: %d, Process: %s", task->comm, task->pid, task_child->pid, task_child->comm);
        }
        printk("-----------------------------------------------------");
    }
    return 0;
}

static void __exit pstraverse_module_exit(void) {
    printk(KERN_INFO "Removing Module.\n");
}

module_init(pstraverse_module_init);
module_exit(pstraverse_module_exit);