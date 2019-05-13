#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/tcp.h>

#include "fastecho.h"

#define BUF_SIZE 4096

struct work_struct_data {
    struct work_struct work;
    struct socket *sock;
};

static int get_request(struct socket *sock, unsigned char *buf, size_t size)
{
    struct msghdr msg;
    struct kvec vec;
    int length;

    /* kvec setting */
    vec.iov_len = size;
    vec.iov_base = buf;

    /* msghdr setting */
    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    printk(MODULE_NAME ": start get response\n");
    /* get msg */
    length = kernel_recvmsg(sock, &msg, &vec, size, size, msg.msg_flags);
    buf[length] = '\0';
    printk(MODULE_NAME ": get request = %s\n", buf);

    return length;
}

static int send_request(struct socket *sock, unsigned char *buf, size_t size)
{
    int length;
    struct kvec vec;
    struct msghdr msg;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    vec.iov_base = buf;
    vec.iov_len = strlen(buf);

    printk(MODULE_NAME ": start send request.\n");

    length = kernel_sendmsg(sock, &msg, &vec, 1, strlen(buf) - 1);

    printk(MODULE_NAME ": send request = %s\n", buf);

    return length;
}

static void echo_server_worker(struct work_struct *work)
{
    struct work_struct_data *wsdata = (struct work_struct_data *) work;
    struct socket *sock;
    unsigned char *buf;
    int res;

    sock = wsdata->sock;
    allow_signal(SIGKILL);
    allow_signal(SIGTERM);

    buf = kmalloc(BUF_SIZE, GFP_KERNEL);
    if (!buf) {
        printk(KERN_ERR MODULE_NAME ": kmalloc error....\n");
        return;
    }

    while (!kthread_should_stop()) {
        res = get_request(sock, buf, BUF_SIZE - 1);
        if (res <= 0) {
            if (res) {
                printk(KERN_ERR MODULE_NAME ": get request error = %d\n", res);
            }
            break;
        }

        res = send_request(sock, buf, strlen(buf));
        if (res < 0) {
            printk(KERN_ERR MODULE_NAME ": send request error = %d\n", res);
            break;
        }
    }


    kernel_sock_shutdown(sock, SHUT_RDWR);
    sock_release(sock);
    kfree(buf);

    return;
}

int echo_server_daemon(void *arg)
{
    struct echo_server_param *param = arg;
    struct socket *sock;
    // struct task_struct *thread;
    int error;

    allow_signal(SIGKILL);
    allow_signal(SIGTERM);

    while (!kthread_should_stop()) {
        /* using blocking I/O */
        error = kernel_accept(param->listen_sock, &sock, 0);
        if (error < 0) {
            if (signal_pending(current))
                break;
            printk(KERN_ERR MODULE_NAME ": socket accept error = %d\n", error);
            continue;
        }

        /* start server worker */
        /*
                thread = kthread_run(echo_server_worker, sock, MODULE_NAME);
                if (IS_ERR(thread)) {
                    printk(KERN_ERR MODULE_NAME ": create worker thread error =
           %d\n",
                           error);
                    continue;
                }
        */
        struct work_struct_data *wdata;
        wdata = kmalloc(sizeof(struct work_struct_data), GFP_KERNEL);
        INIT_WORK(&wdata->work, echo_server_worker);
        wdata->sock = sock;
        schedule_work(&wdata->work);
    }

    return 0;
}
