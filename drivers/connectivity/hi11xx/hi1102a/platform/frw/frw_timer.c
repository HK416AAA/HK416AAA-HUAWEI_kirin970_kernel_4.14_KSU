

/* 头文件包含 */
#include "frw_timer.h"
#include "frw_main.h"
#include "frw_task.h"

#undef THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_FRW_TIMER_C

/* 全局变量定义 */
oal_dlist_head_stru ast_timer_list[WLAN_FRW_MAX_NUM_CORES];
oal_spin_lock_stru timer_list_spinlock[WLAN_FRW_MAX_NUM_CORES]; /*lint !e88 */
oal_timer_list_stru st_timer[WLAN_FRW_MAX_NUM_CORES];
oal_uint32 stop_timestamp = 0;
oal_uint32 restart_timestamp = 0;
oal_uint32 max_deep_sleep_time = 0;  // 记录平台最大睡眠时间
oal_uint32 need_restart = OAL_FALSE;
oal_uint32 frw_timer_start_stamp[WLAN_FRW_MAX_NUM_CORES] = {0};  // 维测信号，用来记录下一次软中断定时器的启动时间

#if defined(_PRE_DEBUG_MODE) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)

oal_uint32 os_time = 0;
frw_timeout_track_stru timeout_track[FRW_TIMEOUT_TRACK_NUM];
oal_uint8 timeout_track_idx = 0;
#endif
/* 函数实现 */
OAL_STATIC OAL_INLINE oal_void __frw_timer_immediate_destroy_timer(oal_uint32 ul_file_id,
                                                                   oal_uint32 ul_line_num,
                                                                   frw_timeout_stru *pst_timeout);

/*
 * 函 数 名  : frw_timer_init
 * 功能描述  : 初始化定时器
 */
oal_void frw_timer_init(oal_uint32 ul_delay, oal_timer_func p_func, oal_ulong arg)
{
    oal_uint32 ul_core_id;

    if (OAL_UNLIKELY(p_func == NULL)) {
        OAL_WARN_ON(1);
        return;
    }

    for (ul_core_id = 0; ul_core_id < WLAN_FRW_MAX_NUM_CORES; ul_core_id++) {
        oal_dlist_init_head(&ast_timer_list[ul_core_id]);
        oal_spin_lock_init(&timer_list_spinlock[ul_core_id]);

        oal_timer_init(&st_timer[ul_core_id], ul_delay, p_func, arg);
        frw_timer_start_stamp[ul_core_id] = 0;
    }

#if defined(_PRE_DEBUG_MODE) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
    memset_s(timeout_track, OAL_SIZEOF(frw_timeout_track_stru) * FRW_TIMEOUT_TRACK_NUM,
             0, OAL_SIZEOF(frw_timeout_track_stru) * FRW_TIMEOUT_TRACK_NUM);
#endif
}

/*
 * 函 数 名  : frw_timer_exit
 * 功能描述  : 定时器退出函数
 */
oal_void frw_timer_exit(oal_void)
{
    oal_uint32 ul_core_id;

    for (ul_core_id = 0; ul_core_id < WLAN_FRW_MAX_NUM_CORES; ul_core_id++) {
        oal_timer_delete_sync(&st_timer[ul_core_id]);
        frw_timer_start_stamp[ul_core_id] = 0;
    }
}

/*
 * 函 数 名  : frw_timer_restart
 * 功能描述  : 重启sys timer
 */
oal_void frw_timer_restart(oal_void)
{
    oal_uint32 ul_core_id;

    /* 重启定时器 */
    if (need_restart == OAL_FALSE) {
        return;
    }

    for (ul_core_id = 0; ul_core_id < WLAN_FRW_MAX_NUM_CORES; ul_core_id++) {
        oal_timer_start(&st_timer[ul_core_id], FRW_TIMER_DEFAULT_TIME);
    }
    need_restart = OAL_FALSE;
}

/*
 * 函 数 名  : frw_timer_stop
 * 功能描述  : 停止sys timer
 */
oal_void frw_timer_stop(oal_void)
{
    oal_uint32 ul_core_id;

    /* stop frw sys timer,record the stop time for restart timer to recalculate timestamp */
    stop_timestamp = (oal_uint32)OAL_TIME_GET_STAMP_MS();

    for (ul_core_id = 0; ul_core_id < WLAN_FRW_MAX_NUM_CORES; ul_core_id++) {
        oal_timer_delete(&st_timer[ul_core_id]);
        frw_timer_start_stamp[ul_core_id] = 0;
    }

    need_restart = OAL_TRUE;
}

/*
 * 函 数 名  : frw_timer_dump
 * 功能描述  : 将所有定时器dump出来
 */
OAL_STATIC oal_void frw_timer_dump(oal_uint32 ul_core_id)
{
    const oal_uint32 ul_dump_mem_size = 64;
    oal_dlist_head_stru *pst_timeout_entry;
    frw_timeout_stru *pst_timeout_element;

    pst_timeout_entry = ast_timer_list[ul_core_id].pst_next;
    while (pst_timeout_entry != &ast_timer_list[ul_core_id]) {
        if (pst_timeout_entry == OAL_PTR_NULL) {
            OAM_ERROR_LOG0(0, OAM_SF_FRW, "{frw_timer_dump:: time broken break}");
            break;
        }

        OAM_ERROR_LOG2(0, OAM_SF_FRW, "{frw_timer_dump:: pst_timeout_entry[0x%x], ast_timer_list addr[0x%x]!}",
            (uintptr_t)pst_timeout_entry, (uintptr_t)ast_timer_list);
        if (pst_timeout_entry->pst_next == NULL) {
            /* If next is null,
             the pst_timeout_entry stru maybe released or memset */
            OAM_ERROR_LOG0(0, OAM_SF_FRW, "{frw_timer_dump:: pst_next is null,dump mem}");
            oal_print_hex_dump(((oal_uint8 *)pst_timeout_entry) - ul_dump_mem_size, ul_dump_mem_size * 2,
                               HEX_DUMP_GROUP_SIZE, "timer broken: "); /* dump前后64字节长度内存 */
        }

        pst_timeout_element = OAL_DLIST_GET_ENTRY(pst_timeout_entry, frw_timeout_stru, st_entry);
        pst_timeout_entry = pst_timeout_entry->pst_next;
        OAM_ERROR_LOG3(0, OAM_SF_FRW, "{frw_timer_dump:: time_stamp[0x%x] timeout[%d]  enabled[%d]}",
                       pst_timeout_element->ul_time_stamp,
                       pst_timeout_element->ul_timeout,
                       pst_timeout_element->en_is_enabled);
        OAM_ERROR_LOG3(0, OAM_SF_FRW, "{frw_timer_dump:: module_id[%d] file_id[%d] line_num[%d]}",
                       pst_timeout_element->en_module_id,
                       pst_timeout_element->ul_file_id,
                       pst_timeout_element->ul_line_num);
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#ifdef CONFIG_PRINTK
        if (pst_timeout_element->p_func) {
            printk(KERN_ERR "frw_timer_dump func : %pF\n", pst_timeout_element->p_func);
        }
#endif
#endif
    }
}

/*
 * 函 数 名  : frw_timer_timeout_proc
 * 功能描述  : 遍历timer链表执行到期超时函数
 */
oal_uint32 frw_timer_timeout_proc(frw_event_mem_stru *pst_timeout_event)
{
    oal_dlist_head_stru *pst_timeout_entry;
    frw_timeout_stru *pst_timeout_element;
    oal_uint32 ul_present_time;
    oal_uint32 ul_end_time;
    oal_uint32 ul_runtime;
    oal_uint32 ul_core_id;
    oal_uint32 ul_runtime_func_start = 0;
    oal_uint32 ul_runtime_func_end = 0;
    oal_uint32 ul_endtime_func = 0;
    oal_uint32 ul_frw_timer_start;

#if defined(_PRE_DEBUG_MODE) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
    oal_uint32 ul_sum_time = 0;
    oal_uint32 ul_start_time = 0;

    if (en_event_track_switch == OAL_TRUE) {
        memset_s(&timeout_track[timeout_track_idx], OAL_SIZEOF(frw_timeout_track_stru),
                 0, OAL_SIZEOF(frw_timeout_track_stru));
    }
#endif

    if (OAL_UNLIKELY(pst_timeout_event == OAL_PTR_NULL)) {
        OAM_ERROR_LOG0(0, OAM_SF_FRW, "{frw_timer_timeout_proc:: pst_timeout_event is null ptr!}");
        return OAL_ERR_CODE_PTR_NULL;
    }

    ul_present_time = (oal_uint32)OAL_TIME_GET_STAMP_MS();
    ul_core_id = OAL_GET_CORE_ID();

    /* 执行超时定时器 */
    oal_spin_lock_bh(&timer_list_spinlock[ul_core_id]);
    pst_timeout_entry = ast_timer_list[ul_core_id].pst_next;

    while (pst_timeout_entry != &ast_timer_list[ul_core_id]) {
        if (pst_timeout_entry == OAL_PTR_NULL) {
            OAM_ERROR_LOG0(0, OAM_SF_FRW, "{frw_timer_timeout_proc:: the timer list is broken! }");
            frw_timer_dump(ul_core_id);
            break;  //lint !e527
        }

        pst_timeout_element = OAL_DLIST_GET_ENTRY(pst_timeout_entry, frw_timeout_stru, st_entry);
        pst_timeout_element->ul_curr_time_stamp = ul_present_time;

        /* 一个定时器超时处理函数中创建新的定时器，如果定时器超时，则将相应的定时器进行删除，取消en_is_deleting标记; */
        if (frw_time_after(ul_present_time, pst_timeout_element->ul_time_stamp)) {
            /* 删除超时定时器，如果是周期定时器，则将其再添加进去:delete first,then add periodic_timer */
            pst_timeout_element->en_is_registerd = OAL_FALSE;
            oal_dlist_delete_entry(&pst_timeout_element->st_entry);

            if ((pst_timeout_element->en_is_periodic == OAL_TRUE) ||
                (pst_timeout_element->en_is_enabled == OAL_FALSE)) {
                pst_timeout_element->ul_time_stamp = ul_present_time + pst_timeout_element->ul_timeout;
                pst_timeout_element->en_is_registerd = OAL_TRUE;
                frw_timer_add_timer(pst_timeout_element);
            }

#if defined(_PRE_DEBUG_MODE) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
            ul_start_time = oal_5115timer_get_10ns();
#endif
            ul_runtime_func_start = (oal_uint32)OAL_TIME_GET_STAMP_MS();
            if (pst_timeout_element->en_is_enabled) {
                oal_spin_unlock_bh(&timer_list_spinlock[ul_core_id]);
                pst_timeout_element->p_func(pst_timeout_element->p_timeout_arg);
                oal_spin_lock_bh(&timer_list_spinlock[ul_core_id]);
            }

            ul_endtime_func = (oal_uint32)OAL_TIME_GET_STAMP_MS();
            ul_runtime_func_end = (oal_uint32)OAL_TIME_GET_RUNTIME(ul_runtime_func_start, ul_endtime_func);
            if (ul_runtime_func_end >= (oal_uint32)OAL_JIFFIES_TO_MSECS(2)) {
                OAM_WARNING_LOG4(0, OAM_SF_FRW,
                                 "{frw_timer_timeout_proc:: fileid=%u, linenum=%u, moduleid=%u, runtime=%u}",
                                 pst_timeout_element->ul_file_id,
                                 pst_timeout_element->ul_line_num,
                                 pst_timeout_element->en_module_id,
                                 ul_runtime_func_end);
            }

#if defined(_PRE_DEBUG_MODE) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)

            if (en_event_track_switch == OAL_TRUE) {
                timeout_track[timeout_track_idx].st_timer_track[timeout_track[timeout_track_idx].uc_timer_cnt].ul_file_id = pst_timeout_element->ul_file_id;
                timeout_track[timeout_track_idx].st_timer_track[timeout_track[timeout_track_idx].uc_timer_cnt].ul_line_num = pst_timeout_element->ul_line_num;
                timeout_track[timeout_track_idx].st_timer_track[timeout_track[timeout_track_idx].uc_timer_cnt].ul_execute_time = ul_start_time - oal_5115timer_get_10ns();
                ul_sum_time += timeout_track[timeout_track_idx].st_timer_track[timeout_track[timeout_track_idx].uc_timer_cnt].ul_execute_time;
            }
#endif
        } else {
            break;
        }

#if defined(_PRE_DEBUG_MODE) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)

        if (en_event_track_switch == OAL_TRUE) {
            OAL_INCR(timeout_track[timeout_track_idx].uc_timer_cnt, FRW_TIMER_TRACK_NUM);
        }
#endif

        pst_timeout_entry = ast_timer_list[ul_core_id].pst_next;
    }

    /* 获得链表的最小超时时间，重启定时器 */
    if (oal_dlist_is_empty(&ast_timer_list[ul_core_id]) == OAL_FALSE) {
        pst_timeout_entry = ast_timer_list[ul_core_id].pst_next;
        pst_timeout_element = OAL_DLIST_GET_ENTRY(pst_timeout_entry, frw_timeout_stru, st_entry);
        ul_present_time = (oal_uint32)OAL_TIME_GET_STAMP_MS();

        if (frw_time_after(pst_timeout_element->ul_time_stamp, ul_present_time)) {
            ul_frw_timer_start = (oal_uint32)OAL_TIME_GET_RUNTIME(ul_present_time, pst_timeout_element->ul_time_stamp);

            frw_timer_start_stamp[ul_core_id] = pst_timeout_element->ul_time_stamp;
        } else {
            ul_frw_timer_start = FRW_TIMER_DEFAULT_TIME;

            frw_timer_start_stamp[ul_core_id] = (ul_present_time + FRW_TIMER_DEFAULT_TIME);
        }

        oal_timer_start(&st_timer[ul_core_id], ul_frw_timer_start);
    } else {
        frw_timer_start_stamp[ul_core_id] = 0;
    }

    oal_spin_unlock_bh(&timer_list_spinlock[ul_core_id]);

#if defined(_PRE_DEBUG_MODE) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
    if (en_event_track_switch == OAL_TRUE) {
        if ((ul_sum_time * 10) > 300000) {
            OAL_INCR(timeout_track_idx, FRW_TIMEOUT_TRACK_NUM);
        }
    }
#endif

    ul_end_time = (oal_uint32)OAL_TIME_GET_STAMP_MS();
    ul_runtime = (oal_uint32)OAL_TIME_GET_RUNTIME(ul_present_time, ul_end_time);
    /* 同device侧检测日志时限一致 */
    if (ul_runtime > (oal_uint32)OAL_JIFFIES_TO_MSECS(2)) {
        OAM_WARNING_LOG1(0, OAM_SF_FRW,
                         "{frw_timer_timeout_proc:: timeout process exucte time too long time[%d]}",
                         ul_runtime);
    }

    return OAL_SUCC;
}

/*
 * 函 数 名  : frw_timer_add_in_order
 * 功能描述  : 向链表中按从小到大的顺序插入节点
 * 输入参数  : pst_new: 要插入的新节点
 *             pst_head: 链表头指针
 */
OAL_STATIC oal_void frw_timer_add_in_order(oal_dlist_head_stru *pst_new, oal_dlist_head_stru *pst_head)
{
    oal_dlist_head_stru *pst_timeout_entry = NULL;
    frw_timeout_stru *pst_timeout_element = NULL;
    frw_timeout_stru *pst_timeout_element_new;
    oal_uint32 ul_core_id;

    pst_timeout_element_new = OAL_DLIST_GET_ENTRY(pst_new, frw_timeout_stru, st_entry);

    ul_core_id = OAL_GET_CORE_ID();

    /* 搜索链表，查找第一个比pst_timeout_element_new->ul_time_stamp大的位置 */
    if (pst_head != NULL) {
        pst_timeout_entry = pst_head->pst_next;

        while (pst_timeout_entry != pst_head) {
            if (pst_timeout_entry == OAL_PTR_NULL) {
                OAM_ERROR_LOG0(0, OAM_SF_FRW, "{Driver frw_timer_add_in_order:: the timer list is broken! }");
                OAM_ERROR_LOG3(0, OAM_SF_FRW,
                               "{new frw_timer_add_in_order:: time_stamp[0x%x] timeout[%d]  enabled[%d]}",
                               pst_timeout_element_new->ul_time_stamp,
                               pst_timeout_element_new->ul_timeout,
                               pst_timeout_element_new->en_is_enabled);
                OAM_ERROR_LOG3(0, OAM_SF_FRW,
                               "{new frw_timer_add_in_order:: module_id[%d] file_id[%d] line_num[%d]}",
                               pst_timeout_element_new->en_module_id,
                               pst_timeout_element_new->ul_file_id,
                               pst_timeout_element_new->ul_line_num);
                frw_timer_dump(ul_core_id);
                break;
            }

            pst_timeout_element = OAL_DLIST_GET_ENTRY(pst_timeout_entry, frw_timeout_stru, st_entry);
            if (frw_time_after(pst_timeout_element->ul_time_stamp, pst_timeout_element_new->ul_time_stamp)) {
                break;
            }

            pst_timeout_entry = pst_timeout_entry->pst_next;
        }

        if ((pst_timeout_entry != NULL) && (pst_timeout_entry->pst_prev != NULL)) {
            oal_dlist_add(pst_new, pst_timeout_entry->pst_prev, pst_timeout_entry);
        } else {
            OAM_ERROR_LOG0(0, OAM_SF_FRW, "{Driver frw_timer_add_in_order::timer list is broken !}");
            frw_timer_dump(ul_core_id);
#ifdef _PRE_WLAN_REPORT_WIFI_ABNORMAL
            OAL_REPORT_WIFI_ABNORMAL(OAL_ABNORMAL_FRW_TIMER_BROKEN, OAL_ACTION_REBOOT, 0, 0);
#endif
        }
    }
}

/*
 * 函 数 名  : frw_timer_add_timer
 * 功能描述  : 删除定时器
 */
oal_void frw_timer_add_timer(frw_timeout_stru *pst_timeout)
{
    oal_int32 l_val;

    if (pst_timeout == OAL_PTR_NULL) {
        OAM_ERROR_LOG0(0, OAM_SF_FRW, "{frw_timer_add_timer:: OAL_PTR_NULL == pst_timeout}");
        return;
    }

    if (oal_dlist_is_empty(&ast_timer_list[pst_timeout->ul_core_id]) == OAL_TRUE) {
        frw_timer_start_stamp[pst_timeout->ul_core_id] = 0;
    }

    /* 将Frw的无序链表改为有序 */
    frw_timer_add_in_order(&pst_timeout->st_entry, &ast_timer_list[pst_timeout->ul_core_id]);

    l_val = frw_time_after(frw_timer_start_stamp[pst_timeout->ul_core_id], pst_timeout->ul_time_stamp);
    if ((frw_timer_start_stamp[pst_timeout->ul_core_id] == 0) || (l_val > 0)) {
        oal_timer_start(&st_timer[pst_timeout->ul_core_id], pst_timeout->ul_timeout);
        frw_timer_start_stamp[pst_timeout->ul_core_id] = pst_timeout->ul_time_stamp;
    }

    return;
}

/*
 * 函 数 名  : frw_timer_create_timer
 * 功能描述  : 启动定时器
 * 输入参数  : en_is_periodic: 该定时器是否需要周期循环
 *             en_module_id: 模块 ID维测用
 *             p_timeout_arg :定时器超时处理函数需要的入参
 *             us_timeout: 超时时间
 */
oal_void frw_timer_create_timer(oal_uint32 ul_file_id,
                                oal_uint32 ul_line_num,
                                frw_timeout_stru *pst_timeout,
                                frw_timeout_func p_timeout_func,
                                oal_uint32 ul_timeout,
                                oal_void *p_timeout_arg,
                                oal_bool_enum_uint8 en_is_periodic,
                                oam_module_id_enum_uint16 en_module_id,
                                oal_uint32 ul_core_id)
{
    if (pst_timeout == OAL_PTR_NULL) {
        OAM_ERROR_LOG0(0, OAM_SF_FRW, "{frw_timer_create_timer:: OAL_PTR_NULL == pst_timeout}");
        return;
    }

    if (ul_timeout == 0 && en_is_periodic == OAL_TRUE) {
        OAM_ERROR_LOG4(0, OAM_SF_FRW,
                       "{frw_timer_create_timer::timeout value invalid! fileid: %d, line: %d, module: %d, core: %d}",
                       ul_file_id, ul_line_num, en_module_id, ul_core_id);
        return;
    }

#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
    pst_timeout->ul_core_id = 0;
#else
    pst_timeout->ul_core_id = ul_core_id;
#endif

    oal_spin_lock_bh(&timer_list_spinlock[pst_timeout->ul_core_id]);

    pst_timeout->p_func = p_timeout_func;
    pst_timeout->p_timeout_arg = p_timeout_arg;
    pst_timeout->ul_timeout = ul_timeout;
    pst_timeout->ul_time_stamp = (oal_uint32)OAL_TIME_GET_STAMP_MS() + ul_timeout;
    pst_timeout->en_is_periodic = en_is_periodic;
    pst_timeout->en_module_id = en_module_id;
    pst_timeout->ul_file_id = ul_file_id;
    pst_timeout->ul_line_num = ul_line_num;
    pst_timeout->en_is_enabled = OAL_TRUE; /* 默认使能 */

    if (pst_timeout->en_is_registerd != OAL_TRUE) {
        pst_timeout->en_is_registerd = OAL_TRUE; /* 默认注册 */
        frw_timer_add_timer(pst_timeout);
    } else {
        oal_dlist_delete_entry(&pst_timeout->st_entry);
        frw_timer_add_timer(pst_timeout);
    }

    oal_spin_unlock_bh(&timer_list_spinlock[pst_timeout->ul_core_id]);

    return;
}

/*
 * 函 数 名  : __frw_timer_immediate_destroy_timer
 * 功能描述  : 立即删除定时器，无锁
 */
OAL_STATIC OAL_INLINE oal_void __frw_timer_immediate_destroy_timer(oal_uint32 ul_file_id,
                                                                   oal_uint32 ul_line_num,
                                                                   frw_timeout_stru *pst_timeout)
{
    if (pst_timeout->st_entry.pst_prev == OAL_PTR_NULL || pst_timeout->st_entry.pst_next == OAL_PTR_NULL) {
        return;
    }

    if (pst_timeout->en_is_registerd == OAL_FALSE) {
        OAM_WARNING_LOG0(0, OAM_SF_FRW,
                         "{frw_timer_immediate_destroy_timer::This timer is not enabled it should not be deleted}");

        return;
    }

    pst_timeout->en_is_enabled = OAL_FALSE;
    pst_timeout->en_is_registerd = OAL_FALSE;

    oal_dlist_delete_entry(&pst_timeout->st_entry);

    if (oal_dlist_is_empty(&ast_timer_list[pst_timeout->ul_core_id]) == OAL_TRUE) {
        frw_timer_start_stamp[pst_timeout->ul_core_id] = 0;
    }
}

/*
 * 函 数 名  : frw_timer_immediate_destroy_timer
 * 功能描述  : 立即删除定时器
 */
oal_void frw_timer_immediate_destroy_timer(oal_uint32 ul_file_id,
                                           oal_uint32 ul_line_num,
                                           frw_timeout_stru *pst_timeout)
{
    oal_spin_lock_bh(&timer_list_spinlock[pst_timeout->ul_core_id]);
    __frw_timer_immediate_destroy_timer(ul_file_id, ul_line_num, pst_timeout);
    oal_spin_unlock_bh(&timer_list_spinlock[pst_timeout->ul_core_id]);
}

/*
 * 函 数 名  : frw_timer_restart_timer
 * 功能描述  : 重启定时器
 */
oal_void frw_timer_restart_timer(frw_timeout_stru *pst_timeout, oal_uint32 ul_timeout,
                                 oal_bool_enum_uint8 en_is_periodic)
{
    if (pst_timeout == OAL_PTR_NULL) {
        OAM_ERROR_LOG0(0, OAM_SF_FRW, "{frw_timer_restart_timer:: OAL_PTR_NULL == pst_timeout}");
        return;
    }
    /* 删除当前定时器 */
    if (pst_timeout->st_entry.pst_prev == OAL_PTR_NULL || pst_timeout->st_entry.pst_next == OAL_PTR_NULL) {
        OAM_ERROR_LOG4(0, OAM_SF_FRW,
                       "{frw_timer_restart_timer::This timer has been deleted!file_id=%d,line=%d,core=%d,mod=%d}",
                       pst_timeout->ul_file_id, pst_timeout->ul_line_num,
                       pst_timeout->ul_core_id, pst_timeout->en_module_id);
        return;
    }

    if (pst_timeout->en_is_registerd == OAL_FALSE) {
        OAM_ERROR_LOG4(0, OAM_SF_FRW,
                       "{frw_timer_restart_timer::This timer is not registerd!file_id=%d,line=%d,core=%d,mod=%d}",
                       pst_timeout->ul_file_id, pst_timeout->ul_line_num,
                       pst_timeout->ul_core_id, pst_timeout->en_module_id);
        return;
    }

    oal_spin_lock_bh(&timer_list_spinlock[pst_timeout->ul_core_id]);
    oal_dlist_delete_entry(&pst_timeout->st_entry);

    pst_timeout->ul_time_stamp = (oal_uint32)OAL_TIME_GET_STAMP_MS() + ul_timeout;
    pst_timeout->ul_timeout = ul_timeout;
    pst_timeout->en_is_periodic = en_is_periodic;
    pst_timeout->en_is_enabled = OAL_TRUE;

    frw_timer_add_timer(pst_timeout);
    oal_spin_unlock_bh(&timer_list_spinlock[pst_timeout->ul_core_id]);
}

/*
 * 函 数 名  : frw_timer_stop_timer
 * 功能描述  : 停止定时器
 */
oal_void frw_timer_stop_timer(frw_timeout_stru *pst_timeout)
{
    if (pst_timeout == OAL_PTR_NULL) {
        OAM_ERROR_LOG0(0, OAM_SF_FRW, "{frw_timer_stop_timer:: OAL_PTR_NULL == pst_timeout}");
        return;
    }

    pst_timeout->en_is_enabled = OAL_FALSE;
}

oal_uint8 timer_pause = OAL_FALSE;
#if defined(_PRE_FRW_TIMER_BIND_CPU) && defined(CONFIG_NR_CPUS)
oal_uint32 frw_timer_cpu_count[CONFIG_NR_CPUS] = {0};
#endif

/*
 * 函 数 名  : frw_timer_timeout_proc_event
 * 功能描述  : 15毫秒定时中断处理
 */
oal_void frw_timer_timeout_proc_event(oal_ulong arg)
{
    frw_event_mem_stru *pst_event_mem;
    frw_event_stru *pst_event;
    oal_uint32 ul_core_id = 0;

#if defined(_PRE_DEBUG_MODE) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
    oal_uint32 ul_time = 0;

    if (en_event_track_switch == OAL_TRUE) {
        if (os_time == 0) {
            os_time = oal_5115timer_get_10ns();
        } else {
            ul_time = oal_5115timer_get_10ns();
            timeout_track[timeout_track_idx].ul_os_timer_interval = os_time - ul_time;
            os_time = ul_time;
        }
    }
#endif

#if defined(_PRE_FRW_TIMER_BIND_CPU) && defined(CONFIG_NR_CPUS)
    do {
        oal_uint32 cpu_id = smp_processor_id();
        if (cpu_id < CONFIG_NR_CPUS) {
            frw_timer_cpu_count[cpu_id]++;
        }
    } while (0);
#endif

    if (timer_pause == OAL_TRUE) {
        return;
    }

    /*lint -e539*/ /*lint -e830*/
#ifdef _PRE_WLAN_FEATURE_SMP_SUPPORT
    for (ul_core_id = 0; ul_core_id < WLAN_FRW_MAX_NUM_CORES; ul_core_id++) {
        if (frw_task_get_state(ul_core_id)) {
#endif
            pst_event_mem = FRW_EVENT_ALLOC(OAL_SIZEOF(frw_event_stru));
            /* 返回值检查 */
            if (OAL_UNLIKELY(pst_event_mem == OAL_PTR_NULL)) {
                /* 重启定时器 */
#ifdef _PRE_FRW_TIMER_BIND_CPU
                oal_timer_start_on(&st_timer[ul_core_id], FRW_TIMER_DEFAULT_TIME, 0);
#else
                oal_timer_start(&st_timer[ul_core_id], FRW_TIMER_DEFAULT_TIME);
#endif
                OAM_ERROR_LOG0(0, OAM_SF_FRW, "{frw_timer_timeout_proc_event:: FRW_EVENT_ALLOC failed!}");
                return;
            }

            pst_event = frw_get_event_stru(pst_event_mem);

            /* 填充事件头 */
            FRW_FIELD_SETUP((&pst_event->st_event_hdr), en_type, (FRW_EVENT_TYPE_TIMEOUT));
            FRW_FIELD_SETUP((&pst_event->st_event_hdr), uc_sub_type, (FRW_TIMEOUT_TIMER_EVENT));
            FRW_FIELD_SETUP((&pst_event->st_event_hdr), us_length, (WLAN_MEM_EVENT_SIZE1));
            FRW_FIELD_SETUP((&pst_event->st_event_hdr), en_pipeline, (FRW_EVENT_PIPELINE_STAGE_0));
            FRW_FIELD_SETUP((&pst_event->st_event_hdr), uc_chip_id, (0));
            FRW_FIELD_SETUP((&pst_event->st_event_hdr), uc_device_id, (0));
            FRW_FIELD_SETUP((&pst_event->st_event_hdr), uc_vap_id, (0));

            /* 抛事件 */
#ifdef _PRE_WLAN_FEATURE_SMP_SUPPORT
            frw_event_post_event(pst_event_mem, ul_core_id);
#else
            frw_event_dispatch_event(pst_event_mem);
#endif
            FRW_EVENT_FREE(pst_event_mem);
#ifdef _PRE_WLAN_FEATURE_SMP_SUPPORT
        }
    }
#endif
/*lint +e539*/ /*lint +e830*/
}

/*
 * 函 数 名  : frw_timer_delete_all_timer
 * 功能描述  : 将定时器链表中的所有定时器删除，用于异常时外部模块清理定时器
 *             不能在定时器回调函数中调用
 */
oal_void frw_timer_delete_all_timer(oal_void)
{
    oal_dlist_head_stru *pst_timeout_entry;
    frw_timeout_stru *pst_timeout_element;

    oal_uint32 ul_core_id;

    for (ul_core_id = 0; ul_core_id < WLAN_FRW_MAX_NUM_CORES; ul_core_id++) {
        oal_spin_lock_bh(&timer_list_spinlock[ul_core_id]);
        /* 删除所有待删除定时器 */
        pst_timeout_entry = ast_timer_list[ul_core_id].pst_next;

        while (pst_timeout_entry != &ast_timer_list[ul_core_id]) {
            pst_timeout_element = OAL_DLIST_GET_ENTRY(pst_timeout_entry, frw_timeout_stru, st_entry);

            pst_timeout_entry = pst_timeout_entry->pst_next;

            /* 删除定时器 */
            oal_dlist_delete_entry(&pst_timeout_element->st_entry);
        }

        frw_timer_start_stamp[ul_core_id] = 0;
        oal_spin_unlock_bh(&timer_list_spinlock[ul_core_id]);
    }
}

/*
 * 函 数 名  : frw_timer_dump_timer
 * 功能描述  : 打印所有timer的维测信息
 */
oal_void frw_timer_dump_timer(oal_uint32 ul_core_id)
{
    oal_dlist_head_stru *pst_dlist_entry;
    frw_timeout_stru *pst_timer;
    oal_uint32 ul_cnt = 0;

    OAM_WARNING_LOG0(0, OAM_SF_ANY, "frw_timer_dump_timer::timer dump start.");

    OAM_WARNING_LOG1(0, OAM_SF_ANY, "frw_timer_dump_timer::g_ul_frw_timer_start_stamp [%u]",
                     frw_timer_start_stamp[ul_core_id]);

    OAL_DLIST_SEARCH_FOR_EACH(pst_dlist_entry, &ast_timer_list[ul_core_id])
    {
        pst_timer = OAL_DLIST_GET_ENTRY(pst_dlist_entry, frw_timeout_stru, st_entry);

        OAM_WARNING_LOG4(0, OAM_SF_ANY, "Driver:TIMER NO.%d, file id[%d], line num[%d], func addr[0x%08x]",
                         ul_cnt, pst_timer->ul_file_id, pst_timer->ul_line_num, (uintptr_t)pst_timer->p_func);
        OAM_WARNING_LOG3(0, OAM_SF_ANY, "Driver:timer enabled[%d], registerd[%d], period[%d] ",
                         pst_timer->en_is_enabled, pst_timer->en_is_registerd, pst_timer->en_is_periodic);
        OAM_WARNING_LOG4(0, OAM_SF_ANY, "Driver:core id[%d], timeout[%u], time stamp[%u], curr time stamp[%u]",
                         ul_core_id, pst_timer->ul_timeout, pst_timer->ul_time_stamp, pst_timer->ul_curr_time_stamp);
        ul_cnt++;
    }
    OAM_WARNING_LOG0(0, OAM_SF_ANY, "frw_timer_dump_timer::timer dump end.");
}
/*
 * 函 数 名  : frw_timer_clean_timer
 * 功能描述  : 删除指定模块残留的所有定时器
 *             本函数不能解决残留定时器的所有问题，一旦发现有残留，需要进行处理。
 * 输入参数  : 模块id
 */
oal_void frw_timer_clean_timer(oam_module_id_enum_uint16 en_module_id)
{
    oal_dlist_head_stru *pst_timeout_entry = NULL;
    frw_timeout_stru *pst_timeout_element = NULL;
    oal_uint32 ul_core_id;

    for (ul_core_id = 0; ul_core_id < WLAN_FRW_MAX_NUM_CORES; ul_core_id++) {
        oal_spin_lock_bh(&timer_list_spinlock[ul_core_id]);
        pst_timeout_entry = ast_timer_list[ul_core_id].pst_next;

        while (pst_timeout_entry != &ast_timer_list[ul_core_id]) {
            if (pst_timeout_entry == NULL) {
                OAL_IO_PRINT("!!!====TIMER LIST BROKEN====!!!\n");
                break;
            }

            pst_timeout_element = OAL_DLIST_GET_ENTRY(pst_timeout_entry, frw_timeout_stru, st_entry);
            pst_timeout_entry = pst_timeout_entry->pst_next;

            if (en_module_id == pst_timeout_element->en_module_id) {
                oal_dlist_delete_entry(&pst_timeout_element->st_entry);
            }
        }

        if (oal_dlist_is_empty(&ast_timer_list[ul_core_id])) {
            frw_timer_start_stamp[ul_core_id] = 0;
        }
        oal_spin_unlock_bh(&timer_list_spinlock[ul_core_id]);
    }
}

/*lint -e578*/ /*lint -e19*/
oal_module_symbol(frw_timer_restart_timer);
oal_module_symbol(frw_timer_create_timer);
oal_module_symbol(frw_timer_dump_timer);
oal_module_symbol(frw_timer_stop_timer);
oal_module_symbol(frw_timer_add_timer);
oal_module_symbol(frw_timer_immediate_destroy_timer);
oal_module_symbol(frw_timer_delete_all_timer);
oal_module_symbol(timer_pause);
oal_module_symbol(frw_timer_clean_timer);

#if defined(_PRE_DEBUG_MODE) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
oal_module_symbol(timeout_track);
oal_module_symbol(timeout_track_idx);
#endif

oal_module_symbol(frw_timer_restart);
oal_module_symbol(frw_timer_stop);
