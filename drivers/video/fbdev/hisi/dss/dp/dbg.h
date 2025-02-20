/*
 * Copyright (c) 2016 Synopsys, Inc.
 *
 * Synopsys DP TX Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/*
* Copyright (c) 2017 Hisilicon Tech. Co., Ltd. Integrated into the Hisilicon display system.
*/

#ifndef __DPTX_DBG_H__
#define __DPTX_DBG_H__

/*#define DPTX_DEBUG_REG*/
/*#define DPTX_DEBUG_AUX*/
#define DPTX_DEBUG_IRQ
#define DPTX_DEBUG_DPCD_CMDS

#define dptx_dbg(_dp, _fmt...) dev_dbg(_dp->dev, _fmt)
#define dptx_info(_dp, _fmt...) dev_info(_dp->dev, _fmt)
#define dptx_warn(_dp, _fmt...) dev_warn(_dp->dev, _fmt)
#define dptx_err(_dp, _fmt...) dev_err(_dp->dev, _fmt)

#ifdef DPTX_DEBUG_AUX
#define dptx_dbg_aux(_dp, _fmt...) dev_dbg(_dp->dev, _fmt)
#else
#define dptx_dbg_aux(_dp, _fmt...)
#endif

#ifdef DPTX_DEBUG_IRQ
#define dptx_dbg_irq(_dp, _fmt...) dev_dbg(_dp->dev, _fmt)
#else
#define dptx_dbg_irq(_dp, _fmt...)
#endif

#endif
