#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/procmon.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>
#include <linux/slab.h>

struct event {
	struct list_head list;
	struct procmon_event p_event;
	// TODO: refcount events
};

struct event_stream {
	struct list_head list;
	struct list_head events;
};

struct list_head event_streams;

static void *procmon_start(struct seq_file *m, loff_t *pos)
{
	struct event_stream *stream;
	stream = m->private;
	return seq_list_start(&stream->events, *pos);
}

static void *procmon_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct event_stream *stream;
	stream = m->private;
	return seq_list_next(v, &stream->events, pos);
}

static void procmon_stop(struct seq_file *m, void *t) {}

static int procmon_show(struct seq_file *m, void *v)
{
	struct event *event;
	
	event = list_entry(v, struct event, list);
	seq_printf(m, "%s\n", event->p_event.comm);

	return 0;
}

static const struct seq_operations procmon_seq_ops = {
	.start = procmon_start,
	.next = procmon_next,
	.stop = procmon_stop,
	.show = procmon_show,
};

static int procmon_open(struct inode *inode, struct file *file)
{
	struct event_stream *stream;
	struct event *dummy;

	printk("procmon: open");
	
	stream = __seq_open_private(file, &procmon_seq_ops, sizeof(*stream));

	if (!stream) {
		return -ENOMEM;
	}

	dummy = kmalloc(sizeof (struct event), GFP_KERNEL);

	if (!dummy) {
		return -ENOMEM;
	}

	strcpy(dummy->p_event.comm, "Test");
	
	INIT_LIST_HEAD(&dummy->list);

	INIT_LIST_HEAD(&stream->events);

	list_add(&dummy->list, &stream->events);

	return 0;
}

static int procmon_release(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	struct event_stream *stream;
	struct event *event;
	struct list_head *pos;

	printk("procmon: release");

	m = file->private_data;
	stream = m->private;

	list_for_each(pos, &stream->events) {
		event = list_entry(pos, struct event, list);
		kfree(event);
	}

	return seq_release_private(inode, file);
}

static const struct file_operations procmon_fops = {
	.open = procmon_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = procmon_release,
};

static int __init procmon_init(void)
{
	proc_create("procmon", 0, NULL, &procmon_fops);
	return 0;
}
fs_initcall(procmon_init);
