/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * Copyright (c) 2014, Sultanxda <sultanxda@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msm_tsens.h>

#define TSENS_SENSOR 0

#define THERM_LOG "MSM_THERMAL: "

static unsigned int thermal_throttled = 0;
static unsigned int saved_maxfreq = 0;

static struct delayed_work msm_thermal_main_work;

static struct msm_thermal_tuners {
	unsigned int trip_high_thresh;
	unsigned int reset_high_thresh;
	unsigned int freq_high_thresh;

	unsigned int trip_mid_thresh;
	unsigned int reset_mid_thresh;
	unsigned int freq_mid_thresh;

	unsigned int trip_low_thresh;
	unsigned int reset_low_thresh;
	unsigned int freq_low_thresh;

	unsigned int check_interval_ms;
} therm_conf = {
	.trip_high_thresh = 80,
	.reset_high_thresh = 75,
	.freq_high_thresh = 384000,

	.trip_mid_thresh = 69,
	.reset_mid_thresh = 65,
	.freq_mid_thresh = 972000,

	.trip_low_thresh = 64,
	.reset_low_thresh = 60,
	.freq_low_thresh = 1188000,

	.check_interval_ms = 3000,
};

static void update_maxfreq(struct cpufreq_policy *policy, unsigned int maxfreq)
{
	policy->user_policy.max = maxfreq;
	cpufreq_update_policy(0);
	pr_warn(THERM_LOG "Setting CPU max frequency to %d\n", maxfreq);
}

static void msm_thermal_main(struct work_struct *work)
{
	struct cpufreq_policy *policy;
	struct tsens_device tsens_dev;
	unsigned long temp;
	static unsigned int maxfreq = 0;
	int ret;

	tsens_dev.sensor_num = TSENS_SENSOR;
	ret = tsens_get_temp(&tsens_dev, &temp);
	if (ret || temp > 1000) {
		pr_err(THERM_LOG "Unable to read tsens sensor #%d\n",
				tsens_dev.sensor_num);
		goto reschedule;
	}

	policy = cpufreq_cpu_get(0);

	if (unlikely(!policy)) {
		pr_err(THERM_LOG "Error acquiring CPU0 policy!\n");
		goto reschedule;
	}

	if (!thermal_throttled || saved_maxfreq < policy->user_policy.max)
		saved_maxfreq = policy->user_policy.max;

	/* low trip point */
	if ((temp >= therm_conf.trip_low_thresh) &&
	    (temp < therm_conf.trip_mid_thresh) && !thermal_throttled) {
		maxfreq = therm_conf.freq_low_thresh;
		thermal_throttled = 1;
		pr_warn(THERM_LOG "Low trip point triggered! temp: %lu\n", temp);
	/* low clear point */
	} else if ((temp <= therm_conf.reset_low_thresh) && thermal_throttled) {
		update_maxfreq(policy, saved_maxfreq);
		thermal_throttled = 0;
		maxfreq = 0;
		pr_warn(THERM_LOG "Low trip point cleared! temp: %lu\n", temp);
	/* mid trip point */
	} else if ((temp >= therm_conf.trip_mid_thresh) &&
		   (temp < therm_conf.trip_high_thresh) &&
		   (thermal_throttled < 2)) {
		maxfreq = therm_conf.freq_mid_thresh;
		thermal_throttled = 2;
		pr_warn(THERM_LOG "Mid trip point triggered! temp: %lu\n", temp);
	/* mid clear point */
	} else if ((temp < therm_conf.reset_mid_thresh) &&
		   (thermal_throttled > 1)) {
		maxfreq = therm_conf.freq_low_thresh;
		thermal_throttled = 1;
		pr_warn(THERM_LOG "Mid trip point cleared! temp: %lu\n", temp);
	/* high trip point */
	} else if ((temp >= therm_conf.trip_high_thresh) &&
		   (thermal_throttled < 3)) {
		maxfreq = therm_conf.freq_high_thresh;
		thermal_throttled = 3;
		pr_warn(THERM_LOG "High trip point triggered! temp: %lu\n", temp);
	/* high clear point */
	} else if ((temp < therm_conf.reset_high_thresh) &&
		   (thermal_throttled > 2)) {
		maxfreq = therm_conf.freq_mid_thresh;
		thermal_throttled = 2;
		pr_warn(THERM_LOG "High trip point cleared! temp: %lu\n", temp);
	}

	if (maxfreq)
		update_maxfreq(policy, maxfreq);

	cpufreq_cpu_put(policy);

reschedule:
	schedule_delayed_work(&msm_thermal_main_work,
			msecs_to_jiffies(therm_conf.check_interval_ms));
}

/**************************** SYSFS START ****************************/
struct kobject *msm_thermal_kobject;

#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)               \
{									\
	return sprintf(buf, "%u\n", therm_conf.object);			\
}

show_one(trip_high_thresh, trip_high_thresh);
show_one(reset_high_thresh, reset_high_thresh);
show_one(freq_high_thresh, freq_high_thresh);
show_one(trip_mid_thresh, trip_mid_thresh);
show_one(reset_mid_thresh, reset_mid_thresh);
show_one(freq_mid_thresh, freq_mid_thresh);
show_one(trip_low_thresh, trip_low_thresh);
show_one(reset_low_thresh, reset_low_thresh);
show_one(freq_low_thresh, freq_low_thresh);

static ssize_t store_trip_high_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.trip_high_thresh = input;

	return count;
}

static ssize_t store_reset_high_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.reset_high_thresh = input;

	return count;
}

static ssize_t store_freq_high_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.freq_high_thresh = input;

	return count;
}

static ssize_t store_trip_mid_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.trip_mid_thresh = input;

	return count;
}

static ssize_t store_reset_mid_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.reset_mid_thresh = input;

	return count;
}

static ssize_t store_freq_mid_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.freq_mid_thresh = input;

	return count;
}

static ssize_t store_trip_low_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.trip_low_thresh = input;

	return count;
}

static ssize_t store_reset_low_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.reset_low_thresh = input;

	return count;
}

static ssize_t store_freq_low_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.freq_low_thresh = input;

	return count;
}

define_one_global_rw(trip_high_thresh);
define_one_global_rw(reset_high_thresh);
define_one_global_rw(freq_high_thresh);
define_one_global_rw(trip_mid_thresh);
define_one_global_rw(reset_mid_thresh);
define_one_global_rw(freq_mid_thresh);
define_one_global_rw(trip_low_thresh);
define_one_global_rw(reset_low_thresh);
define_one_global_rw(freq_low_thresh);

static struct attribute *msm_thermal_attributes[] = {
	&trip_high_thresh.attr,
	&reset_high_thresh.attr,
	&freq_high_thresh.attr,
	&trip_mid_thresh.attr,
	&reset_mid_thresh.attr,
	&freq_mid_thresh.attr,
	&trip_low_thresh.attr,
	&reset_low_thresh.attr,
	&freq_low_thresh.attr,
	NULL
};

static struct attribute_group msm_thermal_attr_group = {
	.attrs = msm_thermal_attributes,
	.name = "conf",
};
/**************************** SYSFS END ****************************/

static int __init msm_thermal_init(void)
{
	int rc, ret = 0;

	INIT_DELAYED_WORK(&msm_thermal_main_work, msm_thermal_main);

	schedule_delayed_work(&msm_thermal_main_work, 0);

	msm_thermal_kobject = kobject_create_and_add("msm_thermal", kernel_kobj);
	if (msm_thermal_kobject) {
		rc = sysfs_create_group(msm_thermal_kobject,
							&msm_thermal_attr_group);
		if (rc)
			pr_err(THERM_LOG "sysfs: ERROR, could not create sysfs group");
	} else
		pr_err(THERM_LOG "sysfs: ERROR, could not create sysfs kobj");

	return ret;
}
late_initcall(msm_thermal_init);
