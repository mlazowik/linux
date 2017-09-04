#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/procmon.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <asm/uaccess.h>

struct event {
	struct list_head list;
	struct procmon_event p_event;
	struct completion happened;
	// TODO: refcount events
};

static struct event *create_empty_event(void)
{
	struct event *event;
	event = kmalloc(sizeof (struct event), GFP_KERNEL);
	if (!event) return ERR_PTR(-ENOMEM);
	init_completion(&event->happened);
	INIT_LIST_HEAD(&event->list);
	return event;
}

struct event_stream {
	struct list_head list;
	struct list_head events;
};

LIST_HEAD(event_streams);

static struct event_stream *create_stream(void)
{
	struct event_stream *stream;
	stream = kmalloc(sizeof (struct event_stream), GFP_KERNEL);
	if (!stream) return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&stream->list);
	INIT_LIST_HEAD(&stream->events);
	list_add_tail(&stream->list, &event_streams);
	return stream;
}

void fill_procmon_event(struct procmon_event *p_event, uint32_t type,
	struct task_struct *task)
{
	const struct cred *cred;

	cred = get_task_cred(task);

        p_event->type = type;
        p_event->tid = task->pid;
        p_event->pid = task->pid;
        p_event->ppid = task_ppid_nr(task);
        p_event->uid = cred->uid.val;
        p_event->euid = cred->euid.val;
        p_event->suid = cred->suid.val;
        p_event->fsuid = cred->fsuid.val;
        p_event->status = 42 << 8;
        strcpy(p_event->comm, task->comm);

	return;
}

/**
 * Appends p_event at the end of stream.
 *
 * *p_event is copied.
 */
static int append_event(struct event_stream *stream, struct procmon_event *p_event) 
{
	struct event *event, *empty;

	empty = create_empty_event();
	if (!empty) return -ENOMEM;
	list_add_tail(&empty->list, &stream->events);
	event = list_entry(empty->list.prev, struct event, list);
	event->p_event = *p_event;
	complete_all(&event->happened);

	return 0;
}

/**
 * Adds event from given task of given type to all open event streams.
 */
void add_event(struct task_struct *task, uint32_t type)
{
	struct event_stream *stream;
	struct list_head *pos;
	struct procmon_event p_event;

	fill_procmon_event(&p_event, type, task);

	list_for_each(pos, &event_streams) {
		stream = list_entry(pos, struct event_stream, list);
		append_event(stream, &p_event);
	}
}

/**
 * Adds event of type PROCMON_EVENT_EXISTING from given task and its children
 * to the end of the given event stream.
 *
 * Ignores init process.
 */
static int add_init_events(struct event_stream *stream, struct task_struct *task)
{
	struct task_struct *child;
	struct list_head *list;
	struct procmon_event p_event;
	int ret;

	if (task->pid != 0) {
		fill_procmon_event(&p_event, PROCMON_EVENT_EXISTING, task);
	
		ret = append_event(stream, &p_event);
		if (ret != 0) return ret;
	}
	
	list_for_each(list, &task->children) {
		child = list_entry(list, struct task_struct, sibling);
		ret = add_init_events(stream, child);
		if (ret != 0) return ret;
	}

	return 0;
}

static int procmon_open(struct inode *inode, struct file *file)
{
	struct event_stream *stream;
	struct event *empty;
	int ret;

	printk("procmon: open");

	stream = create_stream();
	if (!stream) return -ENOMEM;
	
	file->private_data = stream;
	
	empty = create_empty_event();
	if (!empty) return -ENOMEM;
	list_add_tail(&empty->list, &stream->events);

	ret = add_init_events(stream, &init_task);
	
	return 0;
}

static ssize_t procmon_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	struct event_stream *stream;
	struct event *event;
	ssize_t ret;
	int not_written;

	if (size < sizeof (struct procmon_event)) {
		printk("procmon: can't fit procmon_event in %d bytes\n", size);
		return -EINVAL;
	}

	printk("procmon: read\n");

	stream = file->private_data;

	// assert events list nonempty
	
	event = list_first_entry(&stream->events, struct event, list);

	if (wait_for_completion_killable(&event->happened) != 0) {
		return 0;
	}

	printk("procmon: writing %s\n", event->p_event.comm);
	ret = sprintf(buf, "%s\n", event->p_event.comm);

	not_written = copy_to_user(buf, &event->p_event, sizeof (struct procmon_event));

	list_del(&event->list);
	kfree(event);
	return (sizeof (struct procmon_event) - not_written);
}

static int procmon_release(struct inode *inode, struct file *file)
{
	struct event_stream *stream;
	struct event *event;
	struct list_head *pos;

	printk("procmon: release");

	stream = file->private_data;

	list_for_each(pos, &stream->events) {
		event = list_entry(pos, struct event, list);
		kfree(event);
	}

	list_del(&stream->list);
	kfree(stream);

	return 0;
}

static const struct file_operations procmon_fops = {
	.open = procmon_open,
	.read = procmon_read,
	.release = procmon_release,
};

static int __init procmon_init(void)
{
	proc_create("procmon", 0, NULL, &procmon_fops);
	return 0;
}
fs_initcall(procmon_init);
