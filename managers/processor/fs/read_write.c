/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <lego/stat.h>
#include <lego/slab.h>
#include <lego/uaccess.h>
#include <lego/files.h>
#include <lego/syscalls.h>
#include <lego/comp_processor.h>

#include "internal.h"

static int default_p2m_open(struct file *f)
{
	return 0;
}

static ssize_t default_p2m_read(struct file *f, char __user *buf,
				size_t count, loff_t *off)
{
	return -EACCES;
}

static ssize_t default_p2m_write(struct file *f, const char __user *buf,
				 size_t count, loff_t *off)
{
	return -EACCES;
}

struct file_operations default_f_op = {
	.open	= default_p2m_open,
	.read	= default_p2m_read,
	.write	= default_p2m_write,
};

SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
{
	struct file *f;
	long ret;
	loff_t off = 0;

	syscall_enter();
	pr_info("%s(): fd: %d, buf: %p, count: %zu\n",
		__func__, fd, buf, count);

	f = fdget(fd);
	if (!f) {
		ret = -EBADF;
		goto out;
	}

	ret = f->f_op->read(f, buf, count, &off);

	put_file(f);
out:
	syscall_exit(ret);
	return ret;
}

SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
		size_t, count)
{
	struct file *f;
	long ret;
	loff_t off = 0;
	char *s;

	syscall_enter();
	pr_info("%s(): fd: %d buf: %p count: %zu\n",
		FUNC, fd, buf, count);

	s = kmalloc(PAGE_SIZE, GFP_KERNEL);
	ret = copy_from_user(s, buf, count);
	pr_info("%s(): [%s]\n", FUNC, s);
	kfree(s);

	f = fdget(fd);
	if (!f) {
		ret = -EBADF;
		goto out;
	}

	ret = f->f_op->write(f, buf, count, &off);

	put_file(f);
out:
	syscall_exit(ret);
	return ret;
}

static ssize_t do_readv(unsigned long fd, const struct iovec __user *vec,
			unsigned long vlen, int flags)
{
	pr_info("%s: fd: %lu\n", __func__, fd);
	return -EFAULT;
}

static ssize_t do_writev(unsigned long fd, const struct iovec __user *vec,
			 unsigned long vlen, int flags)
{
	struct iovec *kvec;
	char *buf;
	int i;
	ssize_t ret = 0;

	pr_info("%s: fd: %lu, nrvec: %lu\n",
		__func__, fd, vlen);

	kvec = kmalloc(vlen * sizeof(*kvec), GFP_KERNEL);
	if (!kvec)
		return -ENOMEM;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf) {
		kfree(kvec);
		return -ENOMEM;
	}

	if (copy_from_user(kvec, vec, vlen * sizeof(*kvec))) {
		ret = -EFAULT;
		goto free;
	}

	for (i = 0; i < vlen; i++) {
		if (copy_from_user(buf, kvec[i].iov_base, kvec[i].iov_len)) {
			ret = -EFAULT;
			goto free;
		}
		ret += kvec[i].iov_len;

		pr_info("  vec[%d]: %s\n", i, buf);
	}

free:
	kfree(kvec);
	kfree(buf);
	return ret;
}

SYSCALL_DEFINE3(readv, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen)
{
	long ret;

	syscall_enter();
	ret = do_readv(fd, vec, vlen, 0);
	syscall_exit(ret);
	return ret;
}

SYSCALL_DEFINE3(writev, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen)
{
	long ret;

	syscall_enter();
	ret = do_writev(fd, vec, vlen, 0);
	syscall_exit(ret);
	return ret;
}