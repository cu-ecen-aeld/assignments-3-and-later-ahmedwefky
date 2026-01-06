/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/uaccess.h> // copy_{to,from}_user
#include <linux/slab.h> // krealloc, kfree
#include <linux/string.h> // memchr
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Ahmed Wefky");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    /* A pointer to the device structure */
    struct aesd_dev *dev;

    PDEBUG("open");

    /* Get the device structure from the inode */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    /* Provide access to device structure in other methods like read/write/release */
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

/* Read data from the circular buffer managed by the device driver */
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;

    /* A pointer to hold the device structure */
    struct aesd_dev *dev = filp->private_data;
    
    /* A pointer to hold the buffer entry */
    struct aesd_buffer_entry *entry;
    
    /* Byte offset within the entry */
    size_t entry_offset = 0;
    
    size_t bytes_to_read = 0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    /* Lock the device mutex preventing race conditions if a write operation tries to access the buffer while reading */
    if (mutex_lock_interruptible(&dev->lock))
    {
        /* Return error if the lock acquisition is interrupted by a signal */
        return -ERESTARTSYS;
    }

    /* Find the entry in the circular buffer based on the linear file position */
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if (entry)
    {
        /* The driver reads up to the end of a single entry per read call or the requested count whichever is smaller */
        /* Calculate how many bytes we can read from this entry */
        bytes_to_read = entry->size - entry_offset;

        /* Limit bytes_to_read if it exceeds the requested count */
        if (bytes_to_read > count)
        {
            bytes_to_read = count;
        }

        /* Copy data from the kernel buffer to user provided buffer */
        if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_read))
        {
            /* Return error if copy_to_user fails */
            retval = -EFAULT;
        }
        else
        {
            /* Update the file position for the next call */
            *f_pos += bytes_to_read;
            /* Return the number of bytes read */
            retval = bytes_to_read;
        }
    }

    mutex_unlock(&dev->lock);
    return retval;
}

/* Write data to the circular buffer managed by the device driver */
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;

    /* A pointer to hold the device structure */
    struct aesd_dev *dev = filp->private_data;

    /* A pointer to the reallocated buffer */
    const char *new_buffptr;

    /* A pointer to the entry to be freed if the circular buffer is full */
    const char *entry_to_free = NULL;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
    /* Lock the device mutex preventing race conditions if a read operation tries to access the buffer while writing */
    if (mutex_lock_interruptible(&dev->lock))
    {
        /* Return error if the lock acquisition is interrupted by a signal */
        return -ERESTARTSYS;
    }

    /* Reallocate memory for the add_entry buffer to accommodate the new data */
    new_buffptr = krealloc(dev->add_entry.buffptr, dev->add_entry.size + count, GFP_KERNEL);
    if (!new_buffptr)
    {
        /* Return error if krealloc fails */
        mutex_unlock(&dev->lock);
        return -ENOMEM;
    }

    /* Update the add_entry buffer pointer */
    dev->add_entry.buffptr = new_buffptr;

    /* Copy data from user provided buffer to the add_entry buffer */
    if (copy_from_user((char *)dev->add_entry.buffptr + dev->add_entry.size, buf, count))
    {
        /* Return error if copy_from_user fails */
        retval = -EFAULT;
        mutex_unlock(&dev->lock);
        return retval;
    }


    dev->add_entry.size += count;
    retval = count;

    /* Check if the written data contains a newline character */
    /* The driver buffers data until a newline character is received */
    if (memchr(dev->add_entry.buffptr + dev->add_entry.size - count, '\n', count))
    {
        /* If the circular buffer is full, free the oldest entry after adding the new one */
        if (dev->buffer.full)
        {
            entry_to_free = dev->buffer.entry[dev->buffer.in_offs].buffptr;
        }

        /* Add the new entry to the circular buffer */
        aesd_circular_buffer_add_entry(&dev->buffer, &dev->add_entry);

        /* If an entry is to be freed, free it */
        if (entry_to_free)
        {
            kfree(entry_to_free);
        }

        /* Reset the add_entry for the next write operation */
        dev->add_entry.buffptr = NULL;
        dev->add_entry.size = 0;
    }

    mutex_unlock(&dev->lock);
    return retval;
}

struct file_operations aesd_fops =
{
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /* Initialize the AESD device mutex */
    mutex_init(&aesd_device.lock);
    /* Initialize the AESD circular buffer */
    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result )
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    struct aesd_buffer_entry *entry;
    uint8_t index;

    cdev_del(&aesd_device.cdev);

    /* Free all allocated buffer entries in the circular buffer */
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index)
    {
        kfree(entry->buffptr);
    }

    /* Free the partial write buffer if a write started but was not completed */
    kfree(aesd_device.add_entry.buffptr);

    /* Destroy the device mutex */
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
