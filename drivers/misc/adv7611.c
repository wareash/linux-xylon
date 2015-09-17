/*
 * Simple I2C driver for ADV7611 HDMI receiver
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * 2014 (c) Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/i2c.h>

#define ADV7611_I2C_IO_ADDR		0x98
#define ADV7611_I2C_CEC_ADDR		0x80
#define ADV7611_I2C_INFOFRAME_ADDR	0x6A
#define ADV7611_I2C_DPLL_ADDR		0x4C
#define ADV7611_I2C_KSV_ADDR		0x64
#define ADV7611_I2C_EDID_ADDR		0x6C
#define ADV7611_I2C_HDMI_ADDR		0x68
#define ADV7611_I2C_CP_ADDR		0x44

#define ADV7611_EDID_SIZE		256
#define ADV7611_EDID_BUFFER_SIZE	(ADV7611_EDID_SIZE + 1)

#define ADV7611_HPA_DELAY_MS		500

struct adv7611_data {
	struct attribute_group attrs;
	struct mutex lock;
};

struct i2c_adapter *adapter;
struct adv7611_data *data;

static u8 adv7611_edid_buffer[ADV7611_EDID_BUFFER_SIZE];

static int adv7611_set_reg(struct i2c_adapter *adapt, struct i2c_msg *msg,
			   int len)
{
	int ret;

	msg->flags &= ~I2C_M_RD;
	msg->len = len;

	ret = i2c_transfer(adapt, msg, 1);

	return ret;
}

static int adv7611_mapping(struct i2c_adapter *adapter, struct i2c_msg *msg)
{
	static u8 const adv7611_io_mapping[][3] = {
		{ADV7611_I2C_IO_ADDR, 0xF4, ADV7611_I2C_CEC_ADDR},
		{ADV7611_I2C_IO_ADDR, 0xF5, ADV7611_I2C_INFOFRAME_ADDR},
		{ADV7611_I2C_IO_ADDR, 0xF8, ADV7611_I2C_DPLL_ADDR},
		{ADV7611_I2C_IO_ADDR, 0xF9, ADV7611_I2C_KSV_ADDR},
		{ADV7611_I2C_IO_ADDR, 0xFA, ADV7611_I2C_EDID_ADDR},
		{ADV7611_I2C_IO_ADDR, 0xFB, ADV7611_I2C_HDMI_ADDR},
		{ADV7611_I2C_IO_ADDR, 0xFD, ADV7611_I2C_CP_ADDR},
		{0, 0, 0}
	};
	int i = 0;
	int ret;

	while (adv7611_io_mapping[i][0]) {
		msg->addr = adv7611_io_mapping[i][0] >> 1;
		msg->buf[0] = adv7611_io_mapping[i][1];
		msg->buf[1] = adv7611_io_mapping[i][2];
		ret = adv7611_set_reg(adapter, msg, 2);
		if (ret < 0) {
			pr_err("Error setting I2C HDMI input mapping\n");
			return ret;
		}
		i++;
	}

	return 0;
}

static int adv7611_hdmi_edid(struct i2c_adapter *adapter, struct i2c_msg *msg)
{
	static u8 const adv7611_hdmi_hpa[][3] = {
		{ADV7611_I2C_HDMI_ADDR, 0x6C, 0x13},
		{0, 0, 0}
	};
	static u8 const adv7611_hdmi_edid_pre[][3] = {
		{ADV7611_I2C_KSV_ADDR, 0x77, 0x00},
		{0, 0, 0}
	};
	static u8 const adv7611_hdmi_edid_post[][3] = {
		{ADV7611_I2C_KSV_ADDR, 0x77, 0x00},
		{ADV7611_I2C_KSV_ADDR, 0x52, 0x20},
		{ADV7611_I2C_KSV_ADDR, 0x53, 0x00},
		{ADV7611_I2C_KSV_ADDR, 0x70, 0x9E},
		{ADV7611_I2C_KSV_ADDR, 0x74, 0x03},
		{0, 0, 0}
	};
	static u8 const adv7611_initial_edid[] = {
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
		0x06, 0xd4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x16, 0x01, 0x03, 0x81, 0x46, 0x27, 0x78,
		0x0a, 0x32, 0x30, 0xa1, 0x54, 0x52, 0x9e, 0x26,
		0x0a, 0x49, 0x4b, 0xa3, 0x08, 0x00, 0x81, 0xc0,
		0x81, 0x00, 0x81, 0x0f, 0x81, 0x40, 0x81, 0x80,
		0x95, 0x00, 0xb3, 0x00, 0x01, 0x01, 0x02, 0x3a,
		0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
		0x45, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x1e,
		0xa9, 0x1a, 0x00, 0xa0, 0x50, 0x00, 0x16, 0x30,
		0x30, 0x20, 0x37, 0x00, 0xc4, 0x8e, 0x21, 0x00,
		0x00, 0x1a, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x46,
		0x4d, 0x43, 0x2d, 0x49, 0x4d, 0x41, 0x47, 0x45,
		0x4f, 0x4e, 0x0a, 0x20, 0x00, 0x00, 0x00, 0xfd,
		0x00, 0x38, 0x4b, 0x20, 0x44, 0x11, 0x00, 0x0a,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x54,
		0x02, 0x03, 0x1f, 0x71, 0x4b, 0x90, 0x03, 0x04,
		0x05, 0x12, 0x13, 0x14, 0x1f, 0x20, 0x07, 0x16,
		0x26, 0x15, 0x07, 0x50, 0x09, 0x07, 0x01, 0x67,
		0x03, 0x0c, 0x00, 0x10, 0x00, 0x00, 0x1e, 0x01,
		0x1d, 0x00, 0x72, 0x51, 0xd0, 0x1e, 0x20, 0x6e,
		0x28, 0x55, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00,
		0x1e, 0x01, 0x1d, 0x80, 0x18, 0x71, 0x1c, 0x16,
		0x20, 0x58, 0x2c, 0x25, 0x00, 0xc4, 0x8e, 0x21,
		0x00, 0x00, 0x9e, 0x8c, 0x0a, 0xd0, 0x8a, 0x20,
		0xe0, 0x2d, 0x10, 0x10, 0x3e, 0x96, 0x00, 0xc4,
		0x8e, 0x21, 0x00, 0x00, 0x18, 0x01, 0x1d, 0x80,
		0x3e, 0x73, 0x38, 0x2d, 0x40, 0x7e, 0x2c, 0x45,
		0x80, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x1e, 0x1a,
		0x36, 0x80, 0xa0, 0x70, 0x38, 0x1f, 0x40, 0x30,
		0x20, 0x25, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00,
		0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
	};
	int i = 0;
	int ret;

	while (adv7611_hdmi_hpa[i][0]) {
		msg->addr = adv7611_hdmi_hpa[i][0] >> 1;
		msg->buf[0] = adv7611_hdmi_hpa[i][1];
		msg->buf[1] = adv7611_hdmi_hpa[i][2];
		ret = adv7611_set_reg(adapter, msg, 2);
		if (ret < 0) {
			pr_err("Error init EDID (HPA) at %d\n", i);
			return ret;
		}
		i++;
	}

	i = 0;
	while (adv7611_hdmi_edid_pre[i][0]) {
		msg->addr = adv7611_hdmi_edid_pre[i][0] >> 1;
		msg->buf[0] = adv7611_hdmi_edid_pre[i][1];
		msg->buf[1] = adv7611_hdmi_edid_pre[i][2];
		ret = adv7611_set_reg(adapter, msg, 2);
		if (ret < 0) {
			pr_err("Error init EDID (PRE) at %d\n", i);
			return ret;
		}
		i++;
	}

	msg->addr = ADV7611_I2C_EDID_ADDR >> 1;
	/* set EDID write subaddress */
	adv7611_edid_buffer[0] = 0;
	memcpy(&adv7611_edid_buffer[1], adv7611_initial_edid,
	       sizeof(adv7611_initial_edid));
	msg->buf = adv7611_edid_buffer;
	ret = adv7611_set_reg(adapter, msg, sizeof(adv7611_edid_buffer));
	if (ret < 0) {
		pr_err("Error writing EDID\n");
		return ret;
	}

	i = 0;
	while (adv7611_hdmi_edid_post[i][0]) {
		msg->addr = adv7611_hdmi_edid_post[i][0] >> 1;
		msg->buf[0] = adv7611_hdmi_edid_post[i][1];
		msg->buf[1] = adv7611_hdmi_edid_post[i][2];
		ret = adv7611_set_reg(adapter, msg, 2);
		if (ret < 0) {
			pr_err("Error init EDID (POST) at %d\n", i);
			return ret;
		}
		i++;
	}

	return 0;
}

static int adv7611_input_config(struct i2c_adapter *adapter,
				struct i2c_msg *msg)
{
	static u8 const adv7611_config[][3] = {
		{ADV7611_I2C_IO_ADDR, 0x01, 0x06},
		{ADV7611_I2C_IO_ADDR, 0x02, 0xF5},
		{ADV7611_I2C_IO_ADDR, 0x03, 0x80},
		{ADV7611_I2C_IO_ADDR, 0x04, 0x62},
		{ADV7611_I2C_IO_ADDR, 0x05, 0x2C},
		{ADV7611_I2C_CP_ADDR, 0x7B, 0x05},
		{ADV7611_I2C_IO_ADDR, 0x0B, 0x44},
		{ADV7611_I2C_IO_ADDR, 0x0C, 0x42},
		{ADV7611_I2C_IO_ADDR, 0x14, 0x7F},
		{ADV7611_I2C_IO_ADDR, 0x15, 0x80},
		{ADV7611_I2C_IO_ADDR, 0x19, 0x80},
		{ADV7611_I2C_IO_ADDR, 0x33, 0x40},
		{ADV7611_I2C_CP_ADDR, 0xBA, 0x01},
		{ADV7611_I2C_KSV_ADDR, 0x40, 0x81},
		{ADV7611_I2C_HDMI_ADDR, 0x9B, 0x03},
		{ADV7611_I2C_HDMI_ADDR, 0xC1, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xC2, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xC3, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xC4, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xC5, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xC6, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xC7, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xC8, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xC9, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xCA, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xCB, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0xCC, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0x00, 0x08},
		{ADV7611_I2C_HDMI_ADDR, 0x02, 0x03},
		{ADV7611_I2C_HDMI_ADDR, 0x83, 0xFC},
		{ADV7611_I2C_HDMI_ADDR, 0x6F, 0x0C},
		{ADV7611_I2C_HDMI_ADDR, 0x85, 0x1F},
		{ADV7611_I2C_HDMI_ADDR, 0x87, 0x70},
		{ADV7611_I2C_HDMI_ADDR, 0x8D, 0x04},
		{ADV7611_I2C_HDMI_ADDR, 0x8E, 0x1E},
		{ADV7611_I2C_HDMI_ADDR, 0x1A, 0x8A},
		{ADV7611_I2C_HDMI_ADDR, 0x57, 0xDA},
		{ADV7611_I2C_HDMI_ADDR, 0x58, 0x01},
		{ADV7611_I2C_HDMI_ADDR, 0x75, 0x10},
		{ADV7611_I2C_HDMI_ADDR, 0x90, 0x04},
		{ADV7611_I2C_HDMI_ADDR, 0x91, 0x1E},
		{0, 0, 0}
	};
	static u8 const adv7611_i2s_config[][3] = {
		{ADV7611_I2C_HDMI_ADDR, 0x03, 0x10},
		{ADV7611_I2C_HDMI_ADDR, 0x6E, 0x00},
		{0, 0, 0}
	};
	int i = 0;
	int ret;

	while (adv7611_config[i][0]) {
		msg->addr = adv7611_config[i][0] >> 1;
		msg->buf[0] = adv7611_config[i][1];
		msg->buf[1] = adv7611_config[i][2];
		ret = adv7611_set_reg(adapter, msg, 2);
		if (ret < 0) {
			pr_err("Error init HDMI input config at %d\n", i);
			return ret;
		}
		i++;
	}

	i = 0;
	while (adv7611_i2s_config[i][0]) {
		msg->addr = adv7611_i2s_config[i][0] >> 1;
		msg->buf[0] = adv7611_i2s_config[i][1];
		msg->buf[1] = adv7611_i2s_config[i][2];
		ret = adv7611_set_reg(adapter, msg, 2);
		if (ret < 0) {
			pr_err("Error init HDMI input I2S at %d\n", i);
			return ret;
		}
		i++;
	}

	mdelay(10);

	return 0;
}

static int adv7611_hpa(struct i2c_adapter *adapter, struct i2c_msg *msg,
		       int delay)
{
	int ret;

	msg->addr = ADV7611_I2C_IO_ADDR >> 1;
	msg->buf[0] = 0x20;
	msg->buf[1] = 0x00;
	ret = adv7611_set_reg(adapter, msg, 2);
	if (ret < 0) {
		pr_err("Error set manual HPA\n");
		return ret;
	}

	mdelay(delay);

	msg->buf[1] = 0x80;
	ret = adv7611_set_reg(adapter, msg, 2);
	if (ret < 0) {
		pr_err("Error set manual HPA\n");
		return ret;
	}

	return 0;
}

static ssize_t adv7611_edid_read(struct file *fp, struct kobject *kobj,
 				 struct bin_attribute *bin_attr,
				 char *buf, loff_t off, size_t count)
{
	return memory_read_from_buffer(buf, count, &off,
				       &adv7611_edid_buffer[1],
				       ADV7611_EDID_SIZE);
}

int adv7611_set_edid(u8 *buf)
{
	int ret;
	struct i2c_msg msg;
	u8 msg_buf[2];

	msg.flags = 0;
	msg.addr = ADV7611_I2C_EDID_ADDR >> 1;
	/* set EDID write subaddress */
	adv7611_edid_buffer[0] = 0;
	memcpy(&adv7611_edid_buffer[1], buf, ADV7611_EDID_SIZE);
	msg.buf = adv7611_edid_buffer;
	ret = adv7611_set_reg(adapter, &msg, sizeof(adv7611_edid_buffer));
	if (ret < 0) {
		pr_err("%s - Error setting EDID\n", __func__);
		return ret;
	}

	msg.buf = msg_buf;
	if (adv7611_hpa(adapter, &msg, ADV7611_HPA_DELAY_MS))
		pr_err("Error EDID HPA\n");

	return 0;
}
EXPORT_SYMBOL(adv7611_set_edid);

static ssize_t adv7611_edid_write(struct file *fp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buf, loff_t off, size_t count)
{
	int ret;

	mutex_lock(&data->lock);
	ret = adv7611_set_edid((u8 *)buf);
	mutex_unlock(&data->lock);
	if (ret < 0) {
		pr_err("%s - Error writing EDID\n", __func__);
		return ret;
	}

	return count;
}

static struct bin_attribute adv7611_attr = {
	.attr = {.name = "edid", .mode = S_IWUGO | S_IRUGO},
	.size = ADV7611_EDID_SIZE,
	.read = adv7611_edid_read,
	.write = adv7611_edid_write,
};

static int adv7611_device_config(struct i2c_client *client)
{
	struct i2c_driver *i2c_driver = to_i2c_driver(client->dev.driver);
	struct i2c_msg msg;
	int ret;
	u8 data_buff[256];

	msg.addr = 0;
	msg.flags = 0;
	msg.len = 0;
	msg.buf = data_buff;
	ret = 0;

	adapter = client->adapter;
	if (!adapter) {
		pr_err("%s - no I2C device\n", __func__);
		return -ENODEV;
	}
	data = kzalloc(sizeof(struct adv7611_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto adv7611_config_error;
	}

	i2c_set_clientdata(client, data);

	mutex_init(&data->lock);

	ret = adv7611_mapping(adapter, &msg);
	if (ret < 0)
		goto adv7611_config_error;
	ret = adv7611_hdmi_edid(adapter, &msg);
	if (ret < 0)
		goto adv7611_config_error;
	ret = adv7611_input_config(adapter, &msg);
	if (ret < 0)
		goto adv7611_config_error;
	ret = adv7611_hpa(adapter, &msg, ADV7611_HPA_DELAY_MS);
	if (ret < 0)
		goto adv7611_config_error;

	i2c_put_adapter(adapter);

	/* Register sysfs hooks */
	ret = sysfs_create_bin_file(&client->dev.kobj, &adv7611_attr);
	if (ret){
		printk(KERN_ERR "%s: sysfs_create_bin_file failed\n", __func__);
		goto adv7611_config_error;
	}

	dev_info(&client->dev, "%s configured\n", i2c_driver->id_table->name);
	return 0;

adv7611_config_error:
	dev_warn(&client->dev, "failed configuring %s\n",
			i2c_driver->id_table->name);
	return ret;
}

static const struct i2c_device_id adv7611_id[] = {
	{ "adv7611", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7611_id);

static int adv7611_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	return adv7611_device_config(client);
}

static int adv7611_remove(struct i2c_client *client)
{
	sysfs_remove_bin_file(&client->dev.kobj, &adv7611_attr);

	return 0;
}

static const struct of_device_id i2c_adv7611_of_match[] = {
	{ .compatible = "adv7611" },
	{ }
};
MODULE_DEVICE_TABLE(of, i2c_adv7611_of_match);

static struct i2c_driver adv7611_driver = {
	.driver = {
		.name = "adv7611",
		.of_match_table = i2c_adv7611_of_match,
	},
	.probe = adv7611_probe,
	.remove = adv7611_remove,
	.id_table = adv7611_id,
};

static int __init adv7611_init(void)
{
	return i2c_add_driver(&adv7611_driver);
}

static void __exit adv7611_exit(void)
{
	i2c_del_driver(&adv7611_driver);
}

module_init(adv7611_init);
module_exit(adv7611_exit);

MODULE_AUTHOR("Davor Joja <davor.joja@logicbricks.com>");
MODULE_DESCRIPTION("Basic ADV7611 I2C driver");
MODULE_LICENSE("GPL v2");
