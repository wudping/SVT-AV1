// clang-format off
/*
* Copyright(c) 2019 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef EbRateDistortionCost_h
#define EbRateDistortionCost_h

/***************************************
 * Includes
 ***************************************/
#include "EbIntraPrediction.h"
#include "EbInterPrediction.h"
#include "EbLambdaRateTables.h"
#include "EbTransforms.h"
#include "EbModeDecisionProcess.h"
#include "EbEncDecProcess.h"
#include "EbEntropyCoding.h"

#ifdef __cplusplus
extern "C" {
#endif
    extern uint64_t eb_av1_cost_coeffs_txb(
        uint8_t                             allow_update_cdf,
        FRAME_CONTEXT                      *ec_ctx,
        struct ModeDecisionCandidateBuffer *candidate_buffer_ptr,
        const TranLow                      *const qcoeff,
        uint16_t                            eob,
        PlaneType                           plane_type,
        TxSize                              transform_size,
        TxType                              transform_type,
        int16_t                             txb_skip_ctx,
        int16_t                             dc_sign_ctx,
        EbBool                              reducedTransformSetFlag);

    extern void coding_loop_context_generation(
        ModeDecisionContext   *context_ptr,
        CodingUnit            *cu_ptr,
        uint32_t               cu_origin_x,
        uint32_t               cu_origin_y,
        uint32_t               sb_sz,
        NeighborArrayUnit     *skip_coeff_neighbor_array,
        NeighborArrayUnit     *inter_pred_dir_neighbor_array,
        NeighborArrayUnit     *ref_frame_type_neighbor_array,
        NeighborArrayUnit     *intra_luma_mode_neighbor_array,
        NeighborArrayUnit     *skip_flag_neighbor_array,
        NeighborArrayUnit     *mode_type_neighbor_array,
        NeighborArrayUnit     *leaf_depth_neighbor_array,
        NeighborArrayUnit     *leaf_partition_neighbor_array);

    extern EbErrorType av1_tu_calc_cost(
        ModeDecisionCandidate *candidate_ptr,                         // input parameter, prediction result Ptr
        int16_t                  txb_skip_ctx,
        uint32_t                 tu_index,                              // input parameter, TU index inside the CU
        uint32_t                 y_count_non_zero_coeffs,               // input parameter, number of non zero Y quantized coefficients
        uint32_t                 cb_count_non_zero_coeffs,              // input parameter, number of non zero cb quantized coefficients
        uint32_t                 cr_count_non_zero_coeffs,              // input parameter, number of non zero cr quantized coefficients
        uint64_t                 y_tu_distortion[DIST_CALC_TOTAL],      // input parameter, Y distortion for both Normal and Cbf zero modes
        uint64_t                 cb_tu_distortion[DIST_CALC_TOTAL],     // input parameter, Cb distortion for both Normal and Cbf zero modes
        uint64_t                 cr_tu_distortion[DIST_CALC_TOTAL],     // input parameter, Cr distortion for both Normal and Cbf zero modes
        COMPONENT_TYPE           component_type,
        uint64_t                *y_tu_coeff_bits,                        // input parameter, Y quantized coefficients rate
        uint64_t                *cb_tu_coeff_bits,                       // input parameter, Cb quantized coefficients rate
        uint64_t                *cr_tu_coeff_bits,                       // input parameter, Cr quantized coefficients rate
        TxSize                   txsize,
        uint64_t                 lambda);                                // input parameter, lambda for Luma

    extern EbErrorType tu_calc_cost(
        uint32_t                 cu_size,
        ModeDecisionCandidate *candidate_ptr,
        uint32_t                 tu_index,
        uint32_t                 transform_size,
        uint32_t                 transform_chroma_size,
        uint32_t                 y_count_non_zero_coeffs,
        uint32_t                 cb_count_non_zero_coeffs,
        uint32_t                 cr_count_non_zero_coeffs,
        uint64_t                 y_tu_distortion[DIST_CALC_TOTAL],
        uint64_t                 cb_tu_distortion[DIST_CALC_TOTAL],
        uint64_t                 cr_tu_distortion[DIST_CALC_TOTAL],
        uint32_t                 component_mask,
        uint64_t                *y_tu_coeff_bits,
        uint64_t                *cb_tu_coeff_bits,
        uint64_t                *cr_tu_coeff_bits,
        uint32_t                 qp,
        uint64_t                 lambda,
        uint64_t                 lambda_chroma);
    extern EbErrorType av1_tu_calc_cost_luma(
        int16_t                  txb_skip_ctx,
        ModeDecisionCandidate *candidate_ptr,                        // input parameter, prediction result Ptr
        uint32_t                 tu_index,                             // input parameter, TU index inside the CU
        TxSize                   txSize,
        uint32_t                 y_count_non_zero_coeffs,                 // input parameter, number of non zero Y quantized coefficients
        uint64_t                 y_tu_distortion[DIST_CALC_TOTAL],      // input parameter, Y distortion for both Normal and Cbf zero modes
        uint64_t                *y_tu_coeff_bits,                        // input parameter, Y quantized coefficients rate
        uint64_t                *y_full_cost,
        uint64_t                 lambda);                              // input parameter, lambda for Luma

    extern EbErrorType intra_luma_mode_context(
        CodingUnit *cu_ptr,
        uint32_t      luma_mode,
        int32_t      *prediction_index);
    extern EbErrorType intra2_nx2_n_fast_cost_islice(
        CodingUnit                          *cu_ptr,
        struct ModeDecisionCandidateBuffer  *candidate_buffer_ptr,
        uint32_t                               qp,
        uint64_t                               luma_distortion,
        uint64_t                               chroma_distortion,
        uint64_t                               lambda,
        PictureControlSet                   *pcs_ptr);
    extern EbErrorType  merge_skip_full_cost(
        SuperBlock                  *sb_ptr,
        CodingUnit                  *cu_ptr,
        uint32_t                       cu_size,
        uint32_t                       cu_size_log2,
        ModeDecisionCandidateBuffer *candidate_buffer_ptr,
        uint32_t                       qp,
        uint64_t                      *y_distortion,
        uint64_t                      *cb_distortion,
        uint64_t                      *cr_distortion,
        uint64_t                       lambda,
        uint64_t                       lambda_chroma,
        uint64_t                      *y_coeff_bits,
        uint64_t                      *cb_coeff_bits,
        uint64_t                      *cr_coeff_bits,
        uint32_t                       transform_size,
        uint32_t                       transform_chroma_size,
        PictureControlSet           *pcs_ptr);
    extern EbErrorType split_flag_rate(
        ModeDecisionContext     *context_ptr,
        CodingUnit              *cu_ptr,
        uint32_t                   split_flag,
        uint64_t                  *split_rate,
        uint64_t                   lambda,
        MdRateEstimationContext *md_rate_estimation_ptr,
        uint32_t                   tb_max_depth);

#define RDDIV_BITS 7

#define RDCOST(RM, R, D)                                            \
  (ROUND_POWER_OF_TWO(((uint64_t)(R)) * (RM), AV1_PROB_COST_SHIFT) + \
   ((D) * (1 << RDDIV_BITS)))

    extern EbErrorType av1_split_flag_rate(
        SequenceControlSet      *scs_ptr,
        ModeDecisionContext     *context_ptr,
        CodingUnit              *cu_ptr,
        uint32_t                   leaf_index,
        PartitionType              partitionType,
        uint64_t                  *split_rate,
        uint64_t                   lambda,
        MdRateEstimationContext *md_rate_estimation_ptr,
        uint32_t                   tb_max_depth);

    extern EbErrorType av1_encode_tu_calc_cost(
        EncDecContext *context_ptr,
        uint32_t        *count_non_zero_coeffs,
        uint64_t         y_tu_distortion[DIST_CALC_TOTAL],
        uint64_t        *y_tu_coeff_bits,
        uint32_t         component_mask);

    extern uint64_t av1_intra_fast_cost(
        CodingUnit            *cu_ptr,
        ModeDecisionCandidate *candidate_ptr,
        uint32_t                 qp,
        uint64_t                 luma_distortion,
        uint64_t                 chroma_distortion,
        uint64_t                 lambda,
        EbBool                   use_ssd,
        PictureControlSet     *pcs_ptr,
        CandidateMv             *ref_mv_stack,
        const BlockGeom         *blk_geom,
        uint32_t                 miRow,
        uint32_t                 miCol,
#if MULTI_PASS_PD
        uint8_t                  enable_inter_intra,
        EbBool                   full_cost_shut_fast_rate_flag,
#endif
        uint8_t                  md_pass,
        uint32_t                 left_neighbor_mode,
        uint32_t                 top_neighbor_mode);

    extern uint64_t av1_inter_fast_cost(
        CodingUnit            *cu_ptr,
        ModeDecisionCandidate *candidate_ptr,
        uint32_t                 qp,
        uint64_t                 luma_distortion,
        uint64_t                 chroma_distortion,
        uint64_t                 lambda,
        EbBool                   use_ssd,
        PictureControlSet     *pcs_ptr,
        CandidateMv             *ref_mv_stack,
        const BlockGeom         *blk_geom,
        uint32_t                 miRow,
        uint32_t                 miCol,
#if MULTI_PASS_PD
        uint8_t                  enable_inter_intra,
        EbBool                   full_cost_shut_fast_rate_flag,
#endif
        uint8_t                  md_pass,
        uint32_t                 left_neighbor_mode,
        uint32_t                 top_neighbor_mode);

#if ADD_MDC_FULL_COST
    uint64_t mdc_av1_inter_fast_cost(
        CodingUnit             *cu_ptr,
        ModeDecisionCandidate  *candidate_ptr,
        uint64_t                 luma_distortion,
        uint64_t                 lambda,
        EbBool                   use_ssd,
        PictureControlSet       *pcs_ptr,
        CandidateMv             *ref_mv_stack,
        const BlockGeom         *blk_geom);
#endif

    extern EbErrorType av1_intra_full_cost(
        PictureControlSet                    *pcs_ptr,
        ModeDecisionContext                  *context_ptr,
        struct ModeDecisionCandidateBuffer   *candidate_buffer_ptr,
        CodingUnit                           *cu_ptr,
        uint64_t                                 *y_distortion,
        uint64_t                                 *cb_distortion,
        uint64_t                                 *cr_distortion,
        uint64_t                                  lambda,
        uint64_t                                 *y_coeff_bits,
        uint64_t                                 *cb_coeff_bits,
        uint64_t                                 *cr_coeff_bits,
        BlockSize                              bsize);

    extern EbErrorType av1_inter_full_cost(
        PictureControlSet                    *pcs_ptr,
        ModeDecisionContext                  *context_ptr,
        struct ModeDecisionCandidateBuffer   *candidate_buffer_ptr,
        CodingUnit                           *cu_ptr,
        uint64_t                                 *y_distortion,
        uint64_t                                 *cb_distortion,
        uint64_t                                 *cr_distortion,
        uint64_t                                  lambda,
        uint64_t                                 *y_coeff_bits,
        uint64_t                                 *cb_coeff_bits,
        uint64_t                                 *cr_coeff_bits,
        BlockSize                                bsize);

#if ADD_MDC_FULL_COST
    int32_t av1_cost_skip_txb(
        uint8_t                                 allow_update_cdf,
        FRAME_CONTEXT                           *ec_ctx,
        struct ModeDecisionCandidateBuffer    *candidate_buffer_ptr,
        TxSize                                  transform_size,
        PlaneType                               plane_type,
        int16_t                                   txb_skip_ctx);
#endif

    extern uint64_t get_tx_size_bits(
        ModeDecisionCandidateBuffer          *candidateBuffer,
        ModeDecisionContext                  *context_ptr,
        PictureControlSet                    *pcs_ptr,
        uint8_t                               tx_depth,
        EbBool                                block_has_coeff);
#ifdef __cplusplus
}
#endif
#endif //EbRateDistortionCost_h
// clang-format on
