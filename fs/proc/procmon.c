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
};

/**
 * Creates new empty event which has not happened yet.
 */
static struct event *create_empty_event(void)
{
	struct event *event;
	event = kmalloc(sizeof (struct event), GFP_KERNEL);
	if (!event) return NULL;
	init_completion(&event->happened);
	INIT_LIST_HEAD(&event->list);
	return event;
}

struct event_stream {
	struct list_head list;
	struct list_head events;
};

LIST_HEAD(event_streams);

/**
 * Creates new event stream.
 */
static struct event_stream *create_stream(void)
{
	struct event_stream *stream;
	stream = kmalloc(sizeof (struct event_stream), GFP_KERNEL);
	if (!stream) return NULL;
	INIT_LIST_HEAD(&stream->list);
	INIT_LIST_HEAD(&stream->events);
	return stream;
}

/**
 * Fills procmon_event struct with given type and task data.
 */
void fill_procmon_event(struct procmon_event *p_event, uint32_t type,
	struct task_struct *task)
{
	const struct cred *cred;

	cred = get_task_cred(task);

        p_event->type = type;
        p_event->tid = task->pid;
        p_event->pid = task->tgid;
        p_event->ppid = task_ppid_nr(task);
        p_event->uid = cred->uid.val;
        p_event->euid = cred->euid.val;
        p_event->suid = cred->suid.val;
        p_event->fsuid = cred->fsuid.val;
        p_event->status = task->exit_code;
        strcpy(p_event->comm, task->comm);

	return;
}

/**
 * Appends procmon_event at the end of the stream.
 *
 * *p_event is copied.
 */
static void append_event(struct event_stream *stream, struct procmon_event *p_event) 
{
	struct event *event, *empty;

	empty = create_empty_event();
	if (!empty) {
		event = list_last_entry(&stream->events, struct event, list);
		event->p_event.type = PROCMON_EVENT_LOST;
		return;
	}
	list_add_tail(&empty->list, &stream->events);
	event = list_entry(empty->list.prev, struct event, list);

	if (event->p_event.type == PROCMON_EVENT_LOST) {
		append_event(stream, p_event);
	} else {
		event->p_event = *p_event;
	}
	complete_all(&event->happened);
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
		if (list_empty(&stream->events)) return;
		append_event(stream, &p_event);
	}
}

/**
 * Adds event of type PROCMON_EVENT_EXISTING from given task and its children
 * to the end of the given event stream.
 *
 * Ignores init process.
 */
static void add_init_events(struct event_stream *stream, struct task_struct *task)
{
	struct task_struct *child;
	struct list_head *list;
	struct procmon_event p_event;

	if (task->pid != 0) {
		fill_procmon_event(&p_event, PROCMON_EVENT_EXISTING, task);
		append_event(stream, &p_event);
	}
	
	list_for_each(list, &task->children) {
		child = list_entry(list, struct task_struct, sibling);
		add_init_events(stream, child);
	}
}

/**
 * Creates new event stream, attaches it to the given file struct and fills it
 * with one placeholder for a future event.
 */
static int procmon_open(struct inode *inode, struct file *file)
{
	struct event_stream *stream;
	struct event *empty;

	printk("procmon: open");

	stream = create_stream();
	if (!stream) return -ENOMEM;
	
	file->private_data = stream;

	empty = create_empty_event();
	if (!empty) {
		kfree(stream);
		return -ENOMEM;
	}
	list_add_tail(&empty->list, &stream->events);
	
	add_init_events(stream, &init_task);
	
	list_add_tail(&stream->list, &event_streams);
	
	return 0;
}

/**
 * Frees not read events and the stream associated with the given file struct.
 */
static void cleanup(struct file *file) {
	struct event_stream *stream;
        struct event *event;
        struct list_head *pos;

        printk("procmon: cleanup");

        stream = file->private_data;
        list_del(&stream->list);

        list_for_each(pos, &stream->events) {
                event = list_entry(pos, struct event, list);
                kfree(event);
        }

        kfree(stream);
}

/**
 * Returns one event from the beggining of the events stream associated with
 * the given file struct and removes it.
 *
 * Waits if the event has not happened yet.
 */
static ssize_t procmon_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	struct event_stream *stream;
	struct event *event;
	int not_written;

	if (size < sizeof (struct procmon_event)) {
		printk("procmon: event buffer too small\n");
		return -EINVAL;
	}

	stream = file->private_data;

	event = list_first_entry(&stream->events, struct event, list);

	if (wait_for_completion_killable(&event->happened) != 0) {
		return 0;
	}

	not_written = copy_to_user(buf, &event->p_event, sizeof (struct procmon_event));

	list_del(&event->list);
	kfree(event);
	return (sizeof (struct procmon_event) - not_written);
}

static int procmon_release(struct inode *inode, struct file *file)
{
	printk("procmon: release");

	cleanup(file);

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
