
#include "proc_table.h"

#include "array.h"
#include "limits.h"
#include "synch.h"

struct proc_table_entry {
	/* struct cv* pte_waitpid_cv; */
	struct array pte_child_pids;
	pid_t pte_parent_pid;
	bool pte_has_exited;
	int pte_exit_status;
	/* int pte_refcount; */
};



typedef struct proc_table_entry* proc_table[__PID_MAX];

static proc_table p_table = { NULL };

static struct lock* pid_locks[__PID_MAX];

static
struct proc_table_entry*
proc_table_entry_create(pid_t pid, const pid_t* parent_pid) {

	struct proc_table_entry* pte = kmalloc(sizeof(struct proc_table_entry));

	/* TODO: Better name for the CV */
	(void)pid;
	/* pte->pte_waitpid_cv = cv_create(""); */

	array_init(&pte->pte_child_pids);

	pte->pte_parent_pid = parent_pid != NULL ? *parent_pid : -1;
	pte->pte_has_exited = false;

	return pte;
}

static
void
proc_table_entry_destroy(struct proc_table_entry* pte) {
	/* cv_destroy(pte->pte_waitpid_cv); /\* FIXME: kpanic!  *\/ */

	array_setsize(&pte->pte_child_pids, 0);
	array_cleanup(&pte->pte_child_pids);

	kfree(pte);
}

void
proc_table_init() {
        /* TEMP HACK */
	p_table[1] = proc_table_entry_create(1, 0);

        for (pid_t i = 0; i < __PID_MAX; ++i) {
                char buf[64];
                snprintf(buf, sizeof(buf), "pid_lock_%d", i);
                pid_locks[i] = lock_create(buf);
        }
}

void
pid_lock_acquire(pid_t pid) {
        lock_acquire(pid_locks[pid]);
}

void
pid_lock_release(pid_t pid) {
        lock_release(pid_locks[pid]);
}

bool
proc_table_entry_exists(pid_t pid) {
        return p_table[pid] != NULL;
}

void remove_proc_table_entry(pid_t pid) {
        KASSERT(proc_table_entry_exists(pid));
        proc_table_entry_destroy(p_table[pid]);
        p_table[pid] = NULL;
}

bool
proc_has_child(pid_t parent, pid_t child) {
	/* TODO: Maybe lock the parent */

        const struct array* child_pids = &p_table[parent]->pte_child_pids;

	for (unsigned i = 0; i < child_pids->num; ++i) {
		if ((pid_t)array_get(child_pids, i) == child) {
			return true;
		}
	}
	return false;
}

int
proc_add_child(pid_t parent, pid_t child) {
        return array_add(&p_table[parent]->pte_child_pids, (void*)child, NULL);
}

struct array*
proc_get_children(pid_t pid) {
        return &p_table[pid]->pte_child_pids;
}

/* Returns INVALID_PID if the process does not have a parent */
pid_t proc_get_parent(pid_t pid) {
        return p_table[pid]->pte_parent_pid;
}

void proc_exit(pid_t proc, int status) {
        p_table[proc]->pte_has_exited = true;
        p_table[proc]->pte_exit_status = status;
}
bool proc_has_exited(pid_t pid) {
        return p_table[pid]->pte_parent_pid;
}

/* Returns INVALID_PID if a pid cannot be reserved */
pid_t
reserve_pid(const pid_t* parent_pid /* may be NULL */) {
	for (pid_t pid = 1; pid < __PID_MAX; ++pid) {

                if (parent_pid != NULL && pid == *parent_pid) {
                        /* Avoid deadlocking on our own lock */
                        continue;
                }

                if (p_table[pid] == NULL) {
                        pid_lock_acquire(pid);
                        if (p_table[pid] == NULL) {

                                p_table[pid] = proc_table_entry_create(pid, parent_pid);
                                pid_lock_release(pid);
                                return pid;
                        }
                        pid_lock_acquire(pid);
                }
        }
        return INVALID_PID;
}


