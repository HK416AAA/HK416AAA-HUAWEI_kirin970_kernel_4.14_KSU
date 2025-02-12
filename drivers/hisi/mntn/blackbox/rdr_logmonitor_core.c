/*
 * blackbox. (kernel run data recorder.)
 *
 * Copyright (c) 2013 Huawei Technologies CO., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/io.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <securec.h>

#include <linux/hisi/rdr_pub.h>
#include <linux/hisi/util.h>
#include <linux/hisi/hisi_log.h>
#include <linux/hisi/hisi_bbox_diaginfo.h>
#define HISI_LOG_TAG HISI_BLACKBOX_TAG
#include "rdr_inner.h"
#include "rdr_field.h"
#include "rdr_print.h"

#define DATAREADY_NAME "data-ready"
#define DATA_READY 1
#define DATA_NOT_READY 0

#define RDR_TMP_PATH_MAX_LEN 64

static unsigned int g_dataready_flag = DATA_NOT_READY;
static char *g_history_log_buf;
static u32 g_history_log_size;

struct linux_dirent {
	unsigned long d_ino;
	unsigned long d_off;
	unsigned short d_reclen;
	char d_name[1];
};

static int rdr_rm_dir(const char *path);

static int __rdr_create_dir(const char *path)
{
	int fd;

	if (path == NULL) {
		BB_PRINT_ERR("invalid  parameter. path:%pK.\n", path);
		BB_PRINT_END();
		return -1;
	}

	fd = sys_access(path, 0);
	if (0 != fd) {
		BB_PRINT_DBG("rdr: need create dir %s !\n", path);
		fd = sys_mkdir(path, DIR_LIMIT);
		if (fd < 0) {
			BB_PRINT_ERR("rdr: create dir %s failed! ret = %d\n",
				     path, fd);
			BB_PRINT_END();
			return fd;
		}
		BB_PRINT_DBG("rdr: create dir %s successed [%d]!!!\n", path,
			     fd);
	}

	return 0;
}

/*
 * func name: rdr_create_dir
 * .
 * func args:
 *  char*  path,
 * return
 *	<0 fail
 *	0  success
 */
int rdr_create_dir(const char *path)
{
	char cur_path[RDR_TMP_PATH_MAX_LEN];
	int index = 0;

	BB_PRINT_START();
	if (path == NULL) {
		BB_PRINT_ERR("invalid  parameter. path:%pK\n", path);
		BB_PRINT_END();
		return -1;
	}
	if (EOK != memset_s(cur_path, RDR_TMP_PATH_MAX_LEN, 0, RDR_TMP_PATH_MAX_LEN)) {
		BB_PRINT_ERR("%s():%d:memset_s fail!\n", __func__, __LINE__);
	}
	if (*path != '/')
		return -1;
	cur_path[index++] = *path++;

	while (*path != '\0') {
		if (*path == '/')
			__rdr_create_dir(cur_path);
		cur_path[index] = *path;
		path++;
		index++;
	}

	BB_PRINT_END();
	return 0;
}


/*
 * func name: rdr_wait_partition
 * .
 * func args:
 *  char*  path,			path of watit file.
 *  u32 timeouts,       time out.
 * return
 *	<0 fail
 *	0  success
 */
int rdr_wait_partition(char *path, int timeouts)
{
	struct kstat m_stat;
	int timeo;

	if (path == NULL) {
		BB_PRINT_ERR("invalid  parameter. path:%pK\n", path);
		BB_PRINT_END();
		return -1;
	}

	for (;;) {
		if (rdr_get_suspend_state()) {
			BB_PRINT_PN("%s: wait for suspend.\n", __func__);
			msleep(50);
		} else if (rdr_get_reboot_state()) {
			BB_PRINT_PN("%s: wait for reboot.\n", __func__);
			msleep(50);
		} else {
			break;
		}
	}

	timeo = timeouts;

	if (0 == strncmp(path, "/data", strlen("/data"))) {
		while (0 != vfs_stat(path, &m_stat) || DATA_NOT_READY == g_dataready_flag) {
			current->state = TASK_INTERRUPTIBLE;
			(void)schedule_timeout(HZ / 10);	/*wait for 1/10 second */
			if (timeouts-- < 0) {
				BB_PRINT_ERR("%d:rdr:wait partiton[%s] fail. use [%d]'s . skip!\n",
				     __LINE__, path, timeo);
				return -1;
			}
		}
	} else {
		while (0 != vfs_stat(path, &m_stat)) {
			current->state = TASK_INTERRUPTIBLE;
			(void)schedule_timeout(HZ / 10);	/*wait for 1/10 second */
			if (timeouts-- < 0) {
				BB_PRINT_ERR("%d:rdr:wait partiton[%s] fail. use [%d]'s . skip!\n",
				     __LINE__, path, timeo);
				return -1;
			}
		}
	}

	return 0;
}

int rdr_create_epath_bc(char *path, u32 pathLen)
{
	char date[DATATIME_MAXLEN];
	int ret;

	BB_PRINT_START();
	if (!check_himntn(HIMNTN_GOBAL_RESETLOG))
		return -1;
	if (path == NULL) {
		BB_PRINT_ERR("invalid  parameter.\n");
		BB_PRINT_END();
		return -1;
	}

	if (EOK != memset_s(date,DATATIME_MAXLEN, 0, DATATIME_MAXLEN))
	{
		BB_PRINT_ERR("%s():%d:memset_s fail!\n", __func__, __LINE__);
	}
	snprintf(date, DATATIME_MAXLEN,"%s-%08lld",
		 rdr_get_timestamp(), rdr_get_tick());

	if (pathLen < PATH_MAXLEN) {
		BB_PRINT_ERR("invalid len.\n");
	}

	snprintf(path, PATH_MAXLEN, "%s%s/", PATH_ROOT, date);
	BB_PRINT_PN("date buf error, cur log path:[%s].\n", path);

	ret = rdr_create_dir(path);
	BB_PRINT_END();

	return ret;
}

int rdr_create_exception_path(struct rdr_exception_info_s *e,
			      char *path, char *date, u32 datelen)
{
	int ret = 0;

	BB_PRINT_START();
	if (!check_himntn(HIMNTN_GOBAL_RESETLOG))
		return -1;

	if (e == NULL || path == NULL || date == NULL) {
		BB_PRINT_ERR("invalid  parameter. e:%pK\n", e);
		BB_PRINT_END();
		return ret = -1;
	}

	if ( datelen < DATATIME_MAXLEN ) {
		BB_PRINT_ERR("invalid  parameter datelen\n");
	}

	if (EOK != memset_s(date,DATATIME_MAXLEN ,0, DATATIME_MAXLEN))
	{
		BB_PRINT_ERR("%s():%d:memset_s fail!\n", __func__, __LINE__);
	}
	ret = snprintf_s(date, DATATIME_MAXLEN, DATATIME_MAXLEN - 1, "%s-%08lld",
		 rdr_get_timestamp(), rdr_get_tick());
	if(unlikely(ret < 0)){
		BB_PRINT_ERR("[%s], snprintf_s ret %d!\n", __func__, ret);
	}

	memset(path, 0, PATH_MAXLEN);
	ret = snprintf_s(path, PATH_MAXLEN, PATH_MAXLEN - 1, "%s%s/", PATH_ROOT, date);
	if(unlikely(ret < 0)){
		BB_PRINT_ERR("[%s], snprintf_s ret %d!\n", __func__, ret);
	}

	ret = rdr_create_dir(path);
	BB_PRINT_END();
	return ret;
}

/*******************************************************************************
Function:       bbox_create_dfxlog_path
Description:    create dfx's log_path
Input:          NA
Output:         path;date
Return:         NA
********************************************************************************/
int bbox_create_dfxlog_path(char *path, char *date, u32 dataLen)
{
	int ret = 0;
	static int number = 1;

	BB_PRINT_START();
	if (!check_himntn(HIMNTN_GOBAL_RESETLOG))
		return -1;

	if (NULL == path || NULL == date) {
		BB_PRINT_ERR("invalid  parameter.\n");
		BB_PRINT_END();
		return -1;
	}

	if (dataLen < DATATIME_MAXLEN) {
		 BB_PRINT_ERR("invalid  len.\n");
	}

	if (EOK != memset_s(date,DATATIME_MAXLEN, 0, DATATIME_MAXLEN))
	{
		BB_PRINT_ERR("%s():%d:memset_s fail!\n", __func__, __LINE__);
	}
	snprintf(date, DATATIME_MAXLEN, "%s-%08lld",
		 rdr_get_timestamp(), rdr_get_tick()+number);

	number++;
	memset(path, 0, PATH_MAXLEN);
	snprintf(path, PATH_MAXLEN, "%s%s/", PATH_ROOT, date);

	ret = rdr_create_dir(path);
	BB_PRINT_END();
	return ret;
}

static LIST_HEAD(__rdr_logpath_info_list);
static DEFINE_MUTEX(__rdr_logpath_info_list_mutex);
struct logpath_info_s {
	struct list_head s_list;
	struct timespec ctime;
	u32 logpath_idx;
	char path[32];
};

struct RDR_LOG_TM_RANGE {
	char name[10];
	int idx[2];		/*start, len */
	int rg[2];		/*min, max */
};

const struct RDR_LOG_TM_RANGE tbl_rdr_logdir_tm_rg[] = {
/*	name		idx{start,len}	rg{min,max}*/
	{"year", {0, 4}, {1900, 2200} },
	{"month", {4, 2}, {1, 12} },
	{"day", {6, 2}, {1, 31} },
	{"hour", {8, 2}, {0, 23} },
	{"minute", {10, 2}, {0, 59} },
	{"second", {12, 2}, {0, 59} },
};

int rdr_is_logdir_nm_tm(const char *buf, int len)
{
	int i;
	char tempstr[10];
	int val;
	const struct RDR_LOG_TM_RANGE *pt_rg = NULL;

	pt_rg = (const struct RDR_LOG_TM_RANGE *)&tbl_rdr_logdir_tm_rg;

	/* Judge if all char is number */
	for (i = 0; i < len; i++) {
		if (buf[i] > '9' || buf[i] < '0') {
			return 0;
		}
	}
	/* Judge all field range */
	for (i = 0; (unsigned int)i < ARRAY_SIZE(tbl_rdr_logdir_tm_rg); i++, pt_rg++) {
		if (EOK != memcpy_s(tempstr,pt_rg->idx[1], buf + pt_rg->idx[0], pt_rg->idx[1]))
		{
			BB_PRINT_ERR("%s():%d:memcpy_s fail!\n", __func__, __LINE__);
		}
		tempstr[pt_rg->idx[1]] = 0;
		/* cppcheck-suppress * */
		if (1 != sscanf(tempstr, "%d", &val)) {
			BB_PRINT_ERR("[%s], val get failed!\n", __func__);
			return 0;
		}
		BB_PRINT_DBG("%s, val = %d, <%d,%d>\n", pt_rg->name, val,
			     pt_rg->rg[0], pt_rg->rg[1]);
		if (val < pt_rg->rg[0] || val > pt_rg->rg[1]) {
			return 0;
		}
	}
	return 1;
}

u64 rdr_cal_tm_from_logdir_name(const char *path)
{
	char sec[15];
	static u64 date_sec; /* default value is 0 */

	strncpy(sec, path, 15);
	sec[14] = 0;
	if (rdr_is_logdir_nm_tm(sec, 14)) {
		if (1 != sscanf(sec, "%lld", &date_sec)) {
			BB_PRINT_ERR("[%s], date_sec get failed!\n", __func__);
		}
	} else {
		date_sec++;	/* when dir name error, it is a little bigger than perv */
	}
	BB_PRINT_DBG("[%s], date_sec = %lld\n", __func__, date_sec);
	return date_sec;
}

static int rdr_get_history_log_buffer(char **buffer, u32 *size)
{
	if (!g_history_log_buf || !g_history_log_size) {
		BB_PRINT_ERR("[%s]g_history_log_buf not update\n", __func__);
		return -1;
	}

	*buffer = g_history_log_buf;
	*size = g_history_log_size;
	return 0;
}

static void rdr_update_history_log_buffer(void)
{
	char path[RDR_TMP_PATH_MAX_LEN];
	struct kstat stat;
	long cnt;
	int fd = -1;
	int ret;

	if (g_history_log_buf) {
		kfree(g_history_log_buf);
		g_history_log_buf = NULL;
		g_history_log_size = 0;
	}

	(void)memset_s(path, RDR_TMP_PATH_MAX_LEN, 0x0, RDR_TMP_PATH_MAX_LEN);
	ret = snprintf_s(path, RDR_TMP_PATH_MAX_LEN, RDR_TMP_PATH_MAX_LEN - 1,
		"%s/%s", PATH_ROOT, "history.log");
	if (ret < 0) {
		BB_PRINT_ERR("[%s]snprintf_s history.log error\n", __func__);
		return;
	}

	fd = sys_open(path, O_RDONLY, FILE_LIMIT);
	if (fd < 0) {
		BB_PRINT_ERR("[%s]history.log open failed, fd = %d\n", __func__, fd);
		return;
	}

	(void)memset_s(&stat, sizeof(stat), 0x0, sizeof(stat));
	ret = vfs_stat(path, &stat);
	if (ret) {
		BB_PRINT_ERR("[%s]get stat error, ret = %d\n", __func__, ret);
		goto err_close;
	}

	if (stat.size > HISTORY_LOG_MAX)
		stat.size = HISTORY_LOG_MAX;

	g_history_log_buf = kzalloc(stat.size + 1, GFP_KERNEL);
	if (!g_history_log_buf) {
		BB_PRINT_ERR("[%s]kzalloc g_history_log_buf error\n", __func__);
		goto err_close;
	}

	cnt = sys_read(fd, g_history_log_buf, stat.size);
	if (cnt <= 0) {
		BB_PRINT_ERR("[%s]sys_read error, cnt = %ld\n", __func__, cnt);
		goto err_free;
	}
	g_history_log_size = stat.size;

	sys_close(fd);

	return;

err_free:
	kfree(g_history_log_buf);
	g_history_log_buf = NULL;
err_close:
	sys_close(fd);

	return;
}

static u32 rdr_count_logpath_idx(const char *path)
{
	char date_str[DATA_MAXLEN + 1];
	char *buf = NULL;
	u32 size;
	u32 idx = 1;
	int ret;

	ret = rdr_get_history_log_buffer(&buf, &size);
	if (ret < 0) {
		BB_PRINT_ERR("[%s]rdr_get_history_log_buffer error\n", __func__);
		return 0;
	}

	ret = strncpy_s(date_str, DATA_MAXLEN + 1, path, DATA_MAXLEN);
	if (ret < 0) {
		BB_PRINT_ERR("[%s]strncpy error\n", __func__);
		return 0;
	}

	while (1) {
		buf = strnstr(buf, "time [", HISTORY_LOG_SIZE);
		if (!buf)
			break;
		buf = buf + sizeof("time [") - 1;
		if (!strncmp(buf, date_str, DATA_MAXLEN))
			return idx;
		idx++;
	}

	return 0;
}

/*******************************************************************************
Function:       rdr_empty_logpath_list
Description:    Maybe a logpath is in list, but is not exist.
				So need empty logpath_list.
Input:          NA
Output:         NA
Return:         NA
********************************************************************************/
static void rdr_empty_logpath_list(void)
{
	struct logpath_info_s *p_info = NULL;
	struct list_head *cur = NULL;
	struct list_head *next = NULL;

	BB_PRINT_START();
	list_for_each_safe(cur, next, &__rdr_logpath_info_list) {
		p_info = list_entry(cur, struct logpath_info_s, s_list);
		if (NULL == p_info) {
			BB_PRINT_ERR("It might be better to look around here. %s:%d\n",
			     __func__, __LINE__);
			continue;
		}
		list_del(cur);
		kfree(p_info);
	}

	BB_PRINT_END();
	return;

}

void rdr_check_logpath_repeat(struct logpath_info_s *info)
{
	struct logpath_info_s *p_info = NULL;
	struct list_head *cur = NULL;
	struct list_head *next = NULL;

	list_for_each_safe(cur, next, &__rdr_logpath_info_list) {
		p_info = list_entry(cur, struct logpath_info_s, s_list);
		if (p_info == NULL) {
			BB_PRINT_ERR
			    ("It might be better to look around here. %s:%d\n",
			     __func__, __LINE__);
			continue;
		}
		if (0 == memcmp(info->path, p_info->path, strlen(info->path))) {
			list_del(cur);
			kfree(p_info);
		}
	}

	return;
}

u32 __rdr_add_logpath_list(struct logpath_info_s *info)
{
	struct logpath_info_s *p_info = NULL;
	struct list_head *cur = NULL;
	struct list_head *next = NULL;

	if (list_empty(&__rdr_logpath_info_list)) {
		list_add_tail(&info->s_list, &__rdr_logpath_info_list);
		BB_PRINT_END();
		goto out;
	}
	p_info = list_entry(__rdr_logpath_info_list.next,
			    struct logpath_info_s, s_list);
	if (info->logpath_idx >= p_info->logpath_idx) {
		list_add(&info->s_list, &__rdr_logpath_info_list);
		goto out;
	}
	list_for_each_safe(cur, next, &__rdr_logpath_info_list) {
		p_info = list_entry(cur, struct logpath_info_s, s_list);
		if (p_info == NULL) {
			BB_PRINT_ERR("It might be better to look around here. %s:%d\n",
			     __func__, __LINE__);
			continue;
		}
		if (0 == memcmp(info->path, p_info->path, strlen(info->path))) {
			p_info->ctime.tv_sec = info->ctime.tv_sec;
			p_info->ctime.tv_nsec = info->ctime.tv_nsec;
		}
		if (info->logpath_idx >= p_info->logpath_idx) {
			list_add_tail(&info->s_list, cur);
			goto out;
		}
	}
	list_add_tail(&info->s_list, &__rdr_logpath_info_list);
out:
	return 0;
}

void rdr_add_logpath_list(const char *path, struct timespec *time)
{
	struct logpath_info_s *lp_info = NULL;
	u32 len;
	int ret;
	lp_info = kmalloc(sizeof(struct logpath_info_s), GFP_ATOMIC);

	if (lp_info == NULL) {
		BB_PRINT_ERR("kmalloc logpath_info_s faild.\n");
		return;
	}

	if (EOK != memset_s(lp_info, sizeof(struct logpath_info_s), 0, sizeof(struct logpath_info_s)))
	{
		BB_PRINT_ERR("%s():%d:memset_s fail!\n", __func__, __LINE__);
	}
	len = strlen(path);
	ret = memcpy_s(lp_info->path, sizeof(lp_info->path) - 1, path, len);
	if (ret < 0) {
		BB_PRINT_ERR("%s():%d:memcpy_s fail!\n", __func__, __LINE__);
		return;
	}
	lp_info->ctime.tv_sec = time->tv_sec;
	lp_info->ctime.tv_nsec = time->tv_nsec;
	lp_info->logpath_idx = rdr_count_logpath_idx(lp_info->path);

	rdr_check_logpath_repeat(lp_info);
	__rdr_add_logpath_list(lp_info);
}

int bbox_chown(const char *path, uid_t user, gid_t group, bool recursion)
{
	int fd = -1, bpos, count, ret = 0;
	char buf[1024];
	int bufsize;
	struct linux_dirent *d = NULL;
	char d_type;
	char fullname[PATH_MAXLEN];

	BB_PRINT_DBG("[%s], enter, path:%s\n", __func__, path);
	ret = (int)sys_chown((const char __user *)path, user, group);
	if (ret) {
		BB_PRINT_ERR("[%s], chown %s uid [%d] gid [%d] failed err [%d]!\n",
		     __func__, path, user, group, ret);
	}

	if (!recursion) {
		goto not_recursion;
	}

	fd = sys_open(path, O_RDONLY, DIR_LIMIT);
	if (fd < 0) {
		BB_PRINT_ERR("[%s], open %s fail, ret:%d\n", __func__, path,
			     fd);
		BB_PRINT_END();
		return -1;
	}

	for (count = 0; count < 1000; count++) {	/*防止死循环 */
		bufsize = sys_getdents(fd, (struct linux_dirent *)buf, 1024);
		if (bufsize == -1) {
			BB_PRINT_ERR("[%s], sys_getdents failed, ret [%d]\n",
				     __func__, bufsize);
			goto out;
		}

		if (bufsize == 0) {
			/*处理结束 */
			goto out;
		}

		for (bpos = 0; bpos < bufsize; bpos += d->d_reclen) {
			d = (struct linux_dirent *)(buf + bpos);
			d_type = *(buf + bpos + d->d_reclen - 1);
			snprintf(fullname, sizeof(fullname), "%s/%s", path,
				 d->d_name);

			if (d_type == DT_DIR
			    && strncmp(d->d_name, ".", sizeof("."))
			    && strncmp(d->d_name, "..", sizeof(".."))) {	/*处理目录，过滤"."和".."目录，否则会无限递归 */
				/*递归修改目录及子目录的属主及属组 */
				/* cppcheck-suppress * */
				ret = (int)bbox_chown((const char __user *)
						      fullname, user, group,
						      true);
				if (ret) {
					BB_PRINT_ERR("[%s], chown %s uid [%d] gid [%d] failed err [%d]!\n",
					     __func__, fullname, user, group,
					     ret);
				}
			} else if (d_type == DT_REG) {	/*处理文件 */
				ret = (int)sys_chown((const char __user *)
						     fullname, user, group);
				if (ret) {
					BB_PRINT_ERR("[%s], chown %s uid [%d] gid [%d] failed err [%d]!\n",
					     __func__, fullname, user, group,
					     ret);
				}
			}
		}
	}
	if (1000 == count) {
		BB_PRINT_ERR("[%s], [%s] failed, ret [%d]\n", __func__, path,
			     bufsize);
	}
out:
	if (fd >= 0) {
		sys_close(fd);
	}
not_recursion:
	return 0;
}

void rdr_print_all_logpath(void)
{
	int index = 1;
	struct logpath_info_s *p_module_ops = NULL;
	struct list_head *cur = NULL;
	struct list_head *next = NULL;

	BB_PRINT_START();
	list_for_each_safe(cur, next, &__rdr_logpath_info_list) {
		p_module_ops = list_entry(cur, struct logpath_info_s, s_list);
		if (p_module_ops == NULL) {
			BB_PRINT_ERR("It might be better to look around here. %s:%d\n",
			     __func__, __LINE__);
			continue;
		}
		BB_PRINT_PN("==========[%.2d]-start==========\n", index);
		BB_PRINT_PN(" path:    [%s]\n", p_module_ops->path);
		BB_PRINT_PN(" ctime:   [0x%llx]\n",
			     (u64) (p_module_ops->ctime.tv_sec));
		BB_PRINT_PN("==========[%.2d]-e n d==========\n", index);
		index++;
	}

	BB_PRINT_END();
	return;
}

char *known[] = {
	"",
};

char *ignore[] = {
	".",
	"..",
	PATH_MEMDUMP,
	"running_trace",
	"history.log",
	"reboot_times.log",
	"modem_log",
};

int rdr_dump_init(void *arg)
{
	int ret = 0;

	while (rdr_wait_partition("/data/lost+found", 1000) != 0)
		;

	/* 提前检查并设置版本信息 */
	(void)bbox_check_edition();

	ret = rdr_create_dir(PATH_ROOT);
	if (ret) {
		return ret;
	}

	/*根据权限要求，hisi_logs目录及子目录群组调整为root-system */
	ret = (int)bbox_chown((const char __user *)PATH_ROOT, ROOT_UID,
			    SYSTEM_GID, true);
	if (ret) {
		BB_PRINT_ERR("[%s], chown %s uid [%d] gid [%d] failed err [%d]!\n",
		     __func__, PATH_ROOT, ROOT_UID, SYSTEM_GID, ret);
	}
	create_hisi_diaginfo_log_file();
	return 0;
}

void rdr_dump_exit(void)
{
}

int rdr_check_logpath_legality(char *path)
{
	int ret = 1;		/*0 => ignore. !0 => process.*/
	int size;
	int index;

	size = sizeof(ignore) / sizeof(char *);
	for (index = 0; index < size; index++) {
		if (0 == strncmp(path, ignore[index], strlen(path))) {
			ret = -1;
			goto out;
		}
	}

	size = DATA_MAXLEN + TIME_MAXLEN + 1;
	for (index = 0; path[index] != '\0' && index < size; index++) {
		if (path[index] < '0' || path[index] > '9') { /*lint !e690*/
			if (index == DATA_MAXLEN && path[index] == '-') /*lint !e690*/
				continue;
			BB_PRINT_PN("invalid path [%s]\n", path);
			BB_PRINT_END();
			ret = 0;
			goto out;
		}
	}
out:
	return ret;
}

static inline int rdr_dir_size_for_directory(struct linux_dirent *d,
	bool recursion, u32 *size, struct kstat *stat, char *fullname, u32 fullnameLen)
{
	int ret;

	if (fullnameLen < PATH_MAXLEN) {
		BB_PRINT_ERR("rdr:fullnameLen is not correct\n");
	}

	ret = rdr_check_logpath_legality(d->d_name);
	if (ret == -1) {
		return -1;
	}
	if (!recursion && ret == 0) {
		BB_PRINT_ERR("check legality: invalid\n");
		if (!recursion) {
			if (rdr_rm_dir(fullname) < 0)
				BB_PRINT_ERR("%s(): failed to del %s.\n", __func__, fullname);
		}
		return -1;
	}

	if (recursion) {
		/* cppcheck-suppress * */
		(*size) += rdr_dir_size(fullname, PATH_MAXLEN, recursion);
	} else {
		rdr_add_logpath_list(d->d_name,
				     &(stat->ctime));
	}

	return 0;
}

int rdr_dir_size(char *path, u32 pathLen, bool recursion)
{
	/*DT_DIR, DT_REG */
	int fd = -1, bpos;
	char buf[1024];
	int bufsize;
	struct linux_dirent *d = NULL;
	char d_type;
	char fullname[PATH_MAXLEN];
	struct kstat stat;
	u32 size = 0;

	if (!path) {
		BB_PRINT_ERR("rdr:path is null\n");
		return size;
	}

	if (pathLen < PATH_MAXLEN) {
		BB_PRINT_ERR("rdr:len is not correct\n");
	}

	fd = sys_open(path, O_RDONLY, DIR_LIMIT);
	if (fd < 0) {
		BB_PRINT_ERR("rdr:%s(),open %s fail,r:%d\n", __func__, path,
			     fd);
		BB_PRINT_END();
		goto out;
	}

	if (EOK != memset_s(&stat,sizeof(struct kstat), 0, sizeof(struct kstat)))
	{
		BB_PRINT_ERR("%s():%d:memset_s fail!\n", __func__, __LINE__);
	}
	if (0 != vfs_stat(path, &stat)) {
		BB_PRINT_ERR("stat failed :\n");
	}
	size += stat.size;

	for (;;) {
		bufsize = sys_getdents(fd, (struct linux_dirent *)buf, 1024);
		/* NOTE: should that bufsize is negative be the judgement? */
		if (bufsize == -1) {
			BB_PRINT_ERR("rdr:%s():sys_getdents failed\n",
				     __func__);
			break;
		}

		if (bufsize == 0) {
			break;
		}

		for (bpos = 0; bpos < bufsize; bpos += d->d_reclen) {
			d = (struct linux_dirent *)(buf + bpos);
			d_type = *(buf + bpos + d->d_reclen - 1);
			memset(&stat, 0, sizeof(struct kstat));
			snprintf(fullname, sizeof(fullname), "%s/%s", path,
				 d->d_name);
			if (0 != vfs_stat(fullname, &stat)) {
				BB_PRINT_ERR("stat failed :[%s]\n", fullname);
			}
			if (d_type == DT_DIR) {
				if (rdr_dir_size_for_directory(d, recursion, &size, &stat, fullname, PATH_MAXLEN))
					continue;
			} else if (d_type == DT_REG) {
				size += stat.size;
			}
		}
	}
out:
	if (fd >= 0)
		sys_close(fd);

	return size;
}

static bool rdr_check_log_mark(u32 rdr_max_size, u32 rdr_max_logs)
{
	struct logpath_info_s *p_info = NULL;
	struct list_head *cur = NULL;
	struct list_head *next = NULL;
	u32 size = 0;
	u32 tmpsize = 0;
	char fullname[PATH_MAXLEN];
	bool ret = true;
	int ret_s;
	u32 rdr_log_nums = 0;

	mutex_lock(&__rdr_logpath_info_list_mutex);
	rdr_empty_logpath_list();
	rdr_update_history_log_buffer();
	size += rdr_dir_size(PATH_ROOT, PATH_MAXLEN, false);
	list_for_each_safe(cur, next, &__rdr_logpath_info_list) {
		p_info = list_entry(cur, struct logpath_info_s, s_list);
		if (p_info == NULL) {
			list_del(cur);
			BB_PRINT_ERR("It might be better to look around here. %s:%d\n",
			     __func__, __LINE__);
			continue;
		}

		if (EOK != memset_s(fullname, PATH_MAXLEN, 0, PATH_MAXLEN))
		{
			BB_PRINT_ERR("%s():%d:memset_s fail!\n", __func__, __LINE__);
		}
		ret_s = snprintf_s(fullname, PATH_MAXLEN, PATH_MAXLEN-1, "%s%s", PATH_ROOT,
			 p_info->path);
		if (unlikely(ret_s < 0)) {
			BB_PRINT_ERR("[%s], snprintf_s error!\n", __func__);
			break;
		}

		tmpsize = rdr_dir_size(fullname, PATH_MAXLEN, true);
		if ((tmpsize + size > rdr_max_size) || (++rdr_log_nums > rdr_max_logs)) {
			BB_PRINT_PN("over size: cur[0x%x], next[0x%x],max[0x%x], or over nums:cur[%u], max[%u]\n",
			     size, tmpsize, rdr_max_size, rdr_log_nums, rdr_max_logs);
			ret = false;
			break;
		} else {
			size += tmpsize;
		}
	}
	mutex_unlock(&__rdr_logpath_info_list_mutex);

	return ret;
}

bool rdr_check_log_rights(void)
{
	bool ret = true;
	u32 rdr_logs_low, rdr_logs_high, rdr_max_size;

	BB_PRINT_START();
	rdr_logs_low = rdr_get_lognum();
	rdr_max_size = rdr_get_logsize();

	ret = rdr_check_log_mark(rdr_max_size, rdr_logs_low);
	if (!ret) {
		BB_PRINT_ERR("%s():bbox timestamp logs are filled, clean thems.\n", __func__);
		rdr_count_size();

		rdr_logs_high = rdr_logs_low + 10;
		ret = rdr_check_log_mark(rdr_max_size, rdr_logs_high);
		if (!ret) {
			BB_PRINT_ERR("%s():bbox has no rights to save log!\n", __func__);
		}
	}
	BB_PRINT_END();

	return ret;
}

void rdr_count_size(void)
{
	struct logpath_info_s *p_info = NULL;
	struct list_head *cur = NULL;
	struct list_head *next = NULL;
	u32 size = 0;
	u32 tmpsize = 0;
	bool oversize = false;
	char fullname[PATH_MAXLEN];
	int ret;
	u32 rdr_max_logs, rdr_log_nums = 0;

	BB_PRINT_START();
	rdr_max_logs = rdr_get_lognum();

	mutex_lock(&__rdr_logpath_info_list_mutex);
	rdr_empty_logpath_list();
	rdr_update_history_log_buffer();
	size += rdr_dir_size(PATH_ROOT, PATH_MAXLEN, false);
	list_for_each_safe(cur, next, &__rdr_logpath_info_list) {
		p_info = list_entry(cur, struct logpath_info_s, s_list);
		if (p_info == NULL) {
			list_del(cur);
			BB_PRINT_ERR("It might be better to look around here. %s:%d\n",
			     __func__, __LINE__);
			continue;
		}

		if (EOK != memset_s(fullname,PATH_MAXLEN ,0, PATH_MAXLEN))
		{
			BB_PRINT_ERR("%s():%d:memset_s fail!\n", __func__, __LINE__);
		}
		snprintf(fullname, PATH_MAXLEN,"%s%s", PATH_ROOT,
			 p_info->path);

		if (oversize) {
			BB_PRINT_PN("over size: cur[0x%x], max[0x%llx]\n",
				     size, rdr_get_logsize());
			if (rdr_rm_dir(fullname)<0) {
				BB_PRINT_ERR("It might be better to look around here. %s:%d\n",
				    __func__, __LINE__);
			}
			list_del(cur);
			kfree(p_info);
			continue;
		}

		/* 递归检查目录大小，超过指定大小则删除之后的日志目录，同上if (oversize)。 */
		tmpsize = rdr_dir_size(fullname, PATH_MAXLEN, true);
		if ((tmpsize + size > rdr_get_logsize()) || (++rdr_log_nums > rdr_max_logs)) {
			oversize = true;
			BB_PRINT_PN("over size: cur[0x%x], next[0x%x],max[0x%llx], or over nums:cur[%u], max[%u]\n",
			     size, tmpsize, rdr_get_logsize(), rdr_log_nums, rdr_max_logs);
			if (rdr_rm_dir(fullname)<0) {
				BB_PRINT_ERR("It might be better to look around here. %s:%d\n",
				    __func__, __LINE__);
			}
			list_del(cur);
			kfree(p_info);
		} else {
			size += tmpsize;
		}
	}
	mutex_unlock(&__rdr_logpath_info_list_mutex);

	/*根据权限要求，hisi_logs目录及子目录群组调整为root-system */
	ret = (int)bbox_chown((const char __user *)PATH_ROOT, ROOT_UID,
			    SYSTEM_GID, true);
	if (ret) {
		BB_PRINT_ERR("[%s], chown %s uid [%d] gid [%d] failed err [%d]!\n",
		     __func__, PATH_ROOT, ROOT_UID, SYSTEM_GID, ret);
	}

	mutex_lock(&__rdr_logpath_info_list_mutex);
	rdr_print_all_logpath();
	mutex_unlock(&__rdr_logpath_info_list_mutex);
	BB_PRINT_END();
	return;
}

static int rdr_rm_file(const char *fullname)
{
	BB_PRINT_DBG("rdr:%s():delete file [%s]\n", __func__, fullname);
	return sys_unlink(fullname);
}

static int __rdr_rm_dir(const char *path)
{
	char *pdst = (char *)path;
	int ret = 0;
	BB_PRINT_PN("rdr:%s():delete path [%s]\n", __func__, path);

	while (*pdst)
		pdst++;
	pdst--;
	if (*pdst == '/')
		*pdst = '\0';
	ret = sys_rmdir(path);
	if (ret != 0)
		BB_PRINT_ERR("rdr:%s():del %s failed. ret[%d]\n", __func__,
			     path, ret);
	return ret;
}

static int rdr_rm_dir(const char *path)
{
	int fd = -1, bpos;
	char buf[1024];
	int bufsize;
	struct linux_dirent *d = NULL;
	char d_type;
	char fullname[PATH_MAXLEN];
	int ret = 0;

	if (!path) {
		BB_PRINT_ERR("rdr:path is null\n");
		return -1;
	}

	fd = sys_open(path, O_RDONLY, DIR_LIMIT);
	if (fd < 0) {
		BB_PRINT_ERR("rdr:%s(),open %s fail,r:%d\n", __func__, path,
			     fd);
		BB_PRINT_END();
		ret = -1;
		goto out;
	}

	for (;;) {
		bufsize = sys_getdents(fd, (struct linux_dirent *)buf, 1024);
		if (bufsize == -1) {
			BB_PRINT_ERR("rdr:%s():sys_getdents failed\n",
				     __func__);
			break;
		}

		if (bufsize == 0) {
			break;
		}

		for (bpos = 0; bpos < bufsize; bpos += d->d_reclen) {
			d = (struct linux_dirent *)(buf + bpos);
			d_type = *(buf + bpos + d->d_reclen - 1);
			snprintf(fullname, sizeof(fullname), "%s/%s", path,
				 d->d_name);
			if (d_type == DT_DIR) {
				if (rdr_check_logpath_legality(d->d_name) == -1) {
					continue;
				}
				/* cppcheck-suppress * */
				ret = rdr_rm_dir(fullname);
				BB_PRINT_DBG("rdr:%s():dir,fullname:[%s], ret:[%d]\n",
				     __func__, fullname, ret);
			} else if (d_type == DT_REG) {
				ret = rdr_rm_file(fullname);
				if (ret) {
					BB_PRINT_ERR("rdr:%s():failed to delete [%s], errno[%d]\n",
						  __func__, fullname, ret);
				}
			}
		}
	}
out:
	ret = __rdr_rm_dir(path);
	if (fd >= 0)
		sys_close(fd);
	return ret;
}

/*******************************************************************************
Function:       dataready_info_show
Description:    show g_dataready_flag
Input:          struct seq_file *m, void *v
Output:         NA
Return:         0:success;other:fail
********************************************************************************/
static int dataready_info_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_dataready_flag);
	return 0;
}

/*******************************************************************************
Function:       dataready_write_proc
Description:    write /proc/data-ready, for get the status of data_partition
Input:          file;buffer;count;data
Output:         NA
Return:         >0:success;other:fail
********************************************************************************/
ssize_t dataready_write_proc(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
	ssize_t ret = -EINVAL;
	char tmp;

	/*buffer must be '1' or '0', so count<=2 */
	if (count > 2)
		return ret;

	if (!buffer)
		return ret;

	/*should ignore character '\n' */
	if (copy_from_user(&tmp, buffer, sizeof(tmp))) {
		return -EFAULT;
	}

	if (tmp == '1')
		g_dataready_flag = DATA_READY;
	else if (tmp == '0')
		g_dataready_flag = DATA_NOT_READY;
	else
		BB_PRINT_ERR("%s():%d:input arg invalid[%c]\n", __func__, __LINE__, tmp);

	return 1;
}

/*******************************************************************************
Function:       dataready_open
Description:    open /proc/data-ready
Input:          inode;file
Output:         NA
Return:         0:success;other:fail
********************************************************************************/
static int dataready_open(struct inode *inode, struct file *file)
{
	if (!file)
		return -EFAULT;

	return single_open(file, dataready_info_show, NULL);
}


static const struct file_operations dataready_proc_fops = {
	.open    	= dataready_open,
	.read   	= seq_read,
	.write  	= dataready_write_proc,
	.llseek 	= seq_lseek,
	.release	= single_release,
};

/*******************************************************************************
Function:       dataready_proc_init
Description:    create /proc/data-ready
Input:          NA
Output:         NA
Return:         0:success;-1:fail
********************************************************************************/
static int __init dataready_proc_init(void)
{
	struct proc_dir_entry *proc_dir_entry;

	proc_dir_entry = proc_create(DATAREADY_NAME,
		FILE_LIMIT,
		NULL,
		&dataready_proc_fops);
	if (!proc_dir_entry) {
		BB_PRINT_ERR("proc_create DATAREADY_NAME fail\n");
		return -1;
	}

	return 0;
}

module_init(dataready_proc_init);
