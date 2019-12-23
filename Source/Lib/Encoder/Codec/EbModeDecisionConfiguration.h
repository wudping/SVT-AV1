// clang-format off
/*
* Copyright(c) 2019 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef EbModeDecisionConfiguration_h
#define EbModeDecisionConfiguration_h

#include "EbModeDecisionConfigurationProcess.h"
#include "EbSequenceControlSet.h"
#include "EbPictureControlSet.h"
#ifdef __cplusplus
extern "C" {
#endif

/*******************************************
 * early_mode_decision_lcu
 *   predicts candidates (LCU)
 *******************************************/
extern EbErrorType early_mode_decision_lcu(
    SequenceControlSet                   *scs_ptr,
    PictureControlSet                    *pcs_ptr,
    SuperBlock                           *sb_ptr,
    uint32_t                                  sb_index,
    ModeDecisionConfigurationContext     *context_ptr);

/*******************************************
* derive_delta_qp_for_each_leaf_lcu
*   Derive Lcu For Each Leaf (LCU)
*******************************************/
extern EbErrorType derive_delta_qp_for_each_leaf_lcu(
    SequenceControlSet                   *scs_ptr,
    PictureControlSet                    *pcs_ptr,
    SuperBlock                           *sb_ptr,
    uint32_t                                  sb_index,
    int32_t                                  intra_min_distance,
    int32_t                                  intra_max_distance,
    int32_t                                  inter_min_distance,
    int32_t                                  inter_max_distance,
    ModeDecisionConfigurationContext     *context_ptr);

void qpm_derive_delta_qp_map_weights(
    ModeDecisionConfigurationContext    *context_ptr,
    PictureControlSet                  *pcs_ptr);
/**************************************
* Function Ptrs Definitions
**************************************/
typedef EbErrorType(*EB_MDC_FUNC)(
    MdcpLocalCodingUnit                   *localCuArray,
    uint32_t                                   cu_index,
    uint32_t                                   depth,
    EbBool                                 *mdcPrediction64);

#define Pred        0x01
#define Predp1      0x02
#define Predp2      0x04
#define Predp3      0x08
#define Predm1      0x10
#define Predm2      0x20
#define Predm3      0x40
#define ALL64       0x0F
#define ALL32       0x17
#define ALL16       0x33
#define ALL8        0x71
#define AllD        0x80
#if ADD_MDC_REFINEMENT_LOOP
#define Predm1p1    0x90
#define Predm1p2    0xA0
#define Predm1p3    0xB0
#define Predm2p3    0xC0
#define D1all       0x01
#define D1sqonly    0x02
#define Comp_no_4xn 0xA0
#endif
#if MDC_ADAPTIVE_LEVEL
#define Predm2p1 0xA1
#define Predm2p2 0xA2
#define Predm3p2 0xA3
#define Predm3p3 0xA4
#define Predm3p1 0xA5
#endif

EB_ALIGN(16) static const uint8_t ndp_level_0[4] = {Pred + Predp1 + Predp2, Pred + Predp1, Pred + Predp1, Pred + Predm1};
EB_ALIGN(16) static const uint8_t ndp_level_1[4] = {Pred + Predp1         , Pred + Predp1, Pred + Predp1, Pred + Predm1 };

#ifdef __cplusplus
}
#endif
#endif // EbModeDecisionConfiguration_h
// clang-format on
