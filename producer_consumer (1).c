#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/delay.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Micaela Leong");

static int prod = 1;
static int cons = 0;
static int size = 0;
static int uid = 0;

module_param(prod, int, 0);
module_param(cons, int, 0);
module_param(size, int, 0);
module_param(uid, int, 0);

struct semaphore empty;
struct semaphore full;
static DEFINE_MUTEX(mutex);

static struct task_struct **producers;
static struct task_struct **consumers;

static struct task_struct **zombie_buffer;
static int in = 0;
static int out = 0;

#define EXIT_ZOMBIE 0x00000020

static int producer_thread(void *data)
{
	struct task_struct *p;
	int num = (int)(long)data;

	while (!kthread_should_stop())
	{
		for_each_process(p)
		{
			if (p->cred->uid.val != uid)
			{
				continue;
			}

			if (p->exit_state & EXIT_ZOMBIE)
			{
				if (down_interruptible(&empty))
				{
					continue;
				}

				mutex_lock(&mutex);

				get_task_struct(p);
				zombie_buffer[in] = p;
				in = (in + 1) % size;

				printk("Producer-%d has produced a zombie process with pid %d and parent pid %d/n", num, p->pid, p->parent->pid);

				mutex_unlock(&mutex);
				up(&full);
			}
		}

		// if (zombies_found == 0)
		//{
		msleep(250);
		//}
	}

	return 0;
}

static int consumer_thread(void *data)
{
	struct task_struct *zombie;
	int num = (int)(long)data;
	while (!kthread_should_stop())
	{
		if (down_interruptible(&full))
		{
			continue;
		}

		mutex_lock(&mutex);

		zombie = zombie_buffer[out];
		zombie_buffer[out] = NULL;
		out = (out + 1) % size;

		mutex_unlock(&mutex);
		up(&empty);

		printk("Consumer-%d has consumed a zombie process with pid %d and parent pid %d\n", num, zombie->pid, zombie->parent->pid);

		kill_pid(zombie->parent->thread_pid, SIGKILL, 0);
	}

	return 0;
}

static int __init producer_consumer_init(void)
{
	int i;

	sema_init(&empty, size);
	sema_init(&full, 0);

	zombie_buffer = kmalloc_array(size, sizeof(struct task_struct *), GFP_KERNEL);

	producers = kmalloc_array(prod, sizeof(struct task_struct *), GFP_KERNEL);
	for (i = 0; i < prod; i++)
	{
		producers[i] = kthread_run(producer_thread, &i, "Producer-%d", i);
	}

	consumers = kmalloc_array(cons, sizeof(struct task_struct *), GFP_KERNEL);
	for (i = 0; i < cons; i++)
	{
		consumers[i] = kthread_run(consumer_thread, &i, "Consumer-%d", i);
	}

	return 0;
}

static void __exit producer_consumer_exit(void)
{
	int i;

	for (i = 0; i < prod; i++)
	{
		kthread_stop(producers[i]);
	}

	for (i = 0; i < cons; i++)
	{
		kthread_stop(consumers[i]);
	}

	for (i = 0; i < size; i++)
	{
		if (zombie_buffer[i])
		{
			put_task_struct(zombie_buffer[i]);
		}
	}

	kfree(producers);
	kfree(consumers);
	kfree(zombie_buffer);
}

module_init(producer_consumer_init);
module_exit(producer_consumer_exit);
