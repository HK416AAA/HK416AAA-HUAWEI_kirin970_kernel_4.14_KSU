

#ifndef __OAL_WINDOWS_WORKQUEUE_H__
#define __OAL_WINDOWS_WORKQUEUE_H__

/* STRUCT定义 */
typedef struct {
    oal_uint32 ul_resv;
} oal_workqueue_stru;

typedef struct {
    oal_uint32 ul_resv;
} oal_work_stru;

typedef struct {
    oal_work_stru work;
} oal_delayed_work;

/* 宏定义 */
#define OAL_INIT_WORK(_p_work, _p_func)
#define OAL_INIT_DELAYED_WORK(_work, _func)

/*lint -e528*/
static oal_workqueue_stru workqueue;
#define OAL_CREATE_SINGLETHREAD_WORKQUEUE(_name) &workqueue
#define oal_create_workqueue(name)               &workqueue

/*
 * 函 数 名  : oal_create_singlethread_workqueue
 * 功能描述  : 创建一个单线程的工作队列
 */
OAL_STATIC OAL_INLINE oal_workqueue_stru *oal_create_singlethread_workqueue(const oal_int8 *pc_workqueue_name)
{
    return 0;
}

/*
 * 函 数 名  : oal_destroy_workqueue
 * 功能描述  : 销毁工作队列
 */
OAL_STATIC OAL_INLINE oal_void oal_destroy_workqueue(oal_workqueue_stru *pst_workqueue)
{
    return;
}

/*
 * 函 数 名  : oal_queue_work
 * 功能描述  : 添加一个任务到工作队列
 */
OAL_STATIC OAL_INLINE oal_int32 oal_queue_work(oal_workqueue_stru *pst_workqueue, oal_work_stru *pst_work)
{
    return 0;
}

/*
 * queue_delayed_work - queue work on a workqueue after delay
 * @wq: workqueue to use
 * @dwork: delayable work to queue
 * @delay: number of jiffies to wait before queueing
 * Equivalent to queue_delayed_work_on() but tries to use the local CPU.
 */
OAL_STATIC OAL_INLINE oal_int32 oal_queue_delayed_work(oal_workqueue_stru *pst_workqueue,
                                                       oal_delayed_work *pst_work, oal_ulong delay)
{
    return 0;
}

/*
 * queue_delayed_work_on - queue work on specific CPU after delay
 * @cpu: CPU number to execute work on
 * @wq: workqueue to use
 * @dwork: work to queue
 * @delay: number of jiffies to wait before queueing
 * Returns %false if @work was already on a queue, %true otherwise.  If
 * @delay is zero and @dwork is idle, it will be scheduled for immediate
 */
OAL_STATIC OAL_INLINE oal_int32 oal_queue_delayed_work_on(oal_int32 cpu, oal_workqueue_stru *pst_workqueue,
                                                          oal_delayed_work *pst_work, oal_ulong delay)
{
    return 0;
}

/*
 * 函 数 名  : oal_queue_delayed_system_work
 * 功能描述  : queue work on system wq after delay
 * 输入参数  : @dwork: delayable work to queue
 *             @delay: number of jiffies to wait before queueing
 */
OAL_STATIC OAL_INLINE oal_int32 oal_queue_delayed_system_work(oal_delayed_work *pst_work, oal_ulong delay)
{
    return 0;
}

OAL_STATIC OAL_INLINE oal_int32 oal_work_is_busy(oal_work_stru *pst_delay_work)
{
    return 1;
}

OAL_STATIC OAL_INLINE oal_int32 oal_cancel_delayed_work_sync(oal_delayed_work *pst_delay_work)
{
    return 0;
}

OAL_STATIC OAL_INLINE oal_int32 oal_cancel_work_sync(oal_work_stru *pst_work)
{
    return 0;
}

#endif /* end of oal_workqueue.h */
