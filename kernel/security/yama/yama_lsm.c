/*
 * Yama Linux Security Module
 *
 * Author: Kees Cook <keescook@chromium.org>
 *
 * Copyright (C) 2010 Canonical, Ltd.
 * Copyright (C) 2011 The Chromium OS Authors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <linux/security.h>
#include <linux/sysctl.h>
#include <linux/ptrace.h>
#include <linux/fs.h>
#include <linux/prctl.h>
#include <linux/ratelimit.h>

#define YAMA_SCOPE_DISABLED	0
#define YAMA_SCOPE_RELATIONAL	1
#define YAMA_SCOPE_CAPABILITY	2
#define YAMA_SCOPE_NO_ATTACH	3

static int ptrace_scope = YAMA_SCOPE_RELATIONAL;
static int protected_sticky_symlinks = 1;
static int protected_nonaccess_hardlinks = 1;

/* describe a ptrace relationship for potential exception */
struct ptrace_relation {
	struct task_struct *tracer;
	struct task_struct *tracee;
	struct list_head node;
};

static LIST_HEAD(ptracer_relations);
static DEFINE_SPINLOCK(ptracer_relations_lock);

/**
 * yama_ptracer_add - add/replace an exception for this tracer/tracee pair
 * @tracer: the task_struct of the process doing the ptrace
 * @tracee: the task_struct of the process to be ptraced
 *
 * Each tracee can have, at most, one tracer registered. Each time this
 * is called, the prior registered tracer will be replaced for the tracee.
 *
 * Returns 0 if relationship was added, -ve on error.
 */
static int yama_ptracer_add(struct task_struct *tracer,
			    struct task_struct *tracee)
{
	int rc = 0;
	struct ptrace_relation *added;
	struct ptrace_relation *entry, *relation = NULL;

	added = kmalloc(sizeof(*added), GFP_KERNEL);
	if (!added)
		return -ENOMEM;

	spin_lock_bh(&ptracer_relations_lock);
	list_for_each_entry(entry, &ptracer_relations, node)
		if (entry->tracee == tracee) {
			relation = entry;
			break;
		}
	if (!relation) {
		relation = added;
		relation->tracee = tracee;
		list_add(&relation->node, &ptracer_relations);
	}
	relation->tracer = tracer;

	spin_unlock_bh(&ptracer_relations_lock);
	if (added != relation)
		kfree(added);

	return rc;
}

/**
 * yama_ptracer_del - remove exceptions related to the given tasks
 * @tracer: remove any relation where tracer task matches
 * @tracee: remove any relation where tracee task matches
 */
static void yama_ptracer_del(struct task_struct *tracer,
			     struct task_struct *tracee)
{
	struct ptrace_relation *relation, *safe;

	spin_lock_bh(&ptracer_relations_lock);
	list_for_each_entry_safe(relation, safe, &ptracer_relations, node)
		if (relation->tracee == tracee ||
		    (tracer && relation->tracer == tracer)) {
			list_del(&relation->node);
			kfree(relation);
		}
	spin_unlock_bh(&ptracer_relations_lock);
}

/**
 * yama_task_free - check for task_pid to remove from exception list
 * @task: task being removed
 */
void yama_task_free(struct task_struct *task)
{
	yama_ptracer_del(task, task);
}

/**
 * yama_task_prctl - check for Yama-specific prctl operations
 * @option: operation
 * @arg2: argument
 * @arg3: argument
 * @arg4: argument
 * @arg5: argument
 *
 * Return 0 on success, -ve on error.  -ENOSYS is returned when Yama
 * does not handle the given option.
 */
int yama_task_prctl(int option, unsigned long arg2, unsigned long arg3,
			   unsigned long arg4, unsigned long arg5)
{
	int rc;
	struct task_struct *myself = current;

	rc = cap_task_prctl(option, arg2, arg3, arg4, arg5);
	if (rc != -ENOSYS)
		return rc;

	switch (option) {
	case PR_SET_PTRACER:
		/* Since a thread can call prctl(), find the group leader
		 * before calling _add() or _del() on it, since we want
		 * process-level granularity of control. The tracer group
		 * leader checking is handled later when walking the ancestry
		 * at the time of PTRACE_ATTACH check.
		 */
		rcu_read_lock();
		if (!thread_group_leader(myself))
			myself = rcu_dereference(myself->group_leader);
		get_task_struct(myself);
		rcu_read_unlock();

		if (arg2 == 0) {
			yama_ptracer_del(NULL, myself);
			rc = 0;
		} else if (arg2 == PR_SET_PTRACER_ANY || (int)arg2 == -1) {
			rc = yama_ptracer_add(NULL, myself);
		} else {
			struct task_struct *tracer;

			rcu_read_lock();
			tracer = find_task_by_vpid(arg2);
			if (tracer)
				get_task_struct(tracer);
			else
				rc = -EINVAL;
			rcu_read_unlock();

			if (tracer) {
				rc = yama_ptracer_add(tracer, myself);
				put_task_struct(tracer);
			}
		}

		put_task_struct(myself);
		break;
	}

	return rc;
}

/**
 * task_is_descendant - walk up a process family tree looking for a match
 * @parent: the process to compare against while walking up from child
 * @child: the process to start from while looking upwards for parent
 *
 * Returns 1 if child is a descendant of parent, 0 if not.
 */
static int task_is_descendant(struct task_struct *parent,
			      struct task_struct *child)
{
	int rc = 0;
	struct task_struct *walker = child;

	if (!parent || !child)
		return 0;

	rcu_read_lock();
	if (!thread_group_leader(parent))
		parent = rcu_dereference(parent->group_leader);
	while (walker->pid > 0) {
		if (!thread_group_leader(walker))
			walker = rcu_dereference(walker->group_leader);
		if (walker == parent) {
			rc = 1;
			break;
		}
		walker = rcu_dereference(walker->real_parent);
	}
	rcu_read_unlock();

	return rc;
}

/**
 * ptracer_exception_found - tracer registered as exception for this tracee
 * @tracer: the task_struct of the process attempting ptrace
 * @tracee: the task_struct of the process to be ptraced
 *
 * Returns 1 if tracer has is ptracer exception ancestor for tracee.
 */
static int ptracer_exception_found(struct task_struct *tracer,
				   struct task_struct *tracee)
{
	int rc = 0;
	struct ptrace_relation *relation;
	struct task_struct *parent = NULL;
	bool found = false;

	spin_lock_bh(&ptracer_relations_lock);
	rcu_read_lock();
	if (!thread_group_leader(tracee))
		tracee = rcu_dereference(tracee->group_leader);
	list_for_each_entry(relation, &ptracer_relations, node)
		if (relation->tracee == tracee) {
			parent = relation->tracer;
			found = true;
			break;
		}

	if (found && (parent == NULL || task_is_descendant(parent, tracer)))
		rc = 1;
	rcu_read_unlock();
	spin_unlock_bh(&ptracer_relations_lock);

	return rc;
}

/**
 * yama_ptrace_access_check - validate PTRACE_ATTACH calls
 * @child: task that current task is attempting to ptrace
 * @mode: ptrace attach mode
 *
 * Returns 0 if following the ptrace is allowed, -ve on error.
 */
int yama_ptrace_access_check(struct task_struct *child,
				    unsigned int mode)
{
	int rc;

	/* If standard caps disallows it, so does Yama.  We should
	 * only tighten restrictions further.
	 */
	rc = cap_ptrace_access_check(child, mode);
	if (rc)
		return rc;

	/* require ptrace target be a child of ptracer on attach */
	if (mode == PTRACE_MODE_ATTACH) {
		switch (ptrace_scope) {
		case YAMA_SCOPE_DISABLED:
			/* No additional restrictions. */
			break;
		case YAMA_SCOPE_RELATIONAL:
			if (!task_is_descendant(current, child) &&
			    !ptracer_exception_found(current, child) &&
			    !capable(CAP_SYS_PTRACE))
				rc = -EPERM;
			break;
		case YAMA_SCOPE_CAPABILITY:
			if (!capable(CAP_SYS_PTRACE))
				rc = -EPERM;
			break;
		case YAMA_SCOPE_NO_ATTACH:
		default:
			rc = -EPERM;
			break;
		}
	}

	if (rc) {
		char name[sizeof(current->comm)];
		printk_ratelimited(KERN_NOTICE
			"ptrace of pid %d was attempted by: %s (pid %d)\n",
			child->pid,
			get_task_comm(name, current),
			current->pid);
	}

	return rc;
}

/**
 * yama_ptrace_traceme - validate PTRACE_TRACEME calls
 * @parent: task that will become the ptracer of the current task
 *
 * Returns 0 if following the ptrace is allowed, -ve on error.
 */
int yama_ptrace_traceme(struct task_struct *parent)
{
	int rc;

	/* If standard caps disallows it, so does Yama.  We should
	 * only tighten restrictions further.
	 */
	rc = cap_ptrace_traceme(parent);
	if (rc)
		return rc;

	/* Only disallow PTRACE_TRACEME on more aggressive settings. */
	switch (ptrace_scope) {
	case YAMA_SCOPE_CAPABILITY:
		if (!ns_capable(task_user_ns(parent), CAP_SYS_PTRACE))
			rc = -EPERM;
		break;
	case YAMA_SCOPE_NO_ATTACH:
		rc = -EPERM;
		break;
	}

	if (rc) {
		char name[sizeof(current->comm)];
		printk_ratelimited(KERN_NOTICE
			"ptraceme of pid %d was attempted by: %s (pid %d)\n",
			current->pid,
			get_task_comm(name, parent),
			parent->pid);
	}

	return rc;
}

/**
 * yama_inode_follow_link - check for symlinks in sticky world-writeable dirs
 * @dentry: The inode/dentry of the symlink
 * @nameidata: The path data of the symlink
 *
 * In the case of the protected_sticky_symlinks sysctl being enabled,
 * CAP_DAC_OVERRIDE needs to be specifically ignored if the symlink is
 * in a sticky world-writable directory.  This is to protect privileged
 * processes from failing races against path names that may change out
 * from under them by way of other users creating malicious symlinks.
 * It will permit symlinks to only be followed when outside a sticky
 * world-writable directory, or when the uid of the symlink and follower
 * match, or when the directory owner matches the symlink's owner.
 *
 * Returns 0 if following the symlink is allowed, -ve on error.
 */
int yama_inode_follow_link(struct dentry *dentry,
			   struct nameidata *nameidata)
{
	int rc = 0;
	const struct inode *parent;
	const struct inode *inode;
	const struct cred *cred;

	if (!protected_sticky_symlinks)
		return 0;

	/* if inode isn't a symlink, don't try to evaluate blocking it */
	inode = dentry->d_inode;
	if (!S_ISLNK(inode->i_mode))
		return 0;

	/* owner and follower match? */
	cred = current_cred();
	if (cred->fsuid == inode->i_uid)
		return 0;

	/* check parent directory mode and owner */
	spin_lock(&dentry->d_lock);
	parent = dentry->d_parent->d_inode;
	if ((parent->i_mode & (S_ISVTX|S_IWOTH)) == (S_ISVTX|S_IWOTH) &&
	    parent->i_uid != inode->i_uid) {
		rc = -EACCES;
	}
	spin_unlock(&dentry->d_lock);

	if (rc) {
		char name[sizeof(current->comm)];
		printk_ratelimited(KERN_NOTICE "non-matching-uid symlink "
			"following attempted in sticky world-writable "
			"directory by %s (fsuid %d != %d)\n",
			get_task_comm(name, current),
			cred->fsuid, inode->i_uid);
	}

	return rc;
}

static int yama_generic_permission(struct inode *inode, int mask)
{
	int retval;

	if (inode->i_op->permission)
		retval = inode->i_op->permission(inode, mask);
	else
		retval = generic_permission(inode, mask);
	return retval;
}

/**
 * yama_path_link - verify that hardlinking is allowed
 * @old_dentry: the source inode/dentry to hardlink from
 * @new_dir: target directory
 * @new_dentry: the target inode/dentry to hardlink to
 *
 * Block hardlink when all of:
 *  - fsuid does not match inode
 *  - not CAP_FOWNER
 *  - and at least one of:
 *    - inode is not a regular file
 *    - inode is setuid
 *    - inode is setgid and group-exec
 *    - access failure for read and write
 *
 * Returns 0 if successful, -ve on error.
 */
int yama_path_link(struct dentry *old_dentry, struct path *new_dir,
		   struct dentry *new_dentry)
{
	int rc = 0;
	struct inode *inode = old_dentry->d_inode;
	const int mode = inode->i_mode;
	const struct cred *cred = current_cred();

	if (!protected_nonaccess_hardlinks)
		return 0;

	if (cred->fsuid != inode->i_uid &&
	    (!S_ISREG(mode) || (mode & S_ISUID) ||
	     ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) ||
	     (yama_generic_permission(inode, MAY_READ | MAY_WRITE))) &&
	    !capable(CAP_FOWNER)) {
		char name[sizeof(current->comm)];
		printk_ratelimited(KERN_NOTICE "non-accessible hardlink"
			" creation was attempted by: %s (fsuid %d)\n",
			get_task_comm(name, current),
			cred->fsuid);
		rc = -EPERM;
	}

	return rc;
}

#ifndef CONFIG_SECURITY_YAMA_STACKED
static struct security_operations yama_ops = {
	.name =			"yama",

	.ptrace_access_check =	yama_ptrace_access_check,
	.ptrace_traceme =	yama_ptrace_traceme,
	.task_prctl =		yama_task_prctl,
	.task_free =		yama_task_free,
	.inode_follow_link =	yama_inode_follow_link,
	.path_link =		yama_path_link,
};
#endif

#ifdef CONFIG_SYSCTL
static int yama_dointvec_minmax(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int rc;

	if (write && !capable(CAP_SYS_PTRACE))
		return -EPERM;

	rc = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (rc)
		return rc;

	/* Lock the max value if it ever gets set. */
	if (write && *(int *)table->data == *(int *)table->extra2)
		table->extra1 = table->extra2;

	return rc;
}

static int zero;
static int one = 1;
static int max_scope = YAMA_SCOPE_NO_ATTACH;

struct ctl_path yama_sysctl_path[] = {
	{ .procname = "kernel", },
	{ .procname = "yama", },
	{ }
};

static struct ctl_table yama_sysctl_table[] = {
	{
		.procname       = "protected_sticky_symlinks",
		.data           = &protected_sticky_symlinks,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &one,
	},
	{
		.procname       = "protected_nonaccess_hardlinks",
		.data           = &protected_nonaccess_hardlinks,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &one,
	},
	{
		.procname       = "ptrace_scope",
		.data           = &ptrace_scope,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = yama_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &max_scope,
	},
	{ }
};
#endif /* CONFIG_SYSCTL */

static __init int yama_init(void)
{
#ifndef CONFIG_SECURITY_YAMA_STACKED
	if (!security_module_enable(&yama_ops))
		return 0;
#endif

	printk(KERN_INFO "Yama: becoming mindful.\n");

#ifndef CONFIG_SECURITY_YAMA_STACKED
	if (register_security(&yama_ops))
		panic("Yama: kernel registration failed.\n");
#endif

#ifdef CONFIG_SYSCTL
	if (!register_sysctl_paths(yama_sysctl_path, yama_sysctl_table))
		panic("Yama: sysctl registration failed.\n");
#endif

	return 0;
}

security_initcall(yama_init);
