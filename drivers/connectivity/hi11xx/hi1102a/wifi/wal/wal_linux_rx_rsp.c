


#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif


/*****************************************************************************
  1 头文件包含
*****************************************************************************/
#include "oal_ext_if.h"
#include "wlan_types.h"
#include "frw_ext_if.h"
#include "mac_device.h"
#include "mac_resource.h"
#include "mac_vap.h"
#include "mac_regdomain.h"
#include "hmac_ext_if.h"
#include "wal_ext_if.h"
#include "wal_main.h"
#include "wal_config.h"
#include "wal_linux_scan.h"
#include "wal_linux_cfg80211.h"
#include "wal_linux_ioctl.h"
#include "wal_linux_flowctl.h"
#include "oal_cfg80211.h"
#include "oal_net.h"
#include "hmac_resource.h"
#include "hmac_device.h"
#include "hmac_scan.h"

#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#include "hmac_user.h"
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)&&(_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#include "plat_pm_wlan.h"
#endif

#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_WAL_LINUX_RX_RSP_C

/*****************************************************************************
  2 全局变量定义
*****************************************************************************/
#ifdef _PRE_WLAN_FEATURE_DFR
extern  hmac_dfr_info_stru    g_st_dfr_info;
#endif //_PRE_WLAN_FEATURE_DFR


/*****************************************************************************
  3 函数实现
*****************************************************************************/


OAL_STATIC oal_void wal_scan_report(hmac_scan_stru *pst_scan_mgmt, oal_bool_enum_uint8 en_is_aborted)
{
    /* 通知 kernel scan 已经结束 */
    oal_cfg80211_scan_done(pst_scan_mgmt->pst_request, en_is_aborted);

    pst_scan_mgmt->pst_request = OAL_PTR_NULL;
    pst_scan_mgmt->en_complete = OAL_TRUE;

    OAM_WARNING_LOG1(0, OAM_SF_SCAN, "{wal_scan_report::scan complete,abort flag[%d].!}",en_is_aborted);

    /* 让编译器优化时保证OAL_WAIT_QUEUE_WAKE_UP在最后执行 */
    OAL_SMP_MB();
    OAL_WAIT_QUEUE_WAKE_UP_INTERRUPT(&pst_scan_mgmt->st_wait_queue);
}


OAL_STATIC oal_void wal_schedule_scan_report(oal_wiphy_stru *pst_wiphy, hmac_scan_stru *pst_scan_mgmt)
{
    /* 上报调度扫描结果 */
    oal_cfg80211_sched_scan_result(pst_wiphy);

    pst_scan_mgmt->pst_sched_scan_req     = OAL_PTR_NULL;
    pst_scan_mgmt->en_sched_scan_complete = OAL_TRUE;

    OAM_WARNING_LOG0(0, OAM_SF_SCAN, "{wal_schedule_scan_report::sched scan complete.!}");
}


oal_uint32  wal_scan_comp_proc_sta(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru                 *pst_event = OAL_PTR_NULL;
    hmac_scan_rsp_stru             *pst_scan_rsp = OAL_PTR_NULL;
    hmac_device_stru               *pst_hmac_device = OAL_PTR_NULL;
    hmac_vap_stru                  *pst_hmac_vap = OAL_PTR_NULL;
    hmac_bss_mgmt_stru             *pst_bss_mgmt = OAL_PTR_NULL;
    hmac_scan_stru                 *pst_scan_mgmt = OAL_PTR_NULL;
    oal_wiphy_stru                 *pst_wiphy = OAL_PTR_NULL;
    oal_bool_enum_uint8             en_is_aborted;

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_SCAN, "{wal_scan_comp_proc_sta::pst_event_mem is null!}");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event  = (frw_event_stru *)pst_event_mem->puc_data;

    /* 获取hmac vap结构体 */
    pst_hmac_vap = mac_res_get_hmac_vap(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_PTR_NULL == pst_hmac_vap)
    {
        OAM_WARNING_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_SCAN, "{wal_scan_comp_proc_sta::pst_hmac_vap is NULL!}");
        return OAL_ERR_CODE_PTR_NULL;
    }

    /* 删除等待扫描超时定时器 */
    if (OAL_TRUE == pst_hmac_vap->st_scan_timeout.en_is_registerd)
    {
        FRW_TIMER_IMMEDIATE_DESTROY_TIMER(&(pst_hmac_vap->st_scan_timeout));
    }

    /* 获取hmac device 指针*/
    pst_hmac_device = hmac_res_get_mac_dev(pst_event->st_event_hdr.uc_device_id);
    if (OAL_PTR_NULL == pst_hmac_device)
    {
        OAM_ERROR_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_SCAN,
                       "{wal_scan_comp_proc_sta::pst_hmac_device[%d] is null!}",
                       pst_event->st_event_hdr.uc_device_id);
        return OAL_ERR_CODE_PTR_NULL;
    }
    pst_scan_mgmt = &(pst_hmac_device->st_scan_mgmt);
    pst_wiphy      = pst_hmac_device->pst_device_base_info->pst_wiphy;

    /* 获取扫描结果的管理结构地址 */
    pst_bss_mgmt = &(pst_hmac_device->st_scan_mgmt.st_scan_record_mgmt.st_bss_mgmt);

    /* 获取驱动上报的扫描结果结构体指针 */
    pst_scan_rsp = (hmac_scan_rsp_stru *)pst_event->auc_event_data;

    /* 如果扫描返回结果的非成功，打印维测信息 */
    if ((MAC_SCAN_SUCCESS != pst_scan_rsp->uc_result_code) && (MAC_SCAN_PNO != pst_scan_rsp->uc_result_code))
    {
        OAM_WARNING_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_SCAN, "{wal_scan_comp_proc_sta::scan not succ, err_code[%d]!}", pst_scan_rsp->uc_result_code);
    }

    /* 上报所有扫描到的bss, 无论扫描结果成功与否，统一上报扫描结果，有几个上报几个 */
    wal_inform_all_bss(pst_wiphy, pst_bss_mgmt, pst_event->st_event_hdr.uc_vap_id);

    /* 对于内核下发的扫描request资源加锁 */
    oal_spin_lock(&(pst_scan_mgmt->st_scan_request_spinlock));

    /* 没有未释放的扫描资源，直接返回 */
    if((OAL_PTR_NULL == pst_scan_mgmt->pst_request) && (OAL_PTR_NULL == pst_scan_mgmt->pst_sched_scan_req))
    {
        OAM_WARNING_LOG0(0, OAM_SF_SCAN, "{wal_scan_comp_proc_sta::legacy scan and pno scan are complete!}");

        /* 通知完内核，释放资源后解锁 */
        oal_spin_unlock(&(pst_scan_mgmt->st_scan_request_spinlock));
        return OAL_SUCC;
    }

    if((OAL_PTR_NULL != pst_scan_mgmt->pst_request) && (OAL_PTR_NULL != pst_scan_mgmt->pst_sched_scan_req))
    {
        /* 一般情况下,2个扫描同时存在是一种异常情况,在此添加打印,暂不做异常处理 */
        OAM_WARNING_LOG0(0, OAM_SF_SCAN, "{wal_scan_comp_proc_sta::legacy scan and pno scan are all started!!!}");
    }

    /* 上层下发的普通扫描进行对应处理 */
    if (MAC_SCAN_PNO == pst_scan_rsp->uc_result_code)
    {
        /* PNO扫描结束事件 */
        if(OAL_PTR_NULL != pst_scan_mgmt->pst_sched_scan_req)
        {
            wal_schedule_scan_report(pst_wiphy, pst_scan_mgmt);
        }
        else
        {
            OAM_WARNING_LOG0(0, OAM_SF_SCAN, "{wal_scan_comp_proc_sta::sched scan already complete!}");
        }
    }
    else
    {
        /* 普通扫描结束事件 */
        if(OAL_PTR_NULL != pst_scan_mgmt->pst_request)
        {
            /* 是scan abort的话告知内核 */
            en_is_aborted = (MAC_SCAN_ABORT == pst_scan_rsp->uc_result_code) ? OAL_TRUE : OAL_FALSE;
            wal_scan_report(pst_scan_mgmt, en_is_aborted);
        }
        else
        {
            OAM_WARNING_LOG0(0, OAM_SF_SCAN, "{wal_scan_comp_proc_sta::scan already complete!}");
        }
    }

    /* 通知完内核，释放资源后解锁 */
    oal_spin_unlock(&(pst_scan_mgmt->st_scan_request_spinlock));

    return OAL_SUCC;
}



oal_uint32  wal_asoc_comp_proc_sta(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru              *pst_event = OAL_PTR_NULL;
    oal_connet_result_stru       st_connet_result;
    oal_net_device_stru         *pst_net_device = OAL_PTR_NULL;
    hmac_asoc_rsp_stru          *pst_asoc_rsp = OAL_PTR_NULL;
    oal_uint32                   ul_ret;

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_ASSOC, "{wal_asoc_comp_proc_sta::pst_event_mem is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event     = (frw_event_stru *)pst_event_mem->puc_data;
    pst_asoc_rsp  = (hmac_asoc_rsp_stru *)pst_event->auc_event_data;

    /* 获取net_device*/
    pst_net_device = hmac_vap_get_net_device(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_PTR_NULL == pst_net_device)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_asoc_comp_proc_sta::get net device ptr is null!}\r\n");
        oal_free(pst_asoc_rsp->puc_asoc_rsp_ie_buff);
        pst_asoc_rsp->puc_asoc_rsp_ie_buff = OAL_PTR_NULL;
        return OAL_ERR_CODE_PTR_NULL;
    }

    memset_s(&st_connet_result, OAL_SIZEOF(oal_connet_result_stru), 0, OAL_SIZEOF(oal_connet_result_stru));

    /* 准备上报内核的关联结果结构体 */
    oal_memcopy(st_connet_result.auc_bssid, pst_asoc_rsp->auc_addr_ap, WLAN_MAC_ADDR_LEN);
    st_connet_result.puc_req_ie       = pst_asoc_rsp->puc_asoc_req_ie_buff;
    st_connet_result.ul_req_ie_len    = pst_asoc_rsp->ul_asoc_req_ie_len;
    st_connet_result.puc_rsp_ie       = pst_asoc_rsp->puc_asoc_rsp_ie_buff;
    st_connet_result.ul_rsp_ie_len    = pst_asoc_rsp->ul_asoc_rsp_ie_len;
    st_connet_result.us_status_code   = pst_asoc_rsp->en_status_code;

    
    if (st_connet_result.us_status_code == MAC_SUCCESSFUL_STATUSCODE)
    {
        hmac_device_stru    *pst_hmac_device;
        oal_wiphy_stru      *pst_wiphy = OAL_PTR_NULL;
        hmac_bss_mgmt_stru  *pst_bss_mgmt = OAL_PTR_NULL;
        pst_hmac_device = hmac_res_get_mac_dev(pst_event->st_event_hdr.uc_device_id);
        if ((OAL_PTR_NULL == pst_hmac_device)
            || (OAL_PTR_NULL == pst_hmac_device->pst_device_base_info)
            || (OAL_PTR_NULL == pst_hmac_device->pst_device_base_info->pst_wiphy))
        {
            OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC,
                            "{wal_asoc_comp_proc_sta::get ptr is null!}");
            oal_free(pst_asoc_rsp->puc_asoc_rsp_ie_buff);
            pst_asoc_rsp->puc_asoc_rsp_ie_buff = OAL_PTR_NULL;
            return OAL_ERR_CODE_PTR_NULL;
        }
        pst_wiphy       = pst_hmac_device->pst_device_base_info->pst_wiphy;
        pst_bss_mgmt    = &(pst_hmac_device->st_scan_mgmt.st_scan_record_mgmt.st_bss_mgmt);

        wal_update_bss(pst_wiphy, pst_bss_mgmt, st_connet_result.auc_bssid);
    }
    

    /* 调用内核接口，上报关联结果 */
    ul_ret = oal_cfg80211_connect_result(pst_net_device,
                                st_connet_result.auc_bssid,
                                st_connet_result.puc_req_ie,
                                st_connet_result.ul_req_ie_len,
                                st_connet_result.puc_rsp_ie,
                                st_connet_result.ul_rsp_ie_len,
                                st_connet_result.us_status_code,
                                GFP_ATOMIC);
    if (OAL_SUCC != ul_ret)
    {
        OAM_ERROR_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_asoc_comp_proc_sta::oal_cfg80211_connect_result fail[%d]!}\r\n", ul_ret);
    }

    oal_free(pst_asoc_rsp->puc_asoc_rsp_ie_buff);
    pst_asoc_rsp->puc_asoc_rsp_ie_buff = OAL_PTR_NULL;

#ifdef _PRE_WLAN_FEATURE_11D
    /* 如果关联成功，sta根据AP的国家码设置自己的管制域 */
    if (HMAC_MGMT_SUCCESS == pst_asoc_rsp->en_result_code)
    {
        wal_regdomain_update_sta(pst_event->st_event_hdr.uc_vap_id);
    }
#endif


    ul_ret = hmac_vap_free_asoc_req_ie_ptr(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_SUCC != ul_ret)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_asoc_comp_proc_sta::hmac_vap_free_asoc_req_ie_ptr fail!}\r\n");
        return ul_ret;
    }

    /* 启动发送队列，防止发送队列被漫游关闭后无法恢复 */
    oal_net_tx_wake_all_queues(pst_net_device);

    OAM_WARNING_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_asoc_comp_proc_sta status_code[%d] OK!}\r\n", st_connet_result.us_status_code);

    return OAL_SUCC;
}

#ifdef _PRE_WLAN_FEATURE_ROAM

oal_uint32  wal_roam_comp_proc_sta(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru              *pst_event = OAL_PTR_NULL;
    oal_net_device_stru         *pst_net_device = OAL_PTR_NULL;
    mac_device_stru             *pst_mac_device = OAL_PTR_NULL;
    hmac_roam_rsp_stru          *pst_roam_rsp = OAL_PTR_NULL;
    struct ieee80211_channel    *pst_channel = OAL_PTR_NULL;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0))
    enum nl80211_band            en_band = NUM_NL80211_BANDS;
#else
    enum ieee80211_band          en_band = IEEE80211_NUM_BANDS;
#endif
    oal_uint32                   ul_ret;
    oal_int                      l_freq;

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_ASSOC, "{wal_roam_comp_proc_sta::pst_event_mem is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event     = (frw_event_stru *)pst_event_mem->puc_data;
    pst_roam_rsp  = (hmac_roam_rsp_stru *)pst_event->auc_event_data;

    /* 获取net_device*/
    pst_net_device = hmac_vap_get_net_device(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_PTR_NULL == pst_net_device)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ROAM, "{wal_asoc_comp_proc_sta::get net device ptr is null!}\r\n");
        oal_free(pst_roam_rsp->puc_asoc_rsp_ie_buff);
        pst_roam_rsp->puc_asoc_rsp_ie_buff = OAL_PTR_NULL;
        return OAL_ERR_CODE_PTR_NULL;
    }
    /* 获取device id 指针*/
    pst_mac_device = mac_res_get_dev(pst_event->st_event_hdr.uc_device_id);
    if (OAL_PTR_NULL == pst_mac_device)
    {
       OAM_WARNING_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_SCAN, "{wal_asoc_comp_proc_sta::pst_mac_device is null ptr!}");
       oal_free(pst_roam_rsp->puc_asoc_rsp_ie_buff);
       pst_roam_rsp->puc_asoc_rsp_ie_buff = OAL_PTR_NULL;
       return OAL_ERR_CODE_PTR_NULL;
    }

    if (pst_roam_rsp->st_channel.en_band >= WLAN_BAND_BUTT)
    {
        OAM_ERROR_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ROAM, "{wal_asoc_comp_proc_sta::unexpected band[%d]!}\r\n", pst_roam_rsp->st_channel.en_band);
        oal_free(pst_roam_rsp->puc_asoc_rsp_ie_buff);
        pst_roam_rsp->puc_asoc_rsp_ie_buff = OAL_PTR_NULL;
        return OAL_FAIL;
    }
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0))
    if (WLAN_BAND_2G == pst_roam_rsp->st_channel.en_band)
    {
        en_band = NL80211_BAND_2GHZ;
    }
    if (WLAN_BAND_5G == pst_roam_rsp->st_channel.en_band)
    {
        en_band = NL80211_BAND_5GHZ;
    }
#else
    if (WLAN_BAND_2G == pst_roam_rsp->st_channel.en_band)
    {
        en_band = IEEE80211_BAND_2GHZ;
    }
    if (WLAN_BAND_5G == pst_roam_rsp->st_channel.en_band)
    {
        en_band = IEEE80211_BAND_5GHZ;
    }
#endif

    /* for test, flush 192.168.1.1 arp */






    l_freq = oal_ieee80211_channel_to_frequency(pst_roam_rsp->st_channel.uc_chan_number, en_band);

    pst_channel = (struct ieee80211_channel*)oal_ieee80211_get_channel(pst_mac_device->pst_wiphy, l_freq);

    /* 调用内核接口，上报关联结果 */
    ul_ret = oal_cfg80211_roamed(pst_net_device,
                                 pst_channel,
                                 pst_roam_rsp->auc_bssid,
                                 pst_roam_rsp->puc_asoc_req_ie_buff,
                                 pst_roam_rsp->ul_asoc_req_ie_len,
                                 pst_roam_rsp->puc_asoc_rsp_ie_buff,
                                 pst_roam_rsp->ul_asoc_rsp_ie_len,
                                 GFP_ATOMIC);
    if (OAL_SUCC != ul_ret)
    {
        OAM_ERROR_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_roam_comp_proc_sta::oal_cfg80211_roamed fail[%d]!}\r\n", ul_ret);
    }
    OAM_WARNING_LOG4(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_roam_comp_proc_sta::oal_cfg80211_roamed OK asoc_req_ie[%p] len[%d] asoc_rsp_ie[%p] len[%d]!}\r\n",
                   (uintptr_t)(pst_roam_rsp->puc_asoc_req_ie_buff), pst_roam_rsp->ul_asoc_req_ie_len, (uintptr_t)(pst_roam_rsp->puc_asoc_rsp_ie_buff), pst_roam_rsp->ul_asoc_rsp_ie_len);

    oal_free(pst_roam_rsp->puc_asoc_rsp_ie_buff);
    pst_roam_rsp->puc_asoc_rsp_ie_buff = OAL_PTR_NULL;

    ul_ret = hmac_vap_free_asoc_req_ie_ptr(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_SUCC != ul_ret)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_roam_comp_proc_sta::hmac_vap_free_asoc_req_ie_ptr fail!}\r\n");
        return ul_ret;
    }

    return OAL_SUCC;
}
#endif //_PRE_WLAN_FEATURE_ROAM
#ifdef _PRE_WLAN_FEATURE_11R

oal_uint32  wal_ft_event_proc_sta(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru              *pst_event;
    oal_net_device_stru         *pst_net_device;
    hmac_roam_ft_stru           *pst_ft_event;
    oal_cfg80211_ft_event_stru   st_cfg_ft_event;
    oal_uint32                   ul_ret;

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_ASSOC, "{wal_ft_event_proc_sta::pst_event_mem is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event     = (frw_event_stru *)pst_event_mem->puc_data;
    pst_ft_event  = (hmac_roam_ft_stru *)pst_event->auc_event_data;

    /* 获取net_device*/
    pst_net_device = hmac_vap_get_net_device(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_PTR_NULL == pst_net_device)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ROAM, "{wal_ft_event_proc_sta::get net device ptr is null!}\r\n");
        oal_free(pst_ft_event->puc_ft_ie_buff);
        pst_ft_event->puc_ft_ie_buff = OAL_PTR_NULL;
        return OAL_ERR_CODE_PTR_NULL;
    }

    st_cfg_ft_event.ies           = pst_ft_event->puc_ft_ie_buff;
    st_cfg_ft_event.ies_len       = pst_ft_event->us_ft_ie_len;
    st_cfg_ft_event.target_ap     = pst_ft_event->auc_bssid;
    st_cfg_ft_event.ric_ies       = OAL_PTR_NULL;
    st_cfg_ft_event.ric_ies_len   = pst_ft_event->us_ft_ie_len;

    /* 调用内核接口，上报关联结果 */
    ul_ret = oal_cfg80211_ft_event(pst_net_device, &st_cfg_ft_event);
    if (OAL_SUCC != ul_ret)
    {
        OAM_ERROR_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_ft_event_proc_sta::oal_cfg80211_ft_event fail[%d]!}\r\n", ul_ret);
    }

    oal_free(pst_ft_event->puc_ft_ie_buff);
    pst_ft_event->puc_ft_ie_buff = OAL_PTR_NULL;

    return OAL_SUCC;
}
#endif //_PRE_WLAN_FEATURE_11R


oal_uint32  wal_disasoc_comp_proc_sta(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru              *pst_event = OAL_PTR_NULL;
    oal_disconnect_result_stru    st_disconnect_result;
    oal_net_device_stru         *pst_net_device = OAL_PTR_NULL;
    oal_uint16                  *pus_disasoc_reason_code = OAL_PTR_NULL;
    oal_uint32                   ul_ret;

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_ASSOC, "{wal_disasoc_comp_proc_sta::pst_event_mem is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event  = (frw_event_stru *)pst_event_mem->puc_data;

    /* 获取net_device*/
    pst_net_device = hmac_vap_get_net_device(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_PTR_NULL == pst_net_device)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_disasoc_comp_proc_sta::get net device ptr is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    /* 获取去关联原因码指针 */
    pus_disasoc_reason_code = (oal_uint16 *)pst_event->auc_event_data;

    memset_s(&st_disconnect_result, OAL_SIZEOF(oal_disconnect_result_stru), 0, OAL_SIZEOF(oal_disconnect_result_stru));

    /* 准备上报内核的关联结果结构体 */
    st_disconnect_result.us_reason_code = *pus_disasoc_reason_code;

    /* 调用内核接口，上报去关联结果 */
    ul_ret = oal_cfg80211_disconnected(pst_net_device,
                              st_disconnect_result.us_reason_code,
                              st_disconnect_result.pus_disconn_ie,
                              st_disconnect_result.us_disconn_ie_len,
                              GFP_ATOMIC);
    if (OAL_SUCC != ul_ret)
    {
        OAM_ERROR_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_disasoc_comp_proc_sta::oal_cfg80211_disconnected fail[%d]!}\r\n", ul_ret);
        return ul_ret;
    }

    OAM_WARNING_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_disasoc_comp_proc_sta reason_code[%d] OK!}\r\n", st_disconnect_result.us_reason_code);

    return OAL_SUCC;
}



oal_uint32  wal_connect_new_sta_proc_ap(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru              *pst_event = OAL_PTR_NULL;
    oal_net_device_stru         *pst_net_device = OAL_PTR_NULL;
    oal_uint8                    auc_connect_user_addr[WLAN_MAC_ADDR_LEN];
    oal_station_info_stru        st_station_info;
    oal_uint32                   ul_ret;
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
    hmac_asoc_user_req_ie_stru  *pst_asoc_user_req_info = OAL_PTR_NULL;
#endif

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_ASSOC, "{wal_connect_new_sta_proc_ap::pst_event_mem is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event  = (frw_event_stru *)pst_event_mem->puc_data;

    /* 获取net_device*/
    pst_net_device = hmac_vap_get_net_device(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_PTR_NULL == pst_net_device)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_connect_new_sta_proc_ap::get net device ptr is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }



    memset_s(&st_station_info, OAL_SIZEOF(oal_station_info_stru), 0, OAL_SIZEOF(oal_station_info_stru));

#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
    /* 向内核标记填充了关联请求帧的ie信息 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
    /* Linux 4.0 版本不需要STATION_INFO_ASSOC_REQ_IES 标识 */
#else
    st_station_info.filled |=  STATION_INFO_ASSOC_REQ_IES;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) */
    pst_asoc_user_req_info = (hmac_asoc_user_req_ie_stru *)(pst_event->auc_event_data);
    st_station_info.assoc_req_ies = pst_asoc_user_req_info->puc_assoc_req_ie_buff;
    if (OAL_PTR_NULL == st_station_info.assoc_req_ies)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_connect_new_sta_proc_ap::asoc ie is null!}");
        return OAL_ERR_CODE_PTR_NULL;
    }
    st_station_info.assoc_req_ies_len = pst_asoc_user_req_info->ul_assoc_req_ie_len;

    /* 获取关联user mac addr */
    oal_memcopy(auc_connect_user_addr, (oal_uint8 *)pst_asoc_user_req_info->auc_user_mac_addr, WLAN_MAC_ADDR_LEN);
#else
    /* 获取关联user mac addr */
    oal_memcopy(auc_connect_user_addr, (oal_uint8 *)pst_event->auc_event_data, WLAN_MAC_ADDR_LEN);
#endif

    /* 调用内核接口，上报STA关联结果 */
    ul_ret = oal_cfg80211_new_sta(pst_net_device, auc_connect_user_addr, &st_station_info, GFP_ATOMIC);
    if (OAL_SUCC != ul_ret)
    {
        OAM_ERROR_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_connect_new_sta_proc_ap::oal_cfg80211_new_sta fail[%d]!}\r\n", ul_ret);
        return ul_ret;
    }

    OAM_WARNING_LOG4(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_connect_new_sta_proc_ap mac[%02X:XX:XX:%02X:%02X:%02X] OK!}\r\n",
            auc_connect_user_addr[0],
            auc_connect_user_addr[3],
            auc_connect_user_addr[4],
            auc_connect_user_addr[5]);

    return OAL_SUCC;
}



oal_uint32  wal_disconnect_sta_proc_ap(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru            *pst_event = OAL_PTR_NULL;
    oal_net_device_stru       *pst_net_device = OAL_PTR_NULL;
    oal_uint8                  auc_disconn_user_addr[WLAN_MAC_ADDR_LEN];
    oal_uint32                 ul_ret;

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_ASSOC, "{wal_disconnect_sta_proc_ap::pst_event_mem is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event  = (frw_event_stru *)pst_event_mem->puc_data;

    /* 获取net_device*/
    pst_net_device = hmac_vap_get_net_device(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_PTR_NULL == pst_net_device)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_disconnect_sta_proc_ap::get net device ptr is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }


    /* 获取去关联user mac addr */
    oal_memcopy(auc_disconn_user_addr, (oal_uint8 *)pst_event->auc_event_data, WLAN_MAC_ADDR_LEN);

    /* 调用内核接口，上报STA去关联结果 */
    ul_ret = oal_cfg80211_del_sta(pst_net_device, auc_disconn_user_addr, GFP_ATOMIC);
    if (OAL_SUCC != ul_ret)
    {
        OAM_ERROR_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_disconnect_sta_proc_ap::oal_cfg80211_del_sta fail[%d]!}\r\n", ul_ret);
        return ul_ret;
    }

    OAM_WARNING_LOG3(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ASSOC, "{wal_disconnect_sta_proc_ap mac[%x %x %x] OK!}\r\n", auc_disconn_user_addr[3], auc_disconn_user_addr[4], auc_disconn_user_addr[5]);

    return OAL_SUCC;
}


oal_uint32  wal_mic_failure_proc(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru               *pst_event = OAL_PTR_NULL;
    oal_net_device_stru          *pst_net_device = OAL_PTR_NULL;
    hmac_mic_event_stru          *pst_mic_event = OAL_PTR_NULL;

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_CRYPTO, "{wal_mic_failure_proc::pst_event_mem is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event       = (frw_event_stru *)pst_event_mem->puc_data;
    pst_mic_event   = (hmac_mic_event_stru *)(pst_event->auc_event_data);

    /* 获取net_device*/
    pst_net_device = hmac_vap_get_net_device(pst_event->st_event_hdr.uc_vap_id);
    if (OAL_PTR_NULL == pst_net_device)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_CRYPTO, "{wal_mic_failure_proc::get net device ptr is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    /* 调用内核接口，上报mic攻击 */
    oal_cfg80211_mic_failure(pst_net_device, pst_mic_event->auc_user_mac, pst_mic_event->en_key_type, pst_mic_event->l_key_id, NULL, GFP_ATOMIC);

    OAM_WARNING_LOG3(pst_event->st_event_hdr.uc_vap_id, OAM_SF_CRYPTO, "{wal_mic_failure_proc::mac[%x %x %x] OK!}\r\n",
                  pst_mic_event->auc_user_mac[3], pst_mic_event->auc_user_mac[4], pst_mic_event->auc_user_mac[5]);

    return OAL_SUCC;
}



oal_uint32  wal_send_mgmt_to_host(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru               *pst_event = OAL_PTR_NULL;
    oal_net_device_stru          *pst_net_device = OAL_PTR_NULL;
    oal_int32                     l_freq;
    oal_uint8                     uc_rssi;
    oal_uint8                    *puc_buf = OAL_PTR_NULL;
    oal_uint16                    us_len;
    oal_uint32                    ul_ret;
    hmac_rx_mgmt_event_stru      *pst_mgmt_frame = OAL_PTR_NULL;
    oal_ieee80211_mgmt           *pst_ieee80211_mgmt = OAL_PTR_NULL;

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_ANY, "{wal_send_mgmt_to_host::pst_event_mem is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event       = (frw_event_stru *)pst_event_mem->puc_data;
    pst_mgmt_frame  = (hmac_rx_mgmt_event_stru *)(pst_event->auc_event_data);

    /* 获取net_device*/
    pst_net_device = oal_dev_get_by_name(pst_mgmt_frame->ac_name);

    if (OAL_PTR_NULL == pst_net_device)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ANY, "{wal_send_mgmt_to_host::get net device ptr is null!}\r\n");
        oal_free(pst_mgmt_frame->puc_buf);
        return OAL_ERR_CODE_PTR_NULL;
    }
    oal_dev_put(pst_net_device);

    puc_buf = pst_mgmt_frame->puc_buf;
    us_len  = pst_mgmt_frame->us_len;
    l_freq  = pst_mgmt_frame->l_freq;
    uc_rssi = pst_mgmt_frame->uc_rssi;

    pst_ieee80211_mgmt = (oal_ieee80211_mgmt *)puc_buf;
    /* 调用内核接口，上报接收到管理帧 */
    ul_ret = oal_cfg80211_rx_mgmt(pst_net_device, l_freq, uc_rssi, puc_buf, us_len, GFP_ATOMIC);
    if (OAL_SUCC != ul_ret)
    {

        OAM_WARNING_LOG2(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ANY, "{wal_send_mgmt_to_host::fc[0x%04x], if_type[%d]!}\r\n",
                        pst_ieee80211_mgmt->frame_control, pst_net_device->ieee80211_ptr->iftype);
        OAM_WARNING_LOG3(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ANY, "{wal_send_mgmt_to_host::oal_cfg80211_rx_mgmt fail[%d]!len[%d], freq[%d]}\r\n",
                        ul_ret, us_len, l_freq);
        oal_free(puc_buf);
        return OAL_FAIL;
    }

    OAM_INFO_LOG3(pst_event->st_event_hdr.uc_vap_id, OAM_SF_ANY, "{wal_send_mgmt_to_host::freq = %d, len = %d, TYPE[%04X] OK!}\r\n", l_freq, us_len, pst_ieee80211_mgmt->frame_control);
    oal_free(puc_buf);
    return OAL_SUCC;
}



oal_uint32 wal_p2p_listen_timeout(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru               *pst_event = OAL_PTR_NULL;
    oal_wireless_dev_stru        *pst_wdev = OAL_PTR_NULL;
    oal_uint64                    ull_cookie;
    oal_ieee80211_channel_stru    st_listen_channel;
    hmac_p2p_listen_expired_stru *pst_p2p_listen_expired = OAL_PTR_NULL;
    mac_device_stru              *pst_mac_device = OAL_PTR_NULL;

    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_event_mem))
    {
        OAM_ERROR_LOG0(0, OAM_SF_P2P, "{wal_p2p_listen_timeout::pst_event_mem is null!}\r\n");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event               = (frw_event_stru *)pst_event_mem->puc_data;
    /* 获取mac_device_stru */
    pst_mac_device = mac_res_get_dev(pst_event->st_event_hdr.uc_device_id);
    if (OAL_PTR_NULL == pst_mac_device)
    {
        OAM_ERROR_LOG0(pst_event->st_event_hdr.uc_vap_id, OAM_SF_PROXYSTA,
                       "{wal_p2p_listen_timeout::get mac_device ptr is null.}");
        return OAL_ERR_CODE_PTR_NULL;
    }
    pst_p2p_listen_expired  = (hmac_p2p_listen_expired_stru *)(pst_event->auc_event_data);

    pst_wdev                = pst_p2p_listen_expired->pst_wdev;
    ull_cookie              = pst_mac_device->st_p2p_info.ull_last_roc_id;
    st_listen_channel       = pst_p2p_listen_expired->st_listen_channel;
    oal_cfg80211_remain_on_channel_expired(pst_wdev,
                                        ull_cookie,
                                        &st_listen_channel,
                                        GFP_ATOMIC);
    OAM_INFO_LOG1(0, OAM_SF_P2P, "{wal_p2p_listen_timeout::END!cookie [%x]}\r\n", ull_cookie);

    return OAL_SUCC;
}

#ifdef _PRE_WLAN_FEATURE_SAE
/*
 * 函 数 名  : wal_report_external_auth_req
 * 功能描述  : HMAC上报external auth request处理
 */
oal_uint32 wal_report_external_auth_req(frw_event_mem_stru *pst_event_mem)
{
    frw_event_stru *pst_event;
    oal_net_device_stru *pst_net_device;
    hmac_external_auth_req_stru *pst_ext_auth_req;
    oal_cfg80211_external_auth_stru st_external_auth_req;
    oal_int l_ret;

    if (OAL_UNLIKELY(pst_event_mem == OAL_PTR_NULL)) {
        OAM_ERROR_LOG0(0, OAM_SF_SAE, "{wal_report_external_auth_req::pst_event_mem is null!}");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_event = frw_get_event_stru(pst_event_mem);

    /* 获取net_device*/
    pst_net_device = hmac_vap_get_net_device(pst_event->st_event_hdr.uc_vap_id);
    if (pst_net_device == OAL_PTR_NULL) {
        OAM_ERROR_LOG1(pst_event->st_event_hdr.uc_vap_id, OAM_SF_SAE,
                        "{wal_report_external_auth_req::get net device ptr is null! vap_id %d}",
                        pst_event->st_event_hdr.uc_vap_id);
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_ext_auth_req = (hmac_external_auth_req_stru *)(pst_event->auc_event_data);

    st_external_auth_req.action         = pst_ext_auth_req->en_action;
    st_external_auth_req.key_mgmt_suite = pst_ext_auth_req->ul_key_mgmt_suite;
    st_external_auth_req.status         = pst_ext_auth_req->us_status;
    st_external_auth_req.ssid.ssid_len  = OAL_MIN(pst_ext_auth_req->st_ssid.uc_ssid_len, OAL_IEEE80211_MAX_SSID_LEN);
    oal_memcopy(st_external_auth_req.ssid.ssid, pst_ext_auth_req->st_ssid.auc_ssid, st_external_auth_req.ssid.ssid_len);
    oal_memcopy(st_external_auth_req.bssid, pst_ext_auth_req->auc_bssid, OAL_ETH_ALEN);

    l_ret = oal_cfg80211_external_auth_request(pst_net_device,
                                        &st_external_auth_req,
                                        GFP_ATOMIC);

    OAM_WARNING_LOG4(0, OAM_SF_SAE, "{wal_report_external_auth_req::action %x, status %d, key_mgmt 0x%X, ssid_len %d}",
                        st_external_auth_req.action,
                        st_external_auth_req.status,
                        st_external_auth_req.key_mgmt_suite,
                        st_external_auth_req.ssid.ssid_len);

    OAM_WARNING_LOG4(0, OAM_SF_SAE, "{wal_report_external_auth_req::mac[%02X:XX:XX:XX:%02X:%02X], ret[%d]}",
                    st_external_auth_req.bssid[0],
                    st_external_auth_req.bssid[4],
                    st_external_auth_req.bssid[5],
                    l_ret);

    return l_ret;
}
#endif /* _PRE_WLAN_FEATURE_SAE */

#ifdef __cplusplus
    #if __cplusplus
        }
    #endif
#endif

