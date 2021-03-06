/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */

/**
 *  @addtogroup COMPASSDL
 *
 *  @{
 *      @file   ak8963.c
 *      @brief  Magnetometer setup and handling methods for the AKM AK8963,
 *              AKM AK8963B, and AKM AK8963C compass devices.
 */

/* -------------------------------------------------------------------------- */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "mpu-dev.h"

#include <log.h>
#include <linux/mpu.h>
#include "mlsl.h"
#include "mldl_cfg.h"
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-compass"

#include <linux/gpio.h>
#include <mach/gpio.h>
//#include <plat/gpio-cfg.h>

#ifdef CONFIG_OF // by Jay.HF
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

/* -------------------------------------------------------------------------- */
#define AK8963_REG_ST1  (0x02)
#define AK8963_REG_HXL  (0x03)
#define AK8963_REG_ST2  (0x09)

#define AK8963_REG_CNTL (0x0A)
#define AK8963_REG_ASAX (0x10)
#define AK8963_REG_ASAY (0x11)
#define AK8963_REG_ASAZ (0x12)

//define output bit is 16bit
#define AK8963_CNTL_MODE_POWER_DOWN         (0x10)
#define AK8963_CNTL_MODE_SINGLE_MEASUREMENT (0x11)
#define AK8963_CNTL_MODE_FUSE_ROM_ACCESS    (0x1f)

/* -------------------------------------------------------------------------- */
struct ak8963_config {
	char asa[COMPASS_NUM_AXES];	/* axis sensitivity adjustment */
};

struct ak8963_private_data {
	struct ak8963_config init;
};

#ifdef CONFIG_OF
static int ak8963_parse_dt(struct device *dev, struct ext_slave_platform_data *pdata)
{
	int i, ret;
	unsigned int value;
	int orientation[9];
	enum of_gpio_flags flags;
	struct device_node *np = dev->of_node;
	
	/* parse irq and request gpio */
	value = of_get_named_gpio_flags(np, "ak,int-gpio", 0, &flags);
	if (value < 0) {
		return -EINVAL;
	}
	gpio_request(value, "akm-irq");
	gpio_direction_input(value);

	ret = of_property_read_u32(np, "ak,address",(unsigned int *)&value);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"ak,address", np->full_name);
		return -ENODEV;
	}
	pdata->address = value;
	ret = of_property_read_u32(np, "ak,adapt-num", (unsigned int *)&value);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"ak,adapt-num", np->full_name);
		return -ENODEV;
	}
	pdata->adapt_num = value;
	ret = of_property_read_u32(np, "ak,bus", (unsigned int *)&value);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"ak,bus", np->full_name);
		return -ENODEV;
	}
	pdata->bus = value;
	ret = of_property_read_u32_array(np, "ak,orientation", (unsigned int *)orientation, 9);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"ak,orientation", np->full_name);
		return -ENODEV;
	}
	for (i  = 0; i < 9; i++) {
		pdata->orientation[i] = (orientation[i]==2) ? -1:orientation[i];
	}
	printk("address = %d\n", pdata->address);
	printk("adapt-num = %d\n", pdata->adapt_num);
	printk("bus = %d\n", pdata->bus);
	printk("orientation = \n \
			%d, %d, %d,\n \
			%d, %d, %d,\n \
			%d, %d, %d,\n", 
			pdata->orientation[0], pdata->orientation[1], pdata->orientation[2], 
			pdata->orientation[3], pdata->orientation[4], pdata->orientation[5],
			pdata->orientation[6], pdata->orientation[7], pdata->orientation[8]);
#if 0
	ret = of_property_read_string(np, "atmel,vcc_i2c_supply", &pdata->ts_vcc_i2c);
	ret = of_property_read_string(np, "atmel,vdd_ana_supply", &pdata->ts_vdd_ana);
	printk("pwr_en=%d, sleep_pwr_en=%d, vcc_i2c=%s, vdd_ana=%s\n", pdata->pwr_en,
			pdata->sleep_pwr_en, pdata->ts_vcc_i2c, pdata->ts_vdd_ana);
#endif
	return 0;	
}
#endif

/* -------------------------------------------------------------------------- */
static int ak8963_init(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char serial_data[COMPASS_NUM_AXES];

	struct ak8963_private_data *private_data;
	private_data = (struct ak8963_private_data *)
	    kzalloc(sizeof(struct ak8963_private_data), GFP_KERNEL);

	if (!private_data)
		return INV_ERROR_MEMORY_EXAUSTED;

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 AK8963_REG_CNTL,
					 AK8963_CNTL_MODE_POWER_DOWN);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* Wait at least 100us */
	udelay(100);

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 AK8963_REG_CNTL,
					 AK8963_CNTL_MODE_FUSE_ROM_ACCESS);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* Wait at least 200us */
	udelay(200);

	result = inv_serial_read(mlsl_handle, pdata->address,
				 AK8963_REG_ASAX,
				 COMPASS_NUM_AXES, serial_data);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	pdata->private_data = private_data;

	private_data->init.asa[0] = serial_data[0];
	private_data->init.asa[1] = serial_data[1];
	private_data->init.asa[2] = serial_data[2];

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 AK8963_REG_CNTL,
					 AK8963_CNTL_MODE_POWER_DOWN);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	udelay(100);
	printk(KERN_ERR "invensense: %s ok\n", __func__);
	return INV_SUCCESS;
}

static int ak8963_exit(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	kfree(pdata->private_data);
	return INV_SUCCESS;
}

static int ak8963_suspend(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    AK8963_REG_CNTL,
				    AK8963_CNTL_MODE_POWER_DOWN);
	msleep(1);		/* wait at least 100us */
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	return result;
}

static int ak8963_resume(void *mlsl_handle,
		  struct ext_slave_descr *slave,
		  struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    AK8963_REG_CNTL,
				    AK8963_CNTL_MODE_SINGLE_MEASUREMENT);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	return result;
}

static int ak8963_read(void *mlsl_handle,
		struct ext_slave_descr *slave,
		struct ext_slave_platform_data *pdata, unsigned char *data)
{
	unsigned char regs[8];
	unsigned char *stat = &regs[0];
	unsigned char *stat2 = &regs[7];
	int result = INV_SUCCESS;
	int status = INV_SUCCESS;

	result =
	    inv_serial_read(mlsl_handle, pdata->address, AK8963_REG_ST1,
			    8, regs);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* Always return the data and the status registers */
	memcpy(data, &regs[1], 6);
	data[6] = regs[0];
	data[7] = regs[7];

	/*
	 * ST : data ready -
	 * Measurement has been completed and data is ready to be read.
	 */
	if (*stat & 0x01)
		status = INV_SUCCESS;

	/*
	 * ST2 : data error -
	 * occurs when data read is started outside of a readable period;
	 * data read would not be correct.
	 * Valid in continuous measurement mode only.
	 * In single measurement mode this error should not occour but we
	 * stil account for it and return an error, since the data would be
	 * corrupted.
	 * DERR bit is self-clearing when ST2 register is read.
	 */
//	if (*stat2 & 0x04)
//		status = INV_ERROR_COMPASS_DATA_ERROR;
	/*
	 * ST2 : overflow -
	 * the sum of the absolute values of all axis |X|+|Y|+|Z| < 2400uT.
	 * This is likely to happen in presence of an external magnetic
	 * disturbance; it indicates, the sensor data is incorrect and should
	 * be ignored.
	 * An error is returned.
	 * HOFL bit clears when a new measurement starts.
	 */
	if (*stat2 & 0x08)
		status = INV_ERROR_COMPASS_DATA_OVERFLOW;
	/*
	 * ST : overrun -
	 * the previous sample was not fetched and lost.
	 * Valid in continuous measurement mode only.
	 * In single measurement mode this error should not occour and we
	 * don't consider this condition an error.
	 * DOR bit is self-clearing when ST2 or any meas. data register is
	 * read.
	 */
	if (*stat & 0x02) {
		/* status = INV_ERROR_COMPASS_DATA_UNDERFLOW; */
		status = INV_SUCCESS;
	}

	/*
	 * trigger next measurement if:
	 *    - stat is non zero;
	 *    - if stat is zero and stat2 is non zero.
	 * Won't trigger if data is not ready and there was no error.
	 */
	if (*stat != 0x00 || (*stat2 & 0x08) != 0x00 ) {
		result = inv_serial_single_write(
		    mlsl_handle, pdata->address,
		    AK8963_REG_CNTL, AK8963_CNTL_MODE_SINGLE_MEASUREMENT);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	return status;
}

static int ak8963_config(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata,
			 struct ext_slave_config *data)
{
	int result;
	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_WRITE_REGISTERS:
		result = inv_serial_write(mlsl_handle, pdata->address,
					  data->len,
					  (unsigned char *)data->data);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		break;
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
	case MPU_SLAVE_CONFIG_ODR_RESUME:
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
	case MPU_SLAVE_CONFIG_FSR_RESUME:
	case MPU_SLAVE_CONFIG_MOT_THS:
	case MPU_SLAVE_CONFIG_NMOT_THS:
	case MPU_SLAVE_CONFIG_MOT_DUR:
	case MPU_SLAVE_CONFIG_NMOT_DUR:
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
	default:
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return INV_SUCCESS;
}

static int ak8963_get_config(void *mlsl_handle,
			     struct ext_slave_descr *slave,
			     struct ext_slave_platform_data *pdata,
			     struct ext_slave_config *data)
{
	struct ak8963_private_data *private_data = pdata->private_data;
	int result;
	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_READ_REGISTERS:
		{
			unsigned char *serial_data =
			    (unsigned char *)data->data;
			result =
			    inv_serial_read(mlsl_handle, pdata->address,
					    serial_data[0], data->len - 1,
					    &serial_data[1]);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			break;
		}
	case MPU_SLAVE_READ_SCALE:
		{
			unsigned char *serial_data =
			    (unsigned char *)data->data;
			serial_data[0] = private_data->init.asa[0];
			serial_data[1] = private_data->init.asa[1];
			serial_data[2] = private_data->init.asa[2];
			result = INV_SUCCESS;
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			break;
		}
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
		(*(unsigned long *)data->data) = 0;
		break;
	case MPU_SLAVE_CONFIG_ODR_RESUME:
		(*(unsigned long *)data->data) = 8000;
		break;
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
	case MPU_SLAVE_CONFIG_FSR_RESUME:
	case MPU_SLAVE_CONFIG_MOT_THS:
	case MPU_SLAVE_CONFIG_NMOT_THS:
	case MPU_SLAVE_CONFIG_MOT_DUR:
	case MPU_SLAVE_CONFIG_NMOT_DUR:
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
	default:
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return INV_SUCCESS;
}

static struct ext_slave_read_trigger ak8963_read_trigger = {
	/*.reg              = */ 0x0A,
	/*.value            = */ 0x11
};

static struct ext_slave_descr ak8963_descr = {
	.init             = ak8963_init,
	.exit             = ak8963_exit,
	.suspend          = ak8963_suspend,
	.resume           = ak8963_resume,
	.read             = ak8963_read,
	.config           = ak8963_config,
	.get_config       = ak8963_get_config,
	.name             = "ak8963",
	.type             = EXT_SLAVE_TYPE_COMPASS,
	.id               = COMPASS_ID_AK8963,
	.read_reg         = 0x01,
	.read_len         = 10,
	.endian           = EXT_SLAVE_LITTLE_ENDIAN,
	.range            = {9830, 4000},
	.trigger          = &ak8963_read_trigger,
};

static
struct ext_slave_descr *ak8963_get_slave_descr(void)
{
	return &ak8963_descr;
}

/* -------------------------------------------------------------------------- */
struct ak8963_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int ak8963_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	struct ext_slave_platform_data *pdata;
	struct ak8963_mod_private_data *private_data;
	int result = 0;

#ifdef CONFIG_LPM_MODE
    extern unsigned int poweroff_charging;
    extern unsigned int recovery_mode;
	if (1 == poweroff_charging || 1 == recovery_mode) {
        printk(KERN_ERR"%s: probe exit, lpm=%d recovery=%d\n", __func__, poweroff_charging, recovery_mode);
		return -ENODEV;
	}
#endif

	dev_info(&client->adapter->dev, "%s: %s\n", __func__, devid->name);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		result = -ENODEV;
		goto out_no_free;
	}

#ifdef CONFIG_OF
	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&client->adapter->dev, "faild to alloc pdata memory\n");
		result = -ENOMEM;
		goto out_no_free;
	}
	result = ak8963_parse_dt(&client->dev, pdata);
	if (result) {
		dev_err(&client->adapter->dev, "faild to parse dt\n");
		goto out_free_pdata;
	}
#else
	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->adapter->dev,
			"Missing platform data for slave %s\n", devid->name);
		result = -EFAULT;
		goto out_free_pdata;
	}
#endif
	private_data = kzalloc(sizeof(*private_data), GFP_KERNEL);
	if (!private_data) {
		result = -ENOMEM;
		goto out_free_pdata;
	}

	i2c_set_clientdata(client, private_data);
	private_data->client = client;
	private_data->pdata = pdata;

	result = inv_mpu_register_slave(THIS_MODULE, client, pdata,
					ak8963_get_slave_descr);
	printk(KERN_ERR "invensense: in %s, result is %d\n", __func__, result);
	if (result) {
		dev_err(&client->adapter->dev,
			"Slave registration failed: %s, %d\n",
			devid->name, result);
		goto out_free_memory;
	}

	return result;

out_free_memory:
	kfree(private_data);
out_free_pdata:
#ifdef CONFIG_OF
	kfree(pdata);
#endif
out_no_free:
	dev_err(&client->adapter->dev, "%s failed %d\n", __func__, result);
	return result;

}

static int ak8963_mod_remove(struct i2c_client *client)
{
	struct ak8963_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);
	inv_mpu_unregister_slave(client, private_data->pdata,
				ak8963_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id ak8963_mod_id[] = {
	{ "ak8963", COMPASS_ID_AK8963 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ak8963_mod_id);

#ifdef CONFIG_OF
static struct of_device_id ak_match_table[] = {
	{ .compatible = "ak8963",},
	{ },
};
#endif

static struct i2c_driver ak8963_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = ak8963_mod_probe,
	.remove = ak8963_mod_remove,
	.id_table = ak8963_mod_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "ak8963_mod",
	#ifdef CONFIG_OF
      	.of_match_table = ak_match_table,
	#endif
		   },
	.address_list = normal_i2c,
};

static int __init ak8963_mod_init(void)
{
	int res = i2c_add_driver(&ak8963_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "ak8963_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit ak8963_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&ak8963_mod_driver);
}

module_init(ak8963_mod_init);
module_exit(ak8963_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate AK8963 sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ak8963_mod");

/**
 *  @}
 */
