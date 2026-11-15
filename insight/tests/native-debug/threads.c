/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Keep two named workers in nested frames with distinct locals/TLS and one shared global for GDB GUI tests.
 * RU: Удерживает два именованных потока во вложенных кадрах с разными локальными/TLS и общей переменной. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

static atomic_int ready, released;
_Thread_local int tls_value;
volatile int shared_value = 7;

/* EN: Keep id/outer and a frame-local value inspectable until main releases the workers.
 * RU: Сохраняет id/outer и локальный value доступными до освобождения потоков из main. */
__attribute__((noinline)) static void thread_checkpoint(int id, volatile int *outer)
{
    volatile int value = id * 100 + 22;
    atomic_fetch_add(&ready, 1);
    while (!atomic_load(&released))
        value = id * 100 + 22 + (*outer == -1);
    (void)value;
}

/* EN: Retain a differently valued caller local for id while the checkpoint frame is active.
 * RU: Сохраняет локальный value вызывающего кадра для id, пока активен кадр checkpoint. */
__attribute__((noinline)) static int worker_leaf(int id)
{
    volatile int value = id * 100 + 11;
    thread_checkpoint(id, &value);
    return value;
}

/* EN: Name worker arg, initialize its TLS and retain another local with the same spelling in its entry frame.
 * RU: Именует поток arg, задаёт его TLS и сохраняет одноимённую локальную переменную в кадре входа. */
static void *worker_entry(void *arg)
{
    int id = (int)(intptr_t)arg;
    pthread_setname_np(pthread_self(), id == 1 ? "worker-one" : "worker-two");
    tls_value = id * 1000 + 7;
    volatile int value = id * 100 + 1;
    worker_leaf(id);
    return (void *)(intptr_t)value;
}

/* EN: Give GDB a stable main-thread breakpoint after both workers reached inspectable frames.
 * RU: Даёт GDB устойчивую точку главного потока после входа обоих работников в исследуемые кадры. */
__attribute__((noinline)) void capture_threads(void)
{
    atomic_signal_fence(memory_order_seq_cst);
}

/* EN: Start and join two workers; argc/argv are unused because GUI commands control the stopped context.
 * RU: Запускает и объединяет два потока; argc/argv не нужны, контекст остановки выбирается в интерфейсе. */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    pthread_t workers[2];
    if (pthread_create(&workers[0], NULL, worker_entry, (void *)(intptr_t)1) ||
        pthread_create(&workers[1], NULL, worker_entry, (void *)(intptr_t)2))
        return 1;
    while (atomic_load(&ready) != 2)
        atomic_signal_fence(memory_order_seq_cst);
    capture_threads();
    atomic_store(&released, 1);
    pthread_join(workers[0], NULL);
    pthread_join(workers[1], NULL);
    return 0;
}
