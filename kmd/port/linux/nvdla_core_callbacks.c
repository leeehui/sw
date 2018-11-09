/*
 * Copyright (c) 2017-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdarg.h>

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/uaccess.h>

#include <nvdla_interface.h>
#include <nvdla_linux.h>
#include <nvdla_ioctl.h>

static int min_index;
static int max_index;
static int mem_dump;
static int file_dump;
static int dump_filter_on;

/* mem index excluded from dump */
#define FILTER_NUM 3
static int dump_filters[FILTER_NUM] = {10, 12, 15};

static int do_dump_filter(int32_t index)
{
    int i;
    /* check filters if filter switch is on */
    if (dump_filter_on) {
        for (i=0; i<FILTER_NUM; i++) {
            /* find one matched, just return */
            if (dump_filters[i] == index)
                return 0;
        }
        return 1;
    }

    /* dump if filter if off */
    else {
        return 1;
    }
}

module_param(min_index, int, S_IRUSR);
MODULE_PARM_DESC(min_index, "the minimum memory index to dump");
module_param(max_index, int, S_IRUSR);
MODULE_PARM_DESC(max_index, "the maximum memory index to dump");
module_param(mem_dump, int, S_IRUSR);
MODULE_PARM_DESC(mem_dump, "dump mem or not");
module_param(file_dump, int, S_IRUSR);
MODULE_PARM_DESC(file_dump, "dump mem to file or not");
module_param(dump_filter_on, int, S_IRUSR);
MODULE_PARM_DESC(dump_filter_on, "is dump_filter_on");

static struct nvdla_config nvdla_config_os_initial = {
	.atom_size = 32,
	.bdma_enable = true,
	.rubik_enable = true,
	.weight_compress_support = true,
};

static struct nvdla_config nvdla_config_small = {
	.atom_size = 8,
	.bdma_enable = false,
	.rubik_enable = false,
	.weight_compress_support = false,
};

void dla_debug(const char *str, ...)
{
//	va_list args;
//	va_start(args, str);
//	vprintk(pr_fmt(str), args);
//	va_end(args);
}

void dla_info(const char *str, ...)
{
	va_list args;
	va_start(args, str);
	vprintk(str, args);
	va_end(args);
}

void dla_warn(const char *str, ...)
{
	va_list args;
	va_start(args, str);
	vprintk(str, args);
	va_end(args);
}

void dla_error(const char *str, ...)
{
	va_list args;
	va_start(args, str);
	vprintk(str, args);
	va_end(args);
}

void *dla_memset(void *src, int ch, uint64_t len)
{
	return memset(src, ch, len);
}

void *dla_memcpy(void *dest, const void *src, uint64_t len)
{
	return memcpy(dest, src, len);
}

int64_t dla_get_time_us(void)
{
	return ktime_get_ns() / NSEC_PER_USEC;
}

void dla_reg_write(void *driver_context, uint32_t addr, uint32_t reg)
{
	struct nvdla_device *nvdla_dev =
			(struct nvdla_device *)driver_context;

	if (!nvdla_dev)
		return;

    dla_debug("dla_reg_write: addr(0x%x, 0x%x), wval(0x%x)\n", addr, addr>>2, reg);
	writel(reg, nvdla_dev->base + addr);
}

uint32_t dla_reg_read(void *driver_context, uint32_t addr)
{
    uint32_t val;
	struct nvdla_device *nvdla_dev =
			(struct nvdla_device *)driver_context;

	if (!nvdla_dev)
		return 0;

    val = readl(nvdla_dev->base + addr);
    dla_debug("dla_reg_read: addr(0x%x, 0x%x), rval(0x%x)\n", addr, addr>>2, val);
	return val;
}

static irqreturn_t nvdla_engine_isr(int32_t irq, void *data)
{
	unsigned long flags;
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)data;

	if (!nvdla_dev)
		return IRQ_NONE;

	spin_lock_irqsave(&nvdla_dev->nvdla_lock, flags);
	dla_isr_handler(nvdla_dev->engine_context);
	complete(&nvdla_dev->event_notifier);
	spin_unlock_irqrestore(&nvdla_dev->nvdla_lock, flags);

	return IRQ_HANDLED;
}



static void dump_to_file(int32_t index, void *data, uint32_t size)
{
    struct file *fp;
    mm_segment_t fs;
    loff_t pos;

    uint8_t file_name[50];

    memset(file_name, 0, 50);

    sprintf(file_name, "./mem-log/nvdla-mem-%d-raw.log", index);

    fp = filp_open(file_name, O_RDWR|O_CREAT, 0644);
    if (IS_ERR(fp)) {
        printk("create file error.\n");
        return;
    }

    fs = get_fs();
    set_fs(KERNEL_DS);

    pos = 0;
    vfs_write(fp,  data, size, &pos);

    vfs_fsync(fp, 0);

    set_fs(fs);

    filp_close(fp, NULL);
    return;
}

int32_t dla_print_dma_buf_handle(int32_t index, uint32_t handle,
        uint32_t size, uint64_t offset)
{
	int32_t ret;
	void *ptr = NULL;
	struct dma_buf *buf;

	buf = dma_buf_get(handle);
	if (IS_ERR(buf)) {
		pr_err("%s: Failed get dma_buf for handle=%d\n", __func__,
						handle);
		return -EFAULT;
	}

	ret = dma_buf_begin_cpu_access(buf, DMA_BIDIRECTIONAL);
	if (ret)
		goto put_dma_buf;


	ptr = dma_buf_vmap(buf);
	if (!ptr) {
		pr_err("%s: Failed to vmap dma_buf for handle=%d\n", __func__,
						handle);
		ret = -ENOMEM;
		goto end_cpu_access;
	}

    if (file_dump) {
        dump_to_file(index, (void *)(((uint8_t *)ptr) + offset), size);
    }

    if (mem_dump) {
        print_hex_dump(KERN_DEBUG, NULL, DUMP_PREFIX_NONE, 32, 1, (void *)(((uint8_t *)ptr) + offset), size, false);
    }

	dma_buf_vunmap(buf, ptr);

end_cpu_access:
	dma_buf_end_cpu_access(buf, DMA_BIDIRECTIONAL);

put_dma_buf:
	dma_buf_put(buf);

	return ret;
}

void dla_print_handle_by_index(void *driver_context, void *task_data, int32_t index)
{
	struct nvdla_task *task = (struct nvdla_task *)task_data;
	struct nvdla_mem_handle *handles;
	dma_addr_t phys_addr;
	struct nvdla_device *nvdla_dev =
			(struct nvdla_device *)driver_context;
    int32_t ret = 0;
    uint32_t size;

	handles = (struct nvdla_mem_handle *)task->address_list;

    /* TODO: check index value */

    ret = nvdla_gem_dma_addr_and_size(nvdla_dev->drm, task->file,
            handles[index].handle,
            &phys_addr, &size);
    if (ret) {
        printk("index: %d, handle: 0x%x error! ret:%d\n", index, handles[index].handle, ret);
    }
    else {
        dla_debug("index: %d, handle: 0x%x(%d), paddr: %p, size: 0x%x, offset: 0x%llx \n", 
                index, handles[index].handle, handles[index].handle, (void *)phys_addr, size, handles[index].offset); 
        printk("dump index:%d \n", index);
        dla_print_dma_buf_handle(index, handles[index].handle, size, 0);
    }
}

static int compare(const void *l, const void *r) 
{

    uint32_t lvalue = ((const struct nvdla_mem_handle_with_index *)l)->blob.handle;
    uint32_t rvalue = ((const struct nvdla_mem_handle_with_index *)r)->blob.handle;

    if (lvalue < rvalue) return -1;
    if (rvalue < lvalue) return 1;

    return 0;
}

static void stat_size_info(dma_addr_t phys_addr, uint32_t size, dma_addr_t *max_phys_addr, 
                            uint32_t *max_size, uint32_t *sum_size)
{
    if ((uint64_t)*max_phys_addr < (uint64_t)phys_addr) 
        *max_phys_addr = phys_addr;
    if (*max_size < size)
        *max_size = size;

    *sum_size += size;
}

void dla_print_all_mem_handle(void *driver_context, void *task_data)
{
	struct nvdla_task *task = (struct nvdla_task *)task_data;
	struct nvdla_mem_handle_with_index *handles;
	dma_addr_t phys_addr = 0;
	dma_addr_t max_phys_addr = 0;
	struct nvdla_device *nvdla_dev =
			(struct nvdla_device *)driver_context;
    int32_t ret = 0;
    int32_t index = 0;
    uint32_t size = 0;
    uint32_t max_size = 0;
    uint32_t sum_size = 0;

    uint32_t handle_tmp;
    uint32_t handle_current;
    uint32_t size_tmp;
    int32_t index_dump;

    handles = vmalloc(task->num_addresses * sizeof(struct nvdla_mem_handle_with_index));
    if (!handles) {
		printk("Failed to alloc kernel memory \n");
		ret = -ENOMEM;
		return;
    }

    for (index=0; index<task->num_addresses; index++) {
        handles[index].blob = task->address_list[index];
        handles[index].original_index = index;
    }

    sort(handles, task->num_addresses, sizeof(struct nvdla_mem_handle_with_index), &compare, NULL);

    /* get first handle info */
    ret = nvdla_gem_dma_addr_and_size(nvdla_dev->drm, task->file,
            handles[0].blob.handle,
            &phys_addr, &size);
    if (ret) {
        printk("index: %d, handle: 0x%x error! ret:%d\n", index, handles[index].blob.handle, ret);
        return;
    }
    handle_tmp = handles[0].blob.handle;
    size_tmp = size;

    stat_size_info(phys_addr, size, &max_phys_addr, &max_size, &sum_size);

    for (index=0; index<task->num_addresses; index++) {

        handle_current = handles[index].blob.handle;
        ret = nvdla_gem_dma_addr_and_size(nvdla_dev->drm, task->file,
                handle_current,
                &phys_addr, &size);
        if (ret) {
            printk("index: %d, handle: 0x%x error! ret:%d\n", index, handle_current, ret);
            return;
        }

        /* print is deferred when a different handle appears */
        if (handle_tmp != handle_current) {

            index_dump =  handles[index-1].original_index;

            if (index_dump >= min_index && index_dump <= max_index) {

                /* check filters */
                if (do_dump_filter(index_dump)) {
                    printk("dump index:%d \n", index_dump);
                    dla_print_dma_buf_handle(index_dump, handle_tmp, size_tmp, 0);
                }
            }

            stat_size_info(phys_addr, size, &max_phys_addr, &max_size, &sum_size);
        }

        /* print index info after the dump operation */
        printk("index: %d, handle: 0x%x(%d), paddr: %p, size: 0x%x, offset: 0x%llx \n", 
                handles[index].original_index, handle_current, handle_current, 
                (void *)phys_addr, size, handles[index].blob.offset); 

        handle_tmp = handle_current;
        size_tmp = size;
    }

    /* the last one is dumped unconditionally */
    index_dump =  handles[index-1].original_index;
    if (index_dump >= min_index && index_dump <= max_index) {
        printk("dump index:%d \n", index_dump);
        dla_print_dma_buf_handle(index_dump, handle_tmp, size_tmp, 0);
    }

    stat_size_info(phys_addr, size, &max_phys_addr, &max_size, &sum_size);

    printk("max_phys_addr: 0x%llx, max_size: 0x%x, sum_size: 0x%x \n",max_phys_addr, max_size, sum_size);

    vfree(handles);
}

void dla_print_address_list(void *driver_context, void *task_data)
{
	struct nvdla_task *task = (struct nvdla_task *)task_data;
	struct nvdla_mem_handle *handles;
	dma_addr_t phys_addr;
	struct nvdla_device *nvdla_dev =
			(struct nvdla_device *)driver_context;
    int32_t i = 0;
    int32_t ret = 0;
    uint32_t size;
	handles = (struct nvdla_mem_handle *)task->address_list;
    for (i=0; i<task->num_addresses; i++) {
        ret = nvdla_gem_dma_addr_and_size(nvdla_dev->drm, task->file,
					handles[i].handle,
					&phys_addr, &size);
        if (ret) {
            printk("index: %d, handle: 0x%x error!\n", i, handles[i].handle);
        }
        else {
            dla_debug("index: %d, handle: 0x%x(%d), paddr: %p, size: 0x%x, offset: 0x%llx \n", 
                    i, handles[i].handle, handles[i].handle, (void *)phys_addr, size, handles[i].offset); 
            if (i==1 || i==13 || i==55 ) {
                printk("dump index:%d \n", i);
                //dla_print_dma_buf_handle(handles[i].handle, size, 0);
            }
        }
    }
}

static int32_t dla_read_dma_address(void *driver_context, void *task_data,
						int16_t index, void *dst)
{
	int32_t ret = 0;
	struct nvdla_mem_handle *handles;
	dma_addr_t *phys_addr = (dma_addr_t *)(dst);
	struct nvdla_device *nvdla_dev =
			(struct nvdla_device *)driver_context;
	struct nvdla_task *task = (struct nvdla_task *)task_data;

	if (index == -1 || index > task->num_addresses)
		return -EINVAL;

	handles = (struct nvdla_mem_handle *)task->address_list;
	ret = nvdla_gem_dma_addr(nvdla_dev->drm, task->file,
					handles[index].handle,
					phys_addr);

	/* Add offset to IOVA address */
	*phys_addr = *phys_addr + handles[index].offset;

	return ret;
}

static int32_t dla_read_cpu_address(void *driver_context, void *task_data,
						int16_t index, void *dst)
{
	uint64_t *temp = (uint64_t *)dst;
	struct nvdla_task *task = (struct nvdla_task *)task_data;

	if (index == -1 || index > task->num_addresses)
		return -EINVAL;

	*temp = (uint64_t)index;
	return 0;
}

int32_t dla_get_dma_address(void *driver_context, void *task_data,
					int16_t index, void *dst_ptr,
					uint32_t destination)
{
	int32_t ret = 0;

	if (destination == DESTINATION_PROCESSOR) {
		ret = dla_read_cpu_address(driver_context, task_data,
						index, dst_ptr);
	} else if (destination == DESTINATION_DMA) {
		ret = dla_read_dma_address(driver_context, task_data,
						index, dst_ptr);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

int32_t dla_data_write(void *driver_context, void *task_data,
				void *src, uint64_t dst,
				uint32_t size, uint64_t offset)
{
	int32_t ret;
	void *ptr = NULL;
	struct dma_buf *buf;
	struct nvdla_mem_handle *handles;
	struct nvdla_task *task = (struct nvdla_task *)task_data;

	handles = task->address_list;
	buf = dma_buf_get(handles[dst].handle);
	if (IS_ERR(buf)) {
		pr_err("%s: Failed get dma_buf for handle=%d\n", __func__,
						handles[dst].handle);
		return -EFAULT;
	}

	ret = dma_buf_begin_cpu_access(buf, DMA_BIDIRECTIONAL);
	if (ret)
		goto put_dma_buf;

	ptr = dma_buf_vmap(buf);
	if (!ptr) {
		pr_err("%s: Failed to vmap dma_buf for handle=%d\n", __func__,
						handles[dst].handle);
		ret = -ENOMEM;
		goto end_cpu_access;
	}


	memcpy((void *)((uint8_t *)ptr + offset), src, size);

    dla_debug("dla_data_write: src(%p), dst(%p)\n", src, (void *)((uint8_t *)ptr + offset));

	dma_buf_vunmap(buf, ptr);

end_cpu_access:
	dma_buf_end_cpu_access(buf, DMA_BIDIRECTIONAL);

put_dma_buf:
	dma_buf_put(buf);

	return ret;
}

int32_t dla_data_read(void *driver_context, void *task_data,
				uint64_t src, void *dst,
				uint32_t size, uint64_t offset)
{
	int32_t ret;
	void *ptr = NULL;
	struct dma_buf *buf;
	struct nvdla_mem_handle *handles;
	struct nvdla_task *task = (struct nvdla_task *)task_data;

	handles = task->address_list;

	buf = dma_buf_get(handles[src].handle);
	if (IS_ERR(buf)) {
		pr_err("%s: Failed get dma_buf for handle=%d\n", __func__,
						handles[src].handle);
		return -EFAULT;
	}

	ret = dma_buf_begin_cpu_access(buf, DMA_BIDIRECTIONAL);
	if (ret)
		goto put_dma_buf;

	ptr = dma_buf_vmap(buf);
	if (!ptr) {
		pr_err("%s: Failed to vmap dma_buf for handle=%d\n", __func__,
						handles[src].handle);
		ret = -ENOMEM;
		goto end_cpu_access;
	}

	memcpy(dst, (void *)(((uint8_t *)ptr) + offset), size);

    dla_debug("dla_data_read: src(%p), dst(%p)\n", (void *)((uint8_t *)ptr + offset), dst);

	dma_buf_vunmap(buf, ptr);

end_cpu_access:
	dma_buf_end_cpu_access(buf, DMA_BIDIRECTIONAL);

put_dma_buf:
	dma_buf_put(buf);

	return ret;
}

extern uint32_t dump_flag;

int32_t nvdla_task_submit(struct nvdla_device *nvdla_dev, struct nvdla_task *task)
{
	int32_t err = 0;
	uint32_t task_complete = 0;

	nvdla_dev->task = task;


    dla_debug("Enter: lihui dla_execute_task\n"); 
	err = dla_execute_task(nvdla_dev->engine_context, (void *)task, nvdla_dev->config_data);
    dla_debug("Exit: lihui dla_execute_task\n"); 
	if (err) {
		pr_err("Task execution failed\n");
		return err;
	}

	pr_debug("Wait for task complete\n");

	while (1) {
		unsigned long flags;

        dla_debug("Enter: wait_for_completion\n"); 
		wait_for_completion(&nvdla_dev->event_notifier);
        dla_debug("Exit: wait_for_completion\n"); 

        if (dump_flag) {
            dump_flag = 0;//stop print next time
            //dla_print_handle_by_index(nvdla_dev, task, 17);           
            //dla_print_handle_by_index(nvdla_dev, task, 18);           
            //dla_print_all_mem_handle(nvdla_dev, task);
        }

		spin_lock_irqsave(&nvdla_dev->nvdla_lock, flags);

		err = dla_process_events(nvdla_dev->engine_context, &task_complete);

		spin_unlock_irqrestore(&nvdla_dev->nvdla_lock, flags);

		if (err || task_complete) {
            dla_print_all_mem_handle(nvdla_dev, task);
			break;
        }
	}

	pr_debug("Task complete\n");
	dla_clear_task(nvdla_dev->engine_context);

	return err;
}

/* driver probe and init */
static const struct of_device_id nvdla_of_match[] = {
	{
		.compatible = "nvidia,nvdla_os_initial",
		.data = &nvdla_config_os_initial,
	},
	{
		.compatible = "nvidia,nvdla_2",
		.data = &nvdla_config_small,
	},
	{ },
};

static int32_t nvdla_probe(struct platform_device *pdev)
{
	int32_t err = 0;
	struct resource *res;
	struct nvdla_device *nvdla_dev;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;

	if (!pdev->dev.of_node)
		return -EINVAL;

	match = of_match_device(nvdla_of_match, &pdev->dev);
	if (!match) {
		pr_err("Missing DT entry!\n");
		return -EINVAL;
	}

	nvdla_dev = devm_kzalloc(dev, sizeof(*nvdla_dev), GFP_KERNEL);
	if (!nvdla_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, nvdla_dev);
	nvdla_dev->pdev = pdev;
	nvdla_dev->config_data = (struct nvdla_config *)match->data;

	init_completion(&nvdla_dev->event_notifier);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nvdla_dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nvdla_dev->base))
		return PTR_ERR(nvdla_dev->base);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "no irq resource\n");
		return -EINVAL;
	}
	nvdla_dev->irq = res->start;

	err = devm_request_irq(&pdev->dev, nvdla_dev->irq,
				nvdla_engine_isr, 0,
				dev_name(&pdev->dev), nvdla_dev);
	if (err)
		return err;

	dla_register_driver(&nvdla_dev->engine_context, (void *)nvdla_dev);
	dla_clear_task(nvdla_dev->engine_context);

	err = nvdla_drm_probe(nvdla_dev);
	if (err)
		dev_err(&pdev->dev, "failed to register drm device\n");

    printk(KERN_DEBUG "min_index: %d \n", min_index);
    printk(KERN_DEBUG "max_index: %d \n", max_index);
    printk(KERN_DEBUG "mem_dump: %d \n", mem_dump);
    printk(KERN_DEBUG "file_dump: %d \n", file_dump);
    printk(KERN_DEBUG "dump_filter_on: %d \n", dump_filter_on);

	return err;
}

static int32_t __exit nvdla_remove(struct platform_device *pdev)
{
	struct nvdla_device *nvdla_dev = dev_get_drvdata(&pdev->dev);

	nvdla_drm_remove(nvdla_dev);

	return 0;
}

static struct platform_driver nvdla_driver = {
	.probe = nvdla_probe,
	.remove = __exit_p(nvdla_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = "NVDLA",
		.of_match_table = of_match_ptr(nvdla_of_match),
	},
};
module_platform_driver(nvdla_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("Nvidia Deep Learning Accelerator driver");
