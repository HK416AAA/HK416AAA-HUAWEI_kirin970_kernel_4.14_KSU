

#ifndef __HMAC_ROAM_ALG_H__
#define __HMAC_ROAM_ALG_H__

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#ifdef _PRE_WLAN_FEATURE_11V_ENABLE
#include "hmac_11v.h"
#endif

#ifdef _PRE_WLAN_FEATURE_ROAM

/*****************************************************************************
  1 其他头文件包含
*****************************************************************************/


#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_ROAM_ALG_H
/*****************************************************************************
  2 宏定义
*****************************************************************************/
#define ROAM_LIST_MAX                     (4)          /* 名单规格 */
#define ROAM_BLACKLIST_NORMAL_AP_TIME_OUT (30000)      /* 黑名单正常超时时间 */
#define ROAM_BLACKLIST_REJECT_AP_TIME_OUT (30000)      /* 黑名单拒绝模式超时时间 */
#define ROAM_BLACKLIST_COUNT_LIMIT        (1)           /* 黑名单超时次数限制 */

#define ROAM_HISTORY_BSS_TIME_OUT         (20000)       /* 历史优选名单超时时间 */
#define ROAM_HISTORY_COUNT_LIMIT          (1)           /* 历史优选超时次数限制 */
#define ROAM_RSSI_LEVEL                   (3)
#define ROAM_CONCURRENT_USER_NUMBER       (10)
#define ROAM_THROUGHPUT_THRESHOLD         (1000)

#define ROAM_RSSI_NE80_DB                 (-80)
#define ROAM_RSSI_NE78_DB                 (-78)
#define ROAM_RSSI_NE75_DB                 (-75)
#define ROAM_RSSI_NE72_DB                 (-72)
#define ROAM_RSSI_NE70_DB                 (-70)
#define ROAM_RSSI_NE65_DB                 (-65)

#define ROAM_RSSI_DIFF_4_DB               (4)
#define ROAM_RSSI_DIFF_6_DB               (6)
#define ROAM_RSSI_DIFF_8_DB               (8)
#define ROAM_RSSI_DIFF_10_DB              (10)
#define ROAM_RSSI_DIFF_20_DB              (20)

#define ROAM_RSSI_CMD_TYPE                (-128)
#define ROAM_RSSI_LINKLOSS_TYPE           (-121)
#define ROAM_RSSI_MAX_TYPE                (-126)


/*****************************************************************************
  3 枚举定义
*****************************************************************************/
/* 漫游黑名单类型 */
typedef enum
{
    ROAM_BLACKLIST_TYPE_NORMAL_AP       = 0,
    ROAM_BLACKLIST_TYPE_REJECT_AP       = 1,
    ROAM_BLACKLIST_TYPE_BUTT
}roam_blacklist_type_enum;
typedef oal_uint8  roam_blacklist_type_enum_uint8;

/*****************************************************************************
  4 全局变量声明
*****************************************************************************/


/*****************************************************************************
  5 消息头定义
*****************************************************************************/


/*****************************************************************************
  6 消息定义
*****************************************************************************/


/*****************************************************************************
  7 STRUCT定义
*****************************************************************************/
typedef struct
{
    oal_uint32                 ul_time_stamp;           /* 记录黑名单时间戳 */
    oal_uint32                 ul_timeout;              /* 记录黑名单超时时限 */
    oal_uint16                 us_count_limit;          /* 记录黑名单超时前的加入次数限制 */
    oal_uint16                 us_count;                /* 黑名单超时过程中，添加该Bssid的次数 */
    oal_uint8                  auc_bssid[WLAN_MAC_ADDR_LEN];
}hmac_roam_bss_info_stru;

/* 漫游blacklist结构体 */
typedef struct
{
    hmac_roam_bss_info_stru     ast_bss[ROAM_LIST_MAX];
}hmac_roam_bss_list_stru;
/* 漫游算法结构体 */
typedef struct
{
    hmac_roam_bss_list_stru      st_blacklist;          /* 漫游黑名单AP历史记录 */
    hmac_roam_bss_list_stru      st_history;            /* 漫游优选AP历史记录 */
    oal_uint32                   ul_max_capacity;       /* 记录 scan 结果的最大 capacity */
    mac_bss_dscr_stru           *pst_max_capacity_bss;  /* 记录 scan 结果的最大 capacity 的 bss*/
    oal_int8                     c_current_rssi;        /* 当前 dmac 触发 rssi */
    oal_int8                     c_max_rssi;            /* 记录 scan 结果的最大 rssi */
    oal_uint8                    uc_another_bss_scaned; /* 是否扫描到了非当前关联的 bss */
    oal_uint8                    uc_invalid_scan_cnt;   /* 连续扫描到当前关联且首次关联的 bss 的次数 */
    mac_bss_dscr_stru           *pst_max_rssi_bss;      /* 记录 scan 结果的最大 rssi 的 bss */
}hmac_roam_alg_stru;

/* 漫游connect结构体 */
typedef struct
{
    roam_connect_state_enum_uint8   en_state;
    mac_status_code_enum_uint16     en_status_code;     /* auth/assoc rsp frame status code */
    oal_uint8                       uc_rsv[1];
    oal_uint8                       uc_auth_num;
    oal_uint8                       uc_assoc_num;
    oal_uint8                       uc_ft_num;
    frw_timeout_stru                st_timer;           /* 漫游connect使用的定时器 */
    mac_bss_dscr_stru              *pst_bss_dscr;
}hmac_roam_connect_stru;
typedef struct
{
    oal_int8                       c_rssi;
    oal_uint32                     ul_capacity_kbps;
} hmac_roam_rssi_capacity_stru;

/* 旧bss数据结构体 */
typedef struct
{
    oal_uint8                       auc_bssid[WLAN_MAC_ADDR_LEN];
    oal_uint16                      us_sta_aid;
#ifdef _PRE_WLAN_FEATURE_TXBF
    mac_vap_txbf_add_stru           st_txbf_add_cap;
#endif
    mac_cap_flag_stru               st_cap_flag;
    mac_channel_stru                st_channel;
    wlan_mib_ieee802dot11_stru      st_mib_info;
    mac_user_cap_info_stru          st_cap_info;
    mac_key_mgmt_stru               st_key_info;
    mac_user_tx_param_stru          st_user_tx_info;    /* TX相关参数 */
    mac_rate_stru                   st_op_rates;
    mac_user_ht_hdl_stru            st_ht_hdl;
    mac_vht_hdl_stru                st_vht_hdl;
    wlan_bw_cap_enum_uint8          en_bandwidth_cap;
    wlan_bw_cap_enum_uint8          en_avail_bandwidth;
    wlan_bw_cap_enum_uint8          en_cur_bandwidth;
    wlan_protocol_enum_uint8        en_protocol_mode;
    wlan_protocol_enum_uint8        en_avail_protocol_mode;
    wlan_protocol_enum_uint8        en_cur_protocol_mode;
    oal_uint8                       uc_num_spatial_stream;
    oal_uint8                       uc_avail_num_spatial_stream;
    oal_uint8                       uc_cur_num_spatial_stream;
    oal_uint8                       uc_avail_bf_num_spatial_stream;
    oal_uint16                      us_cap_info;        /* 旧的bss的能力位信息 */
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
    oal_bool_enum_uint8             en_pmf_required;    /* 原BSS是否强制要求pmf */
#endif
}hmac_roam_old_bss_stru;











/* 漫游主结构体 */
typedef struct
{
    oal_uint8                       uc_enable;          /* 漫游使能开关 */
    roam_trigger_enum_uint8         en_roam_trigger;    /* 漫游触发源 */
    roam_main_state_enum_uint8      en_main_state;      /* 漫游主状态 */
    oal_uint8                       uc_rssi_ignore;     /* 忽略rssi触发漫游 */
    oal_uint8                       uc_invalid_scan_cnt;/* 无效扫描统计 */
    oal_bool_enum_uint8             en_current_bss_ignore;  /* 是否支持漫游回自己(支持驱动发起重关联) */
    oal_uint8                       auc_target_bssid[WLAN_MAC_ADDR_LEN];
#ifdef _PRE_WLAN_FEATURE_11V_ENABLE
    hmac_bsst_rsp_info_stru         st_bsst_rsp_info;   /* 11v漫游协议response帧相关信息 */
#endif
    hmac_vap_stru                  *pst_hmac_vap;       /* 漫游对应的vap */
    hmac_user_stru                 *pst_hmac_user;      /* 漫游对应的BSS user */
    hmac_roam_old_bss_stru          st_old_bss;         /* 漫游之前保存旧bss相关信息 */
    mac_scan_req_stru               st_scan_params;     /* 漫游扫描参数 */
    hmac_roam_config_stru           st_config;          /* 漫游相关配置信息 */
    hmac_roam_connect_stru          st_connect;         /* 漫游connect信息 */
    hmac_roam_alg_stru              st_alg;             /* 漫游算法信息 */
    hmac_roam_static_stru           st_static;          /* 漫游统计信息 */
    frw_timeout_stru                st_timer;           /* 漫游使用的定时器 */
    wpas_connect_state_enum_uint32  ul_connected_state; /* 外部关联的状态进度 */
    oal_uint32                      ul_ip_addr_obtained;/* IP地址是否获取 */



}hmac_roam_info_stru;
typedef oal_uint32  (*hmac_roam_fsm_func)(hmac_roam_info_stru *pst_roam_info, oal_void *p_param);


/*****************************************************************************
  8 UNION定义
*****************************************************************************/


/*****************************************************************************
  9 OTHERS定义
*****************************************************************************/


/*****************************************************************************
  10 函数声明
*****************************************************************************/
oal_uint32 hmac_roam_alg_add_blacklist(hmac_roam_info_stru *pst_roam_info, oal_uint8 *puc_bssid, roam_blacklist_type_enum_uint8 list_type);
oal_uint32 hmac_roam_alg_add_history(hmac_roam_info_stru *pst_roam_info, oal_uint8 *puc_bssid);
oal_uint32 hmac_roam_alg_bss_check(hmac_roam_info_stru *pst_roam_info, mac_bss_dscr_stru *pst_bss_dscr);
oal_uint32 hmac_roam_alg_scan_channel_init(hmac_roam_info_stru *pst_roam_info, mac_scan_req_stru *pst_scan_params);
oal_void hmac_roam_alg_init(hmac_roam_info_stru *pst_roam_info, oal_int8 c_current_rssi);
mac_bss_dscr_stru *hmac_roam_alg_select_bss(hmac_roam_info_stru *pst_roam_info);
oal_bool_enum_uint8 hmac_roam_alg_find_in_blacklist(hmac_roam_info_stru *pst_roam_info, oal_uint8 *puc_bssid);
oal_bool_enum_uint8 hmac_roam_alg_find_in_history(hmac_roam_info_stru *pst_roam_info, oal_uint8 *puc_bssid);
oal_bool_enum_uint8 hmac_roam_alg_need_to_stop_roam_trigger(hmac_roam_info_stru *pst_roam_info);
oal_uint32 hmac_roam_alg_bss_in_ess(hmac_roam_info_stru *pst_roam_info, mac_bss_dscr_stru *pst_bss_dscr);
oal_uint32 hmac_roam_check_cipher_limit(hmac_roam_info_stru *pst_roam_info, mac_bss_dscr_stru *pst_bss_dscr);

#ifdef _PRE_WLAN_1102A_CHR
oal_void hmac_chr_roam_info_report(hmac_roam_info_stru *pst_roam_info, oal_uint8 ul_result);
#endif
#endif //_PRE_WLAN_FEATURE_ROAM

#ifdef __cplusplus
    #if __cplusplus
        }
    #endif
#endif

#endif /* end of hmac_roam_alg.h */
