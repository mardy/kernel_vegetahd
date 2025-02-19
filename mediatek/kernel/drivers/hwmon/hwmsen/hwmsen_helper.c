/* alps/ALPS_SW/TRUNK/MAIN/alps/kernel/drivers/hwmon/mt6516/hwmsen_helper.c
 *
 * (C) Copyright 2009 
 * MediaTek <www.MediaTek.com>
 *
 * Sensor helper
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <generated/autoconf.h>
#include <linux/platform_device.h>
#include <linux/hwmon-sysfs.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <linux/ctype.h>

#include <linux/hwmsensor.h>
#include <linux/hwmsen_helper.h>


/*----------------------------------------------------------------------------*/
#define hex2int(c) ( (c >= '0') && (c <= '9') ? (c - '0') : ((c & 0xf) + 9) )
/*----------------------------------------------------------------------------*/
#define C_MAX_REG_LEN (4)
/*----------------------------------------------------------------------------*/
//#define HWMSEN_DEBUG
/******************************************************************************
 * Functions 
******************************************************************************/
int hwmsen_set_bits(struct i2c_client *client, u8 addr, u8 bits)
{
    int err;
    u8 cur, nxt;

    if ((err = hwmsen_read_byte(client, addr, &cur))) {
        HWM_ERR("read err: 0x%02X\n", addr);
        return -EFAULT;
    }
    
    nxt = (cur | bits);

    if (nxt ^ cur) {
        if ((err = hwmsen_write_byte(client, addr, nxt))) {
            HWM_ERR("write err: 0x%02X\n", addr);
            return -EFAULT;
        }
    }
    return 0;    
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_set_bits);
/*----------------------------------------------------------------------------*/
int hwmsen_clr_bits(struct i2c_client *client, u8 addr, u8 bits)
{
    int err;
    u8 cur, nxt;

    if ((err = hwmsen_read_byte(client, addr, &cur))) {
        HWM_ERR("read err: 0x%02X\n", addr);
        return -EFAULT;
    }
    
    nxt = cur & (~bits);
    
    if (nxt ^ cur) {
        if ((err = hwmsen_write_byte(client, addr, nxt))) {
            HWM_ERR("write err: 0x%02X\n", addr);
            return -EFAULT;
        }
    }
    return 0;    
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_clr_bits);
/*----------------------------------------------------------------------------*/
int hwmsen_read_byte(struct i2c_client *client, u8 addr, u8 *data)
{
    	u8 beg = addr;
	int err;
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,	.flags = 0,
			.len = 1,	.buf = &beg
		},
		{
			.addr = client->addr,	.flags = I2C_M_RD,
			.len = 1,	.buf = data,
		}
	};

	if (!client)
	return -EINVAL;
	err = i2c_transfer(client->adapter, msgs, sizeof(msgs)/sizeof(msgs[0]));
	if (err != 2) {
		HWM_ERR("i2c_transfer error: (%d %p) %d\n",
			addr, data, err);
		err = -EIO;
	} else {
		err = 0;
	}
    return err;
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_read_byte);
/*----------------------------------------------------------------------------*/
int hwmsen_write_byte(struct i2c_client *client, u8 addr, u8 data)
{
    u8 buf[] = {addr, data};
    int ret = 0;

    ret = i2c_master_send(client, (const char*)buf, sizeof(buf));
    if (ret < 0) {
        HWM_ERR("send command error!!\n");
        return -EFAULT;
    } else {
#if defined(HWMSEN_DEBUG)    
        HWM_LOG("%s(0x%02X)= %02X\n", __func__, addr, data);
#endif
    }
    return 0;
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_write_byte);
/*----------------------------------------------------------------------------*/
int hwmsen_read_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
    if (len == 1) {
        return hwmsen_read_byte(client, addr, data);
    } else {
        u8 beg = addr; 
        struct i2c_msg msgs[2] = {
            {
                .addr = client->addr,    .flags = 0,
                .len = 1,                .buf= &beg
            },
            {
                .addr = client->addr,    .flags = I2C_M_RD,
                .len = len,             .buf = data,
            }
        };
        int err;

        if (!client)
            return -EINVAL;
        else if (len > C_I2C_FIFO_SIZE) {        
            HWM_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
            return -EINVAL;
        }

        err = i2c_transfer(client->adapter, msgs, sizeof(msgs)/sizeof(msgs[0]));
        if (err != 2) {
            HWM_ERR("i2c_transfer error: (%d %p %d) %d\n", addr, data, len, err);
            err = -EIO;
        } else {
#if defined(HWMSEN_DEBUG)        
            static char buf[128];
            int idx, buflen = 0;
            for (idx = 0; idx < len; idx++)
                buflen += snprintf(buf+buflen, sizeof(buf)-buflen, "%02X ", data[idx]);
            HWM_LOG("%s(0x%02X,%2d) = %s\n", __func__, addr, len, buf);
#endif             
            err = 0;    /*no error*/
        }
        return err;
    }

}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_read_block);
/*----------------------------------------------------------------------------*/
int hwmsen_write_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{   /*because address also occupies one byte, the maximum length for write is 7 bytes*/
    int err, idx, num;
    char buf[C_I2C_FIFO_SIZE];

    if (!client)
        return -EINVAL;
    else if (len >= C_I2C_FIFO_SIZE) {        
        HWM_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
        return -EINVAL;
    }    

    num = 0;
    buf[num++] = addr;
    for (idx = 0; idx < len; idx++)
        buf[num++] = data[idx];

    err = i2c_master_send(client, buf, num);
    if (err < 0) {
        HWM_ERR("send command error!!\n");
        return -EFAULT;
    } else {
#if defined(HWMSEN_DEBUG)    
        static char buf[128];
        int idx, buflen = 0;
        for (idx = 0; idx < len; idx++)
            buflen += snprintf(buf+buflen, sizeof(buf)-buflen, "%02X ", data[idx]);
        HWM_LOG("%s(0x%02X,%2d)= %s\n", __func__, addr, len, buf);    
#endif        
        err = 0;    /*no error*/
    }
    return err;
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_write_block);
/*----------------------------------------------------------------------------*/
static int hwmsen_chrs_to_integer(u8 dat[C_MAX_REG_LEN], int datlen)
{
    int idx;
    u32 val = 0;
    for (idx = 0; idx < datlen; idx++)
        val += (dat[idx] << (8*idx));
    return val;
}
/*----------------------------------------------------------------------------*/
static int hwmsen_byte_to_integer(u8 dat, int datlen)
{
    int idx;
    u32 val = 0;
    for (idx = 0; idx < datlen; idx++)
        val += (dat << (8*idx));
    return val;
}
/*----------------------------------------------------------------------------*/
void hwmsen_single_rw(struct i2c_client *client, 
                      struct hwmsen_reg *regs, int reg_num) 
{
    int idx, pos, num, err = 0, cmp1, cmp2;
    u8  pattern[] = {0x55, 0xAA};
    u8  old[C_MAX_REG_LEN], val[C_MAX_REG_LEN], mask;
    struct hwmsen_reg *ptr;
    
    for (idx = 0; idx < reg_num; idx++) {
        if (regs[idx].mode != REG_RW) 
            continue;
        if (regs[idx].len > C_MAX_REG_LEN) {
            HWM_ERR("exceed maximum length\n");
            continue;
        }
        ptr = &regs[idx];
        
        if ((err = hwmsen_read_block(client, ptr->addr, old, ptr->len))) {
            HWM_ERR("read %s fails\n", ptr->name);
            continue;
        }
        for (pos = 0; pos < sizeof(pattern)/sizeof(pattern[0]); pos++) { 
            mask = ptr->mask;
            for (num = 0; num < ptr->len; num++)
                val[num] = pattern[pos] & mask;
            if ((err = hwmsen_write_block(client, ptr->addr, val, ptr->len))) {
                HWM_ERR("test: write %s fails, pat[0x%02X]\n", ptr->name, pattern[pos]);
                continue;
            }
            if ((err = hwmsen_read_block(client, ptr->addr, val, ptr->len))) {
                HWM_ERR("test: read %s fails\n", ptr->name);
                continue;
            }

            cmp1 = hwmsen_chrs_to_integer(val, ptr->len);
            cmp2 = hwmsen_byte_to_integer(pattern[pos], ptr->len);
            if ((cmp1 ^ cmp2) & ptr->mask) 
                HWM_ERR("test: reg %s[%d] 0x%08X <-> 0x%08X\n", ptr->name, num, cmp1, cmp2);
        }
        if ((err = hwmsen_write_block(client, ptr->addr, old, ptr->len))) {
            HWM_ERR("restore: write %s \n", ptr->name);
            continue;
        }
        if ((err = hwmsen_read_block(client, ptr->addr, val, ptr->len))) {
            HWM_ERR("restore: read %s \n", ptr->name);
            continue;
        }

        cmp1 = hwmsen_chrs_to_integer(val, ptr->len);
        cmp2 = hwmsen_chrs_to_integer(old, ptr->len);
        if ((cmp1 ^ cmp2) & ptr->mask) {
            HWM_ERR("restore %s fails\n", ptr->name);
            err = -EFAULT;
        }
    }
    if (!err) 
        HWM_VER("hwmsen_single_rw pass!! \n");
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_single_rw);
/*----------------------------------------------------------------------------*/
void hwmsen_multi_rw(struct i2c_client *client, find_reg_t findreg, 
                     struct hwmsen_reg_test_multi *items, int inum) 
{
    u8 pattern[] = {0x55, 0xAA};
    u8 buf[C_I2C_FIFO_SIZE], dat[C_I2C_FIFO_SIZE], old[C_I2C_FIFO_SIZE];
    int pos, idx, p, err = 0;

    for (idx = 0; idx < inum; idx++) {        
        u8 addr = items[idx].addr;
        u8 len = items[idx].len;
        u8 mode = items[idx].mode; 
        for (pos = 0; pos < sizeof(pattern)/sizeof(pattern[0]); pos++) {        
            if ((err = hwmsen_read_block(client, addr, old, len))) {
                HWM_ERR("(%d) save block fail!!\n", idx);
                continue;
            }
            if (!(mode & REG_WO))
                continue;
               
            memset(buf, pattern[pos], sizeof(buf));
            for (p = 0; p < len; p++) 
                buf[p] = buf[p] & findreg(addr+p)->mask;
            if ((err = hwmsen_write_block(client, addr, buf, len))) {
                HWM_ERR("(%d) block write fail!!\n", idx);
                continue;
            }
            memset(dat, 0x00, sizeof(dat));
            if ((err = hwmsen_read_block(client, addr, dat, len))) {
                HWM_ERR("(%d) block read fail!!\n", idx);
                continue;
            }
            if ((err = hwmsen_write_block(client, addr, old, len))) {
                HWM_ERR("(%d) restore block fail!!\n", idx);
                continue;
            }
                
            if (memcmp(buf, dat, len)) {
                HWM_ERR("(%d) data compare fail!!\n", idx);
                printk("    buf: ");
                for (p = 0; p < len; p++)
                    printk("%02X ", buf[p]);
                printk("\n");
                printk("    dat: ");
                for (p = 0; p < len; p++)
                    printk("%02X ", dat[p]);
                printk("\n");
                err = 1;
            }
        }
    }
    if (!err)
        HWM_DBG("%s success : %d cases\n", __func__, inum);
    else
        HWM_DBG("%s: %d cases\n", __func__, inum);
        
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_multi_rw);
/*----------------------------------------------------------------------------*/
ssize_t hwmsen_show_dump(struct i2c_client *client, 
                         u8 startAddr, u8 *regtbl, u32 regnum, 
                         find_reg_t findreg, char *buf, u32 buflen)
{
    int err = 0;
    u8 addr = startAddr; 
    u8 max_len = 8, read_len;
    const char* name = NULL;
    

    if (!client)
        return -EINVAL;

    memset(regtbl, 0x00, regnum);
    
    do{
        read_len = ((regnum - addr) > max_len) ? (max_len) : (regnum - addr);
        if (!read_len)
            break;
        
        err = hwmsen_read_block(client, addr, &regtbl[addr-startAddr], read_len);
        if (!err)
            addr += read_len;
        //SEN_VER("read block data: %d %d\n", addr, read_len);
    }while(!err);

    if (!err){        
        int idx;
        ssize_t len = 0;
        
        for (idx = startAddr; idx < regnum ; idx++) {
            if (NULL != (name = findreg(idx)->name))
                len += snprintf(buf+len, buflen-len, "%-16s = 0x%02X\n", name, regtbl[idx-startAddr]);
            //printk("add=> (%d)%s\n", strlen(sensor_reg_name[idx-REG_OFFSET]), sensor_reg_name[idx-REG_OFFSET]);
            //printk("end=> %d %p %d %s\n", len, ptr, left, buf);
        }
        return len;
    }
    return 0;
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_show_dump);
/*----------------------------------------------------------------------------*/
ssize_t hwmsen_read_all_regs(struct i2c_client *client, struct hwmsen_reg *regs,
                             u32 num, char *buf, u32 buflen)
{
    int err = 0, pos, idx, val;
    struct hwmsen_reg *ptr;
    u8 dat[4];
    ssize_t len = 0;
    

    if (!client)
        return -EINVAL;

    for (idx = 0; idx < num; idx++) {
        ptr = &regs[idx];
        memset(dat, 0x00, sizeof(dat));
        if (ptr->len > sizeof(dat)) {
            HWM_ERR("exceeds capacity, %d\n", ptr->len);
            break;
        } else if ((err = hwmsen_read_block(client, ptr->addr, dat, ptr->len))) {
            HWM_ERR("read reg %s (0x%2X) fail!!\n", ptr->name, ptr->addr);
            break;
        }
        val = 0;
        for (pos = 0; pos < ptr->len; pos++)
            val += (dat[pos] << (8*pos));
        len += snprintf(buf+len, buflen-len, "%-16s = %8X\n", ptr->name, val);
    }    
    return (err) ? (0) : len;
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_read_all_regs);
/*----------------------------------------------------------------------------*/
ssize_t hwmsen_show_reg(struct i2c_client *client, u8 addr, char *buf, u32 buflen) 
{
    u8 data = 0;
    int err;

    if ((err = hwmsen_read_byte(client, addr, &data))) {
        HWM_ERR("reading address 0x%02X fail!!", addr);
        return 0;
    } else {
	    return snprintf(buf, buflen, "0x%02X\n", data);    
    }
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_show_reg);
/*----------------------------------------------------------------------------*/
ssize_t hwmsen_store_reg(struct i2c_client *client, u8 addr, const char* buf, size_t count) 
{   /*if the function doesn't return count, the funtion will be called again to consume buffer*/
	int err, val;
    u8 data;

    if (1 != sscanf(buf, "0x%x", &val)) {
        HWM_ERR("format not match: (0xAB) <-> '%s'\n", buf);
        //MSG_ERR("format not match: %d %x %x %x %x\n", q-p+1, p[0], p[1], p[2], p[3]);
        return count;
    }
    data  = (u8)val;

    if ((err = hwmsen_write_byte(client, addr, data))) 
        HWM_ERR("write address 0x%02X fail!!\n", addr);
    return count;
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_store_reg);
/*----------------------------------------------------------------------------*/
ssize_t hwmsen_show_byte(struct device *dev, struct device_attribute *attr, 
                         char* buf, u32 buflen) 
{
	int err, index = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
    u8 addr = (u8)index;
    u8 dat;

    if ((err = hwmsen_read_byte(client, addr, &dat))) {
        HWM_ERR("reading address 0x%02X fail!!", addr);
        return 0;
    } else {
	    return snprintf(buf, buflen, "0x%02X\n", dat);    
    }
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_show_byte);
/*----------------------------------------------------------------------------*/
ssize_t hwmsen_store_byte(struct device *dev, struct device_attribute *attr, 
						  const char* buf, size_t count)
{   /*if the function doesn't return count, the funtion will be called again to consume buffer*/
	int err, val;
	int index = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
    u8 addr = (u8)index;
    u8 dat;
    
    if (1 != sscanf(buf, "0x%x", &val)) {
        HWM_ERR("format not match: (0xAB) <-> '%s'\n", buf);
        //MSG_ERR("format not match: %d %x %x %x %x\n", q-p+1, p[0], p[1], p[2], p[3]);
        return count;
    }
    dat  = (u8)val;

    if ((err = hwmsen_write_byte(client, addr, dat))) 
        HWM_ERR("write address 0x%02X fail!!\n", addr);
    return count;
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_store_byte);
/*----------------------------------------------------------------------------*/
ssize_t hwmsen_show_word(struct device *dev, struct device_attribute *attr, 
                         char* buf, u32 buflen) 
{
	int err, index = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
    u8 addr = (u8)index;
    u8 dat[2];

    if ((err = hwmsen_read_block(client, addr, dat, sizeof(dat)))) {
        HWM_ERR("reading address 0x%02X fail!!", addr);
        return 0;
    } else {
	    return snprintf(buf, buflen, "0x%04X\n", (dat[0] | (dat[1] << 8)));    
    }
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_show_word);
/*----------------------------------------------------------------------------*/
ssize_t hwmsen_store_word(struct device *dev, struct device_attribute *attr, 
						  const char* buf, size_t count)
{   /*if the function doesn't return count, the funtion will be called again to consume buffer*/
	int err, val;
	int index = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
    u8 addr = (u8)index;
    u8 dat[2];
    
    if (1 != sscanf(buf, "0x%x", &val)) {
        HWM_ERR("format not match: (0xAB) <-> '%s'\n", buf);
        //MSG_ERR("format not match: %d %x %x %x %x\n", q-p+1, p[0], p[1], p[2], p[3]);
        return count;
    }
    dat[0] = (u8)((val & 0x00FF));
    dat[1] = (u8)((val & 0xFF00) >> 8);

    if ((err = hwmsen_write_block(client, addr, dat, sizeof(dat)))) 
        HWM_ERR("write address 0x%02X fail!!\n", addr);
    return count;
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_store_word);
/*----------------------------------------------------------------------------*/
struct hwmsen_convert map[] = {
    { { 1, 1, 1}, {0, 1, 2} },
    { {-1, 1, 1}, {1, 0, 2} },
    { {-1,-1, 1}, {0, 1, 2} },
    { { 1,-1, 1}, {1, 0, 2} },

    { {-1, 1,-1}, {0, 1, 2} },
    { { 1, 1,-1}, {1, 0, 2} },
    { { 1,-1,-1}, {0, 1, 2} },
    { {-1,-1,-1}, {1, 0, 2} },      

};
/*----------------------------------------------------------------------------*/
int hwmsen_get_convert(int direction, struct hwmsen_convert *cvt) 
{
    if (!cvt)
        return -EINVAL;
    else if (direction >= sizeof(map)/sizeof(map[0]))
        return -EINVAL;

    *cvt = map[direction];
    return 0;
}
/*----------------------------------------------------------------------------*/
EXPORT_SYMBOL_GPL(hwmsen_get_convert); 
/*----------------------------------------------------------------------------*/
