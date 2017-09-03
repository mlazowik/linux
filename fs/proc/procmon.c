#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/procmon.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>
#include <linux/slab.h>
#include <linux/completion.h>

struct event {
	struct list_head list;
	struct procmon_event p_event;
	struct completion happened;
	// TODO: refcount events
};

struct event *get_empty_event(void)
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

struct list_head event_streams;

static int procmon_open(struct inode *inode, struct file *file)
{
	struct event_stream *stream;
	struct event *dummy;

	printk("procmon: open");

	stream = kmalloc(sizeof (struct event_stream), GFP_KERNEL);
	if (!stream) {
		return -ENOMEM;
	}
	
	file->private_data = stream;
	
	INIT_LIST_HEAD(&stream->events);

	dummy = get_empty_event();
	if (!dummy) return -ENOMEM;
	list_add_tail(&dummy->list, &stream->events);

	strcpy(dummy->p_event.comm, "Test");
	complete_all(&dummy->happened);
	
	dummy = get_empty_event();
	if (!dummy) return -ENOMEM;
	list_add_tail(&dummy->list, &stream->events);

	return 0;
}

static ssize_t procmon_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	struct event_stream *stream;
	struct event *event;
	ssize_t ret;

	printk("procmon: read\n");

	stream = file->private_data;

	if (list_empty(&stream->events)) {
		printk("procmon: list empty\n");
		return 0;
	}

	event = list_first_entry(&stream->events, struct event, list);

	if (wait_for_completion_killable(&event->happened) != 0) {
		return 0;
	}

	printk("procmon: writing %s\n", event->p_event.comm);
	ret = sprintf(buf, "%s\n", event->p_event.comm);
	list_del(&event->list);
	kfree(event);
	return ret;
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
