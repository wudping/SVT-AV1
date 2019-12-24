// clang-format off
/*
* Copyright(c) 2019 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef EbPictureControlSet_h
#define EbPictureControlSet_h

#include <time.h>

#include "EbSvtAv1Enc.h"
#include "EbDefinitions.h"
#include "EbSystemResourceManager.h"
#include "EbPictureBufferDesc.h"
#include "EbCodingUnit.h"
#include "EbEntropyCodingObject.h"
#include "EbDefinitions.h"
#include "EbPredictionStructure.h"
#include "EbNeighborArrays.h"
#include "EbModeDecisionSegments.h"
#include "EbEncDecSegments.h"
#include "EbRateControlTables.h"
#include "EbRestoration.h"
#include "EbObject.h"
#include "noise_model.h"
#include "EbSegmentationParams.h"
#include "EbAv1Structs.h"
#include "EbMdRateEstimation.h"
#include "EbCdef.h"

#include"av1me.h"
#include "hash_motion.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SEGMENT_ENTROPY_BUFFER_SIZE         40000000 // Entropy Bitstream Buffer Size
#define PACKETIZATION_PROCESS_BUFFER_SIZE SEGMENT_ENTROPY_BUFFER_SIZE
#define PACKETIZATION_PROCESS_SPS_BUFFER_SIZE 2000
#define HISTOGRAM_NUMBER_OF_BINS            256
#define MAX_NUMBER_OF_REGIONS_IN_WIDTH      4
#define MAX_NUMBER_OF_REGIONS_IN_HEIGHT     4
#define MAX_REF_QP_NUM                      81
    // Segment Macros
#define SEGMENT_MAX_COUNT   64
#define SEGMENT_COMPLETION_MASK_SET(mask, index)        MULTI_LINE_MACRO_BEGIN (mask) |= (((uint64_t) 1) << (index)); MULTI_LINE_MACRO_END
#define SEGMENT_COMPLETION_MASK_TEST(mask, total_count)  (mask) == ((((uint64_t) 1) << (total_count)) - 1)
#define SEGMENT_ROW_COMPLETION_TEST(mask, row_index, width) ((((mask) >> ((row_index) * (width))) & ((1ull << (width))-1)) == ((1ull << (width))-1))
#define SEGMENT_CONVERT_IDX_TO_XY(index, x, y, pic_width_in_lcu) \
                                                        MULTI_LINE_MACRO_BEGIN \
                                                            (y) = (index) / (pic_width_in_lcu); \
                                                            (x) = (index) - (y) * (pic_width_in_lcu); \
                                                        MULTI_LINE_MACRO_END
#define SEGMENT_START_IDX(index, pic_size_in_lcu, num_of_seg) (((index) * (pic_size_in_lcu)) / (num_of_seg))
#define SEGMENT_END_IDX(index, pic_size_in_lcu, num_of_seg)   ((((index)+1) * (pic_size_in_lcu)) / (num_of_seg))

// BDP OFF
#define MD_NEIGHBOR_ARRAY_INDEX                0
#if MULTI_PASS_PD
#define MULTI_STAGE_PD_NEIGHBOR_ARRAY_INDEX    4
#endif
#if MULTI_PASS_PD
#define NEIGHBOR_ARRAY_TOTAL_COUNT             5
#else
#define NEIGHBOR_ARRAY_TOTAL_COUNT             4
#endif
#define AOM_QM_BITS                            5


    static const int32_t tx_size_2d[TX_SIZES_ALL + 1] = {
        16, 64, 256, 1024, 4096, 32, 32, 128, 128, 512,
        512, 2048, 2048, 64, 64, 256, 256, 1024, 1024,
    };

    struct Buf2d
    {
        uint8_t *buf;
        uint8_t *buf0;
        int32_t  width;
        int32_t  height;
        int32_t  stride;
    };

    typedef struct MacroblockPlane
    {
        // Quantizer setings
        // These are used/accessed only in the quantization process
        // RDO does not / must not depend on any of these values
        // All values below share the coefficient scale/shift used in TX
        const int16_t *quant_fp_QTX;
        const int16_t *round_fp_QTX;
        const int16_t *quant_QTX;
        const int16_t *quant_shift_QTX;
        const int16_t *zbin_QTX;
        const int16_t *round_QTX;
        const int16_t *dequant_QTX;
    } MacroblockPlane;

    // The Quants structure is used only for internal quantizer setup in
    // av1_quantize.c.
    // All of its fields use the same coefficient shift/scaling at TX.
    typedef struct Quants
    {
        // 0: dc 1: ac 2-8: ac repeated to SIMD width
        DECLARE_ALIGNED(16, int16_t, y_quant[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, y_quant_shift[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, y_zbin[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, y_round[QINDEX_RANGE][8]);

        // TODO(jingning): in progress of re-working the quantization. will decide
        // if we want to deprecate the current use of y_quant.
        DECLARE_ALIGNED(16, int16_t, y_quant_fp[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, u_quant_fp[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, v_quant_fp[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, y_round_fp[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, u_round_fp[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, v_round_fp[QINDEX_RANGE][8]);

        DECLARE_ALIGNED(16, int16_t, u_quant[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, v_quant[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, u_quant_shift[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, v_quant_shift[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, u_zbin[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, v_zbin[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, u_round[QINDEX_RANGE][8]);
        DECLARE_ALIGNED(16, int16_t, v_round[QINDEX_RANGE][8]);
    } Quants;

    // The Dequants structure is used only for internal quantizer setup in
    // av1_quantize.c.
    // Fields are sufffixed according to whether or not they're expressed in
    // the same coefficient shift/precision as TX or a fixed Q3 format.
    typedef struct Dequants
    {
        DECLARE_ALIGNED(16, int16_t,
        y_dequant_QTX[QINDEX_RANGE][8]);  // 8: SIMD width
        DECLARE_ALIGNED(16, int16_t,
        u_dequant_QTX[QINDEX_RANGE][8]);  // 8: SIMD width
        DECLARE_ALIGNED(16, int16_t,
        v_dequant_QTX[QINDEX_RANGE][8]);              // 8: SIMD width
        DECLARE_ALIGNED(16, int16_t, y_dequant_Q3[QINDEX_RANGE][8]);  // 8: SIMD width
        DECLARE_ALIGNED(16, int16_t, u_dequant_Q3[QINDEX_RANGE][8]);  // 8: SIMD width
        DECLARE_ALIGNED(16, int16_t, v_dequant_Q3[QINDEX_RANGE][8]);  // 8: SIMD width
    } Dequants;

    typedef struct MacroblockdPlane
    {
        //TranLow *dqcoeff;
        PlaneType plane_type;
        int32_t subsampling_x;
        int32_t subsampling_y;
        struct Buf2d dst;
        int32_t is16Bit;
        //struct Buf2d pre[2];
        //EntropyContext *above_context;
        //EntropyContext *left_context;
        // The dequantizers below are true dequntizers used only in the
        // dequantization process.  They have the same coefficient
        // shift/scale as TX.
        //int16_t seg_dequant_QTX[MAX_SEGMENTS][2];
        //uint8_t *color_index_map;
        // block size in pixels
        //uint8_t width, height;
        //QmVal *seg_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
        //QmVal *seg_qmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
        // the 'dequantizers' below are not literal dequantizer values.
        // They're used by encoder RDO to generate ad-hoc lambda values.
        // They use a hardwired Q3 coeff shift and do not necessarily match
        // the TX scale in use.
        //const int16_t *dequant_Q3;
    } MacroblockdPlane;

    struct PredictionUnit;

    typedef struct Av1Common
    {
        int32_t  mi_rows;
        int32_t  mi_cols;
        int32_t ref_frame_sign_bias[REF_FRAMES]; /* Two state 0, 1 */
        InterpFilter interp_filter;
        int32_t mi_stride;

        // Marks if we need to use 16bit frame buffers (1: yes, 0: no).
        int32_t use_highbitdepth;
        int32_t bit_depth;
        int32_t color_format;
        int32_t subsampling_x;
        int32_t subsampling_y;
        RestorationInfo rst_info[MAX_MB_PLANE];
        // rst_end_stripe[i] is one more than the index of the bottom stripe
        // for tile row i.
        int32_t rst_end_stripe[MAX_TILE_ROWS];
        // Output of loop restoration
        Yv12BufferConfig rst_frame;
        // pointer to a scratch buffer used by self-guided restoration
        int32_t *rst_tmpbuf;
        Yv12BufferConfig *frame_to_show;
        int32_t byte_alignment;
        int32_t last_tile_cols, last_tile_rows;
        int32_t log2_tile_cols;                        // only valid for uniform tiles
        int32_t log2_tile_rows;                        // only valid for uniform tiles
        int32_t tile_width, tile_height;               // In MI units
        struct PictureParentControlSet               *p_pcs_ptr;
        int8_t  sg_filter_mode;
        int32_t sg_frame_ep_cnt[SGRPROJ_PARAMS];
        int32_t sg_frame_ep;
        int8_t  sg_ref_frame_ep[2];
        int8_t  wn_filter_mode;

        struct PictureControlSet               *pcs_ptr;

        FrameSize                       frm_size;
        TilesInfo                       tiles_info;

    } Av1Common;

    /**************************************
     * Segment-based Control Sets
     **************************************/

    typedef struct EbMdcLeafData
    {
        uint32_t          mds_idx;
        uint32_t          tot_d1_blocks; //how many d1 bloks every parent square would have
        uint8_t           leaf_index;
        EbBool            split_flag;
#if PREDICT_NSQ_SHAPE
        //uint8_t           open_loop_ranking;
        uint8_t           early_split_flag;
#if COMBINE_MDC_NSQ_TABLE
        uint8_t           ol_best_nsq_shape1;
        uint8_t           ol_best_nsq_shape2;
        uint8_t           ol_best_nsq_shape3;
        uint8_t           ol_best_nsq_shape4;
        uint8_t           ol_best_nsq_shape5;
        uint8_t           ol_best_nsq_shape6;
        uint8_t           ol_best_nsq_shape7;
        uint8_t           ol_best_nsq_shape8;
#endif
#endif
#if ADD_MDC_REFINEMENT_LOOP || MULTI_PASS_PD
        uint8_t           consider_block;
        uint8_t           refined_split_flag;
#endif
    } EbMdcLeafData;

    typedef struct MdcLcuData
    {
        // Rate Control
        uint8_t           qp;

        // ME Results
        uint64_t          treeblock_variance;
        uint32_t          leaf_count;
        EbMdcLeafData   leaf_data_array[BLOCK_MAX_COUNT_SB_128];
    } MdcLcuData;

    /**************************************
     * MD Segment Control
     **************************************/
    typedef struct MdSegmentCtrl
    {
        uint64_t completion_mask;
        EbHandle write_lock_mutex;
        uint32_t total_count;
        uint32_t column_count;
        uint32_t row_count;
        EbBool   in_progress;
        uint32_t current_row_idx;
    } MdSegmentCtrl;

    /**************************************
     * Picture Control Set
     **************************************/
    struct CodedTreeblock_s;
    struct SuperBlock;
#define MAX_MESH_STEP 4

    typedef struct MeshPattern
    {
        int range;
        int interval;
    } MeshPattern;

    enum {
        NOT_IN_USE,
        DIRECT_PRED,
        RELAXED_PRED,
        ADAPT_PRED
    } UENUM1BYTE(MAX_PART_PRED_MODE);


    typedef struct SpeedFeatures
    {
        // TODO(jingning): combine the related motion search speed features
        // This allows us to use motion search at other sizes as a starting
        // point for this motion search and limits the search range around it.
        int adaptive_motion_search;

        // Flag for allowing some use of exhaustive searches;
        int allow_exhaustive_searches;

        // Threshold for allowing exhaistive motion search.
        int exhaustive_searches_thresh;

        // Maximum number of exhaustive searches for a frame.
        int max_exaustive_pct;

        // Pattern to be used for any exhaustive mesh searches.
        MeshPattern mesh_patterns[MAX_MESH_STEP];
        // Sets min and max square partition levels for this superblock based on
        // motion vector and prediction error distribution produced from 16x16
        // simple motion search
        MAX_PART_PRED_MODE auto_max_partition_based_on_simple_motion;

    } SpeedFeatures;

    typedef struct PictureControlSet
    {
        EbDctor                            dctor;
        EbObjectWrapper                    *sequence_control_set_wrapper_ptr;

        EbPictureBufferDesc                *recon_picture_ptr;
        EbPictureBufferDesc                *film_grain_picture_ptr;
        EbPictureBufferDesc                *recon_picture16bit_ptr;
        EbPictureBufferDesc                *film_grain_picture16bit_ptr;
        EbPictureBufferDesc                *recon_picture32bit_ptr;
        EbPictureBufferDesc                *input_frame16bit;

        struct PictureParentControlSet     *parent_pcs_ptr;  //The parent of this PCS.
        EbObjectWrapper                    *picture_parent_control_set_wrapper_ptr;
        EntropyCoder                       *entropy_coder_ptr;
        // Packetization (used to encode SPS, PPS, etc)
        Bitstream                          *bitstream_ptr;

        // Reference Lists
        // Reference Lists
        EbObjectWrapper                    *ref_pic_ptr_array[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];
        //EB_S64                                refPicPocArray[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];

        uint8_t                               ref_pic_qp_array[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];
        EB_SLICE                              ref_slice_type_array[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];
#if TWO_PASS
        uint64_t                            ref_pic_referenced_area_avg_array[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];
#endif
        // GOP
        uint64_t                              picture_number;
        uint8_t                               temporal_layer_index;

        EbColorFormat                         color_format;

        EncDecSegments                     *enc_dec_segment_ctrl;

        // Entropy Process Rows
        int8_t                                entropy_coding_current_available_row;
        EbBool                                entropy_coding_row_array[MAX_LCU_ROWS];
        int8_t                                entropy_coding_current_row;
        int8_t                                entropy_coding_row_count;
        EbHandle                              entropy_coding_mutex;
        EbBool                                entropy_coding_in_progress;
        EbBool                                entropy_coding_pic_done;
        EbHandle                              intra_mutex;
        uint32_t                              intra_coded_area;
        uint32_t                              tot_seg_searched_cdef;
        EbHandle                              cdef_search_mutex;

        uint16_t                              cdef_segments_total_count;
        uint8_t                               cdef_segments_column_count;
        uint8_t                               cdef_segments_row_count;

        uint64_t(*mse_seg[2])[TOTAL_STRENGTHS];

        uint16_t *src[3];        //dlfed recon in 16bit form
        uint16_t *ref_coeff[3];  //input video in 16bit form

        uint32_t                              tot_seg_searched_rest;
        EbHandle                              rest_search_mutex;
        uint16_t                              rest_segments_total_count;
        uint8_t                               rest_segments_column_count;
        uint8_t                               rest_segments_row_count;

        // Mode Decision Config
        MdcLcuData                         *mdc_sb_array;

        // Error Resilience
        EbBool                                constrained_intra_flag;

        // Slice Type
        EB_SLICE                              slice_type;

        // Rate Control
        uint8_t                               picture_qp;
        uint8_t                               dif_cu_delta_qp_depth;

        // SB Array
        uint8_t                               sb_max_depth;
        uint16_t                              sb_total_count;
        SuperBlock                            **sb_ptr_array;
        // DLF
        uint8_t                              *qp_array;
        uint16_t                              qp_array_stride;
        uint32_t                              qp_array_size;
        // EncDec Entropy Coder (for rate estimation)
        EntropyCoder                       *coeff_est_entropy_coder_ptr;

        // Mode Decision Neighbor Arrays
        NeighborArrayUnit                  *md_intra_luma_mode_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_intra_chroma_mode_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_mv_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_skip_flag_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_mode_type_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_leaf_depth_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_luma_recon_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_tx_depth_1_luma_recon_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_cb_recon_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_cr_recon_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];

        uint8_t                             hbd_mode_decision;
        NeighborArrayUnit                  *md_luma_recon_neighbor_array16bit[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_tx_depth_1_luma_recon_neighbor_array16bit[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_cb_recon_neighbor_array16bit[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_cr_recon_neighbor_array16bit[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_skip_coeff_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_luma_dc_sign_level_coeff_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_tx_depth_1_luma_dc_sign_level_coeff_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_cb_dc_sign_level_coeff_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_cr_dc_sign_level_coeff_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_txfm_context_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_inter_pred_dir_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        NeighborArrayUnit                  *md_ref_frame_type_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];

        NeighborArrayUnit32                *md_interpolation_type_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];

        NeighborArrayUnit                  *mdleaf_partition_neighbor_array[NEIGHBOR_ARRAY_TOTAL_COUNT];
        // Encode Pass Neighbor Arrays
        NeighborArrayUnit                  *ep_intra_luma_mode_neighbor_array;
        NeighborArrayUnit                  *ep_intra_chroma_mode_neighbor_array;
        NeighborArrayUnit                  *ep_mv_neighbor_array;
        NeighborArrayUnit                  *ep_skip_flag_neighbor_array;
        NeighborArrayUnit                  *ep_mode_type_neighbor_array;
        NeighborArrayUnit                  *ep_leaf_depth_neighbor_array;
        NeighborArrayUnit                  *ep_luma_recon_neighbor_array;
        NeighborArrayUnit                  *ep_cb_recon_neighbor_array;
        NeighborArrayUnit                  *ep_cr_recon_neighbor_array;
        NeighborArrayUnit                  *ep_luma_recon_neighbor_array16bit;
        NeighborArrayUnit                  *ep_cb_recon_neighbor_array16bit;
        NeighborArrayUnit                  *ep_cr_recon_neighbor_array16bit;
        NeighborArrayUnit                  *ep_luma_dc_sign_level_coeff_neighbor_array;
        NeighborArrayUnit                  *ep_cr_dc_sign_level_coeff_neighbor_array;
        NeighborArrayUnit                  *ep_cb_dc_sign_level_coeff_neighbor_array;
        NeighborArrayUnit                  *ep_partition_context_neighbor_array;
        // Entropy Coding Neighbor Arrays
        NeighborArrayUnit                  *mode_type_neighbor_array;
        NeighborArrayUnit                  *partition_context_neighbor_array;
        NeighborArrayUnit                  *intra_luma_mode_neighbor_array;
        NeighborArrayUnit                  *skip_flag_neighbor_array;
        NeighborArrayUnit                  *skip_coeff_neighbor_array;
        NeighborArrayUnit                  *luma_dc_sign_level_coeff_neighbor_array; // Stored per 4x4. 8 bit: lower 6 bits (COEFF_CONTEXT_BITS), shows if there is at least one Coef. Top 2 bit store the sign of DC as follow: 0->0,1->-1,2-> 1
        NeighborArrayUnit                  *cr_dc_sign_level_coeff_neighbor_array; // Stored per 4x4. 8 bit: lower 6 bits(COEFF_CONTEXT_BITS), shows if there is at least one Coef. Top 2 bit store the sign of DC as follow: 0->0,1->-1,2-> 1
        NeighborArrayUnit                  *cb_dc_sign_level_coeff_neighbor_array; // Stored per 4x4. 8 bit: lower 6 bits(COEFF_CONTEXT_BITS), shows if there is at least one Coef. Top 2 bit store the sign of DC as follow: 0->0,1->-1,2-> 1
        NeighborArrayUnit                  *txfm_context_array;
        NeighborArrayUnit                  *inter_pred_dir_neighbor_array;
        NeighborArrayUnit                  *ref_frame_type_neighbor_array;
        NeighborArrayUnit32                *interpolation_type_neighbor_array;

        NeighborArrayUnit                  *segmentation_id_pred_array;
        SegmentationNeighborMap              *segmentation_neighbor_map;

        ModeInfo                            **mi_grid_base; //2 SB Rows of mi Data are enough

        ModeInfo                             *mip;

        int32_t                               mi_stride;
        EbReflist                             colocated_pu_ref_list;
        EbBool                                is_low_delay;

        // slice level chroma QP offsets
        EbBool                                slice_level_chroma_qp_flag;
        int8_t                                slice_cb_qp_offset;
        int8_t                                slice_cr_qp_offset;
        int8_t                                cb_qp_offset;
        int8_t                                cr_qp_offset;
        EbBool                                adjust_min_qp_flag;

        EbEncMode                             enc_mode;
        EbBool                                intra_md_open_loop_flag;
        EbBool                                limit_intra;
        int32_t                               cdef_preset[4];
        WienerInfo                            wiener_info[MAX_MB_PLANE];
        SgrprojInfo                           sgrproj_info[MAX_MB_PLANE];
        SpeedFeatures sf;
        SearchSiteConfig ss_cfg;//CHKN this might be a seq based
        HashTable hash_table;
        CRC_CALCULATOR crc_calculator1;
        CRC_CALCULATOR crc_calculator2;

        FRAME_CONTEXT * ec_ctx_array;
        struct MdRateEstimationContext* rate_est_array;
        uint8_t  update_cdf;
        FRAME_CONTEXT           ref_frame_context[REF_FRAMES];
        EbWarpedMotionParams    ref_global_motion[TOTAL_REFS_PER_FRAME];
        struct MdRateEstimationContext *md_rate_estimation_array;
        int8_t ref_frame_side[REF_FRAMES];
        TPL_MV_REF  *tpl_mvs;
        uint8_t pic_filter_intra_mode;
#if PAL_SUP
        TOKENEXTRA *tile_tok[64][64];
#endif
    } PictureControlSet;

    // To optimize based on the max input size
    // To study speed-memory trade-offs
    typedef struct SbParams
    {
        uint8_t   horizontal_index;
        uint8_t   vertical_index;
        uint16_t  origin_x;
        uint16_t  origin_y;
        uint8_t   width;
        uint8_t   height;
        uint8_t   is_complete_sb;
        EbBool    raster_scan_cu_validity[CU_MAX_COUNT];
        uint8_t   potential_logo_sb;
        uint8_t   is_edge_sb;
        uint32_t  tile_start_x;
        uint32_t  tile_start_y;
        uint32_t  tile_end_x;
        uint32_t  tile_end_y;
    } SbParams;

    typedef struct SbGeom
    {
        uint16_t   horizontal_index;
        uint16_t   vertical_index;
        uint16_t   origin_x;
        uint16_t   origin_y;
        uint8_t    width;
        uint8_t    height;
        uint8_t    is_complete_sb;
        EbBool     block_is_inside_md_scan[BLOCK_MAX_COUNT_SB_128];
        EbBool     block_is_allowed[BLOCK_MAX_COUNT_SB_128];
    } SbGeom;

    typedef struct CuStat
    {
        uint16_t          edge_cu;
        uint16_t          similar_edge_count;
        uint32_t          grad;
    } CuStat;

    typedef struct sb_stat
    {
        CuStat          cu_stat_array[CU_MAX_COUNT];
        uint8_t           stationary_edge_over_time_flag;
        uint8_t           pm_stationary_edge_over_time_flag;
        uint8_t           pm_check1_for_logo_stationary_edge_over_time_flag;
        uint8_t           check1_for_logo_stationary_edge_over_time_flag;
        uint8_t           check2_for_logo_stationary_edge_over_time_flag;
        uint8_t           low_dist_logo;
    } sb_stat;

    //CHKN
    // Add the concept of PictureParentControlSet which is a subset of the old PictureControlSet.
    // It actually holds only high level Picture based control data:(GOP management,when to start a picture, when to release the PCS, ....).
    // The regular PictureControlSet(Child) will be dedicated to store SB based encoding results and information.
    // Parent is created before the Child, and continue to live more. Child PCS only lives the exact time needed to encode the picture: from ME to EC/ALF.
    typedef struct PictureParentControlSet
    {
        EbDctor                            dctor;
        EbObjectWrapper                    *sequence_control_set_wrapper_ptr;
        EbObjectWrapper                    *input_picture_wrapper_ptr;
        EbObjectWrapper                    *reference_picture_wrapper_ptr;
        EbObjectWrapper                    *pa_reference_picture_wrapper_ptr;
        EbPictureBufferDesc                *enhanced_picture_ptr;
        EbPictureBufferDesc                *chroma_downsampled_picture_ptr; //if 422/444 input, down sample to 420 for MD
        EbBool                              is_chroma_downsampled_picture_ptr_owner;
        PredictionStructure                *pred_struct_ptr;          // need to check
        struct SequenceControlSet          *scs_ptr;
        EbObjectWrapper                    *p_pcs_wrapper_ptr;
        EbObjectWrapper                    *previous_picture_control_set_wrapper_ptr;
        EbObjectWrapper                    *output_stream_wrapper_ptr;
        Av1Common                            *av1_cm;

        // Data attached to the picture. This includes data passed from the application, or other data the encoder attaches
        // to the picture.
        EbLinkedListNode                     *data_ll_head_ptr;
        // pointer to data to be passed back to the application when picture encoding is done
        EbLinkedListNode                     *app_out_data_ll_head_ptr;

        EbBufferHeaderType                   *input_ptr;            // input picture buffer

        EbBool                                idr_flag;
        EbBool                                cra_flag;
        EbBool                                open_gop_cra_flag;
        EbBool                                scene_change_flag;
        EbBool                                end_of_sequence_flag;
        EbBool                                eos_coming;
        uint8_t                               picture_qp;
        uint64_t                              picture_number;
        uint8_t                               wedge_mode;
        uint32_t                             cur_order_hint;
        uint32_t                             ref_order_hint[7];
        EbPicnoiseClass                       pic_noise_class;
        EB_SLICE                              slice_type;
        uint8_t                               pred_struct_index;
        EbBool                                use_rps_in_sps;
        uint8_t                               reference_struct_index;
        uint8_t                               temporal_layer_index;
        uint64_t                              decode_order;
        EbBool                                is_used_as_reference_flag;
        uint8_t                               ref_list0_count;
        uint8_t                               ref_list1_count;
        MvReferenceFrame                      ref_frame_type_arr[MODE_CTX_REF_FRAMES];
        uint8_t                               tot_ref_frame_types;
        // Rate Control
        uint64_t                              pred_bits_ref_qp[MAX_REF_QP_NUM];
        uint64_t                              target_bits_best_pred_qp;
        uint64_t                              target_bits_rc;
        uint8_t                               best_pred_qp;
        uint64_t                              total_num_bits;
        uint8_t                               first_frame_in_temporal_layer;
        uint8_t                               first_non_intra_frame_in_temporal_layer;
        uint64_t                              frames_in_interval[EB_MAX_TEMPORAL_LAYERS];
        uint64_t                              bits_per_sw_per_layer[EB_MAX_TEMPORAL_LAYERS];
        uint64_t                              total_bits_per_gop;
        EbBool                                tables_updated;
        EbBool                                percentage_updated;
        uint32_t                              target_bit_rate;
        uint32_t                              vbv_bufsize;
        EbBool                                min_target_rate_assigned;
        uint32_t                              frame_rate;
        uint16_t                              sb_total_count;
        EbBool                                end_of_sequence_region;
        EbBool                                scene_change_in_gop;
        uint8_t                               frames_in_sw;             // used for Look ahead
        int8_t                                historgram_life_count;
        EbBool                                qp_on_the_fly;
        uint8_t                               calculated_qp;
        uint8_t                               intra_selected_org_qp;
        uint64_t                              sad_me;
        uint64_t                              quantized_coeff_num_bits;
        uint64_t                              average_qp;
        uint64_t                              last_idr_picture;
        uint64_t                              start_time_seconds;
        uint64_t                              start_time_u_seconds;
        uint32_t                              luma_sse;
        uint32_t                              cr_sse;
        uint32_t                              cb_sse;

        // Pre Analysis
        EbObjectWrapper                      *ref_pa_pic_ptr_array[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];
        uint64_t                              ref_pic_poc_array[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];
        uint16_t                            **variance;
        uint8_t                             **y_mean;
        uint8_t                             **cbMean;
        uint8_t                             **crMean;
        uint32_t                              pre_assignment_buffer_count;
        uint16_t                              pic_avg_variance;
        EbBool                                scene_transition_flag[MAX_NUM_OF_REF_PIC_LIST];
        EbBool                                intensity_transition_flag;
        uint8_t                               average_intensity[3];
        // Non moving index array
        uint8_t                              *non_moving_index_array;
        int                                   kf_zeromotion_pct; // percent of zero motion blocks

        uint8_t                               fade_out_from_black;
        uint8_t                               fade_in_to_black;
        EbBool                                is_pan;
        EbBool                                is_tilt;
        uint8_t                              *sb_flat_noise_array;
        uint8_t                              *sharp_edge_sb_flag;
        EbBool                                logo_pic_flag;                    // used by EncDecProcess()
        uint16_t                              non_moving_index_average;            // used by ModeDecisionConfigurationProcess()
        int16_t                               non_moving_index_min_distance;
        int16_t                               non_moving_index_max_distance;
        uint16_t                              qp_scaling_average_complexity;
        EbBool                                dark_back_groundlight_fore_ground;
        sb_stat                            *sb_stat_array;
        uint8_t                               very_low_var_pic_flag;
        uint32_t                              intra_complexity_min[4];
        uint32_t                              intra_complexity_max[4];
        uint32_t                              intra_complexity_accum[4];
        uint32_t                              intra_complexity_avg[4];
        uint32_t                              inter_complexity_min[4];
        uint32_t                              inter_complexity_max[4];
        uint32_t                              inter_complexity_accum[4];
        uint32_t                              inter_complexity_avg[4];
        uint32_t                              processed_leaf_count[4];
        uint32_t                              intra_complexity_min_pre;
        uint32_t                              intra_complexity_max_pre;
        uint32_t                              inter_complexity_min_pre;
        uint32_t                              inter_complexity_max_pre;
        int32_t                               intra_min_distance[4];
        int32_t                               intra_max_distance[4];
        int32_t                               inter_min_distance[4];
        int32_t                               inter_max_distance[4];
        // Histograms
        uint32_t                          ****picture_histogram;
        uint64_t                              average_intensity_per_region[MAX_NUMBER_OF_REGIONS_IN_WIDTH][MAX_NUMBER_OF_REGIONS_IN_HEIGHT][3];

        // Segments
        uint16_t                              me_segments_total_count;
        uint8_t                               me_segments_column_count;
        uint8_t                               me_segments_row_count;
        uint64_t                              me_segments_completion_mask;

        // Motion Estimation Results
        uint8_t                               max_number_of_pus_per_sb;
        uint8_t                               max_number_of_candidates_per_block;
        MeLcuResults                        **me_results;
        uint32_t                             *rc_me_distortion;

        // Global motion estimation results
        EbBool                                is_global_motion[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];
        EbWarpedMotionParams                  global_motion_estimation[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];

        // Motion Estimation Distortion and OIS Historgram
        uint16_t                             *me_distortion_histogram;
        uint16_t                             *ois_distortion_histogram;
        uint32_t                             *intra_sad_interval_index;
        uint32_t                             *inter_sad_interval_index;
        EbHandle                              rc_distortion_histogram_mutex;

        // Open loop Intra candidate Search Results
        OisSbResults                    **ois_sb_results;
        OisCandidate                    **ois_candicate;
        // Dynamic GOP
        EbPred                                pred_structure;
        uint8_t                               hierarchical_levels;
        uint16_t                              full_sb_count;
        EbBool                                init_pred_struct_position_flag;
        int8_t                                hierarchical_layers_diff;

        // HME Flags
        EbBool                                enable_hme_flag;
        EbBool                                enable_hme_level0_flag;
        EbBool                                enable_hme_level1_flag;
        EbBool                                enable_hme_level2_flag;

        // HME Flags form Temporal Filtering
        EbBool                                tf_enable_hme_flag;
        EbBool                                tf_enable_hme_level0_flag;
        EbBool                                tf_enable_hme_level1_flag;
        EbBool                                tf_enable_hme_level2_flag;

        // MD
        EbEncMode                             enc_mode;
        EbEncMode                             snd_pass_enc_mode;
        EB_SB_DEPTH_MODE                     *sb_depth_mode_array;
        EbCu8x8Mode                           cu8x8_mode;
        EbBool                                use_src_ref;
        EbBool                                limit_ois_to_dc_mode_flag;

        // Multi-modes signal(s)
        EbPictureDepthMode                    pic_depth_mode;
        uint8_t                               loop_filter_mode;
        uint8_t                               intra_pred_mode;
        uint8_t                               skip_sub_blks;
        uint8_t                               atb_mode;
        uint8_t                               frame_end_cdf_update_mode; // mm-signal: 0: OFF, 1:ON
        //**********************************************************************************************************//
        Av1RpsNode                          av1_ref_signal;
        EbBool                                has_show_existing;
        int32_t                               ref_frame_map[REF_FRAMES]; /* maps fb_idx to reference slot */
        int32_t                               is_skip_mode_allowed;
        int32_t                               skip_mode_flag;

        // Flag for a frame used as a reference - not written to the bitstream
        int32_t                               is_reference_frame;

        // Flag signaling that the frame is encoded using only INTRA modes.
        uint8_t                               intra_only;
        /* profile settings */
#if CONFIG_ENTROPY_STATS
        int32_t                               coef_cdf_category;
#endif
#if PREDICT_NSQ_SHAPE
        uint16_t                              base_qindex;
#endif
        int32_t                               separate_uv_delta_q;

        // Global quant matrix tables
        const QmVal                       *giqmatrix[NUM_QM_LEVELS][3][TX_SIZES_ALL];
        const QmVal                       *gqmatrix[NUM_QM_LEVELS][3][TX_SIZES_ALL];
        Quants                                quants;
        Dequants                              deq;
        Quants                                quantsMd;
        Dequants                              deqMd;
        int32_t                               min_qmlevel;
        int32_t                               max_qmlevel;
        // Encoder
        LoopFilterInfoN                   lf_info;

        // Flag signaling how frame contexts should be updated at the end of
        // a frame decode
        RefreshFrameContextMode               refresh_frame_context;
        uint32_t                              frame_context_idx; /* Context to use/update */
        int32_t                               fb_of_context_type[REF_FRAMES];
        uint64_t                              frame_offset;
        uint32_t                              large_scale_tile;
        int32_t                               nb_cdef_strengths;
#if PREDICT_NSQ_SHAPE
        ReferenceMode                         reference_mode;
        int32_t                               delta_q_present_flag;
        int32_t                               reduced_tx_set_used;
#endif

#if ADD_DELTA_QP_SUPPORT
        // Resolution of delta quant
        int32_t                               num_tg;
        int32_t                               monochrome;
        int32_t                               prev_qindex;
        // Since actual frame level loop filtering level value is not available
        // at the beginning of the tile (only available during actual filtering)
        // at encoder side.we record the delta_lf (against the frame level loop
        // filtering level) and code the delta between previous superblock's delta
        // lf and current delta lf. It is equivalent to the delta between previous
        // superblock's actual lf and current lf.
        int32_t                               prev_delta_lf_from_base;
        int32_t                               current_delta_lf_from_base;

        // For this experiment, we have four frame filter levels for different plane
        // and direction. So, to support the per superblock update, we need to add
        // a few more params as below.
        // 0: delta loop filter level for y plane vertical
        // 1: delta loop filter level for y plane horizontal
        // 2: delta loop filter level for u plane
        // 3: delta loop filter level for v plane
        // To make it consistent with the reference to each filter level in segment,
        // we need to -1, since
        // SEG_LVL_ALT_LF_Y_V = 1;
        // SEG_LVL_ALT_LF_Y_H = 2;
        // SEG_LVL_ALT_LF_U   = 3;
        // SEG_LVL_ALT_LF_V   = 4;
//
        int32_t                               prev_delta_lf[FRAME_LF_COUNT];
        int32_t                               curr_delta_lf[FRAME_LF_COUNT];
#endif
        // Resolution of delta quant
        // int32_t delta_q_res;
        int32_t                               allow_comp_inter_inter;
        int16_t                               panMvx;
        int16_t                               panMvy;
        int16_t                               tiltMvx;
        int16_t                               tiltMvy;
        EbWarpedMotionParams                  global_motion[TOTAL_REFS_PER_FRAME];
        PictureControlSet                    *childPcs;
        Macroblock                           *av1x;
        int32_t                               film_grain_params_present; //todo (AN): Do we need this flag at picture level?
        AomDenoiseAndModel                   *denoise_and_model;
        RestUnitSearchInfo                   *rusi_picture[3];//for 3 planes
        int8_t                                cdef_filter_mode;
        int32_t                               cdef_frame_strength;
        int32_t                               cdf_ref_frame_strenght;
        int32_t                               use_ref_frame_cdef_strength;
#if !MULTI_PASS_PD
        uint8_t                               tx_search_level;
        uint64_t                              tx_weight;
        uint8_t                               tx_search_reduced_set;
        uint8_t                               interpolation_search_level;
#endif
        uint8_t                               nsq_search_level;
#if PAL_SUP
        uint8_t                               palette_mode;
#endif
        uint8_t                               nsq_max_shapes_md; // max number of shapes to be tested in MD
        uint8_t                              sc_content_detected;
        uint8_t                              ibc_mode;
        SkipModeInfo                         skip_mode_info;
        uint64_t                             picture_number_alt; // The picture number overlay includes all the overlay frames
        uint8_t                              is_alt_ref;
        uint8_t                              is_overlay;
        struct PictureParentControlSet      *overlay_ppcs_ptr;
        struct PictureParentControlSet      *alt_ref_ppcs_ptr;
        uint8_t                               altref_strength;
        int32_t                               pic_decision_reorder_queue_idx;
        struct PictureParentControlSet       *temp_filt_pcs_list[ALTREF_MAX_NFRAMES];
        EbByte                               save_enhanced_picture_ptr[3];
        EbByte                               save_enhanced_picture_bit_inc_ptr[3];
        EbHandle temp_filt_done_semaphore;
        EbHandle temp_filt_mutex;
        EbHandle debug_mutex;

        uint8_t  temp_filt_prep_done;
        uint16_t temp_filt_seg_acc;

        int16_t                               tf_segments_total_count;
        uint8_t                               tf_segments_column_count;
        uint8_t                               tf_segments_row_count;
        uint8_t                               past_altref_nframes;
        uint8_t                               future_altref_nframes;
        EbBool                                temporal_filtering_on;
        uint64_t                              filtered_sse; // the normalized SSE between filtered and original alt_ref with 8 bit precision.
                                                            // I Slice has the value of the next ALT_REF picture
        uint64_t                              filtered_sse_uv;
        FrameHeader                           frm_hdr;
#if !MULTI_PASS_PD
        MD_COMP_TYPE                          compound_types_to_try;
#endif
        uint8_t                               compound_mode;
        uint8_t                               prune_unipred_at_me;
        uint8_t                               coeff_based_skip_atb;
        uint16_t*                             altref_buffer_highbd[3];
#if II_COMP_FLAG
        uint8_t                              enable_inter_intra;
#endif
#if OBMC_FLAG
        uint8_t                              pic_obmc_mode;
#endif
#if TWO_PASS
        StatStruct*                       stat_struct_first_pass_ptr; // pointer to stat_struct in the first pass
        struct StatStruct                 stat_struct; // stat_struct used in the second pass
        uint64_t                             referenced_area_avg; // average referenced area per frame
        uint8_t                              referenced_area_has_non_zero;
#endif
#if PREDICT_NSQ_SHAPE && !MDC_ADAPTIVE_LEVEL
        uint8_t                                mdc_depth_level;
#endif
#if MDC_ADAPTIVE_LEVEL
        uint8_t                                enable_adaptive_ol_partitioning;
#endif
#if GM_OPT
        uint8_t                                gm_level;
#endif
        uint8_t                                tx_size_early_exit;

    } PictureParentControlSet;

    typedef struct picture_control_set_init_data
    {
        uint16_t                           picture_width;
        uint16_t                           picture_height;
        uint16_t                           left_padding;
        uint16_t                           right_padding;
        uint16_t                           top_padding;
        uint16_t                           bot_padding;
        EbBitDepthEnum                     bit_depth;
        EbColorFormat                      color_format;
        uint32_t                           sb_sz;
#if PAL_SUP
        uint8_t                            cfg_palette;
#endif
        uint32_t                           sb_size_pix;   //since we still have lot of code assuming 64x64 LCU, we add a new paramter supporting both128x128 and 64x64,
                                                          //ultimately the fixed code supporting 64x64 should be upgraded to use 128x128 and the above could be removed.
        uint32_t                           max_depth;
        //EbBool                             is16bit;
        uint32_t                           ten_bit_format;
        uint32_t                           compressed_ten_bit_format;
        uint16_t                           enc_dec_segment_col;
        uint16_t                           enc_dec_segment_row;
        EbEncMode                          enc_mode;
        uint8_t                            speed_control;
        uint8_t                            hbd_mode_decision;
        uint16_t                           film_grain_noise_level;
        EbBool                             ext_block_flag;
        uint8_t                            mrp_mode;
        uint8_t                            cdf_mode;
        uint8_t                            nsq_present;
        uint8_t                            over_boundary_block_mode;
        uint8_t                            mfmv;
    } picture_control_set_init_data;

    typedef struct Av1Comp
    {
        //    Quants quants;
        //    ThreadData td;
        //    MB_MODE_INFO_EXT *mbmi_ext_base;
        //    CB_COEFF_BUFFER *coeff_buffer_base;
        //    Dequants dequants;
        //    Av1Common common;
        //    AV1EncoderConfig oxcf;
        //    struct lookahead_ctx *lookahead;
        //    struct lookahead_entry *alt_ref_source;
        //
        //    int32_t optimize_speed_feature;
        //    int32_t optimize_seg_arr[MAX_SEGMENTS];
        //
        //    Yv12BufferConfig *source;
        //    Yv12BufferConfig *last_source;  // NULL for first frame and alt_ref frames
        //    Yv12BufferConfig *unscaled_source;
        //    Yv12BufferConfig scaled_source;
        //    Yv12BufferConfig *unscaled_last_source;
        //    Yv12BufferConfig scaled_last_source;
        //
        //    // For a still frame, this flag is set to 1 to skip partition search.
        //    int32_t partition_search_skippable_frame;
        //    double csm_rate_array[32];
        //    double m_rate_array[32];
        //    int32_t rate_size;
        //    int32_t rate_index;
        //    HashTable *previous_hash_table;
        //    int32_t previous_index;
        //    int32_t cur_poc;  // DebugInfo
        //
        //    int32_t scaled_ref_idx[REF_FRAMES];
        //    int32_t ref_fb_idx[REF_FRAMES];
        //    int32_t refresh_fb_idx;  // ref frame buffer index to refresh
        //
        //    int32_t last_show_frame_buf_idx;  // last show frame buffer index
        //
        //    int32_t refresh_last_frame;
        //    int32_t refresh_golden_frame;
        //    int32_t refresh_bwd_ref_frame;
        //    int32_t refresh_alt2_ref_frame;
        //    int32_t refresh_alt_ref_frame;
        //
        //    int32_t ext_refresh_frame_flags_pending;
        //    int32_t ext_refresh_last_frame;
        //    int32_t ext_refresh_golden_frame;
        //    int32_t ext_refresh_bwd_ref_frame;
        //    int32_t ext_refresh_alt2_ref_frame;
        //    int32_t ext_refresh_alt_ref_frame;
        //
        //    int32_t ext_refresh_frame_context_pending;
        //    int32_t ext_refresh_frame_context;
        //    int32_t ext_use_ref_frame_mvs;
        //    int32_t ext_use_error_resilient;
        //    int32_t ext_use_s_frame;
        //    int32_t ext_use_primary_ref_none;
        //
        //    Yv12BufferConfig last_frame_uf;
        Yv12BufferConfig trial_frame_rst;
        //
        //    // Ambient reconstruction err target for force key frames
        //    int64_t ambient_err;
        //
        //    RD_OPT rd;
        //
        //    CODING_CONTEXT coding_context;
        //
        //    int32_t gmtype_cost[TRANS_TYPES];
        //    int32_t gmparams_cost[REF_FRAMES];
        //
        //    int32_t nmv_costs[2][MV_VALS];
        //    int32_t nmv_costs_hp[2][MV_VALS];
        //
        //    int64_t last_time_stamp_seen;
        //    int64_t last_end_time_stamp_seen;
        //    int64_t first_time_stamp_ever;
        //
        //    RATE_CONTROL rc;
        //    double frame_rate;
        //
        //    // NOTE(zoeliu): Any inter frame allows maximum of REF_FRAMES inter
        //    // references; Plus the currently coded frame itself, it is needed to allocate
        //    // sufficient space to the size of the maximum possible number of frames.
        //    int32_t interp_filter_selected[REF_FRAMES + 1][SWITCHABLE];
        //
        //    struct aom_codec_pkt_list *output_pkt_list;
        //
        //    MBGRAPH_FRAME_STATS mbgraph_stats[MAX_LAG_BUFFERS];
        //    int32_t mbgraph_n_frames;  // number of frames filled in the above
        //    int32_t static_mb_pct;     // % forced skip mbs by segmentation
        //    int32_t ref_frame_flags;
        //    int32_t ext_ref_frame_flags;
        //    RATE_FACTOR_LEVEL frame_rf_level[FRAME_BUFFERS];
        //
        //    SpeedFeatures sf;
        //
        //    uint32_t max_mv_magnitude;
        //    int32_t mv_step_param;
        //
        //    int32_t allow_comp_inter_inter;
        //    int32_t all_one_sided_refs;
        //
        //    uint8_t *segmentation_map;
        //
        //    CYCLIC_REFRESH *cyclic_refresh;
        //    ActiveMap active_map;
        //
        //    fractional_mv_step_fp *find_fractional_mv_step;
        //    av1_diamond_search_fn_t diamond_search_sad;
        //    aom_variance_fn_ptr_t fn_ptr[BlockSizeS_ALL];
        //    uint64_t time_receive_data;
        //    uint64_t time_compress_data;
        //    uint64_t time_pick_lpf;
        //    uint64_t time_encode_sb_row;
        //
        //#if CONFIG_FP_MB_STATS
        //    int32_t use_fp_mb_stats;
        //#endif
        //
        //    TWO_PASS twopass;
        //
        //    Yv12BufferConfig alt_ref_buffer;
        //
        //#if CONFIG_INTERNAL_STATS
        //    uint32_t mode_chosen_counts[MAX_MODES];
        //
        //    int32_t count;
        //    uint64_t total_sq_error;
        //    uint64_t total_samples;
        //    ImageStat psnr;
        //
        //    double total_blockiness;
        //    double worst_blockiness;
        //
        //    int32_t bytes;
        //    double summed_quality;
        //    double summed_weights;
        //    uint32_t tot_recode_hits;
        //    double worst_ssim;
        //
        //    ImageStat fastssim;
        //    ImageStat psnrhvs;
        //
        //    int32_t b_calculate_blockiness;
        //    int32_t b_calculate_consistency;
        //
        //    double total_inconsistency;
        //    double worst_consistency;
        //    Ssimv *ssim_vars;
        //    Metrics metrics;
        //#endif
        //    int32_t b_calculate_psnr;
        //
        //    int32_t droppable;
        //
        //    int32_t initial_width;
        //    int32_t initial_height;
        //    int32_t initial_mbs;  // Number of MBs in the full-size frame; to be used to
        //    // normalize the firstpass stats. This will differ from the
        //    // number of MBs in the current frame when the frame is
        //    // scaled.
        //
        //    // When resize is triggered through external control, the desired width/height
        //    // are stored here until use in the next frame coded. They are effective only
        //    // for
        //    // one frame and are reset after use.
        //    int32_t resize_pending_width;
        //    int32_t resize_pending_height;
        //
        //    int32_t frame_flags;
        //
        //    SearchSiteConfig ss_cfg;
        //
        //    int32_t multi_arf_allowed;
        //
        //    TileDataEnc *tile_data;
        //    int32_t allocated_tiles;  // Keep track of memory allocated for tiles.
        //
        //    TOKENEXTRA *tile_tok[MAX_TILE_ROWS][MAX_TILE_COLS];
        //    uint32_t tok_count[MAX_TILE_ROWS][MAX_TILE_COLS];
        //
        //    TileBufferEnc tile_buffers[MAX_TILE_ROWS][MAX_TILE_COLS];
        //
        //    int32_t resize_state;
        //    int32_t resize_avg_qp;
        //    int32_t resize_buffer_underflow;
        //    int32_t resize_count;
        //
        //    // Sequence parameters have been transmitted already and locked
        //    // or not. Once locked av1_change_config cannot change the seq
        //    // parameters.
        //    int32_t seq_params_locked;
        //
        //    // VARIANCE_AQ segment map refresh
        //    int32_t vaq_refresh;
        //
        //    // Multi-threading
        //    int32_t num_workers;
        //    AVxWorker *workers;
        //    struct EncWorkerData *tile_thr_data;
        //    int32_t refresh_frame_mask;
        //    int32_t existing_fb_idx_to_show;
        //    int32_t is_arf_filter_off[MAX_EXT_ARFS + 1];
        //    int32_t num_extra_arfs;
        //    int32_t arf_map[MAX_EXT_ARFS + 1];
        //    int32_t arf_pos_in_gf[MAX_EXT_ARFS + 1];
        //    int32_t arf_pos_for_ovrly[MAX_EXT_ARFS + 1];
        //    int32_t global_motion_search_done;
        //    TranLow *tcoeff_buf[MAX_MB_PLANE];
        //    int32_t extra_arf_allowed;
        //    int32_t bwd_ref_allowed;
        //    // A flag to indicate if intrabc is ever used in current frame.
        //    int32_t intrabc_used;
        //    int32_t dv_cost[2][MV_VALS];
        //    // TODO(huisu@google.com): we can update dv_joint_cost per SB.
        //    int32_t dv_joint_cost[MV_JOINTS];
        //    int32_t has_lossless_segment;
        //
        //    // For frame refs short signaling:
        //    //   A mapping of each reference frame from its encoder side value to the
        //    //   decoder side value obtained following the short signaling procedure.
        //    int32_t ref_conv[REF_FRAMES];
        //
        //    AV1LfSync lf_row_sync;
    } Av1Comp;

    /**************************************
     * Extern Function Declarations
     **************************************/
    extern EbErrorType picture_control_set_creator(
        EbPtr *object_dbl_ptr,
        EbPtr  object_init_data_ptr);

    extern EbErrorType picture_parent_control_set_creator(
        EbPtr *object_dbl_ptr,
        EbPtr  object_init_data_ptr);

    extern EbErrorType me_sb_results_ctor(
        MeLcuResults      *objectPtr,
        uint32_t           maxNumberOfPusPerLcu,
        uint8_t            mrp_mode,
        uint32_t           maxNumberOfMeCandidatesPerPU);
#ifdef __cplusplus
}
#endif
#endif // EbPictureControlSet_h
// clang-format on
