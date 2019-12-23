// clang-format off
/*
* Copyright(c) 2019 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "EbPictureControlSet.h"
#include "EbSequenceControlSet.h"

#include "EbSourceBasedOperationsProcess.h"
#include "EbInitialRateControlResults.h"
#include "EbPictureDemuxResults.h"
#include "emmintrin.h"
#include "EbEncHandle.h"
#include "EbUtility.h"

/**************************************
 * Context
 **************************************/

typedef struct SourceBasedOperationsContext
{
    EbDctor  dctor;
    EbFifo  *initial_rate_control_results_input_fifo_ptr;
    EbFifo  *picture_demux_results_output_fifo_ptr;
    // local zz cost array
    uint32_t    complete_sb_count;
    uint8_t    *y_mean_ptr;
    uint8_t    *cr_mean_ptr;
    uint8_t    *cb_mean_ptr;
} SourceBasedOperationsContext;

static void source_based_operations_context_dctor(EbPtr p)
{
    EbThreadContext   *thread_context_ptr = (EbThreadContext*)p;
    SourceBasedOperationsContext* obj = (SourceBasedOperationsContext*)thread_context_ptr->priv;
    EB_FREE_ARRAY(obj);
}

/************************************************
* Source Based Operation Context Constructor
************************************************/
EbErrorType source_based_operations_context_ctor(
    EbThreadContext     *thread_context_ptr,
    const EbEncHandle   *enc_handle_ptr,
    int index)
{
    SourceBasedOperationsContext  *context_ptr;
    EB_CALLOC_ARRAY(context_ptr, 1);
    thread_context_ptr->priv = context_ptr;
    thread_context_ptr->dctor = source_based_operations_context_dctor;

    context_ptr->initial_rate_control_results_input_fifo_ptr =
        eb_system_resource_get_consumer_fifo(enc_handle_ptr->initial_rate_control_results_resource_ptr, index);
    context_ptr->picture_demux_results_output_fifo_ptr       =
        eb_system_resource_get_producer_fifo(enc_handle_ptr->picture_demux_results_resource_ptr, index);
    return EB_ErrorNone;
}

/***************************************************
* Derives BEA statistics and set activity flags
***************************************************/
void derive_picture_activity_statistics(
    SequenceControlSet            *scs_ptr,
    PictureParentControlSet       *pcs_ptr)

{
    uint64_t               nonMovingIndexMin = ~0u;
    uint64_t               nonMovingIndexMax = 0;
    uint64_t               nonMovingIndexSum = 0;
    uint32_t               complete_sb_count = 0;
    uint32_t               non_moving_sb_count = 0;
    uint32_t               sb_total_count = pcs_ptr->sb_total_count;
    uint32_t                 totNmvIdx = 0;

    uint32_t               sb_index;
    for (sb_index = 0; sb_index < sb_total_count; ++sb_index) {
        SbParams *sb_params = &scs_ptr->sb_params_array[sb_index];
        if (sb_params->is_complete_sb)
        {
            nonMovingIndexMin = pcs_ptr->non_moving_index_array[sb_index] < nonMovingIndexMin ?
                pcs_ptr->non_moving_index_array[sb_index] :
                nonMovingIndexMin;

            nonMovingIndexMax = pcs_ptr->non_moving_index_array[sb_index] > nonMovingIndexMax ?
                pcs_ptr->non_moving_index_array[sb_index] :
                nonMovingIndexMax;
            if (pcs_ptr->non_moving_index_array[sb_index] < NON_MOVING_SCORE_1)
                non_moving_sb_count++;
            complete_sb_count++;

            nonMovingIndexSum += pcs_ptr->non_moving_index_array[sb_index];

            if (pcs_ptr->non_moving_index_array[sb_index] < NON_MOVING_SCORE_1)
                totNmvIdx++;
        }
    }

    if (complete_sb_count > 0) {
        pcs_ptr->non_moving_index_average = (uint16_t)(nonMovingIndexSum / complete_sb_count);
        pcs_ptr->kf_zeromotion_pct = (non_moving_sb_count * 100) / complete_sb_count;
    }
    pcs_ptr->non_moving_index_min_distance = (uint16_t)(ABS((int32_t)(pcs_ptr->non_moving_index_average) - (int32_t)nonMovingIndexMin));
    pcs_ptr->non_moving_index_max_distance = (uint16_t)(ABS((int32_t)(pcs_ptr->non_moving_index_average) - (int32_t)nonMovingIndexMax));
    return;
}

/************************************************
 * Source Based Operations Kernel
 ************************************************/
void* source_based_operations_kernel(void *input_ptr)
{
    EbThreadContext                 *thread_context_ptr = (EbThreadContext*)input_ptr;
    SourceBasedOperationsContext    *context_ptr = (SourceBasedOperationsContext*)thread_context_ptr->priv;
    PictureParentControlSet       *pcs_ptr;
    SequenceControlSet            *scs_ptr;
    EbObjectWrapper               *inputResultsWrapperPtr;
    InitialRateControlResults        *in_results_ptr;
    EbObjectWrapper               *outputResultsWrapperPtr;
    PictureDemuxResults           *out_results_ptr;

    for (;;) {
        // Get Input Full Object
        eb_get_full_object(
            context_ptr->initial_rate_control_results_input_fifo_ptr,
            &inputResultsWrapperPtr);

        in_results_ptr = (InitialRateControlResults*)inputResultsWrapperPtr->object_ptr;
        pcs_ptr = (PictureParentControlSet*)in_results_ptr->picture_control_set_wrapper_ptr->object_ptr;
        scs_ptr = (SequenceControlSet*)pcs_ptr->sequence_control_set_wrapper_ptr->object_ptr;
        pcs_ptr->dark_back_groundlight_fore_ground = EB_FALSE;
        context_ptr->complete_sb_count = 0;
        uint32_t sb_total_count = pcs_ptr->sb_total_count;
        uint32_t sb_index;

        /***********************************************LCU-based operations************************************************************/
        for (sb_index = 0; sb_index < sb_total_count; ++sb_index) {
            SbParams *sb_params = &scs_ptr->sb_params_array[sb_index];
            EbBool is_complete_sb = sb_params->is_complete_sb;
            uint8_t  *y_mean_ptr = pcs_ptr->y_mean[sb_index];

            _mm_prefetch((const char*)y_mean_ptr, _MM_HINT_T0);
            uint8_t  *cr_mean_ptr = pcs_ptr->crMean[sb_index];
            uint8_t  *cb_mean_ptr = pcs_ptr->cbMean[sb_index];

            _mm_prefetch((const char*)cr_mean_ptr, _MM_HINT_T0);
            _mm_prefetch((const char*)cb_mean_ptr, _MM_HINT_T0);

            context_ptr->y_mean_ptr = y_mean_ptr;
            context_ptr->cr_mean_ptr = cr_mean_ptr;
            context_ptr->cb_mean_ptr = cb_mean_ptr;

            if (is_complete_sb) {
                context_ptr->complete_sb_count++;
            }
        }
        /*********************************************Picture-based operations**********************************************************/

        // Activity statistics derivation
        derive_picture_activity_statistics(
            scs_ptr,
            pcs_ptr);

        // Get Empty Results Object
        eb_get_empty_object(
            context_ptr->picture_demux_results_output_fifo_ptr,
            &outputResultsWrapperPtr);

        out_results_ptr = (PictureDemuxResults*)outputResultsWrapperPtr->object_ptr;
        out_results_ptr->picture_control_set_wrapper_ptr = in_results_ptr->picture_control_set_wrapper_ptr;
        out_results_ptr->picture_type = EB_PIC_INPUT;

        // Release the Input Results
        eb_release_object(inputResultsWrapperPtr);

        // Post the Full Results Object
        eb_post_full_object(outputResultsWrapperPtr);
    }
    return EB_NULL;
}
// clang-format on
