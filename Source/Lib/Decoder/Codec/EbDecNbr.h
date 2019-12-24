/*
* Copyright(c) 2019 Netflix, Inc.
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef EbDecNbr_h
#define EbDecNbr_h

#include "EbObuParse.h"
#include "EbDecParseFrame.h"

#if !FRAME_MI_MAP
void update_nbrs_before_sb(FrameMiMap *frame_mi_map, int32_t sb_col);

void update_nbrs_after_sb(FrameMiMap *frame_mi_map, int32_t sb_col);
#endif
block_mode_info* get_cur_mode_info(void *pv_dec_handle,
    int mi_row, int mi_col, SBInfo *sb_info);

void update_block_nbrs(EbDecHandle *dec_handle,
    ParseCtxt *parse_ctx, int mi_row, int mi_col,
    BlockSize subsize);

block_mode_info * get_left_mode_info(EbDecHandle *dec_handle,
    int mi_row, int mi_col, SBInfo *sb_info);

block_mode_info* get_top_mode_info(EbDecHandle *dec_handle,
    int mi_row, int mi_col, SBInfo *sb_info);

#endif //EbDecNbr_h
