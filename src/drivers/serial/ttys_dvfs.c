/**
 * @file
 * @brief
 *
 * @author  Anton Kozlov
 * @date    09.08.2013
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <kernel/irq.h>
#include <mem/misc/pool.h>
#include <util/indexator.h>

#include <drivers/char_dev.h>
#include <drivers/serial/uart_device.h>
#include <fs/dvfs.h>

#define UART_MAX_N OPTION_GET(NUMBER,uart_max_n)

INDEX_DEF(serial_indexator, 0, UART_MAX_N);

static DLIST_DEFINE(uart_list);

static inline int uart_state_test(struct uart *uart, int mask) {
	return uart->state & mask;
}

static inline void uart_state_set(struct uart *uart, int mask) {
	uart->state |= mask;
}

static inline void uart_state_clear(struct uart *uart, int mask) {
	uart->state &= ~mask;
}

static int uart_fill_name(struct uart *dev) {

	dev->idx = index_alloc(&serial_indexator, INDEX_MIN);
	if(dev->idx < 0) {
		return -EBUSY;
	}

	snprintf(dev->dev_name, UART_NAME_MAXLEN, "ttyS%d", dev->idx);

	return 0;
}

static int uart_attach_irq(struct uart *uart) {

	if (!uart->params.irq) {
		return 0;
	}

	if (!uart->irq_handler) {
		return -EINVAL;
	}

	return irq_attach(uart->irq_num, uart->irq_handler, 0, uart,
			uart->dev_name);
}

static int uart_detach_irq(struct uart *uart) {

	if (uart->params.irq) {
		return irq_detach(uart->irq_num, uart);
	}

	return 0;
}

static int uart_setup(struct uart *uart) {
	const struct uart_ops *uops = uart->uart_ops;

	if (uops->uart_setup) {
		return uops->uart_setup(uart, &uart->params);
	}

	return 0;
}
POOL_DEF(cdev_serials_pool, struct device_module, 1);
static struct device_module *cdev_uart;

static int uart_fsop_open(struct inode *node, struct file *desc) {
	return 0;
}
static int uart_fsop_close(struct file *desc){
	return 0;
}

static size_t uart_fsop_read(struct file *desc, void *buf, size_t size) {
	struct uart * uart = cdev_uart->dev;
	int i;
	char *b = buf;

	for(i = 0; i < size; i ++) {
		while(!uart->uart_ops->uart_hasrx(uart)) {
		}
		b[i] = uart->uart_ops->uart_getc(uart);
	}

	return size;
}

static size_t uart_fsop_write(struct file *desc, void *buf, size_t size) {
	struct uart * uart = cdev_uart->dev;
	int i;
	char *b = buf;
	for(i = 0; i < size; i ++) {
		uart->uart_ops->uart_putc(uart, b[i]);
	}
	return 0;
}

static const struct file_operations uart_fops = {
	.open = uart_fsop_open,
	.close = uart_fsop_close,
	.read = uart_fsop_read,
	.write = uart_fsop_write
};

int uart_register(struct uart *uart,
		const struct uart_params *uart_defparams) {
	struct device_module *cdev;

	cdev = pool_alloc(&cdev_serials_pool);
	if (!cdev)
		return -ENOMEM;

	cdev_uart = cdev;

	if (uart_fill_name(uart)) {
		return -EBUSY;
	}

	dlist_head_init(&uart->lnk);

	if (uart_defparams) {
		memcpy(&uart->params, uart_defparams, sizeof(struct uart_params));
	} else {
		memset(&uart->params, 0, sizeof(struct uart_params));
	}

	dlist_add_next(&uart->lnk, &uart_list);

	memset(cdev, 0, sizeof(*cdev));
	cdev->name = uart->dev_name;
	cdev->fops = &uart_fops;
	cdev->dev = uart;

	char_dev_register(cdev);

	return 0;
}

void uart_deregister(struct uart *uart) {

	dlist_del(&uart->lnk);

	index_free(&serial_indexator, uart->idx);
}

struct uart *uart_dev_lookup(const char *name) {
	struct uart *uart;

	dlist_foreach_entry(uart, &uart_list, lnk) {
		if (!name || !strcmp(name, uart->dev_name)) {
			return uart;
		}
	}

	return NULL;
}

int uart_open(struct uart *uart) {
	if (uart_state_test(uart, UART_STATE_OPEN)) {
		return -EINVAL;
	}

	uart_state_set(uart, UART_STATE_OPEN);

	uart_setup(uart);

	return uart_attach_irq(uart);
}

int uart_close(struct uart *uart) {
	if (!uart_state_test(uart, UART_STATE_OPEN)) {
		return -EINVAL;
	}

	uart_state_clear(uart, UART_STATE_OPEN);

	return uart_detach_irq(uart);
}

int uart_set_params(struct uart *uart, const struct uart_params *params) {

	if (uart_state_test(uart, UART_STATE_OPEN)) {
		uart_detach_irq(uart);
	}

	memcpy(&uart->params, params, sizeof(struct uart_params));

	if (uart_state_test(uart, UART_STATE_OPEN)) {
		uart_attach_irq(uart);
		uart_setup(uart);
	}

	return 0;
}

int uart_get_params(struct uart *uart, struct uart_params *params) {

	memcpy(params, &uart->params, sizeof(struct uart_params));

	return 0;
}

