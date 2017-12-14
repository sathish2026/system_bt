/******************************************************************************
 * Copyright (C) 2017, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 ******************************************************************************/
/******************************************************************************
 *
 *  Copyright (C) 2016 The Android Open Source Project
 *  Copyright (C) 2009-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#define LOG_TAG "bt_btif_a2dp"

#include <stdbool.h>

#include "audio_a2dp_hw/include/audio_a2dp_hw.h"
#include "bt_common.h"
#include "bta_av_api.h"
#include "btif_a2dp.h"
#include "btif_a2dp_control.h"
#include "btif_a2dp_sink.h"
#include "btif_a2dp_source.h"
#include "btif_av.h"
#include "btif_util.h"
#include "osi/include/log.h"
#include "btif_a2dp_audio_interface.h"
#include "btif_hf.h"

#ifdef ENABLE_SPLIT_A2DP
extern bool btif_a2dp_audio_if_init;
#endif // ENABLE_SPLIT_A2DP
extern tBTIF_A2DP_SOURCE_VSC btif_a2dp_src_vsc;
extern void btif_av_reset_reconfig_flag();

void btif_a2dp_on_idle(int index) {
  APPL_TRACE_EVENT("## ON A2DP IDLE ## peer_sep = %d", btif_av_get_peer_sep(index));
  if (btif_av_get_peer_sep(index) == AVDT_TSEP_SNK) {
    btif_a2dp_source_on_idle();
  } else if (btif_av_get_peer_sep(index) == AVDT_TSEP_SRC) {
    btif_a2dp_sink_on_idle();
  }
}

bool btif_a2dp_on_started(tBTA_AV_START* p_av_start, bool pending_start,
                          tBTA_AV_HNDL hdl) {
  bool ack = false;
  tA2DP_CTRL_CMD pending_cmd = btif_a2dp_get_pending_command();

  APPL_TRACE_WARNING("## ON A2DP STARTED ##");

  if (p_av_start == NULL) {
    /* ack back a local start request */
#ifdef ENABLE_SPLIT_A2DP
    if (btif_av_is_split_a2dp_enabled()) {
      if (btif_hf_is_call_vr_idle())
        //btif_dispatch_sm_event(BTIF_AV_OFFLOAD_START_REQ_EVT, NULL, 0);
        btif_dispatch_sm_event(BTIF_AV_OFFLOAD_START_REQ_EVT, (char *)&hdl, 1);
      else {
        APPL_TRACE_ERROR("call in progress, do not start offload");
        btif_a2dp_audio_on_started(A2DP_CTRL_ACK_INCALL_FAILURE);
      }
      return true;
    } else {
#endif // ENABLE_SPLIT_A2DP
      if (pending_cmd == A2DP_CTRL_CMD_START)
        btif_a2dp_command_ack(A2DP_CTRL_ACK_SUCCESS);
      return true;
#ifdef ENABLE_SPLIT_A2DP
    }
#endif // ENABLE_SPLIT_A2DP
  }

  APPL_TRACE_WARNING(
      "%s: pending_start = %d status = %d suspending = %d initiator = %d",
      __func__, pending_start, p_av_start->status, p_av_start->suspending,
      p_av_start->initiator);

  if (p_av_start->status == BTA_AV_SUCCESS) {
    if (!p_av_start->suspending) {
      if (p_av_start->initiator) {
        if (pending_start) {
#ifdef ENABLE_SPLIT_A2DP
          if (btif_av_is_split_a2dp_enabled()) {
            btif_dispatch_sm_event(BTIF_AV_OFFLOAD_START_REQ_EVT, (char *)&hdl, 1);
          } else {
#endif // ENABLE_SPLIT_A2DP
            if (pending_cmd == A2DP_CTRL_CMD_START)
              btif_a2dp_command_ack(A2DP_CTRL_ACK_SUCCESS);
#ifdef ENABLE_SPLIT_A2DP
          }
#endif // ENABLE_SPLIT_A2DP
          ack = true;
        }
      } else {
        /* We were remotely started, make sure codec
         * is setup before datapath is started.
         */
         btif_a2dp_source_setup_codec(hdl);
#ifdef ENABLE_SPLIT_A2DP
         if (btif_av_is_split_a2dp_enabled()) {
           APPL_TRACE_IMP("Do not Initiate VSC exchange on remote start");
         }
#endif // ENABLE_SPLIT_A2DP
         ack = true;
      }

      /* media task is autostarted upon a2dp audiopath connection */
    }
  } else if (pending_start) {
    APPL_TRACE_WARNING("%s: A2DP start request failed: status = %d", __func__,
                       p_av_start->status);
    if (pending_cmd == A2DP_CTRL_CMD_START)
      btif_a2dp_command_ack(A2DP_CTRL_ACK_FAILURE);
    ack = true;
  }
  return ack;
}

void btif_a2dp_on_stopped(tBTA_AV_SUSPEND* p_av_suspend) {
  APPL_TRACE_WARNING("## ON A2DP STOPPED ##");

  int idx = btif_av_get_latest_playing_device_idx();
  if (btif_av_get_peer_sep(idx) == AVDT_TSEP_SRC) {
    btif_a2dp_sink_on_stopped(p_av_suspend);
    return;
  }
#ifdef ENABLE_SPLIT_A2DP
  if (!btif_av_is_split_a2dp_enabled()) {
#endif // ENABLE_SPLIT_A2DP
    btif_a2dp_source_on_stopped(p_av_suspend);
#ifdef ENABLE_SPLIT_A2DP
  } else { //TODO send command to btif_a2dp_audio_interface
    if (btif_a2dp_audio_if_init) {
      if (p_av_suspend != NULL)
        btif_a2dp_audio_on_stopped(p_av_suspend->status);
    }
    else
        APPL_TRACE_EVENT("btif_a2dp_on_stopped, audio interface not up");
  }
#endif // ENABLE_SPLIT_A2DP
}

void btif_a2dp_on_suspended(tBTA_AV_SUSPEND* p_av_suspend) {
  APPL_TRACE_EVENT("## ON A2DP SUSPENDED ##");
  int idx = btif_av_get_latest_playing_device_idx();
#ifdef ENABLE_SPLIT_A2DP
  if (!btif_av_is_split_a2dp_enabled()) {
#endif // ENABLE_SPLIT_A2DP
    if (btif_av_get_peer_sep(idx) == AVDT_TSEP_SRC) {
      btif_a2dp_sink_on_suspended(p_av_suspend);
    } else {
      btif_a2dp_source_on_suspended(p_av_suspend);
    }
#ifdef ENABLE_SPLIT_A2DP
  } else {
     if (p_av_suspend != NULL)
       btif_a2dp_audio_on_suspended(p_av_suspend->status);
  }
#endif // ENABLE_SPLIT_A2DP
}

void btif_a2dp_on_offload_started(tBTA_AV_STATUS status) {
  tA2DP_CTRL_ACK ack;
  APPL_TRACE_EVENT("%s status %d", __func__, status);

  switch (status) {
    case BTA_AV_SUCCESS:
      btif_a2dp_src_vsc.tx_start_initiated = FALSE;
      btif_a2dp_src_vsc.tx_started = TRUE;
      ack = A2DP_CTRL_ACK_SUCCESS;
      break;
    case BTA_AV_FAIL_RESOURCES:
      APPL_TRACE_ERROR("%s FAILED UNSUPPORTED", __func__);
      btif_a2dp_src_vsc.tx_start_initiated = FALSE;
      ack = A2DP_CTRL_ACK_UNSUPPORTED;
      break;
    default:
      APPL_TRACE_ERROR("%s FAILED: status = %d", __func__, status);
      btif_a2dp_src_vsc.tx_start_initiated = FALSE;
      ack = A2DP_CTRL_ACK_FAILURE;
      break;
  }
#ifdef ENABLE_SPLIT_A2DP
  if (btif_av_is_split_a2dp_enabled()) {
    btif_av_reset_reconfig_flag();
    btif_a2dp_audio_on_started(status);
    if (ack != BTA_AV_SUCCESS &&
        btif_av_stream_started_ready()) {
      /* Offload request will return with failure from btif_av sm if
      ** suspend is triggered for remote start. Disconnect only if SoC
      ** returned failure for offload VSC
      */
      APPL_TRACE_ERROR("%s offload start failed", __func__);
      RawAddress bd_addr;
      btif_av_get_peer_addr(&bd_addr);
      btif_dispatch_sm_event(BTIF_AV_DISCONNECT_REQ_EVT, (void *)bd_addr.address,
                             sizeof(RawAddress));
    }
  } else {
#endif // ENABLE_SPLIT_A2DP
    btif_a2dp_command_ack(ack);
#ifdef ENABLE_SPLIT_A2DP
  }
#endif // ENABLE_SPLIT_A2DP
}

void btif_a2dp_honor_remote_start() {
  APPL_TRACE_WARNING("%s",__func__);
  btif_a2dp_source_on_remote_start();
}

void btif_debug_a2dp_dump(int fd) {
  btif_a2dp_source_debug_dump(fd);
  btif_a2dp_sink_debug_dump(fd);
}
