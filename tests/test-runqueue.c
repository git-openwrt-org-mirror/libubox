/*
 * runqueue-example.c
 *
 * Copyright (C) 2013 Felix Fietkau <nbd@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "uloop.h"
#include "runqueue.h"

static struct runqueue q;

struct sleeper {
	int val;
	bool kill;
	struct uloop_timeout t;
	struct runqueue_process proc;
};

static void q_empty(struct runqueue *q)
{
	fprintf(stderr, "All done!\n");
	uloop_end();
}

static const char* sleeper_type(struct sleeper *s)
{
	return s->kill ? "killer" : "sleeper";
}

static void q_sleep_run(struct runqueue *q, struct runqueue_task *t)
{
	struct sleeper *s = container_of(t, struct sleeper, proc.task);
	char str[32];
	pid_t pid;

	fprintf(stderr, "[%d/%d] start 'sleep %d' (%s)\n", q->running_tasks,
			q->max_running_tasks, s->val, sleeper_type(s));

	pid = fork();
	if (pid < 0)
		return;

	if (pid) {
		runqueue_process_add(q, &s->proc, pid);
		return;
	}

	sprintf(str, "%d", s->val);
	execlp("sleep", "sleep", str, NULL);
	exit(1);
}

static void q_sleep_cancel(struct runqueue *q, struct runqueue_task *t, int type)
{
	struct sleeper *s = container_of(t, struct sleeper, proc.task);

	fprintf(stderr, "[%d/%d] cancel 'sleep %d' (%s)\n", q->running_tasks,
			q->max_running_tasks, s->val, sleeper_type(s));
	runqueue_process_cancel_cb(q, t, type);
}

static void q_sleep_complete(struct runqueue *q, struct runqueue_task *p)
{
	struct sleeper *s = container_of(p, struct sleeper, proc.task);

	fprintf(stderr, "[%d/%d] finish 'sleep %d' (%s) \n", q->running_tasks,
			q->max_running_tasks, s->val, sleeper_type(s));
	free(s);
}

static void my_runqueue_process_kill_cb(struct runqueue *q, struct runqueue_task *p)
{
	struct sleeper *s = container_of(p, struct sleeper, proc.task);

	fprintf(stderr, "[%d/%d] killing process (%s)\n", q->running_tasks,
			q->max_running_tasks, sleeper_type(s));
	runqueue_process_kill_cb(q, p);
}

static void timer_cb(struct uloop_timeout *t)
{
	struct sleeper *s = container_of(t, struct sleeper, t);
	if (s->kill)
		runqueue_task_kill(&s->proc.task);
}

static struct sleeper* create_sleeper(int val, const struct runqueue_task_type *type, bool kill)
{
	struct sleeper *s = calloc(1, sizeof(*s));
	s->kill = kill;
	s->t.cb = timer_cb;
	s->proc.task.type = type;
	s->proc.task.run_timeout = 500;
	s->proc.task.complete = q_sleep_complete;
	s->val = val;

	return s;
}

static void add_sleeper(int val)
{
	static const struct runqueue_task_type sleeper_type = {
		.run = q_sleep_run,
		.cancel = q_sleep_cancel,
		.kill = runqueue_process_kill_cb,
	};

	static const struct runqueue_task_type killer_type = {
		.run = q_sleep_run,
		.cancel = q_sleep_cancel,
		.kill = my_runqueue_process_kill_cb,
	};

	struct sleeper *k = create_sleeper(val, &killer_type, true);
	uloop_timeout_set(&k->t, 10);
	uloop_timeout_add(&k->t);
	runqueue_task_add(&q, &k->proc.task, false);

	struct sleeper *s = create_sleeper(val, &sleeper_type, false);
	runqueue_task_add(&q, &s->proc.task, false);
}

int main(int argc, char **argv)
{
	uloop_init();

	runqueue_init(&q);
	q.empty_cb = q_empty;
	q.max_running_tasks = 1;

	if (argc > 1)
		q.max_running_tasks = atoi(argv[1]);

	add_sleeper(1);
	add_sleeper(1);
	add_sleeper(1);

	uloop_run();
	uloop_done();

	return 0;
}
