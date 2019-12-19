// clang-format off
/*
 * Copyright (c) 2018, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <immintrin.h>  // AVX2
#include "synonyms.h"
#include "synonyms_avx2.h"
#include "aom_dsp_rtcd.h"
#include "EbPictureOperators_Inline_AVX2.h"
#include "EbRestoration.h"
#include "EbRestorationPick.h"
#include "EbUtility.h"
#include "pickrst_avx2.h"
#include "transpose_sse2.h"
#include "transpose_avx2.h"

static INLINE uint8_t find_average_avx2(const uint8_t *src, int32_t h_start,
                                        int32_t h_end, int32_t v_start,
                                        int32_t v_end, int32_t stride) {
    const int32_t width = h_end - h_start;
    const int32_t height = v_end - v_start;
    const uint8_t *srcT = src + v_start * stride + h_start;
    const int32_t leftover = width & 31;
    int32_t i = height;
    __m256i ss = _mm256_setzero_si256();

    if (!leftover) {
        do {
            int32_t j = 0;
            do {
                const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                const __m256i sad = _mm256_sad_epu8(s, _mm256_setzero_si256());
                ss = _mm256_add_epi32(ss, sad);
                j += 32;
            } while (j < width);

            srcT += stride;
        } while (--i);
    } else {
        const int32_t w32 = width - leftover;
        __m128i maskL, maskH;

        if (leftover >= 16) {
            maskL = _mm_set1_epi8(-1);
            maskH = _mm_load_si128((__m128i *)(mask_8bit[leftover - 16]));
        } else {
            maskL = _mm_load_si128((__m128i *)(mask_8bit[leftover]));
            maskH = _mm_setzero_si128();
        }
        const __m256i mask =
            _mm256_inserti128_si256(_mm256_castsi128_si256(maskL), maskH, 1);

        do {
            int32_t j = 0;
            do {
                const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                const __m256i sad = _mm256_sad_epu8(s, _mm256_setzero_si256());
                ss = _mm256_add_epi32(ss, sad);
                j += 32;
            } while (j < w32);

            const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
            const __m256i sT = _mm256_and_si256(s, mask);
            const __m256i sad = _mm256_sad_epu8(sT, _mm256_setzero_si256());
            ss = _mm256_add_epi32(ss, sad);
            srcT += stride;
        } while (--i);
    }

    const uint32_t sum = Hadd32_AVX2_INTRIN(ss);
    const uint32_t avg = sum / (width * height);
    return (uint8_t)avg;
}

static INLINE void add_u16_to_u32_avx2(const __m256i src, __m256i *const sum) {
    const __m256i s0 = _mm256_unpacklo_epi16(src, _mm256_setzero_si256());
    const __m256i s1 = _mm256_unpackhi_epi16(src, _mm256_setzero_si256());
    *sum = _mm256_add_epi32(*sum, s0);
    *sum = _mm256_add_epi32(*sum, s1);
}

static INLINE void add_32_to_64_avx2(const __m256i src, __m256i *const sum) {
    const __m256i s0 = _mm256_cvtepi32_epi64(_mm256_extracti128_si256(src, 0));
    const __m256i s1 = _mm256_cvtepi32_epi64(_mm256_extracti128_si256(src, 1));
    *sum = _mm256_add_epi64(*sum, s0);
    *sum = _mm256_add_epi64(*sum, s1);
}

static INLINE uint16_t find_average_highbd_avx2(const uint16_t *src,
                                                int32_t h_start, int32_t h_end,
                                                int32_t v_start, int32_t v_end,
                                                int32_t stride,
                                                AomBitDepth bit_depth) {
    const int32_t width = h_end - h_start;
    const int32_t height = v_end - v_start;
    const uint16_t *srcT = src + v_start * stride + h_start;
    const int32_t leftover = width & 15;
    int32_t i = height;
    __m256i sss = _mm256_setzero_si256();

    if (bit_depth <= 10 || width <= 256) {
        if (!leftover) {
            do {
                __m256i ss = _mm256_setzero_si256();

                int32_t j = 0;
                do {
                    const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                    ss = _mm256_add_epi16(ss, s);
                    j += 16;
                } while (j < width);

                add_u16_to_u32_avx2(ss, &sss);

                srcT += stride;
            } while (--i);
        } else {
            const int32_t w16 = width - leftover;
            const __m256i mask =
                _mm256_load_si256((__m256i *)(mask_16bit[leftover]));

            do {
                __m256i ss = _mm256_setzero_si256();

                int32_t j = 0;
                do {
                    const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                    ss = _mm256_add_epi16(ss, s);
                    j += 16;
                } while (j < w16);

                const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                const __m256i sT = _mm256_and_si256(s, mask);
                ss = _mm256_add_epi16(ss, sT);

                add_u16_to_u32_avx2(ss, &sss);

                srcT += stride;
            } while (--i);
        }
    } else {
        if (!leftover) {
            do {
                __m256i ss = _mm256_setzero_si256();

                int32_t j = 0;
                do {
                    const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                    ss = _mm256_add_epi16(ss, s);
                    j += 16;
                } while (j < 256);

                add_u16_to_u32_avx2(ss, &sss);
                ss = _mm256_setzero_si256();

                do {
                    const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                    ss = _mm256_add_epi16(ss, s);
                    j += 16;
                } while (j < width);

                add_u16_to_u32_avx2(ss, &sss);

                srcT += stride;
            } while (--i);
        } else {
            const int32_t w16 = width - leftover;
            const __m256i mask =
                _mm256_load_si256((__m256i *)(mask_16bit[leftover]));

            do {
                __m256i ss = _mm256_setzero_si256();

                int32_t j = 0;
                do {
                    const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                    ss = _mm256_add_epi16(ss, s);
                    j += 16;
                } while (j < 256);

                add_u16_to_u32_avx2(ss, &sss);
                ss = _mm256_setzero_si256();

                while (j < w16) {
                    const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                    ss = _mm256_add_epi16(ss, s);
                    j += 16;
                }

                const __m256i s = _mm256_loadu_si256((__m256i *)(srcT + j));
                const __m256i sT = _mm256_and_si256(s, mask);
                ss = _mm256_add_epi16(ss, sT);

                add_u16_to_u32_avx2(ss, &sss);

                srcT += stride;
            } while (--i);
        }
    }

    const uint32_t sum = Hadd32_AVX2_INTRIN(sss);
    const uint32_t avg = sum / (width * height);
    return (uint16_t)avg;
}

// Note: when n = (width % 16) is not 0, it writes (16 - n) more data than
// required.
static INLINE void sub_avg_block_avx2(const uint8_t *src,
                                      const int32_t src_stride,
                                      const uint8_t avg, const int32_t width,
                                      const int32_t height, int16_t *dst,
                                      const int32_t dst_stride) {
    const __m256i a = _mm256_set1_epi16(avg);

    int32_t i = height;
    do {
        int32_t j = 0;
        do {
            const __m128i s = _mm_loadu_si128((__m128i *)(src + j));
            const __m256i ss = _mm256_cvtepu8_epi16(s);
            const __m256i d = _mm256_sub_epi16(ss, a);
            _mm256_store_si256((__m256i *)(dst + j), d);
            j += 16;
        } while (j < width);

        src += src_stride;
        dst += dst_stride;
    } while (--i);
}

// Note: when n = (width % 16) is not 0, it writes (16 - n) more data than
// required.
static INLINE void sub_avg_block_highbd_avx2(const uint16_t *src,
                                             const int32_t src_stride,
                                             const uint16_t avg,
                                             const int32_t width,
                                             const int32_t height, int16_t *dst,
                                             const int32_t dst_stride) {
    const __m256i a = _mm256_set1_epi16(avg);

    int32_t i = height;
    do {
        int32_t j = 0;
        do {
            const __m256i s = _mm256_loadu_si256((__m256i *)(src + j));
            const __m256i d = _mm256_sub_epi16(s, a);
            _mm256_store_si256((__m256i *)(dst + j), d);
            j += 16;
        } while (j < width);

        src += src_stride;
        dst += dst_stride;
    } while (--i);
}

static INLINE void stats_top_win3_avx2(const __m256i src, const __m256i dgd,
                                       const int16_t *const d,
                                       const int32_t d_stride,
                                       __m256i sumM[WIENER_WIN_3TAP],
                                       __m256i sumH[WIENER_WIN_3TAP]) {
    __m256i dgds[WIENER_WIN_3TAP];

    dgds[0] = _mm256_loadu_si256((__m256i *)(d + 0 * d_stride));
    dgds[1] = _mm256_loadu_si256((__m256i *)(d + 1 * d_stride));
    dgds[2] = _mm256_loadu_si256((__m256i *)(d + 2 * d_stride));

    madd_avx2(src, dgds[0], &sumM[0]);
    madd_avx2(src, dgds[1], &sumM[1]);
    madd_avx2(src, dgds[2], &sumM[2]);

    madd_avx2(dgd, dgds[0], &sumH[0]);
    madd_avx2(dgd, dgds[1], &sumH[1]);
    madd_avx2(dgd, dgds[2], &sumH[2]);
}

static INLINE void stats_top_win5_avx2(const __m256i src, const __m256i dgd,
                                       const int16_t *const d,
                                       const int32_t d_stride,
                                       __m256i sumM[WIENER_WIN_CHROMA],
                                       __m256i sumH[WIENER_WIN_CHROMA]) {
    __m256i dgds[WIENER_WIN_CHROMA];

    dgds[0] = _mm256_loadu_si256((__m256i *)(d + 0 * d_stride));
    dgds[1] = _mm256_loadu_si256((__m256i *)(d + 1 * d_stride));
    dgds[2] = _mm256_loadu_si256((__m256i *)(d + 2 * d_stride));
    dgds[3] = _mm256_loadu_si256((__m256i *)(d + 3 * d_stride));
    dgds[4] = _mm256_loadu_si256((__m256i *)(d + 4 * d_stride));

    madd_avx2(src, dgds[0], &sumM[0]);
    madd_avx2(src, dgds[1], &sumM[1]);
    madd_avx2(src, dgds[2], &sumM[2]);
    madd_avx2(src, dgds[3], &sumM[3]);
    madd_avx2(src, dgds[4], &sumM[4]);

    madd_avx2(dgd, dgds[0], &sumH[0]);
    madd_avx2(dgd, dgds[1], &sumH[1]);
    madd_avx2(dgd, dgds[2], &sumH[2]);
    madd_avx2(dgd, dgds[3], &sumH[3]);
    madd_avx2(dgd, dgds[4], &sumH[4]);
}

static INLINE void stats_top_win7_avx2(const __m256i src, const __m256i dgd,
                                       const int16_t *const d,
                                       const int32_t d_stride,
                                       __m256i sumM[WIENER_WIN],
                                       __m256i sumH[WIENER_WIN]) {
    __m256i dgds[WIENER_WIN];

    dgds[0] = _mm256_loadu_si256((__m256i *)(d + 0 * d_stride));
    dgds[1] = _mm256_loadu_si256((__m256i *)(d + 1 * d_stride));
    dgds[2] = _mm256_loadu_si256((__m256i *)(d + 2 * d_stride));
    dgds[3] = _mm256_loadu_si256((__m256i *)(d + 3 * d_stride));
    dgds[4] = _mm256_loadu_si256((__m256i *)(d + 4 * d_stride));
    dgds[5] = _mm256_loadu_si256((__m256i *)(d + 5 * d_stride));
    dgds[6] = _mm256_loadu_si256((__m256i *)(d + 6 * d_stride));

    madd_avx2(src, dgds[0], &sumM[0]);
    madd_avx2(src, dgds[1], &sumM[1]);
    madd_avx2(src, dgds[2], &sumM[2]);
    madd_avx2(src, dgds[3], &sumM[3]);
    madd_avx2(src, dgds[4], &sumM[4]);
    madd_avx2(src, dgds[5], &sumM[5]);
    madd_avx2(src, dgds[6], &sumM[6]);

    madd_avx2(dgd, dgds[0], &sumH[0]);
    madd_avx2(dgd, dgds[1], &sumH[1]);
    madd_avx2(dgd, dgds[2], &sumH[2]);
    madd_avx2(dgd, dgds[3], &sumH[3]);
    madd_avx2(dgd, dgds[4], &sumH[4]);
    madd_avx2(dgd, dgds[5], &sumH[5]);
    madd_avx2(dgd, dgds[6], &sumH[6]);
}

static INLINE void stats_left_win3_avx2(const __m256i src, const int16_t *d,
                                        const int32_t d_stride,
                                        __m256i sum[WIENER_WIN_3TAP - 1]) {
    __m256i dgds[WIENER_WIN_3TAP - 1];

    dgds[0] = _mm256_load_si256((__m256i *)(d + 1 * d_stride));
    dgds[1] = _mm256_load_si256((__m256i *)(d + 2 * d_stride));

    madd_avx2(src, dgds[0], &sum[0]);
    madd_avx2(src, dgds[1], &sum[1]);
}

static INLINE void stats_left_win5_avx2(const __m256i src, const int16_t *d,
                                        const int32_t d_stride,
                                        __m256i sum[WIENER_WIN_CHROMA - 1]) {
    __m256i dgds[WIENER_WIN_CHROMA - 1];

    dgds[0] = _mm256_load_si256((__m256i *)(d + 1 * d_stride));
    dgds[1] = _mm256_load_si256((__m256i *)(d + 2 * d_stride));
    dgds[2] = _mm256_load_si256((__m256i *)(d + 3 * d_stride));
    dgds[3] = _mm256_load_si256((__m256i *)(d + 4 * d_stride));

    madd_avx2(src, dgds[0], &sum[0]);
    madd_avx2(src, dgds[1], &sum[1]);
    madd_avx2(src, dgds[2], &sum[2]);
    madd_avx2(src, dgds[3], &sum[3]);
}

static INLINE void stats_left_win7_avx2(const __m256i src, const int16_t *d,
                                        const int32_t d_stride,
                                        __m256i sum[WIENER_WIN - 1]) {
    __m256i dgds[WIENER_WIN - 1];

    dgds[0] = _mm256_load_si256((__m256i *)(d + 1 * d_stride));
    dgds[1] = _mm256_load_si256((__m256i *)(d + 2 * d_stride));
    dgds[2] = _mm256_load_si256((__m256i *)(d + 3 * d_stride));
    dgds[3] = _mm256_load_si256((__m256i *)(d + 4 * d_stride));
    dgds[4] = _mm256_load_si256((__m256i *)(d + 5 * d_stride));
    dgds[5] = _mm256_load_si256((__m256i *)(d + 6 * d_stride));

    madd_avx2(src, dgds[0], &sum[0]);
    madd_avx2(src, dgds[1], &sum[1]);
    madd_avx2(src, dgds[2], &sum[2]);
    madd_avx2(src, dgds[3], &sum[3]);
    madd_avx2(src, dgds[4], &sum[4]);
    madd_avx2(src, dgds[5], &sum[5]);
}

static INLINE void load_square_win3_avx2(
    const int16_t *const dI, const int16_t *const dJ, const int32_t d_stride,
    const int32_t height, __m256i dIs[WIENER_WIN_3TAP - 1],
    __m256i dIe[WIENER_WIN_3TAP - 1], __m256i dJs[WIENER_WIN_3TAP - 1],
    __m256i dJe[WIENER_WIN_3TAP - 1]) {
    dIs[0] = _mm256_loadu_si256((__m256i *)(dI + 0 * d_stride));
    dJs[0] = _mm256_loadu_si256((__m256i *)(dJ + 0 * d_stride));
    dIs[1] = _mm256_loadu_si256((__m256i *)(dI + 1 * d_stride));
    dJs[1] = _mm256_loadu_si256((__m256i *)(dJ + 1 * d_stride));

    dIe[0] = _mm256_loadu_si256((__m256i *)(dI + (0 + height) * d_stride));
    dJe[0] = _mm256_loadu_si256((__m256i *)(dJ + (0 + height) * d_stride));
    dIe[1] = _mm256_loadu_si256((__m256i *)(dI + (1 + height) * d_stride));
    dJe[1] = _mm256_loadu_si256((__m256i *)(dJ + (1 + height) * d_stride));
}

static INLINE void load_square_win5_avx2(
    const int16_t *const dI, const int16_t *const dJ, const int32_t d_stride,
    const int32_t height, __m256i dIs[WIENER_WIN_CHROMA - 1],
    __m256i dIe[WIENER_WIN_CHROMA - 1], __m256i dJs[WIENER_WIN_CHROMA - 1],
    __m256i dJe[WIENER_WIN_CHROMA - 1]) {
    dIs[0] = _mm256_loadu_si256((__m256i *)(dI + 0 * d_stride));
    dJs[0] = _mm256_loadu_si256((__m256i *)(dJ + 0 * d_stride));
    dIs[1] = _mm256_loadu_si256((__m256i *)(dI + 1 * d_stride));
    dJs[1] = _mm256_loadu_si256((__m256i *)(dJ + 1 * d_stride));
    dIs[2] = _mm256_loadu_si256((__m256i *)(dI + 2 * d_stride));
    dJs[2] = _mm256_loadu_si256((__m256i *)(dJ + 2 * d_stride));
    dIs[3] = _mm256_loadu_si256((__m256i *)(dI + 3 * d_stride));
    dJs[3] = _mm256_loadu_si256((__m256i *)(dJ + 3 * d_stride));

    dIe[0] = _mm256_loadu_si256((__m256i *)(dI + (0 + height) * d_stride));
    dJe[0] = _mm256_loadu_si256((__m256i *)(dJ + (0 + height) * d_stride));
    dIe[1] = _mm256_loadu_si256((__m256i *)(dI + (1 + height) * d_stride));
    dJe[1] = _mm256_loadu_si256((__m256i *)(dJ + (1 + height) * d_stride));
    dIe[2] = _mm256_loadu_si256((__m256i *)(dI + (2 + height) * d_stride));
    dJe[2] = _mm256_loadu_si256((__m256i *)(dJ + (2 + height) * d_stride));
    dIe[3] = _mm256_loadu_si256((__m256i *)(dI + (3 + height) * d_stride));
    dJe[3] = _mm256_loadu_si256((__m256i *)(dJ + (3 + height) * d_stride));
}

static INLINE void load_square_win7_avx2(
    const int16_t *const dI, const int16_t *const dJ, const int32_t d_stride,
    const int32_t height, __m256i dIs[WIENER_WIN - 1],
    __m256i dIe[WIENER_WIN - 1], __m256i dJs[WIENER_WIN - 1],
    __m256i dJe[WIENER_WIN - 1]) {
    dIs[0] = _mm256_loadu_si256((__m256i *)(dI + 0 * d_stride));
    dJs[0] = _mm256_loadu_si256((__m256i *)(dJ + 0 * d_stride));
    dIs[1] = _mm256_loadu_si256((__m256i *)(dI + 1 * d_stride));
    dJs[1] = _mm256_loadu_si256((__m256i *)(dJ + 1 * d_stride));
    dIs[2] = _mm256_loadu_si256((__m256i *)(dI + 2 * d_stride));
    dJs[2] = _mm256_loadu_si256((__m256i *)(dJ + 2 * d_stride));
    dIs[3] = _mm256_loadu_si256((__m256i *)(dI + 3 * d_stride));
    dJs[3] = _mm256_loadu_si256((__m256i *)(dJ + 3 * d_stride));
    dIs[4] = _mm256_loadu_si256((__m256i *)(dI + 4 * d_stride));
    dJs[4] = _mm256_loadu_si256((__m256i *)(dJ + 4 * d_stride));
    dIs[5] = _mm256_loadu_si256((__m256i *)(dI + 5 * d_stride));
    dJs[5] = _mm256_loadu_si256((__m256i *)(dJ + 5 * d_stride));

    dIe[0] = _mm256_loadu_si256((__m256i *)(dI + (0 + height) * d_stride));
    dJe[0] = _mm256_loadu_si256((__m256i *)(dJ + (0 + height) * d_stride));
    dIe[1] = _mm256_loadu_si256((__m256i *)(dI + (1 + height) * d_stride));
    dJe[1] = _mm256_loadu_si256((__m256i *)(dJ + (1 + height) * d_stride));
    dIe[2] = _mm256_loadu_si256((__m256i *)(dI + (2 + height) * d_stride));
    dJe[2] = _mm256_loadu_si256((__m256i *)(dJ + (2 + height) * d_stride));
    dIe[3] = _mm256_loadu_si256((__m256i *)(dI + (3 + height) * d_stride));
    dJe[3] = _mm256_loadu_si256((__m256i *)(dJ + (3 + height) * d_stride));
    dIe[4] = _mm256_loadu_si256((__m256i *)(dI + (4 + height) * d_stride));
    dJe[4] = _mm256_loadu_si256((__m256i *)(dJ + (4 + height) * d_stride));
    dIe[5] = _mm256_loadu_si256((__m256i *)(dI + (5 + height) * d_stride));
    dJe[5] = _mm256_loadu_si256((__m256i *)(dJ + (5 + height) * d_stride));
}

static INLINE void load_triangle_win3_avx2(const int16_t *const dI,
                                           const int32_t d_stride,
                                           const int32_t height,
                                           __m256i dIs[WIENER_WIN_3TAP - 1],
                                           __m256i dIe[WIENER_WIN_3TAP - 1]) {
    dIs[0] = _mm256_loadu_si256((__m256i *)(dI + 0 * d_stride));
    dIs[1] = _mm256_loadu_si256((__m256i *)(dI + 1 * d_stride));

    dIe[0] = _mm256_loadu_si256((__m256i *)(dI + (0 + height) * d_stride));
    dIe[1] = _mm256_loadu_si256((__m256i *)(dI + (1 + height) * d_stride));
}

static INLINE void load_triangle_win5_avx2(const int16_t *const dI,
                                           const int32_t d_stride,
                                           const int32_t height,
                                           __m256i dIs[WIENER_WIN_CHROMA - 1],
                                           __m256i dIe[WIENER_WIN_CHROMA - 1]) {
    dIs[0] = _mm256_loadu_si256((__m256i *)(dI + 0 * d_stride));
    dIs[1] = _mm256_loadu_si256((__m256i *)(dI + 1 * d_stride));
    dIs[2] = _mm256_loadu_si256((__m256i *)(dI + 2 * d_stride));
    dIs[3] = _mm256_loadu_si256((__m256i *)(dI + 3 * d_stride));

    dIe[0] = _mm256_loadu_si256((__m256i *)(dI + (0 + height) * d_stride));
    dIe[1] = _mm256_loadu_si256((__m256i *)(dI + (1 + height) * d_stride));
    dIe[2] = _mm256_loadu_si256((__m256i *)(dI + (2 + height) * d_stride));
    dIe[3] = _mm256_loadu_si256((__m256i *)(dI + (3 + height) * d_stride));
}

static INLINE void load_triangle_win7_avx2(const int16_t *const dI,
                                           const int32_t d_stride,
                                           const int32_t height,
                                           __m256i dIs[WIENER_WIN - 1],
                                           __m256i dIe[WIENER_WIN - 1]) {
    dIs[0] = _mm256_loadu_si256((__m256i *)(dI + 0 * d_stride));
    dIs[1] = _mm256_loadu_si256((__m256i *)(dI + 1 * d_stride));
    dIs[2] = _mm256_loadu_si256((__m256i *)(dI + 2 * d_stride));
    dIs[3] = _mm256_loadu_si256((__m256i *)(dI + 3 * d_stride));
    dIs[4] = _mm256_loadu_si256((__m256i *)(dI + 4 * d_stride));
    dIs[5] = _mm256_loadu_si256((__m256i *)(dI + 5 * d_stride));

    dIe[0] = _mm256_loadu_si256((__m256i *)(dI + (0 + height) * d_stride));
    dIe[1] = _mm256_loadu_si256((__m256i *)(dI + (1 + height) * d_stride));
    dIe[2] = _mm256_loadu_si256((__m256i *)(dI + (2 + height) * d_stride));
    dIe[3] = _mm256_loadu_si256((__m256i *)(dI + (3 + height) * d_stride));
    dIe[4] = _mm256_loadu_si256((__m256i *)(dI + (4 + height) * d_stride));
    dIe[5] = _mm256_loadu_si256((__m256i *)(dI + (5 + height) * d_stride));
}

static INLINE void derive_square_win3_avx2(
    const __m256i dIs[WIENER_WIN_3TAP - 1],
    const __m256i dIe[WIENER_WIN_3TAP - 1],
    const __m256i dJs[WIENER_WIN_3TAP - 1],
    const __m256i dJe[WIENER_WIN_3TAP - 1],
    __m256i deltas[WIENER_WIN_3TAP - 1][WIENER_WIN_3TAP - 1]) {
    msub_avx2(dIs[0], dJs[0], &deltas[0][0]);
    msub_avx2(dIs[0], dJs[1], &deltas[0][1]);
    msub_avx2(dIs[1], dJs[0], &deltas[1][0]);
    msub_avx2(dIs[1], dJs[1], &deltas[1][1]);

    madd_avx2(dIe[0], dJe[0], &deltas[0][0]);
    madd_avx2(dIe[0], dJe[1], &deltas[0][1]);
    madd_avx2(dIe[1], dJe[0], &deltas[1][0]);
    madd_avx2(dIe[1], dJe[1], &deltas[1][1]);
}

static INLINE void derive_square_win5_avx2(
    const __m256i dIs[WIENER_WIN_CHROMA - 1],
    const __m256i dIe[WIENER_WIN_CHROMA - 1],
    const __m256i dJs[WIENER_WIN_CHROMA - 1],
    const __m256i dJe[WIENER_WIN_CHROMA - 1],
    __m256i deltas[WIENER_WIN_CHROMA - 1][WIENER_WIN_CHROMA - 1]) {
    msub_avx2(dIs[0], dJs[0], &deltas[0][0]);
    msub_avx2(dIs[0], dJs[1], &deltas[0][1]);
    msub_avx2(dIs[0], dJs[2], &deltas[0][2]);
    msub_avx2(dIs[0], dJs[3], &deltas[0][3]);
    msub_avx2(dIs[1], dJs[0], &deltas[1][0]);
    msub_avx2(dIs[1], dJs[1], &deltas[1][1]);
    msub_avx2(dIs[1], dJs[2], &deltas[1][2]);
    msub_avx2(dIs[1], dJs[3], &deltas[1][3]);
    msub_avx2(dIs[2], dJs[0], &deltas[2][0]);
    msub_avx2(dIs[2], dJs[1], &deltas[2][1]);
    msub_avx2(dIs[2], dJs[2], &deltas[2][2]);
    msub_avx2(dIs[2], dJs[3], &deltas[2][3]);
    msub_avx2(dIs[3], dJs[0], &deltas[3][0]);
    msub_avx2(dIs[3], dJs[1], &deltas[3][1]);
    msub_avx2(dIs[3], dJs[2], &deltas[3][2]);
    msub_avx2(dIs[3], dJs[3], &deltas[3][3]);

    madd_avx2(dIe[0], dJe[0], &deltas[0][0]);
    madd_avx2(dIe[0], dJe[1], &deltas[0][1]);
    madd_avx2(dIe[0], dJe[2], &deltas[0][2]);
    madd_avx2(dIe[0], dJe[3], &deltas[0][3]);
    madd_avx2(dIe[1], dJe[0], &deltas[1][0]);
    madd_avx2(dIe[1], dJe[1], &deltas[1][1]);
    madd_avx2(dIe[1], dJe[2], &deltas[1][2]);
    madd_avx2(dIe[1], dJe[3], &deltas[1][3]);
    madd_avx2(dIe[2], dJe[0], &deltas[2][0]);
    madd_avx2(dIe[2], dJe[1], &deltas[2][1]);
    madd_avx2(dIe[2], dJe[2], &deltas[2][2]);
    madd_avx2(dIe[2], dJe[3], &deltas[2][3]);
    madd_avx2(dIe[3], dJe[0], &deltas[3][0]);
    madd_avx2(dIe[3], dJe[1], &deltas[3][1]);
    madd_avx2(dIe[3], dJe[2], &deltas[3][2]);
    madd_avx2(dIe[3], dJe[3], &deltas[3][3]);
}

static INLINE void derive_square_win7_avx2(
    const __m256i dIs[WIENER_WIN - 1], const __m256i dIe[WIENER_WIN - 1],
    const __m256i dJs[WIENER_WIN - 1], const __m256i dJe[WIENER_WIN - 1],
    __m256i deltas[WIENER_WIN - 1][WIENER_WIN - 1]) {
    msub_avx2(dIs[0], dJs[0], &deltas[0][0]);
    msub_avx2(dIs[0], dJs[1], &deltas[0][1]);
    msub_avx2(dIs[0], dJs[2], &deltas[0][2]);
    msub_avx2(dIs[0], dJs[3], &deltas[0][3]);
    msub_avx2(dIs[0], dJs[4], &deltas[0][4]);
    msub_avx2(dIs[0], dJs[5], &deltas[0][5]);
    msub_avx2(dIs[1], dJs[0], &deltas[1][0]);
    msub_avx2(dIs[1], dJs[1], &deltas[1][1]);
    msub_avx2(dIs[1], dJs[2], &deltas[1][2]);
    msub_avx2(dIs[1], dJs[3], &deltas[1][3]);
    msub_avx2(dIs[1], dJs[4], &deltas[1][4]);
    msub_avx2(dIs[1], dJs[5], &deltas[1][5]);
    msub_avx2(dIs[2], dJs[0], &deltas[2][0]);
    msub_avx2(dIs[2], dJs[1], &deltas[2][1]);
    msub_avx2(dIs[2], dJs[2], &deltas[2][2]);
    msub_avx2(dIs[2], dJs[3], &deltas[2][3]);
    msub_avx2(dIs[2], dJs[4], &deltas[2][4]);
    msub_avx2(dIs[2], dJs[5], &deltas[2][5]);
    msub_avx2(dIs[3], dJs[0], &deltas[3][0]);
    msub_avx2(dIs[3], dJs[1], &deltas[3][1]);
    msub_avx2(dIs[3], dJs[2], &deltas[3][2]);
    msub_avx2(dIs[3], dJs[3], &deltas[3][3]);
    msub_avx2(dIs[3], dJs[4], &deltas[3][4]);
    msub_avx2(dIs[3], dJs[5], &deltas[3][5]);
    msub_avx2(dIs[4], dJs[0], &deltas[4][0]);
    msub_avx2(dIs[4], dJs[1], &deltas[4][1]);
    msub_avx2(dIs[4], dJs[2], &deltas[4][2]);
    msub_avx2(dIs[4], dJs[3], &deltas[4][3]);
    msub_avx2(dIs[4], dJs[4], &deltas[4][4]);
    msub_avx2(dIs[4], dJs[5], &deltas[4][5]);
    msub_avx2(dIs[5], dJs[0], &deltas[5][0]);
    msub_avx2(dIs[5], dJs[1], &deltas[5][1]);
    msub_avx2(dIs[5], dJs[2], &deltas[5][2]);
    msub_avx2(dIs[5], dJs[3], &deltas[5][3]);
    msub_avx2(dIs[5], dJs[4], &deltas[5][4]);
    msub_avx2(dIs[5], dJs[5], &deltas[5][5]);

    madd_avx2(dIe[0], dJe[0], &deltas[0][0]);
    madd_avx2(dIe[0], dJe[1], &deltas[0][1]);
    madd_avx2(dIe[0], dJe[2], &deltas[0][2]);
    madd_avx2(dIe[0], dJe[3], &deltas[0][3]);
    madd_avx2(dIe[0], dJe[4], &deltas[0][4]);
    madd_avx2(dIe[0], dJe[5], &deltas[0][5]);
    madd_avx2(dIe[1], dJe[0], &deltas[1][0]);
    madd_avx2(dIe[1], dJe[1], &deltas[1][1]);
    madd_avx2(dIe[1], dJe[2], &deltas[1][2]);
    madd_avx2(dIe[1], dJe[3], &deltas[1][3]);
    madd_avx2(dIe[1], dJe[4], &deltas[1][4]);
    madd_avx2(dIe[1], dJe[5], &deltas[1][5]);
    madd_avx2(dIe[2], dJe[0], &deltas[2][0]);
    madd_avx2(dIe[2], dJe[1], &deltas[2][1]);
    madd_avx2(dIe[2], dJe[2], &deltas[2][2]);
    madd_avx2(dIe[2], dJe[3], &deltas[2][3]);
    madd_avx2(dIe[2], dJe[4], &deltas[2][4]);
    madd_avx2(dIe[2], dJe[5], &deltas[2][5]);
    madd_avx2(dIe[3], dJe[0], &deltas[3][0]);
    madd_avx2(dIe[3], dJe[1], &deltas[3][1]);
    madd_avx2(dIe[3], dJe[2], &deltas[3][2]);
    madd_avx2(dIe[3], dJe[3], &deltas[3][3]);
    madd_avx2(dIe[3], dJe[4], &deltas[3][4]);
    madd_avx2(dIe[3], dJe[5], &deltas[3][5]);
    madd_avx2(dIe[4], dJe[0], &deltas[4][0]);
    madd_avx2(dIe[4], dJe[1], &deltas[4][1]);
    madd_avx2(dIe[4], dJe[2], &deltas[4][2]);
    madd_avx2(dIe[4], dJe[3], &deltas[4][3]);
    madd_avx2(dIe[4], dJe[4], &deltas[4][4]);
    madd_avx2(dIe[4], dJe[5], &deltas[4][5]);
    madd_avx2(dIe[5], dJe[0], &deltas[5][0]);
    madd_avx2(dIe[5], dJe[1], &deltas[5][1]);
    madd_avx2(dIe[5], dJe[2], &deltas[5][2]);
    madd_avx2(dIe[5], dJe[3], &deltas[5][3]);
    madd_avx2(dIe[5], dJe[4], &deltas[5][4]);
    madd_avx2(dIe[5], dJe[5], &deltas[5][5]);
}

static INLINE void derive_triangle_win3_avx2(
    const __m256i dIs[WIENER_WIN_3TAP - 1],
    const __m256i dIe[WIENER_WIN_3TAP - 1],
    __m256i deltas[WIENER_WIN_3TAP * (WIENER_WIN_3TAP - 1) / 2]) {
    msub_avx2(dIs[0], dIs[0], &deltas[0]);
    msub_avx2(dIs[0], dIs[1], &deltas[1]);
    msub_avx2(dIs[1], dIs[1], &deltas[2]);

    madd_avx2(dIe[0], dIe[0], &deltas[0]);
    madd_avx2(dIe[0], dIe[1], &deltas[1]);
    madd_avx2(dIe[1], dIe[1], &deltas[2]);
}

static INLINE void derive_triangle_win5_avx2(
    const __m256i dIs[WIENER_WIN_CHROMA - 1],
    const __m256i dIe[WIENER_WIN_CHROMA - 1],
    __m256i deltas[WIENER_WIN_CHROMA * (WIENER_WIN_CHROMA - 1) / 2]) {
    msub_avx2(dIs[0], dIs[0], &deltas[0]);
    msub_avx2(dIs[0], dIs[1], &deltas[1]);
    msub_avx2(dIs[0], dIs[2], &deltas[2]);
    msub_avx2(dIs[0], dIs[3], &deltas[3]);
    msub_avx2(dIs[1], dIs[1], &deltas[4]);
    msub_avx2(dIs[1], dIs[2], &deltas[5]);
    msub_avx2(dIs[1], dIs[3], &deltas[6]);
    msub_avx2(dIs[2], dIs[2], &deltas[7]);
    msub_avx2(dIs[2], dIs[3], &deltas[8]);
    msub_avx2(dIs[3], dIs[3], &deltas[9]);

    madd_avx2(dIe[0], dIe[0], &deltas[0]);
    madd_avx2(dIe[0], dIe[1], &deltas[1]);
    madd_avx2(dIe[0], dIe[2], &deltas[2]);
    madd_avx2(dIe[0], dIe[3], &deltas[3]);
    madd_avx2(dIe[1], dIe[1], &deltas[4]);
    madd_avx2(dIe[1], dIe[2], &deltas[5]);
    madd_avx2(dIe[1], dIe[3], &deltas[6]);
    madd_avx2(dIe[2], dIe[2], &deltas[7]);
    madd_avx2(dIe[2], dIe[3], &deltas[8]);
    madd_avx2(dIe[3], dIe[3], &deltas[9]);
}

static INLINE void derive_triangle_win7_avx2(
    const __m256i dIs[WIENER_WIN - 1], const __m256i dIe[WIENER_WIN - 1],
    __m256i deltas[WIENER_WIN * (WIENER_WIN - 1) / 2]) {
    msub_avx2(dIs[0], dIs[0], &deltas[0]);
    msub_avx2(dIs[0], dIs[1], &deltas[1]);
    msub_avx2(dIs[0], dIs[2], &deltas[2]);
    msub_avx2(dIs[0], dIs[3], &deltas[3]);
    msub_avx2(dIs[0], dIs[4], &deltas[4]);
    msub_avx2(dIs[0], dIs[5], &deltas[5]);
    msub_avx2(dIs[1], dIs[1], &deltas[6]);
    msub_avx2(dIs[1], dIs[2], &deltas[7]);
    msub_avx2(dIs[1], dIs[3], &deltas[8]);
    msub_avx2(dIs[1], dIs[4], &deltas[9]);
    msub_avx2(dIs[1], dIs[5], &deltas[10]);
    msub_avx2(dIs[2], dIs[2], &deltas[11]);
    msub_avx2(dIs[2], dIs[3], &deltas[12]);
    msub_avx2(dIs[2], dIs[4], &deltas[13]);
    msub_avx2(dIs[2], dIs[5], &deltas[14]);
    msub_avx2(dIs[3], dIs[3], &deltas[15]);
    msub_avx2(dIs[3], dIs[4], &deltas[16]);
    msub_avx2(dIs[3], dIs[5], &deltas[17]);
    msub_avx2(dIs[4], dIs[4], &deltas[18]);
    msub_avx2(dIs[4], dIs[5], &deltas[19]);
    msub_avx2(dIs[5], dIs[5], &deltas[20]);

    madd_avx2(dIe[0], dIe[0], &deltas[0]);
    madd_avx2(dIe[0], dIe[1], &deltas[1]);
    madd_avx2(dIe[0], dIe[2], &deltas[2]);
    madd_avx2(dIe[0], dIe[3], &deltas[3]);
    madd_avx2(dIe[0], dIe[4], &deltas[4]);
    madd_avx2(dIe[0], dIe[5], &deltas[5]);
    madd_avx2(dIe[1], dIe[1], &deltas[6]);
    madd_avx2(dIe[1], dIe[2], &deltas[7]);
    madd_avx2(dIe[1], dIe[3], &deltas[8]);
    madd_avx2(dIe[1], dIe[4], &deltas[9]);
    madd_avx2(dIe[1], dIe[5], &deltas[10]);
    madd_avx2(dIe[2], dIe[2], &deltas[11]);
    madd_avx2(dIe[2], dIe[3], &deltas[12]);
    madd_avx2(dIe[2], dIe[4], &deltas[13]);
    madd_avx2(dIe[2], dIe[5], &deltas[14]);
    madd_avx2(dIe[3], dIe[3], &deltas[15]);
    madd_avx2(dIe[3], dIe[4], &deltas[16]);
    madd_avx2(dIe[3], dIe[5], &deltas[17]);
    madd_avx2(dIe[4], dIe[4], &deltas[18]);
    madd_avx2(dIe[4], dIe[5], &deltas[19]);
    madd_avx2(dIe[5], dIe[5], &deltas[20]);
}

static INLINE __m256i div4_avx2(const __m256i src) {
    __m256i sign, dst;

    // get sign
    sign = _mm256_srli_epi64(src, 63);
    sign = _mm256_sub_epi64(_mm256_setzero_si256(), sign);

    // abs
    dst = _mm256_xor_si256(src, sign);
    dst = _mm256_sub_epi64(dst, sign);

    // divide by 4
    dst = _mm256_srli_epi64(dst, 2);

    // apply sign
    dst = _mm256_xor_si256(dst, sign);
    return _mm256_sub_epi64(dst, sign);
}

static INLINE __m256i div16_avx2(const __m256i src) {
    __m256i sign, dst;

    // get sign
    sign = _mm256_srli_epi64(src, 63);
    sign = _mm256_sub_epi64(_mm256_setzero_si256(), sign);

    // abs
    dst = _mm256_xor_si256(src, sign);
    dst = _mm256_sub_epi64(dst, sign);

    // divide by 16
    dst = _mm256_srli_epi64(dst, 4);

    // apply sign
    dst = _mm256_xor_si256(dst, sign);
    return _mm256_sub_epi64(dst, sign);
}

static INLINE void div4_4x4_avx2(const int32_t wiener_win2, int64_t *const H,
                                 __m256i out[4]) {
    out[0] = _mm256_loadu_si256((__m256i *)(H + 0 * wiener_win2));
    out[1] = _mm256_loadu_si256((__m256i *)(H + 1 * wiener_win2));
    out[2] = _mm256_loadu_si256((__m256i *)(H + 2 * wiener_win2));
    out[3] = _mm256_loadu_si256((__m256i *)(H + 3 * wiener_win2));

    out[0] = div4_avx2(out[0]);
    out[1] = div4_avx2(out[1]);
    out[2] = div4_avx2(out[2]);
    out[3] = div4_avx2(out[3]);

    _mm256_storeu_si256((__m256i *)(H + 0 * wiener_win2), out[0]);
    _mm256_storeu_si256((__m256i *)(H + 1 * wiener_win2), out[1]);
    _mm256_storeu_si256((__m256i *)(H + 2 * wiener_win2), out[2]);
    _mm256_storeu_si256((__m256i *)(H + 3 * wiener_win2), out[3]);
}

static INLINE void div16_4x4_avx2(const int32_t wiener_win2, int64_t *const H,
                                  __m256i out[4]) {
    out[0] = _mm256_loadu_si256((__m256i *)(H + 0 * wiener_win2));
    out[1] = _mm256_loadu_si256((__m256i *)(H + 1 * wiener_win2));
    out[2] = _mm256_loadu_si256((__m256i *)(H + 2 * wiener_win2));
    out[3] = _mm256_loadu_si256((__m256i *)(H + 3 * wiener_win2));

    out[0] = div16_avx2(out[0]);
    out[1] = div16_avx2(out[1]);
    out[2] = div16_avx2(out[2]);
    out[3] = div16_avx2(out[3]);

    _mm256_storeu_si256((__m256i *)(H + 0 * wiener_win2), out[0]);
    _mm256_storeu_si256((__m256i *)(H + 1 * wiener_win2), out[1]);
    _mm256_storeu_si256((__m256i *)(H + 2 * wiener_win2), out[2]);
    _mm256_storeu_si256((__m256i *)(H + 3 * wiener_win2), out[3]);
}

// Transpose each 4x4 block starting from the second column, and save the needed
// points only.
static INLINE void diagonal_copy_stats_avx2(const int32_t wiener_win2,
                                            int64_t *const H) {
    for (int32_t i = 0; i < wiener_win2 - 1; i += 4) {
        __m256i in[4], out[4];

        in[0] =
            _mm256_loadu_si256((__m256i *)(H + (i + 0) * wiener_win2 + i + 1));
        in[1] =
            _mm256_loadu_si256((__m256i *)(H + (i + 1) * wiener_win2 + i + 1));
        in[2] =
            _mm256_loadu_si256((__m256i *)(H + (i + 2) * wiener_win2 + i + 1));
        in[3] =
            _mm256_loadu_si256((__m256i *)(H + (i + 3) * wiener_win2 + i + 1));

        transpose_64bit_4x4_avx2(in, out);

        _mm_storel_epi64((__m128i *)(H + (i + 1) * wiener_win2 + i),
                         _mm256_extracti128_si256(out[0], 0));
        _mm_storeu_si128((__m128i *)(H + (i + 2) * wiener_win2 + i),
                         _mm256_extracti128_si256(out[1], 0));
        _mm256_storeu_si256((__m256i *)(H + (i + 3) * wiener_win2 + i), out[2]);
        _mm256_storeu_si256((__m256i *)(H + (i + 4) * wiener_win2 + i), out[3]);

        for (int32_t j = i + 5; j < wiener_win2; j += 4) {
            in[0] =
                _mm256_loadu_si256((__m256i *)(H + (i + 0) * wiener_win2 + j));
            in[1] =
                _mm256_loadu_si256((__m256i *)(H + (i + 1) * wiener_win2 + j));
            in[2] =
                _mm256_loadu_si256((__m256i *)(H + (i + 2) * wiener_win2 + j));
            in[3] =
                _mm256_loadu_si256((__m256i *)(H + (i + 3) * wiener_win2 + j));

            transpose_64bit_4x4_avx2(in, out);

            _mm256_storeu_si256((__m256i *)(H + (j + 0) * wiener_win2 + i),
                                out[0]);
            _mm256_storeu_si256((__m256i *)(H + (j + 1) * wiener_win2 + i),
                                out[1]);
            _mm256_storeu_si256((__m256i *)(H + (j + 2) * wiener_win2 + i),
                                out[2]);
            _mm256_storeu_si256((__m256i *)(H + (j + 3) * wiener_win2 + i),
                                out[3]);
        }
    }
}

// Transpose each 4x4 block starting from the second column, and save the needed
// points only.
// H[4 * k * wiener_win2 + 4 * k] on the diagonal is omitted, and must be
// processed separately.
static INLINE void div4_diagonal_copy_stats_avx2(const int32_t wiener_win2,
                                                 int64_t *const H) {
    for (int32_t i = 0; i < wiener_win2 - 1; i += 4) {
        __m256i in[4], out[4];

        div4_4x4_avx2(wiener_win2, H + i * wiener_win2 + i + 1, in);
        transpose_64bit_4x4_avx2(in, out);

        _mm_storel_epi64((__m128i *)(H + (i + 1) * wiener_win2 + i),
                         _mm256_extracti128_si256(out[0], 0));
        _mm_storeu_si128((__m128i *)(H + (i + 2) * wiener_win2 + i),
                         _mm256_extracti128_si256(out[1], 0));
        _mm256_storeu_si256((__m256i *)(H + (i + 3) * wiener_win2 + i), out[2]);
        _mm256_storeu_si256((__m256i *)(H + (i + 4) * wiener_win2 + i), out[3]);

        for (int32_t j = i + 5; j < wiener_win2; j += 4) {
            div4_4x4_avx2(wiener_win2, H + i * wiener_win2 + j, in);
            transpose_64bit_4x4_avx2(in, out);

            _mm256_storeu_si256((__m256i *)(H + (j + 0) * wiener_win2 + i),
                                out[0]);
            _mm256_storeu_si256((__m256i *)(H + (j + 1) * wiener_win2 + i),
                                out[1]);
            _mm256_storeu_si256((__m256i *)(H + (j + 2) * wiener_win2 + i),
                                out[2]);
            _mm256_storeu_si256((__m256i *)(H + (j + 3) * wiener_win2 + i),
                                out[3]);
        }
    }
}

// Transpose each 4x4 block starting from the second column, and save the needed
// points only.
// H[4 * k * wiener_win2 + 4 * k] on the diagonal is omitted, and must be
// processed separately.
static INLINE void div16_diagonal_copy_stats_avx2(const int32_t wiener_win2,
                                                  int64_t *const H) {
    for (int32_t i = 0; i < wiener_win2 - 1; i += 4) {
        __m256i in[4], out[4];

        div16_4x4_avx2(wiener_win2, H + i * wiener_win2 + i + 1, in);
        transpose_64bit_4x4_avx2(in, out);

        _mm_storel_epi64((__m128i *)(H + (i + 1) * wiener_win2 + i),
                         _mm256_extracti128_si256(out[0], 0));
        _mm_storeu_si128((__m128i *)(H + (i + 2) * wiener_win2 + i),
                         _mm256_extracti128_si256(out[1], 0));
        _mm256_storeu_si256((__m256i *)(H + (i + 3) * wiener_win2 + i), out[2]);
        _mm256_storeu_si256((__m256i *)(H + (i + 4) * wiener_win2 + i), out[3]);

        for (int32_t j = i + 5; j < wiener_win2; j += 4) {
            div16_4x4_avx2(wiener_win2, H + i * wiener_win2 + j, in);
            transpose_64bit_4x4_avx2(in, out);

            _mm256_storeu_si256((__m256i *)(H + (j + 0) * wiener_win2 + i),
                                out[0]);
            _mm256_storeu_si256((__m256i *)(H + (j + 1) * wiener_win2 + i),
                                out[1]);
            _mm256_storeu_si256((__m256i *)(H + (j + 2) * wiener_win2 + i),
                                out[2]);
            _mm256_storeu_si256((__m256i *)(H + (j + 3) * wiener_win2 + i),
                                out[3]);
        }
    }
}

static INLINE void compute_stats_win3_avx2(
    const int16_t *const d, const int32_t d_stride, const int16_t *const s,
    const int32_t s_stride, const int32_t width, const int32_t height,
    int64_t *const M, int64_t *const H, AomBitDepth bit_depth) {
    const int32_t wiener_win = WIENER_WIN_3TAP;
    const int32_t wiener_win2 = wiener_win * wiener_win;
    const int32_t w16 = width & ~15;
    const int32_t h4 = height & ~3;
    const int32_t h8 = height & ~7;
    const __m256i mask =
        _mm256_load_si256((__m256i *)(mask_16bit[width - w16]));
    int32_t i, j, x, y;

    if (bit_depth == AOM_BITS_8) {
        // Step 1: Calculate the top edge of the whole matrix, i.e., the top
        // edge of each triangle and square on the top row.
        j = 0;
        do {
            const int16_t *sT = s;
            const int16_t *dT = d;
            __m256i sumM[WIENER_WIN_3TAP] = {0};
            __m256i sumH[WIENER_WIN_3TAP] = {0};

            y = height;
            do {
                x = 0;
                do {
                    const __m256i src = _mm256_load_si256((__m256i *)(sT + x));
                    const __m256i dgd = _mm256_load_si256((__m256i *)(dT + x));
                    stats_top_win3_avx2(
                        src, dgd, dT + j + x, d_stride, sumM, sumH);
                    x += 16;
                } while (x < w16);

                if (w16 != width) {
                    const __m256i src =
                        _mm256_load_si256((__m256i *)(sT + w16));
                    const __m256i dgd =
                        _mm256_load_si256((__m256i *)(dT + w16));
                    const __m256i srcMask = _mm256_and_si256(src, mask);
                    const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                    stats_top_win3_avx2(
                        srcMask, dgdMask, dT + j + w16, d_stride, sumM, sumH);
                }

                sT += s_stride;
                dT += d_stride;
            } while (--y);

            const __m256i sM =
                hadd_four_32_to_64_avx2(sumM[0], sumM[1], sumM[2], sumM[2]);
            _mm_storeu_si128((__m128i *)(M + wiener_win * j),
                             _mm256_extracti128_si256(sM, 0));
            _mm_storel_epi64((__m128i *)&M[wiener_win * j + 2],
                             _mm256_extracti128_si256(sM, 1));

            const __m256i sH =
                hadd_four_32_to_64_avx2(sumH[0], sumH[1], sumH[2], sumH[2]);
            // Writing one more H on the top edge falls to the second row, so it
            // won't overflow.
            _mm256_storeu_si256((__m256i *)(H + wiener_win * j), sH);
        } while (++j < wiener_win);

        // Step 2: Calculate the left edge of each square on the top row.
        j = 1;
        do {
            const int16_t *dT = d;
            __m256i sumH[WIENER_WIN_3TAP - 1] = {0};

            y = height;
            do {
                x = 0;
                do {
                    const __m256i dgd =
                        _mm256_loadu_si256((__m256i *)(dT + j + x));
                    stats_left_win3_avx2(dgd, dT + x, d_stride, sumH);
                    x += 16;
                } while (x < w16);

                if (w16 != width) {
                    const __m256i dgd =
                        _mm256_loadu_si256((__m256i *)(dT + j + x));
                    const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                    stats_left_win3_avx2(dgdMask, dT + x, d_stride, sumH);
                }

                dT += d_stride;
            } while (--y);

            const __m128i sum = hadd_two_32_to_64_avx2(sumH[0], sumH[1]);
            _mm_storel_epi64((__m128i *)&H[1 * wiener_win2 + j * wiener_win],
                             sum);
            _mm_storeh_epi64((__m128i *)&H[2 * wiener_win2 + j * wiener_win],
                             sum);
        } while (++j < wiener_win);
    } else {
        const int32_t numBitLeft =
            32 - 1 /* sign */ - 2 * bit_depth /* energy */ + 3 /* SIMD */;
        const int32_t hAllowed =
            (1 << numBitLeft) / (w16 + ((w16 != width) ? 16 : 0));

        // Step 1: Calculate the top edge of the whole matrix, i.e., the top
        // edge of each triangle and square on the top row.
        j = 0;
        do {
            const int16_t *sT = s;
            const int16_t *dT = d;
            int32_t heightT = 0;
            __m256i sumM[WIENER_WIN_3TAP] = {0};
            __m256i sumH[WIENER_WIN_3TAP] = {0};

            do {
                const int32_t hT = ((height - heightT) < hAllowed)
                                       ? (height - heightT)
                                       : hAllowed;
                __m256i rowM[WIENER_WIN_3TAP] = {0};
                __m256i rowH[WIENER_WIN_3TAP] = {0};

                y = hT;
                do {
                    x = 0;
                    do {
                        const __m256i src =
                            _mm256_load_si256((__m256i *)(sT + x));
                        const __m256i dgd =
                            _mm256_load_si256((__m256i *)(dT + x));
                        stats_top_win3_avx2(
                            src, dgd, dT + j + x, d_stride, rowM, rowH);
                        x += 16;
                    } while (x < w16);

                    if (w16 != width) {
                        const __m256i src =
                            _mm256_load_si256((__m256i *)(sT + w16));
                        const __m256i dgd =
                            _mm256_load_si256((__m256i *)(dT + w16));
                        const __m256i srcMask = _mm256_and_si256(src, mask);
                        const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                        stats_top_win3_avx2(srcMask,
                                            dgdMask,
                                            dT + j + w16,
                                            d_stride,
                                            rowM,
                                            rowH);
                    }

                    sT += s_stride;
                    dT += d_stride;
                } while (--y);

                add_32_to_64_avx2(rowM[0], &sumM[0]);
                add_32_to_64_avx2(rowM[1], &sumM[1]);
                add_32_to_64_avx2(rowM[2], &sumM[2]);
                add_32_to_64_avx2(rowH[0], &sumH[0]);
                add_32_to_64_avx2(rowH[1], &sumH[1]);
                add_32_to_64_avx2(rowH[2], &sumH[2]);

                heightT += hT;
            } while (heightT < height);

            const __m256i sM =
                hadd_four_64_avx2(sumM[0], sumM[1], sumM[2], sumM[2]);
            _mm_storeu_si128((__m128i *)(M + wiener_win * j),
                             _mm256_extracti128_si256(sM, 0));
            _mm_storel_epi64((__m128i *)&M[wiener_win * j + 2],
                             _mm256_extracti128_si256(sM, 1));

            const __m256i sH =
                hadd_four_64_avx2(sumH[0], sumH[1], sumH[2], sumH[2]);
            // Writing one more H on the top edge falls to the second row, so it
            // won't overflow.
            _mm256_storeu_si256((__m256i *)(H + wiener_win * j), sH);
        } while (++j < wiener_win);

        // Step 2: Calculate the left edge of each square on the top row.
        j = 1;
        do {
            const int16_t *dT = d;
            int32_t heightT = 0;
            __m256i sumH[WIENER_WIN_3TAP - 1] = {0};

            do {
                const int32_t hT = ((height - heightT) < hAllowed)
                                       ? (height - heightT)
                                       : hAllowed;
                __m256i rowH[WIENER_WIN_3TAP - 1] = {0};

                y = hT;
                do {
                    x = 0;
                    do {
                        const __m256i dgd =
                            _mm256_loadu_si256((__m256i *)(dT + j + x));
                        stats_left_win3_avx2(dgd, dT + x, d_stride, rowH);
                        x += 16;
                    } while (x < w16);

                    if (w16 != width) {
                        const __m256i dgd =
                            _mm256_loadu_si256((__m256i *)(dT + j + x));
                        const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                        stats_left_win3_avx2(dgdMask, dT + x, d_stride, rowH);
                    }

                    dT += d_stride;
                } while (--y);

                add_32_to_64_avx2(rowH[0], &sumH[0]);
                add_32_to_64_avx2(rowH[1], &sumH[1]);

                heightT += hT;
            } while (heightT < height);

            const __m128i sum = hadd_two_64_avx2(sumH[0], sumH[1]);
            _mm_storel_epi64((__m128i *)&H[1 * wiener_win2 + j * wiener_win],
                             sum);
            _mm_storeh_epi64((__m128i *)&H[2 * wiener_win2 + j * wiener_win],
                             sum);
        } while (++j < wiener_win);
    }

    // Step 3: Derive the top edge of each triangle along the diagonal. No
    // triangle in top row.
    {
        const int16_t *dT = d;
        __m256i dd = _mm256_setzero_si256();  // Initialize to avoid warning.
        __m256i deltas[4] = {0};
        __m256i delta;

        dd = _mm256_insert_epi32(dd, *(int32_t *)(dT + 0 * d_stride), 0);
        dd =
            _mm256_insert_epi32(dd, *(int32_t *)(dT + 0 * d_stride + width), 4);
        dd = _mm256_insert_epi32(dd, *(int32_t *)(dT + 1 * d_stride), 1);
        dd =
            _mm256_insert_epi32(dd, *(int32_t *)(dT + 1 * d_stride + width), 5);

        if (bit_depth < AOM_BITS_12) {
            step3_win3_avx2(&dT, d_stride, width, h4, &dd, deltas);

            // 00 00 10 10  00 00 10 10
            // 01 01 11 11  01 01 11 11
            // 02 02 12 12  02 02 12 12
            deltas[0] = _mm256_hadd_epi32(
                deltas[0], deltas[1]);  // 00 10 01 11  00 10 01 11
            deltas[2] = _mm256_hadd_epi32(
                deltas[2], deltas[2]);  // 02 12 02 12  02 12 02 12
            const __m128i delta0 = sub_hi_lo_32_avx2(deltas[0]);  // 00 10 01 11
            const __m128i delta1 = sub_hi_lo_32_avx2(deltas[2]);  // 02 12 02 12
            delta = _mm256_inserti128_si256(_mm256_castsi128_si256(delta0),
                                            delta1,
                                            1);  // 00 10 01 11  02 12 02 12
        } else {
            int32_t h4T = 0;

            do {
                __m256i deltasT[WIENER_WIN_3TAP] = {0};

                const int32_t hT = ((h4 - h4T) < 256) ? (h4 - h4T) : 256;

                step3_win3_avx2(&dT, d_stride, width, hT, &dd, deltasT);

                deltasT[0] = hsub_32x8_to_64x4_avx2(deltasT[0]);  // 00 00 10 10
                deltasT[1] = hsub_32x8_to_64x4_avx2(deltasT[1]);  // 01 01 11 11
                deltasT[2] = hsub_32x8_to_64x4_avx2(deltasT[2]);  // 02 02 12 12
                deltasT[0] =
                    hadd_x_64_avx2(deltasT[0], deltasT[1]);  // 00 10 01 11
                deltasT[2] =
                    hadd_x_64_avx2(deltasT[2], deltasT[2]);  // 02 12 02 12
                deltas[0] = _mm256_add_epi64(deltas[0], deltasT[0]);
                deltas[1] = _mm256_add_epi64(deltas[1], deltasT[2]);

                h4T += hT;
            } while (h4T < h4);

            delta = _mm256_setzero_si256();
        }

        if (h4 != height) {
            // 16-bit idx: 0, 2, 1, 3, 0, 2, 1, 3
            const __m128i shf0 =
                _mm_setr_epi8(0, 1, 4, 5, 2, 3, 6, 7, 0, 1, 4, 5, 2, 3, 6, 7);
            // 16-bit idx: 0, 2, 1, 3, 4, 6, 5, 7, 0, 2, 1, 3, 4, 6, 5, 7
            const __m256i shf1 = _mm256_setr_epi8(0,
                                                  1,
                                                  4,
                                                  5,
                                                  2,
                                                  3,
                                                  6,
                                                  7,
                                                  8,
                                                  9,
                                                  12,
                                                  13,
                                                  10,
                                                  11,
                                                  14,
                                                  15,
                                                  0,
                                                  1,
                                                  4,
                                                  5,
                                                  2,
                                                  3,
                                                  6,
                                                  7,
                                                  8,
                                                  9,
                                                  12,
                                                  13,
                                                  10,
                                                  11,
                                                  14,
                                                  15);

            dd = _mm256_insert_epi32(dd, *(int32_t *)(dT + 0 * d_stride), 0);
            dd = _mm256_insert_epi32(
                dd, *(int32_t *)(dT + 0 * d_stride + width), 1);
            dd = _mm256_insert_epi32(dd, *(int32_t *)(dT + 1 * d_stride), 2);
            dd = _mm256_insert_epi32(
                dd, *(int32_t *)(dT + 1 * d_stride + width), 3);

            y = height - h4;
            do {
                __m128i t0;

                // -00s -01s 00e 01e
                t0 = _mm_cvtsi32_si128(*(int32_t *)dT);
                t0 = _mm_sub_epi16(_mm_setzero_si128(), t0);
                t0 = _mm_insert_epi32(t0, *(int32_t *)(dT + width), 1);
                t0 = _mm_shuffle_epi8(t0, shf0);
                // -00s 00e -01s 01e -00s 00e -01s 01e  -00s 00e -01s 01e -00s
                // 00e -01s 01e
                const __m256i t =
                    _mm256_inserti128_si256(_mm256_castsi128_si256(t0), t0, 1);

                // 00s 01s 00e 01e 10s 11s 10e 11e  20s 21s 20e 21e xx xx xx xx
                dd =
                    _mm256_insert_epi32(dd, *(int32_t *)(dT + 2 * d_stride), 4);
                dd = _mm256_insert_epi32(
                    dd, *(int32_t *)(dT + 2 * d_stride + width), 5);
                // 00s 00e 01s 01e 10s 10e 11s 11e  20s 20e 21e 21s xx xx xx xx
                const __m256i ddT = _mm256_shuffle_epi8(dd, shf1);
                madd_avx2(t, ddT, &delta);

                dd = _mm256_permute4x64_epi64(dd, 0x39);  // right shift 8 bytes
                dT += d_stride;
            } while (--y);
        }

        // Writing one more H on the top edge of a triangle along the diagonal
        // falls to the next triangle in the same row, which would be calculated
        // later, so it won't overflow.
        if (bit_depth < AOM_BITS_12) {
            // 00 01 02 02  10 11 12 12
            delta = _mm256_permutevar8x32_epi32(
                delta, _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7));

            update_4_stats_avx2(
                H + 0 * wiener_win * wiener_win2 + 0 * wiener_win,
                _mm256_extracti128_si256(delta, 0),
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win);
            update_4_stats_avx2(
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win,
                _mm256_extracti128_si256(delta, 1),
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win);
        } else {
            const __m256i d0 =
                _mm256_cvtepi32_epi64(_mm256_extracti128_si256(delta, 0));
            const __m256i d1 =
                _mm256_cvtepi32_epi64(_mm256_extracti128_si256(delta, 1));
            deltas[0] = _mm256_add_epi64(deltas[0], d0);
            deltas[1] = _mm256_add_epi64(deltas[1], d1);

            deltas[2] =
                _mm256_unpacklo_epi64(deltas[0], deltas[1]);  // 00 02 01 02
            deltas[3] =
                _mm256_unpackhi_epi64(deltas[0], deltas[1]);  // 10 12 11 12

            deltas[2] =
                _mm256_permute4x64_epi64(deltas[2], 0xD8);  // 00 01 02 02
            deltas[3] =
                _mm256_permute4x64_epi64(deltas[3], 0xD8);  // 10 11 12 12

            update_4_stats_highbd_avx2(
                H + 0 * wiener_win * wiener_win2 + 0 * wiener_win,
                deltas[2],
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win);
            update_4_stats_highbd_avx2(
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win,
                deltas[3],
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win);
        }
    }

    // Step 4: Derive the top and left edge of each square. No square in top and
    // bottom row.
    {
        const int16_t *dT = d;
        __m256i deltas[2 * WIENER_WIN_3TAP - 1] = {0};
        __m256i dd[WIENER_WIN_3TAP], ds[WIENER_WIN_3TAP];
        __m256i se0, se1, xx, yy;
        __m256i delta;
        se0 = _mm256_setzero_si256();  // Initialize to avoid warning.

        y = 0;
        do {
            // 00s 01s 10s 11s 20s 21s 30s 31s  00e 01e 10e 11e 20e 21e 30e 31e
            se0 = _mm256_insert_epi32(se0, *(int32_t *)(dT + 0 * d_stride), 0);
            se0 = _mm256_insert_epi32(
                se0, *(int32_t *)(dT + 0 * d_stride + width), 4);
            se0 = _mm256_insert_epi32(se0, *(int32_t *)(dT + 1 * d_stride), 1);
            se0 = _mm256_insert_epi32(
                se0, *(int32_t *)(dT + 1 * d_stride + width), 5);
            se0 = _mm256_insert_epi32(se0, *(int32_t *)(dT + 2 * d_stride), 2);
            se0 = _mm256_insert_epi32(
                se0, *(int32_t *)(dT + 2 * d_stride + width), 6);
            se0 = _mm256_insert_epi32(se0, *(int32_t *)(dT + 3 * d_stride), 3);
            se0 = _mm256_insert_epi32(
                se0, *(int32_t *)(dT + 3 * d_stride + width), 7);

            // 40s 41s 50s 51s 60s 61s 70s 71s  40e 41e 50e 51e 60e 61e 70e 71e
            se1 = _mm256_insert_epi32(se0, *(int32_t *)(dT + 4 * d_stride), 0);
            se1 = _mm256_insert_epi32(
                se1, *(int32_t *)(dT + 4 * d_stride + width), 4);
            se1 = _mm256_insert_epi32(se1, *(int32_t *)(dT + 5 * d_stride), 1);
            se1 = _mm256_insert_epi32(
                se1, *(int32_t *)(dT + 5 * d_stride + width), 5);
            se1 = _mm256_insert_epi32(se1, *(int32_t *)(dT + 6 * d_stride), 2);
            se1 = _mm256_insert_epi32(
                se1, *(int32_t *)(dT + 6 * d_stride + width), 6);
            se1 = _mm256_insert_epi32(se1, *(int32_t *)(dT + 7 * d_stride), 3);
            se1 = _mm256_insert_epi32(
                se1, *(int32_t *)(dT + 7 * d_stride + width), 7);

            // 00s 10s 20s 30s 40s 50s 60s 70s  00e 10e 20e 30e 40e 50e 60e 70e
            xx = _mm256_slli_epi32(se0, 16);
            yy = _mm256_slli_epi32(se1, 16);
            xx = _mm256_srai_epi32(xx, 16);
            yy = _mm256_srai_epi32(yy, 16);
            dd[0] = _mm256_packs_epi32(xx, yy);

            // 01s 11s 21s 31s 41s 51s 61s 71s  01e 11e 21e 31e 41e 51e 61e 71e
            se0 = _mm256_srai_epi32(se0, 16);
            se1 = _mm256_srai_epi32(se1, 16);
            ds[0] = _mm256_packs_epi32(se0, se1);

            load_more_16_avx2(dT + 8 * d_stride + 0, width, dd[0], &dd[1]);
            load_more_16_avx2(dT + 8 * d_stride + 1, width, ds[0], &ds[1]);
            load_more_16_avx2(dT + 9 * d_stride + 0, width, dd[1], &dd[2]);
            load_more_16_avx2(dT + 9 * d_stride + 1, width, ds[1], &ds[2]);

            madd_avx2(dd[0], ds[0], &deltas[0]);
            madd_avx2(dd[0], ds[1], &deltas[1]);
            madd_avx2(dd[0], ds[2], &deltas[2]);
            madd_avx2(dd[1], ds[0], &deltas[3]);
            madd_avx2(dd[2], ds[0], &deltas[4]);

            dT += 8 * d_stride;
            y += 8;
        } while (y < h8);

        if (bit_depth < AOM_BITS_12) {
            deltas[0] = _mm256_hadd_epi32(
                deltas[0], deltas[1]);  // T0 T0 T1 T1  T0 T0 T1 T1
            deltas[2] = _mm256_hadd_epi32(
                deltas[2], deltas[2]);  // T2 T2 T2 T2  T2 T2 T2 T2
            deltas[3] = _mm256_hadd_epi32(
                deltas[3], deltas[4]);  // L0 L0 L1 L1  L0 L0 L1 L1
            deltas[0] = _mm256_hadd_epi32(
                deltas[0], deltas[2]);  // T0 T1 T2 T2  T0 T1 T2 T2
            deltas[3] = _mm256_hadd_epi32(
                deltas[3], deltas[3]);  // L0 L1 L0 L1  L0 L1 L0 L1
            const __m128i delta0 = sub_hi_lo_32_avx2(deltas[0]);  // T0 T1 T2 T2
            const __m128i delta1 = sub_hi_lo_32_avx2(deltas[3]);  // L0 L1 L0 L1
            delta = _mm256_inserti128_si256(
                _mm256_castsi128_si256(delta0), delta1, 1);
        } else {
            deltas[0] = hsub_32x8_to_64x4_avx2(deltas[0]);     // T0 T0 T0 T0
            deltas[1] = hsub_32x8_to_64x4_avx2(deltas[1]);     // T1 T1 T1 T1
            deltas[2] = hsub_32x8_to_64x4_avx2(deltas[2]);     // T2 T2 T2 T2
            deltas[3] = hsub_32x8_to_64x4_avx2(deltas[3]);     // L0 L0 L0 L0
            deltas[4] = hsub_32x8_to_64x4_avx2(deltas[4]);     // L1 L1 L1 L1
            deltas[0] = hadd_x_64_avx2(deltas[0], deltas[1]);  // T0 T0 T1 T1
            deltas[2] = hadd_x_64_avx2(deltas[2], deltas[2]);  // T2 T2 T2 T2
            deltas[3] = hadd_x_64_avx2(deltas[3], deltas[4]);  // L0 L0 L1 L1
            deltas[0] = hadd_x_64_avx2(deltas[0], deltas[2]);  // T0 T1 T2 T2
            deltas[1] = hadd_x_64_avx2(deltas[3], deltas[3]);  // L0 L1 L0 L1
            delta = _mm256_setzero_si256();
        }

        if (h8 != height) {
            const __m256i perm = _mm256_setr_epi32(1, 2, 3, 4, 5, 6, 7, 0);

            ds[0] = _mm256_insert_epi16(ds[0], dT[0 * d_stride + 1], 0);
            ds[0] = _mm256_insert_epi16(ds[0], dT[0 * d_stride + 1 + width], 1);

            dd[0] = _mm256_insert_epi16(dd[0], -dT[1 * d_stride], 8);
            ds[0] = _mm256_insert_epi16(ds[0], dT[1 * d_stride + 1], 2);
            dd[0] = _mm256_insert_epi16(dd[0], dT[1 * d_stride + width], 9);
            ds[0] = _mm256_insert_epi16(ds[0], dT[1 * d_stride + 1 + width], 3);

            do {
                dd[0] = _mm256_insert_epi16(dd[0], -dT[0 * d_stride], 0);
                dd[0] = _mm256_insert_epi16(dd[0], dT[0 * d_stride + width], 1);
                dd[0] = _mm256_unpacklo_epi32(dd[0], dd[0]);
                dd[0] = _mm256_unpacklo_epi32(dd[0], dd[0]);

                ds[0] = _mm256_insert_epi16(ds[0], dT[0 * d_stride + 1], 8);
                ds[0] = _mm256_insert_epi16(ds[0], dT[0 * d_stride + 1], 10);
                ds[0] =
                    _mm256_insert_epi16(ds[0], dT[0 * d_stride + 1 + width], 9);
                ds[0] = _mm256_insert_epi16(
                    ds[0], dT[0 * d_stride + 1 + width], 11);

                dd[0] = _mm256_insert_epi16(dd[0], -dT[2 * d_stride], 10);
                ds[0] = _mm256_insert_epi16(ds[0], dT[2 * d_stride + 1], 4);
                dd[0] =
                    _mm256_insert_epi16(dd[0], dT[2 * d_stride + width], 11);
                ds[0] =
                    _mm256_insert_epi16(ds[0], dT[2 * d_stride + 1 + width], 5);

                madd_avx2(dd[0], ds[0], &delta);

                // right shift 4 bytes
                dd[0] = _mm256_permutevar8x32_epi32(dd[0], perm);
                ds[0] = _mm256_permutevar8x32_epi32(ds[0], perm);
                dT += d_stride;
            } while (++y < height);
        }

        // Writing one more H on the top edge of a square falls to the next
        // square in the same row or the first H in the next row, which would be
        // calculated later, so it won't overflow.
        if (bit_depth < AOM_BITS_12) {
            update_4_stats_avx2(
                H + 0 * wiener_win * wiener_win2 + 1 * wiener_win,
                _mm256_extracti128_si256(delta, 0),
                H + 1 * wiener_win * wiener_win2 + 2 * wiener_win);
            H[(1 * wiener_win + 1) * wiener_win2 + 2 * wiener_win] =
                H[(0 * wiener_win + 1) * wiener_win2 + 1 * wiener_win] +
                _mm256_extract_epi32(delta, 4);
            H[(1 * wiener_win + 2) * wiener_win2 + 2 * wiener_win] =
                H[(0 * wiener_win + 2) * wiener_win2 + 1 * wiener_win] +
                _mm256_extract_epi32(delta, 5);
        } else {
            const __m256i d0 =
                _mm256_cvtepi32_epi64(_mm256_extracti128_si256(delta, 0));
            const __m256i d1 =
                _mm256_cvtepi32_epi64(_mm256_extracti128_si256(delta, 1));
            deltas[0] = _mm256_add_epi64(deltas[0], d0);
            deltas[1] = _mm256_add_epi64(deltas[1], d1);

            update_4_stats_highbd_avx2(
                H + 0 * wiener_win * wiener_win2 + 1 * wiener_win,
                deltas[0],
                H + 1 * wiener_win * wiener_win2 + 2 * wiener_win);
            H[(1 * wiener_win + 1) * wiener_win2 + 2 * wiener_win] =
                H[(0 * wiener_win + 1) * wiener_win2 + 1 * wiener_win] +
                _mm256_extract_epi64(deltas[1], 0);
            H[(1 * wiener_win + 2) * wiener_win2 + 2 * wiener_win] =
                H[(0 * wiener_win + 2) * wiener_win2 + 1 * wiener_win] +
                _mm256_extract_epi64(deltas[1], 1);
        }
    }

    // Step 5: Derive other points of each square. No square in bottom row.
    i = 0;
    do {
        const int16_t *const dI = d + i;

        j = i + 1;
        do {
            const int16_t *const dJ = d + j;
            __m256i deltas[WIENER_WIN_3TAP - 1][WIENER_WIN_3TAP - 1] = {{{0}},{{0}}};
            __m256i dIs[WIENER_WIN_3TAP - 1], dIe[WIENER_WIN_3TAP - 1];
            __m256i dJs[WIENER_WIN_3TAP - 1], dJe[WIENER_WIN_3TAP - 1];

            x = 0;
            do {
                load_square_win3_avx2(
                    dI + x, dJ + x, d_stride, height, dIs, dIe, dJs, dJe);
                derive_square_win3_avx2(dIs, dIe, dJs, dJe, deltas);

                x += 16;
            } while (x < w16);

            if (w16 != width) {
                load_square_win3_avx2(
                    dI + x, dJ + x, d_stride, height, dIs, dIe, dJs, dJe);

                dIs[0] = _mm256_and_si256(dIs[0], mask);
                dIs[1] = _mm256_and_si256(dIs[1], mask);
                dIe[0] = _mm256_and_si256(dIe[0], mask);
                dIe[1] = _mm256_and_si256(dIe[1], mask);

                derive_square_win3_avx2(dIs, dIe, dJs, dJe, deltas);
            }

            __m256i delta64;
            if (bit_depth < AOM_BITS_12) {
                const __m128i delta32 = hadd_four_32_avx2(
                    deltas[0][0], deltas[0][1], deltas[1][0], deltas[1][1]);
                delta64 = _mm256_cvtepi32_epi64(delta32);
            } else {
                delta64 = hadd_four_31_to_64_avx2(
                    deltas[0][0], deltas[0][1], deltas[1][0], deltas[1][1]);
            }
            update_2_stats_sse2(
                H + (i * wiener_win + 0) * wiener_win2 + j * wiener_win,
                _mm256_extracti128_si256(delta64, 0),
                H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win + 1);
            update_2_stats_sse2(
                H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win,
                _mm256_extracti128_si256(delta64, 1),
                H + (i * wiener_win + 2) * wiener_win2 + j * wiener_win + 1);
        } while (++j < wiener_win);
    } while (++i < wiener_win - 1);

    // Step 6: Derive other points of each upper triangle along the diagonal.
    i = 0;
    do {
        const int16_t *const dI = d + i;
        __m256i deltas[WIENER_WIN_3TAP * (WIENER_WIN_3TAP - 1) / 2] = {0};
        __m256i dIs[WIENER_WIN_3TAP - 1], dIe[WIENER_WIN_3TAP - 1];

        x = 0;
        do {
            load_triangle_win3_avx2(dI + x, d_stride, height, dIs, dIe);
            derive_triangle_win3_avx2(dIs, dIe, deltas);

            x += 16;
        } while (x < w16);

        if (w16 != width) {
            load_triangle_win3_avx2(dI + x, d_stride, height, dIs, dIe);

            dIs[0] = _mm256_and_si256(dIs[0], mask);
            dIs[1] = _mm256_and_si256(dIs[1], mask);
            dIe[0] = _mm256_and_si256(dIe[0], mask);
            dIe[1] = _mm256_and_si256(dIe[1], mask);

            derive_triangle_win3_avx2(dIs, dIe, deltas);
        }

        __m128i delta01;
        int64_t delta2;

        if (bit_depth < AOM_BITS_12) {
            const __m128i delta32 =
                hadd_four_32_avx2(deltas[0], deltas[1], deltas[2], deltas[2]);
            delta01 = _mm_cvtepi32_epi64(delta32);
            delta2 = _mm_extract_epi32(delta32, 2);
        } else {
            const __m256i delta64 = hadd_four_31_to_64_avx2(
                deltas[0], deltas[1], deltas[2], deltas[2]);
            delta01 = _mm256_extracti128_si256(delta64, 0);
            delta2 = _mm256_extract_epi64(delta64, 2);
        }

        update_2_stats_sse2(
            H + (i * wiener_win + 0) * wiener_win2 + i * wiener_win,
            delta01,
            H + (i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1);
        H[(i * wiener_win + 2) * wiener_win2 + i * wiener_win + 2] =
            H[(i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1] + delta2;
    } while (++i < wiener_win);
}

static INLINE void compute_stats_win5_avx2(
    const int16_t *const d, const int32_t d_stride, const int16_t *const s,
    const int32_t s_stride, const int32_t width, const int32_t height,
    int64_t *const M, int64_t *const H, AomBitDepth bit_depth) {
    const int32_t wiener_win = WIENER_WIN_CHROMA;
    const int32_t wiener_win2 = wiener_win * wiener_win;
    const int32_t w16 = width & ~15;
    const int32_t h8 = height & ~7;
    const __m256i mask =
        _mm256_load_si256((__m256i *)(mask_16bit[width - w16]));
    int32_t i, j, x, y;

    if (bit_depth == AOM_BITS_8) {
        // Step 1: Calculate the top edge of the whole matrix, i.e., the top
        // edge of each triangle and square on the top row.
        j = 0;
        do {
            const int16_t *sT = s;
            const int16_t *dT = d;
            __m256i sumM[WIENER_WIN_CHROMA] = {0};
            __m256i sumH[WIENER_WIN_CHROMA] = {0};

            y = height;
            do {
                x = 0;
                do {
                    const __m256i src = _mm256_load_si256((__m256i *)(sT + x));
                    const __m256i dgd = _mm256_load_si256((__m256i *)(dT + x));
                    stats_top_win5_avx2(
                        src, dgd, dT + j + x, d_stride, sumM, sumH);
                    x += 16;
                } while (x < w16);

                if (w16 != width) {
                    const __m256i src =
                        _mm256_load_si256((__m256i *)(sT + w16));
                    const __m256i dgd =
                        _mm256_load_si256((__m256i *)(dT + w16));
                    const __m256i srcMask = _mm256_and_si256(src, mask);
                    const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                    stats_top_win5_avx2(
                        srcMask, dgdMask, dT + j + w16, d_stride, sumM, sumH);
                }

                sT += s_stride;
                dT += d_stride;
            } while (--y);

            const __m256i sM =
                hadd_four_32_to_64_avx2(sumM[0], sumM[1], sumM[2], sumM[3]);
            const __m128i sMH = hadd_two_32_to_64_avx2(sumM[4], sumH[4]);
            _mm256_storeu_si256((__m256i *)(M + wiener_win * j), sM);
            _mm_storel_epi64((__m128i *)&M[wiener_win * j + 4], sMH);

            const __m256i sH =
                hadd_four_32_to_64_avx2(sumH[0], sumH[1], sumH[2], sumH[3]);
            _mm256_storeu_si256((__m256i *)(H + wiener_win * j), sH);
            _mm_storeh_epi64((__m128i *)&H[wiener_win * j + 4], sMH);
        } while (++j < wiener_win);

        // Step 2: Calculate the left edge of each square on the top row.
        j = 1;
        do {
            const int16_t *dT = d;
            __m256i sumH[WIENER_WIN_CHROMA - 1] = {0};

            y = height;
            do {
                x = 0;
                do {
                    const __m256i dgd =
                        _mm256_loadu_si256((__m256i *)(dT + j + x));
                    stats_left_win5_avx2(dgd, dT + x, d_stride, sumH);
                    x += 16;
                } while (x < w16);

                if (w16 != width) {
                    const __m256i dgd =
                        _mm256_loadu_si256((__m256i *)(dT + j + x));
                    const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                    stats_left_win5_avx2(dgdMask, dT + x, d_stride, sumH);
                }

                dT += d_stride;
            } while (--y);

            const __m256i sum =
                hadd_four_32_to_64_avx2(sumH[0], sumH[1], sumH[2], sumH[3]);
            _mm_storel_epi64((__m128i *)&H[1 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum, 0));
            _mm_storeh_epi64((__m128i *)&H[2 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum, 0));
            _mm_storel_epi64((__m128i *)&H[3 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum, 1));
            _mm_storeh_epi64((__m128i *)&H[4 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum, 1));
        } while (++j < wiener_win);
    } else {
        const int32_t numBitLeft =
            32 - 1 /* sign */ - 2 * bit_depth /* energy */ + 3 /* SIMD */;
        const int32_t hAllowed =
            (1 << numBitLeft) / (w16 + ((w16 != width) ? 16 : 0));

        // Step 1: Calculate the top edge of the whole matrix, i.e., the top
        // edge of each triangle and square on the top row.
        j = 0;
        do {
            const int16_t *sT = s;
            const int16_t *dT = d;
            int32_t heightT = 0;
            __m256i sumM[WIENER_WIN_CHROMA] = {0};
            __m256i sumH[WIENER_WIN_CHROMA] = {0};

            do {
                const int32_t hT = ((height - heightT) < hAllowed)
                                       ? (height - heightT)
                                       : hAllowed;
                __m256i rowM[WIENER_WIN_CHROMA] = {0};
                __m256i rowH[WIENER_WIN_CHROMA] = {0};

                y = hT;
                do {
                    x = 0;
                    do {
                        const __m256i src =
                            _mm256_load_si256((__m256i *)(sT + x));
                        const __m256i dgd =
                            _mm256_load_si256((__m256i *)(dT + x));
                        stats_top_win5_avx2(
                            src, dgd, dT + j + x, d_stride, rowM, rowH);
                        x += 16;
                    } while (x < w16);

                    if (w16 != width) {
                        const __m256i src =
                            _mm256_load_si256((__m256i *)(sT + w16));
                        const __m256i dgd =
                            _mm256_load_si256((__m256i *)(dT + w16));
                        const __m256i srcMask = _mm256_and_si256(src, mask);
                        const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                        stats_top_win5_avx2(srcMask,
                                            dgdMask,
                                            dT + j + w16,
                                            d_stride,
                                            rowM,
                                            rowH);
                    }

                    sT += s_stride;
                    dT += d_stride;
                } while (--y);

                add_32_to_64_avx2(rowM[0], &sumM[0]);
                add_32_to_64_avx2(rowM[1], &sumM[1]);
                add_32_to_64_avx2(rowM[2], &sumM[2]);
                add_32_to_64_avx2(rowM[3], &sumM[3]);
                add_32_to_64_avx2(rowM[4], &sumM[4]);
                add_32_to_64_avx2(rowH[0], &sumH[0]);
                add_32_to_64_avx2(rowH[1], &sumH[1]);
                add_32_to_64_avx2(rowH[2], &sumH[2]);
                add_32_to_64_avx2(rowH[3], &sumH[3]);
                add_32_to_64_avx2(rowH[4], &sumH[4]);

                heightT += hT;
            } while (heightT < height);

            const __m256i sM =
                hadd_four_64_avx2(sumM[0], sumM[1], sumM[2], sumM[3]);
            const __m128i sMH = hadd_two_64_avx2(sumM[4], sumH[4]);
            _mm256_storeu_si256((__m256i *)(M + wiener_win * j), sM);
            M[wiener_win * j + 4] = _mm_cvtsi128_si64(sMH);

            const __m256i sH =
                hadd_four_64_avx2(sumH[0], sumH[1], sumH[2], sumH[3]);
            _mm256_storeu_si256((__m256i *)(H + wiener_win * j), sH);
            _mm_storeh_epi64((__m128i *)&H[wiener_win * j + 4], sMH);
        } while (++j < wiener_win);

        // Step 2: Calculate the left edge of each square on the top row.
        j = 1;
        do {
            const int16_t *dT = d;
            int32_t heightT = 0;
            __m256i sumH[WIENER_WIN_CHROMA - 1] = {0};

            do {
                const int32_t hT = ((height - heightT) < hAllowed)
                                       ? (height - heightT)
                                       : hAllowed;
                __m256i rowH[WIENER_WIN_CHROMA - 1] = {0};

                y = hT;
                do {
                    x = 0;
                    do {
                        const __m256i dgd =
                            _mm256_loadu_si256((__m256i *)(dT + j + x));
                        stats_left_win5_avx2(dgd, dT + x, d_stride, rowH);
                        x += 16;
                    } while (x < w16);

                    if (w16 != width) {
                        const __m256i dgd =
                            _mm256_loadu_si256((__m256i *)(dT + j + x));
                        const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                        stats_left_win5_avx2(dgdMask, dT + x, d_stride, rowH);
                    }

                    dT += d_stride;
                } while (--y);

                add_32_to_64_avx2(rowH[0], &sumH[0]);
                add_32_to_64_avx2(rowH[1], &sumH[1]);
                add_32_to_64_avx2(rowH[2], &sumH[2]);
                add_32_to_64_avx2(rowH[3], &sumH[3]);

                heightT += hT;
            } while (heightT < height);

            const __m256i sum =
                hadd_four_64_avx2(sumH[0], sumH[1], sumH[2], sumH[3]);
            _mm_storel_epi64((__m128i *)&H[1 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum, 0));
            _mm_storeh_epi64((__m128i *)&H[2 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum, 0));
            _mm_storel_epi64((__m128i *)&H[3 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum, 1));
            _mm_storeh_epi64((__m128i *)&H[4 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum, 1));
        } while (++j < wiener_win);
    }

    // Step 3: Derive the top edge of each triangle along the diagonal. No
    // triangle in top row.
    {
        const int16_t *dT = d;
        // 16-bit idx: 0, 4, 1, 5, 2, 6, 3, 7
        const __m256i shf = _mm256_setr_epi8(0,
                                             1,
                                             8,
                                             9,
                                             2,
                                             3,
                                             10,
                                             11,
                                             4,
                                             5,
                                             12,
                                             13,
                                             6,
                                             7,
                                             14,
                                             15,
                                             0,
                                             1,
                                             8,
                                             9,
                                             2,
                                             3,
                                             10,
                                             11,
                                             4,
                                             5,
                                             12,
                                             13,
                                             6,
                                             7,
                                             14,
                                             15);
        __m256i deltas[WIENER_WIN_CHROMA] = {0};
        __m256i dd = _mm256_setzero_si256();  // Initialize to avoid warning.
        __m256i ds[WIENER_WIN_CHROMA];

        // 00s 01s 02s 03s 10s 11s 12s 13s  00e 01e 02e 03e 10e 11e 12e 13e
        dd = _mm256_insert_epi64(dd, *(int64_t *)(dT + 0 * d_stride), 0);
        dd =
            _mm256_insert_epi64(dd, *(int64_t *)(dT + 0 * d_stride + width), 2);
        dd = _mm256_insert_epi64(dd, *(int64_t *)(dT + 1 * d_stride), 1);
        dd =
            _mm256_insert_epi64(dd, *(int64_t *)(dT + 1 * d_stride + width), 3);
        // 00s 10s 01s 11s 02s 12s 03s 13s  00e 10e 01e 11e 02e 12e 03e 13e
        ds[0] = _mm256_shuffle_epi8(dd, shf);

        // 10s 11s 12s 13s 20s 21s 22s 23s  10e 11e 12e 13e 20e 21e 22e 23e
        load_more_64_avx2(dT + 2 * d_stride, width, &dd);
        // 10s 20s 11s 21s 12s 22s 13s 23s  10e 20e 11e 21e 12e 22e 13e 23e
        ds[1] = _mm256_shuffle_epi8(dd, shf);

        // 20s 21s 22s 23s 30s 31s 32s 33s  20e 21e 22e 23e 30e 31e 32e 33e
        load_more_64_avx2(dT + 3 * d_stride, width, &dd);
        // 20s 30s 21s 31s 22s 32s 23s 33s  20e 30e 21e 31e 22e 32e 23e 33e
        ds[2] = _mm256_shuffle_epi8(dd, shf);

        if (bit_depth < AOM_BITS_12) {
            __m128i dlts[WIENER_WIN_CHROMA];

            step3_win5_avx2(&dT, d_stride, width, height, &dd, ds, deltas);

            dlts[0] = sub_hi_lo_32_avx2(deltas[0]);
            dlts[1] = sub_hi_lo_32_avx2(deltas[1]);
            dlts[2] = sub_hi_lo_32_avx2(deltas[2]);
            dlts[3] = sub_hi_lo_32_avx2(deltas[3]);
            dlts[4] = sub_hi_lo_32_avx2(deltas[4]);

            transpose_32bit_4x4(dlts, dlts);
            deltas[4] = _mm256_cvtepi32_epi64(dlts[4]);

            update_5_stats_avx2(
                H + 0 * wiener_win * wiener_win2 + 0 * wiener_win,
                dlts[0],
                _mm256_extract_epi64(deltas[4], 0),
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win);

            update_5_stats_avx2(
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win,
                dlts[1],
                _mm256_extract_epi64(deltas[4], 1),
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win);

            update_5_stats_avx2(
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win,
                dlts[2],
                _mm256_extract_epi64(deltas[4], 2),
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win);

            update_5_stats_avx2(
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win,
                dlts[3],
                _mm256_extract_epi64(deltas[4], 3),
                H + 4 * wiener_win * wiener_win2 + 4 * wiener_win);
        } else {
            int32_t heightT = 0;

            do {
                __m256i deltasT[WIENER_WIN_CHROMA] = {0};
                const int32_t hT =
                    ((height - heightT) < 128) ? (height - heightT) : 128;

                step3_win5_avx2(&dT, d_stride, width, hT, &dd, ds, deltasT);

                deltasT[0] = hsub_32x8_to_64x4_avx2(deltasT[0]);
                deltasT[1] = hsub_32x8_to_64x4_avx2(deltasT[1]);
                deltasT[2] = hsub_32x8_to_64x4_avx2(deltasT[2]);
                deltasT[3] = hsub_32x8_to_64x4_avx2(deltasT[3]);
                deltasT[4] = hsub_32x8_to_64x4_avx2(deltasT[4]);
                deltas[0] = _mm256_add_epi64(deltas[0], deltasT[0]);
                deltas[1] = _mm256_add_epi64(deltas[1], deltasT[1]);
                deltas[2] = _mm256_add_epi64(deltas[2], deltasT[2]);
                deltas[3] = _mm256_add_epi64(deltas[3], deltasT[3]);
                deltas[4] = _mm256_add_epi64(deltas[4], deltasT[4]);

                heightT += hT;
            } while (heightT < height);

            transpose_64bit_4x4_avx2(deltas, deltas);

            update_5_stats_highbd_avx2(
                H + 0 * wiener_win * wiener_win2 + 0 * wiener_win,
                deltas[0],
                _mm256_extract_epi64(deltas[4], 0),
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win);

            update_5_stats_highbd_avx2(
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win,
                deltas[1],
                _mm256_extract_epi64(deltas[4], 1),
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win);

            update_5_stats_highbd_avx2(
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win,
                deltas[2],
                _mm256_extract_epi64(deltas[4], 2),
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win);

            update_5_stats_highbd_avx2(
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win,
                deltas[3],
                _mm256_extract_epi64(deltas[4], 3),
                H + 4 * wiener_win * wiener_win2 + 4 * wiener_win);
        }
    }

    // Step 4: Derive the top and left edge of each square. No square in top and
    // bottom row.
    i = 1;
    do {
        j = i + 1;
        do {
            const int16_t *dI = d + i - 1;
            const int16_t *dJ = d + j - 1;
            __m128i delta128, delta4;
            __m256i delta;
            __m256i deltas[2 * WIENER_WIN_CHROMA - 1] = {0};
            __m256i dd[WIENER_WIN_CHROMA], ds[WIENER_WIN_CHROMA];

            dd[0] = _mm256_setzero_si256();  // Initialize to avoid warning.
            ds[0] = _mm256_setzero_si256();  // Initialize to avoid warning.

            dd[0] = _mm256_insert_epi16(dd[0], dI[0 * d_stride], 0);
            dd[0] = _mm256_insert_epi16(dd[0], dI[0 * d_stride + width], 8);
            dd[0] = _mm256_insert_epi16(dd[0], dI[1 * d_stride], 1);
            dd[0] = _mm256_insert_epi16(dd[0], dI[1 * d_stride + width], 9);
            dd[0] = _mm256_insert_epi16(dd[0], dI[2 * d_stride], 2);
            dd[0] = _mm256_insert_epi16(dd[0], dI[2 * d_stride + width], 10);
            dd[0] = _mm256_insert_epi16(dd[0], dI[3 * d_stride], 3);
            dd[0] = _mm256_insert_epi16(dd[0], dI[3 * d_stride + width], 11);

            ds[0] = _mm256_insert_epi16(ds[0], dJ[0 * d_stride], 0);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[0 * d_stride + width], 8);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[1 * d_stride], 1);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[1 * d_stride + width], 9);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[2 * d_stride], 2);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[2 * d_stride + width], 10);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[3 * d_stride], 3);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[3 * d_stride + width], 11);

            y = 0;
            do {
                // 00s 10s 20s 30s 40s 50s 60s 70s  00e 10e 20e 30e 40e 50e 60e
                // 70e
                dd[0] = _mm256_insert_epi16(dd[0], dI[4 * d_stride], 4);
                dd[0] =
                    _mm256_insert_epi16(dd[0], dI[4 * d_stride + width], 12);
                dd[0] = _mm256_insert_epi16(dd[0], dI[5 * d_stride], 5);
                dd[0] =
                    _mm256_insert_epi16(dd[0], dI[5 * d_stride + width], 13);
                dd[0] = _mm256_insert_epi16(dd[0], dI[6 * d_stride], 6);
                dd[0] =
                    _mm256_insert_epi16(dd[0], dI[6 * d_stride + width], 14);
                dd[0] = _mm256_insert_epi16(dd[0], dI[7 * d_stride], 7);
                dd[0] =
                    _mm256_insert_epi16(dd[0], dI[7 * d_stride + width], 15);

                // 01s 11s 21s 31s 41s 51s 61s 71s  01e 11e 21e 31e 41e 51e 61e
                // 71e
                ds[0] = _mm256_insert_epi16(ds[0], dJ[4 * d_stride], 4);
                ds[0] =
                    _mm256_insert_epi16(ds[0], dJ[4 * d_stride + width], 12);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[5 * d_stride], 5);
                ds[0] =
                    _mm256_insert_epi16(ds[0], dJ[5 * d_stride + width], 13);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[6 * d_stride], 6);
                ds[0] =
                    _mm256_insert_epi16(ds[0], dJ[6 * d_stride + width], 14);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[7 * d_stride], 7);
                ds[0] =
                    _mm256_insert_epi16(ds[0], dJ[7 * d_stride + width], 15);

                load_more_16_avx2(dI + 8 * d_stride, width, dd[0], &dd[1]);
                load_more_16_avx2(dJ + 8 * d_stride, width, ds[0], &ds[1]);
                load_more_16_avx2(dI + 9 * d_stride, width, dd[1], &dd[2]);
                load_more_16_avx2(dJ + 9 * d_stride, width, ds[1], &ds[2]);
                load_more_16_avx2(dI + 10 * d_stride, width, dd[2], &dd[3]);
                load_more_16_avx2(dJ + 10 * d_stride, width, ds[2], &ds[3]);
                load_more_16_avx2(dI + 11 * d_stride, width, dd[3], &dd[4]);
                load_more_16_avx2(dJ + 11 * d_stride, width, ds[3], &ds[4]);

                madd_avx2(dd[0], ds[0], &deltas[0]);
                madd_avx2(dd[0], ds[1], &deltas[1]);
                madd_avx2(dd[0], ds[2], &deltas[2]);
                madd_avx2(dd[0], ds[3], &deltas[3]);
                madd_avx2(dd[0], ds[4], &deltas[4]);
                madd_avx2(dd[1], ds[0], &deltas[5]);
                madd_avx2(dd[2], ds[0], &deltas[6]);
                madd_avx2(dd[3], ds[0], &deltas[7]);
                madd_avx2(dd[4], ds[0], &deltas[8]);

                dd[0] = _mm256_srli_si256(dd[4], 8);
                ds[0] = _mm256_srli_si256(ds[4], 8);
                dI += 8 * d_stride;
                dJ += 8 * d_stride;
                y += 8;
            } while (y < h8);

            if (bit_depth < AOM_BITS_12) {
                deltas[0] = _mm256_hadd_epi32(deltas[0], deltas[1]);
                deltas[2] = _mm256_hadd_epi32(deltas[2], deltas[3]);
                deltas[4] = _mm256_hadd_epi32(deltas[4], deltas[4]);
                deltas[5] = _mm256_hadd_epi32(deltas[5], deltas[6]);
                deltas[7] = _mm256_hadd_epi32(deltas[7], deltas[8]);
                deltas[0] = _mm256_hadd_epi32(deltas[0], deltas[2]);
                deltas[4] = _mm256_hadd_epi32(deltas[4], deltas[4]);
                deltas[5] = _mm256_hadd_epi32(deltas[5], deltas[7]);
                const __m128i delta0 = sub_hi_lo_32_avx2(deltas[0]);
                const __m128i delta1 = sub_hi_lo_32_avx2(deltas[4]);
                delta128 = sub_hi_lo_32_avx2(deltas[5]);
                delta = _mm256_inserti128_si256(
                    _mm256_castsi128_si256(delta0), delta1, 1);
            } else {
                deltas[0] = hsub_32x8_to_64x4_avx2(deltas[0]);
                deltas[1] = hsub_32x8_to_64x4_avx2(deltas[1]);
                deltas[2] = hsub_32x8_to_64x4_avx2(deltas[2]);
                deltas[3] = hsub_32x8_to_64x4_avx2(deltas[3]);
                deltas[4] = hsub_32x8_to_64x4_avx2(deltas[4]);
                deltas[5] = hsub_32x8_to_64x4_avx2(deltas[5]);
                deltas[6] = hsub_32x8_to_64x4_avx2(deltas[6]);
                deltas[7] = hsub_32x8_to_64x4_avx2(deltas[7]);
                deltas[8] = hsub_32x8_to_64x4_avx2(deltas[8]);

                transpose_64bit_4x4_avx2(deltas + 0, deltas + 0);
                transpose_64bit_4x4_avx2(deltas + 5, deltas + 5);

                deltas[0] = _mm256_add_epi64(deltas[0], deltas[1]);
                deltas[2] = _mm256_add_epi64(deltas[2], deltas[3]);
                deltas[0] = _mm256_add_epi64(deltas[0], deltas[2]);
                deltas[5] = _mm256_add_epi64(deltas[5], deltas[6]);
                deltas[7] = _mm256_add_epi64(deltas[7], deltas[8]);
                deltas[5] = _mm256_add_epi64(deltas[5], deltas[7]);
                delta4 = hadd_64_avx2(deltas[4]);
                delta128 = _mm_setzero_si128();
                delta = _mm256_setzero_si256();
            }

            if (h8 != height) {
                const __m256i perm = _mm256_setr_epi32(1, 2, 3, 4, 5, 6, 7, 0);
                __m128i dd128, ds128;

                ds[0] = _mm256_insert_epi16(ds[0], dJ[0 * d_stride], 0);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[0 * d_stride + width], 1);

                dd128 = _mm_cvtsi32_si128(-dI[1 * d_stride]);
                dd128 = _mm_insert_epi16(dd128, dI[1 * d_stride + width], 1);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[1 * d_stride], 2);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[1 * d_stride + width], 3);

                dd128 = _mm_insert_epi16(dd128, -dI[2 * d_stride], 2);
                dd128 = _mm_insert_epi16(dd128, dI[2 * d_stride + width], 3);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[2 * d_stride], 4);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[2 * d_stride + width], 5);

                dd128 = _mm_insert_epi16(dd128, -dI[3 * d_stride], 4);
                dd128 = _mm_insert_epi16(dd128, dI[3 * d_stride + width], 5);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[3 * d_stride], 6);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[3 * d_stride + width], 7);

                do {
                    __m128i t;

                    t = _mm_cvtsi32_si128(-dI[0 * d_stride]);
                    t = _mm_insert_epi16(t, dI[0 * d_stride + width], 1);
                    dd[0] = _mm256_broadcastd_epi32(t);

                    ds128 = _mm_cvtsi32_si128(dJ[0 * d_stride]);
                    ds128 =
                        _mm_insert_epi16(ds128, dJ[0 * d_stride + width], 1);
                    ds128 = _mm_unpacklo_epi32(ds128, ds128);
                    ds128 = _mm_unpacklo_epi32(ds128, ds128);

                    dd128 = _mm_insert_epi16(dd128, -dI[4 * d_stride], 6);
                    dd128 =
                        _mm_insert_epi16(dd128, dI[4 * d_stride + width], 7);
                    ds[0] = _mm256_insert_epi16(ds[0], dJ[4 * d_stride], 8);
                    ds[0] =
                        _mm256_insert_epi16(ds[0], dJ[4 * d_stride + width], 9);

                    madd_avx2(dd[0], ds[0], &delta);
                    madd_sse2(dd128, ds128, &delta128);

                    // right shift 4 bytes
                    ds[0] = _mm256_permutevar8x32_epi32(ds[0], perm);
                    dd128 = _mm_srli_si128(dd128, 4);
                    dI += d_stride;
                    dJ += d_stride;
                } while (++y < height);
            }

            if (bit_depth < AOM_BITS_12) {
                update_4_stats_avx2(
                    H + (i - 1) * wiener_win * wiener_win2 +
                        (j - 1) * wiener_win,
                    _mm256_extracti128_si256(delta, 0),
                    H + i * wiener_win * wiener_win2 + j * wiener_win);
                H[i * wiener_win * wiener_win2 + j * wiener_win + 4] =
                    H[(i - 1) * wiener_win * wiener_win2 +
                      (j - 1) * wiener_win + 4] +
                    _mm256_extract_epi32(delta, 4);

                H[(i * wiener_win + 1) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 1) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm_extract_epi32(delta128, 0);
                H[(i * wiener_win + 2) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 2) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm_extract_epi32(delta128, 1);
                H[(i * wiener_win + 3) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 3) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm_extract_epi32(delta128, 2);
                H[(i * wiener_win + 4) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 4) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm_extract_epi32(delta128, 3);
            } else {
                const __m256i d0 =
                    _mm256_cvtepi32_epi64(_mm256_extracti128_si256(delta, 0));
                const __m128i d1 =
                    _mm_cvtepi32_epi64(_mm256_extracti128_si256(delta, 1));
                const __m256i d2 = _mm256_cvtepi32_epi64(delta128);
                deltas[0] = _mm256_add_epi64(deltas[0], d0);
                delta4 = _mm_add_epi64(delta4, d1);
                deltas[5] = _mm256_add_epi64(deltas[5], d2);

                update_4_stats_highbd_avx2(
                    H + (i - 1) * wiener_win * wiener_win2 +
                        (j - 1) * wiener_win,
                    deltas[0],
                    H + i * wiener_win * wiener_win2 + j * wiener_win);
                H[i * wiener_win * wiener_win2 + j * wiener_win + 4] =
                    H[(i - 1) * wiener_win * wiener_win2 +
                      (j - 1) * wiener_win + 4] +
                    _mm_extract_epi64(delta4, 0);

                H[(i * wiener_win + 1) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 1) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[5], 0);
                H[(i * wiener_win + 2) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 2) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[5], 1);
                H[(i * wiener_win + 3) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 3) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[5], 2);
                H[(i * wiener_win + 4) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 4) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[5], 3);
            }
        } while (++j < wiener_win);
    } while (++i < wiener_win - 1);

    // Step 5: Derive other points of each square. No square in bottom row.
    i = 0;
    do {
        const int16_t *const dI = d + i;

        j = i + 1;
        do {
            const int16_t *const dJ = d + j;
            __m256i deltas[WIENER_WIN_CHROMA - 1][WIENER_WIN_CHROMA - 1] = {{{0}},{{0}}};
            __m256i dIs[WIENER_WIN_CHROMA - 1], dIe[WIENER_WIN_CHROMA - 1];
            __m256i dJs[WIENER_WIN_CHROMA - 1], dJe[WIENER_WIN_CHROMA - 1];

            x = 0;
            do {
                load_square_win5_avx2(
                    dI + x, dJ + x, d_stride, height, dIs, dIe, dJs, dJe);
                derive_square_win5_avx2(dIs, dIe, dJs, dJe, deltas);

                x += 16;
            } while (x < w16);

            if (w16 != width) {
                load_square_win5_avx2(
                    dI + x, dJ + x, d_stride, height, dIs, dIe, dJs, dJe);

                dIs[0] = _mm256_and_si256(dIs[0], mask);
                dIs[1] = _mm256_and_si256(dIs[1], mask);
                dIs[2] = _mm256_and_si256(dIs[2], mask);
                dIs[3] = _mm256_and_si256(dIs[3], mask);
                dIe[0] = _mm256_and_si256(dIe[0], mask);
                dIe[1] = _mm256_and_si256(dIe[1], mask);
                dIe[2] = _mm256_and_si256(dIe[2], mask);
                dIe[3] = _mm256_and_si256(dIe[3], mask);

                derive_square_win5_avx2(dIs, dIe, dJs, dJe, deltas);
            }

            if (bit_depth < AOM_BITS_12) {
                hadd_update_4_stats_avx2(
                    H + (i * wiener_win + 0) * wiener_win2 + j * wiener_win,
                    deltas[0],
                    H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_4_stats_avx2(
                    H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win,
                    deltas[1],
                    H + (i * wiener_win + 2) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_4_stats_avx2(
                    H + (i * wiener_win + 2) * wiener_win2 + j * wiener_win,
                    deltas[2],
                    H + (i * wiener_win + 3) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_4_stats_avx2(
                    H + (i * wiener_win + 3) * wiener_win2 + j * wiener_win,
                    deltas[3],
                    H + (i * wiener_win + 4) * wiener_win2 + j * wiener_win +
                        1);
            } else {
                hadd_update_4_stats_highbd_avx2(
                    H + (i * wiener_win + 0) * wiener_win2 + j * wiener_win,
                    deltas[0],
                    H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_4_stats_highbd_avx2(
                    H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win,
                    deltas[1],
                    H + (i * wiener_win + 2) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_4_stats_highbd_avx2(
                    H + (i * wiener_win + 2) * wiener_win2 + j * wiener_win,
                    deltas[2],
                    H + (i * wiener_win + 3) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_4_stats_highbd_avx2(
                    H + (i * wiener_win + 3) * wiener_win2 + j * wiener_win,
                    deltas[3],
                    H + (i * wiener_win + 4) * wiener_win2 + j * wiener_win +
                        1);
            }
        } while (++j < wiener_win);
    } while (++i < wiener_win - 1);

    // Step 6: Derive other points of each upper triangle along the diagonal.
    i = 0;
    do {
        const int16_t *const dI = d + i;
        __m256i deltas[WIENER_WIN_CHROMA * (WIENER_WIN_CHROMA - 1) / 2] = {0};
        __m256i dIs[WIENER_WIN_CHROMA - 1], dIe[WIENER_WIN_CHROMA - 1];

        x = 0;
        do {
            load_triangle_win5_avx2(dI + x, d_stride, height, dIs, dIe);
            derive_triangle_win5_avx2(dIs, dIe, deltas);

            x += 16;
        } while (x < w16);

        if (w16 != width) {
            load_triangle_win5_avx2(dI + x, d_stride, height, dIs, dIe);

            dIs[0] = _mm256_and_si256(dIs[0], mask);
            dIs[1] = _mm256_and_si256(dIs[1], mask);
            dIs[2] = _mm256_and_si256(dIs[2], mask);
            dIs[3] = _mm256_and_si256(dIs[3], mask);
            dIe[0] = _mm256_and_si256(dIe[0], mask);
            dIe[1] = _mm256_and_si256(dIe[1], mask);
            dIe[2] = _mm256_and_si256(dIe[2], mask);
            dIe[3] = _mm256_and_si256(dIe[3], mask);

            derive_triangle_win5_avx2(dIs, dIe, deltas);
        }

        if (bit_depth < AOM_BITS_12) {
            hadd_update_4_stats_avx2(
                H + (i * wiener_win + 0) * wiener_win2 + i * wiener_win,
                deltas,
                H + (i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1);

            const __m128i delta32 =
                hadd_four_32_avx2(deltas[4], deltas[5], deltas[6], deltas[9]);
            const __m128i delta64 = _mm_cvtepi32_epi64(delta32);

            update_2_stats_sse2(
                H + (i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1,
                delta64,
                H + (i * wiener_win + 2) * wiener_win2 + i * wiener_win + 2);
            H[(i * wiener_win + 2) * wiener_win2 + i * wiener_win + 4] =
                H[(i * wiener_win + 1) * wiener_win2 + i * wiener_win + 3] +
                _mm_extract_epi32(delta32, 2);

            const __m128i d32 = hadd_two_32_avx2(deltas[7], deltas[8]);
            const __m128i d64 = _mm_cvtepi32_epi64(d32);

            update_2_stats_sse2(
                H + (i * wiener_win + 2) * wiener_win2 + i * wiener_win + 2,
                d64,
                H + (i * wiener_win + 3) * wiener_win2 + i * wiener_win + 3);
            H[(i * wiener_win + 4) * wiener_win2 + i * wiener_win + 4] =
                H[(i * wiener_win + 3) * wiener_win2 + i * wiener_win + 3] +
                _mm_extract_epi32(delta32, 3);
        } else {
            hadd_update_4_stats_highbd_avx2(
                H + (i * wiener_win + 0) * wiener_win2 + i * wiener_win,
                deltas,
                H + (i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1);

            const __m256i delta64 = hadd_four_31_to_64_avx2(
                deltas[4], deltas[5], deltas[6], deltas[9]);

            update_2_stats_sse2(
                H + (i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1,
                _mm256_extracti128_si256(delta64, 0),
                H + (i * wiener_win + 2) * wiener_win2 + i * wiener_win + 2);
            H[(i * wiener_win + 2) * wiener_win2 + i * wiener_win + 4] =
                H[(i * wiener_win + 1) * wiener_win2 + i * wiener_win + 3] +
                _mm256_extract_epi64(delta64, 2);

            const __m128i d64 = hadd_two_31_to_64_avx2(deltas[7], deltas[8]);

            update_2_stats_sse2(
                H + (i * wiener_win + 2) * wiener_win2 + i * wiener_win + 2,
                d64,
                H + (i * wiener_win + 3) * wiener_win2 + i * wiener_win + 3);
            H[(i * wiener_win + 4) * wiener_win2 + i * wiener_win + 4] =
                H[(i * wiener_win + 3) * wiener_win2 + i * wiener_win + 3] +
                _mm256_extract_epi64(delta64, 3);
        }
    } while (++i < wiener_win);
}

static INLINE void compute_stats_win7_avx2(
    const int16_t *const d, const int32_t d_stride, const int16_t *const s,
    const int32_t s_stride, const int32_t width, const int32_t height,
    int64_t *const M, int64_t *const H, AomBitDepth bit_depth) {
    const int32_t wiener_win = WIENER_WIN;
    const int32_t wiener_win2 = wiener_win * wiener_win;
    const int32_t w16 = width & ~15;
    const int32_t h8 = height & ~7;
    const __m256i mask =
        _mm256_load_si256((__m256i *)(mask_16bit[width - w16]));
    int32_t i, j, x, y;

    if (bit_depth == AOM_BITS_8) {
        // Step 1: Calculate the top edge of the whole matrix, i.e., the top
        // edge of each triangle and square on the top row.
        j = 0;
        do {
            const int16_t *sT = s;
            const int16_t *dT = d;
            __m256i sumM[WIENER_WIN] = {0};
            __m256i sumH[WIENER_WIN] = {0};

            y = height;
            do {
                x = 0;
                do {
                    const __m256i src = _mm256_load_si256((__m256i *)(sT + x));
                    const __m256i dgd = _mm256_load_si256((__m256i *)(dT + x));
                    stats_top_win7_avx2(
                        src, dgd, dT + j + x, d_stride, sumM, sumH);
                    x += 16;
                } while (x < w16);

                if (w16 != width) {
                    const __m256i src =
                        _mm256_load_si256((__m256i *)(sT + w16));
                    const __m256i dgd =
                        _mm256_load_si256((__m256i *)(dT + w16));
                    const __m256i srcMask = _mm256_and_si256(src, mask);
                    const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                    stats_top_win7_avx2(
                        srcMask, dgdMask, dT + j + w16, d_stride, sumM, sumH);
                }

                sT += s_stride;
                dT += d_stride;
            } while (--y);

            const __m256i sM0 =
                hadd_four_32_to_64_avx2(sumM[0], sumM[1], sumM[2], sumM[3]);
            const __m256i sM1 =
                hadd_four_32_to_64_avx2(sumM[4], sumM[5], sumM[6], sumM[6]);
            _mm256_storeu_si256((__m256i *)(M + wiener_win * j + 0), sM0);
            _mm_storeu_si128((__m128i *)(M + wiener_win * j + 4),
                             _mm256_extracti128_si256(sM1, 0));
            _mm_storel_epi64((__m128i *)&M[wiener_win * j + 6],
                             _mm256_extracti128_si256(sM1, 1));

            const __m256i sH0 =
                hadd_four_32_to_64_avx2(sumH[0], sumH[1], sumH[2], sumH[3]);
            const __m256i sH1 =
                hadd_four_32_to_64_avx2(sumH[4], sumH[5], sumH[6], sumH[6]);
            _mm256_storeu_si256((__m256i *)(H + wiener_win * j + 0), sH0);
            // Writing one more H on the top edge falls to the second row, so it
            // won't overflow.
            _mm256_storeu_si256((__m256i *)(H + wiener_win * j + 4), sH1);
        } while (++j < wiener_win);

        // Step 2: Calculate the left edge of each square on the top row.
        j = 1;
        do {
            const int16_t *dT = d;
            __m256i sumH[WIENER_WIN - 1] = {0};

            y = height;
            do {
                x = 0;
                do {
                    const __m256i dgd =
                        _mm256_loadu_si256((__m256i *)(dT + j + x));
                    stats_left_win7_avx2(dgd, dT + x, d_stride, sumH);
                    x += 16;
                } while (x < w16);

                if (w16 != width) {
                    const __m256i dgd =
                        _mm256_loadu_si256((__m256i *)(dT + j + x));
                    const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                    stats_left_win7_avx2(dgdMask, dT + x, d_stride, sumH);
                }

                dT += d_stride;
            } while (--y);

            const __m256i sum0123 =
                hadd_four_32_to_64_avx2(sumH[0], sumH[1], sumH[2], sumH[3]);
            _mm_storel_epi64((__m128i *)&H[1 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum0123, 0));
            _mm_storeh_epi64((__m128i *)&H[2 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum0123, 0));
            _mm_storel_epi64((__m128i *)&H[3 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum0123, 1));
            _mm_storeh_epi64((__m128i *)&H[4 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum0123, 1));

            const __m128i sum45 = hadd_two_32_to_64_avx2(sumH[4], sumH[5]);
            _mm_storel_epi64((__m128i *)&H[5 * wiener_win2 + j * wiener_win],
                             sum45);
            _mm_storeh_epi64((__m128i *)&H[6 * wiener_win2 + j * wiener_win],
                             sum45);
        } while (++j < wiener_win);
    } else {
        const int32_t numBitLeft =
            32 - 1 /* sign */ - 2 * bit_depth /* energy */ + 3 /* SIMD */;
        const int32_t hAllowed =
            (1 << numBitLeft) / (w16 + ((w16 != width) ? 16 : 0));

        // Step 1: Calculate the top edge of the whole matrix, i.e., the top
        // edge of each triangle and square on the top row.
        j = 0;
        do {
            const int16_t *sT = s;
            const int16_t *dT = d;
            int32_t heightT = 0;
            __m256i sumM[WIENER_WIN] = {0};
            __m256i sumH[WIENER_WIN] = {0};

            do {
                const int32_t hT = ((height - heightT) < hAllowed)
                                       ? (height - heightT)
                                       : hAllowed;
                __m256i rowM[WIENER_WIN] = {0};
                __m256i rowH[WIENER_WIN] = {0};

                y = hT;
                do {
                    x = 0;
                    do {
                        const __m256i src =
                            _mm256_load_si256((__m256i *)(sT + x));
                        const __m256i dgd =
                            _mm256_load_si256((__m256i *)(dT + x));
                        stats_top_win7_avx2(
                            src, dgd, dT + j + x, d_stride, rowM, rowH);
                        x += 16;
                    } while (x < w16);

                    if (w16 != width) {
                        const __m256i src =
                            _mm256_load_si256((__m256i *)(sT + w16));
                        const __m256i dgd =
                            _mm256_load_si256((__m256i *)(dT + w16));
                        const __m256i srcMask = _mm256_and_si256(src, mask);
                        const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                        stats_top_win7_avx2(srcMask,
                                            dgdMask,
                                            dT + j + w16,
                                            d_stride,
                                            rowM,
                                            rowH);
                    }

                    sT += s_stride;
                    dT += d_stride;
                } while (--y);

                add_32_to_64_avx2(rowM[0], &sumM[0]);
                add_32_to_64_avx2(rowM[1], &sumM[1]);
                add_32_to_64_avx2(rowM[2], &sumM[2]);
                add_32_to_64_avx2(rowM[3], &sumM[3]);
                add_32_to_64_avx2(rowM[4], &sumM[4]);
                add_32_to_64_avx2(rowM[5], &sumM[5]);
                add_32_to_64_avx2(rowM[6], &sumM[6]);
                add_32_to_64_avx2(rowH[0], &sumH[0]);
                add_32_to_64_avx2(rowH[1], &sumH[1]);
                add_32_to_64_avx2(rowH[2], &sumH[2]);
                add_32_to_64_avx2(rowH[3], &sumH[3]);
                add_32_to_64_avx2(rowH[4], &sumH[4]);
                add_32_to_64_avx2(rowH[5], &sumH[5]);
                add_32_to_64_avx2(rowH[6], &sumH[6]);

                heightT += hT;
            } while (heightT < height);

            const __m256i sM0 =
                hadd_four_64_avx2(sumM[0], sumM[1], sumM[2], sumM[3]);
            const __m256i sM1 =
                hadd_four_64_avx2(sumM[4], sumM[5], sumM[6], sumM[6]);
            _mm256_storeu_si256((__m256i *)(M + wiener_win * j + 0), sM0);
            _mm_storeu_si128((__m128i *)(M + wiener_win * j + 4),
                             _mm256_extracti128_si256(sM1, 0));
            _mm_storel_epi64((__m128i *)&M[wiener_win * j + 6],
                             _mm256_extracti128_si256(sM1, 1));

            const __m256i sH0 =
                hadd_four_64_avx2(sumH[0], sumH[1], sumH[2], sumH[3]);
            const __m256i sH1 =
                hadd_four_64_avx2(sumH[4], sumH[5], sumH[6], sumH[6]);
            _mm256_storeu_si256((__m256i *)(H + wiener_win * j + 0), sH0);
            // Writing one more H on the top edge falls to the second row, so it
            // won't overflow.
            _mm256_storeu_si256((__m256i *)(H + wiener_win * j + 4), sH1);
        } while (++j < wiener_win);

        // Step 2: Calculate the left edge of each square on the top row.
        j = 1;
        do {
            const int16_t *dT = d;
            int32_t heightT = 0;
            __m256i sumH[WIENER_WIN - 1] = {0};

            do {
                const int32_t hT = ((height - heightT) < hAllowed)
                                       ? (height - heightT)
                                       : hAllowed;
                __m256i rowH[WIENER_WIN - 1] = {0};

                y = hT;
                do {
                    x = 0;
                    do {
                        const __m256i dgd =
                            _mm256_loadu_si256((__m256i *)(dT + j + x));
                        stats_left_win7_avx2(dgd, dT + x, d_stride, rowH);
                        x += 16;
                    } while (x < w16);

                    if (w16 != width) {
                        const __m256i dgd =
                            _mm256_loadu_si256((__m256i *)(dT + j + x));
                        const __m256i dgdMask = _mm256_and_si256(dgd, mask);
                        stats_left_win7_avx2(dgdMask, dT + x, d_stride, rowH);
                    }

                    dT += d_stride;
                } while (--y);

                add_32_to_64_avx2(rowH[0], &sumH[0]);
                add_32_to_64_avx2(rowH[1], &sumH[1]);
                add_32_to_64_avx2(rowH[2], &sumH[2]);
                add_32_to_64_avx2(rowH[3], &sumH[3]);
                add_32_to_64_avx2(rowH[4], &sumH[4]);
                add_32_to_64_avx2(rowH[5], &sumH[5]);

                heightT += hT;
            } while (heightT < height);

            const __m256i sum0123 =
                hadd_four_64_avx2(sumH[0], sumH[1], sumH[2], sumH[3]);
            _mm_storel_epi64((__m128i *)&H[1 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum0123, 0));
            _mm_storeh_epi64((__m128i *)&H[2 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum0123, 0));
            _mm_storel_epi64((__m128i *)&H[3 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum0123, 1));
            _mm_storeh_epi64((__m128i *)&H[4 * wiener_win2 + j * wiener_win],
                             _mm256_extracti128_si256(sum0123, 1));

            const __m128i sum45 = hadd_two_64_avx2(sumH[4], sumH[5]);
            _mm_storel_epi64((__m128i *)&H[5 * wiener_win2 + j * wiener_win],
                             sum45);
            _mm_storeh_epi64((__m128i *)&H[6 * wiener_win2 + j * wiener_win],
                             sum45);
        } while (++j < wiener_win);
    }

    // Step 3: Derive the top edge of each triangle along the diagonal. No
    // triangle in top row.
    {
        const int16_t *dT = d;
        // Pad to call transpose function.
        __m256i deltas[WIENER_WIN + 1] = {0};
        __m256i ds[WIENER_WIN];

        // 00s 00e 01s 01e 02s 02e 03s 03e  04s 04e 05s 05e 06s 06e 07s 07e
        // 10s 10e 11s 11e 12s 12e 13s 13e  14s 14e 15s 15e 16s 16e 17s 17e
        // 20s 20e 21s 21e 22s 22e 23s 23e  24s 24e 25s 25e 26s 26e 27s 27e
        // 30s 30e 31s 31e 32s 32e 33s 33e  34s 34e 35s 35e 36s 36e 37s 37e
        // 40s 40e 41s 41e 42s 42e 43s 43e  44s 44e 45s 45e 46s 46e 47s 47e
        // 50s 50e 51s 51e 52s 52e 53s 53e  54s 54e 55s 55e 56s 56e 57s 57e
        ds[0] = load_win7_avx2(dT + 0 * d_stride, width);
        ds[1] = load_win7_avx2(dT + 1 * d_stride, width);
        ds[2] = load_win7_avx2(dT + 2 * d_stride, width);
        ds[3] = load_win7_avx2(dT + 3 * d_stride, width);
        ds[4] = load_win7_avx2(dT + 4 * d_stride, width);
        ds[5] = load_win7_avx2(dT + 5 * d_stride, width);
        dT += 6 * d_stride;

        if (bit_depth < AOM_BITS_12) {
            step3_win7_avx2(&dT, d_stride, width, height, ds, deltas);

            transpose_32bit_8x8_avx2(deltas, deltas);

            update_8_stats_avx2(
                H + 0 * wiener_win * wiener_win2 + 0 * wiener_win,
                deltas[0],
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win);
            update_8_stats_avx2(
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win,
                deltas[1],
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win);
            update_8_stats_avx2(
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win,
                deltas[2],
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win);
            update_8_stats_avx2(
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win,
                deltas[3],
                H + 4 * wiener_win * wiener_win2 + 4 * wiener_win);
            update_8_stats_avx2(
                H + 4 * wiener_win * wiener_win2 + 4 * wiener_win,
                deltas[4],
                H + 5 * wiener_win * wiener_win2 + 5 * wiener_win);
            update_8_stats_avx2(
                H + 5 * wiener_win * wiener_win2 + 5 * wiener_win,
                deltas[5],
                H + 6 * wiener_win * wiener_win2 + 6 * wiener_win);
        } else {
            __m128i deltas128[WIENER_WIN] = {0};
            int32_t heightT = 0;

            do {
                __m256i deltasT[WIENER_WIN] = {0};
                const int32_t hT =
                    ((height - heightT) < 128) ? (height - heightT) : 128;

                step3_win7_avx2(&dT, d_stride, width, hT, ds, deltasT);

                add_six_32_to_64_avx2(deltasT[0], &deltas[0], &deltas128[0]);
                add_six_32_to_64_avx2(deltasT[1], &deltas[1], &deltas128[1]);
                add_six_32_to_64_avx2(deltasT[2], &deltas[2], &deltas128[2]);
                add_six_32_to_64_avx2(deltasT[3], &deltas[3], &deltas128[3]);
                add_six_32_to_64_avx2(deltasT[4], &deltas[4], &deltas128[4]);
                add_six_32_to_64_avx2(deltasT[5], &deltas[5], &deltas128[5]);
                add_six_32_to_64_avx2(deltasT[6], &deltas[6], &deltas128[6]);

                heightT += hT;
            } while (heightT < height);

            transpose_64bit_4x8_avx2(deltas, deltas);
            update_4_stats_highbd_avx2(
                H + 0 * wiener_win * wiener_win2 + 0 * wiener_win + 0,
                deltas[0],
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win + 0);
            update_4_stats_highbd_avx2(
                H + 0 * wiener_win * wiener_win2 + 0 * wiener_win + 4,
                deltas[1],
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win + 4);
            update_4_stats_highbd_avx2(
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win + 0,
                deltas[2],
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win + 0);
            update_4_stats_highbd_avx2(
                H + 1 * wiener_win * wiener_win2 + 1 * wiener_win + 4,
                deltas[3],
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win + 4);
            update_4_stats_highbd_avx2(
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win + 0,
                deltas[4],
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win + 0);
            update_4_stats_highbd_avx2(
                H + 2 * wiener_win * wiener_win2 + 2 * wiener_win + 4,
                deltas[5],
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win + 4);
            update_4_stats_highbd_avx2(
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win + 0,
                deltas[6],
                H + 4 * wiener_win * wiener_win2 + 4 * wiener_win + 0);
            update_4_stats_highbd_avx2(
                H + 3 * wiener_win * wiener_win2 + 3 * wiener_win + 4,
                deltas[7],
                H + 4 * wiener_win * wiener_win2 + 4 * wiener_win + 4);

            const __m128i d0 = _mm_unpacklo_epi64(deltas128[0], deltas128[1]);
            const __m128i d1 = _mm_unpacklo_epi64(deltas128[2], deltas128[3]);
            const __m128i d2 = _mm_unpacklo_epi64(deltas128[4], deltas128[5]);
            const __m128i d3 = _mm_unpacklo_epi64(deltas128[6], deltas128[6]);
            const __m128i d4 = _mm_unpackhi_epi64(deltas128[0], deltas128[1]);
            const __m128i d5 = _mm_unpackhi_epi64(deltas128[2], deltas128[3]);
            const __m128i d6 = _mm_unpackhi_epi64(deltas128[4], deltas128[5]);
            const __m128i d7 = _mm_unpackhi_epi64(deltas128[6], deltas128[6]);

            deltas[0] =
                _mm256_inserti128_si256(_mm256_castsi128_si256(d0), d1, 1);
            deltas[1] =
                _mm256_inserti128_si256(_mm256_castsi128_si256(d2), d3, 1);
            deltas[2] =
                _mm256_inserti128_si256(_mm256_castsi128_si256(d4), d5, 1);
            deltas[3] =
                _mm256_inserti128_si256(_mm256_castsi128_si256(d6), d7, 1);
            update_4_stats_highbd_avx2(
                H + 4 * wiener_win * wiener_win2 + 4 * wiener_win + 0,
                deltas[0],
                H + 5 * wiener_win * wiener_win2 + 5 * wiener_win + 0);
            update_4_stats_highbd_avx2(
                H + 4 * wiener_win * wiener_win2 + 4 * wiener_win + 4,
                deltas[1],
                H + 5 * wiener_win * wiener_win2 + 5 * wiener_win + 4);
            update_4_stats_highbd_avx2(
                H + 5 * wiener_win * wiener_win2 + 5 * wiener_win + 0,
                deltas[2],
                H + 6 * wiener_win * wiener_win2 + 6 * wiener_win + 0);
            update_4_stats_highbd_avx2(
                H + 5 * wiener_win * wiener_win2 + 5 * wiener_win + 4,
                deltas[3],
                H + 6 * wiener_win * wiener_win2 + 6 * wiener_win + 4);
        }
    }

    // Step 4: Derive the top and left edge of each square. No square in top and
    // bottom row.
    i = 1;
    do {
        j = i + 1;
        do {
            const int16_t *dI = d + i - 1;
            const int16_t *dJ = d + j - 1;
            __m256i deltas[2 * WIENER_WIN - 1] = {0};
            __m256i deltasT[8], deltasTT[4];
            __m256i dd[WIENER_WIN], ds[WIENER_WIN];
            dd[0] = _mm256_setzero_si256();  // Initialize to avoid warning.
            ds[0] = _mm256_setzero_si256();  // Initialize to avoid warning.

            dd[0] = _mm256_insert_epi16(dd[0], dI[0 * d_stride], 0);
            dd[0] = _mm256_insert_epi16(dd[0], dI[0 * d_stride + width], 8);
            dd[0] = _mm256_insert_epi16(dd[0], dI[1 * d_stride], 1);
            dd[0] = _mm256_insert_epi16(dd[0], dI[1 * d_stride + width], 9);
            dd[0] = _mm256_insert_epi16(dd[0], dI[2 * d_stride], 2);
            dd[0] = _mm256_insert_epi16(dd[0], dI[2 * d_stride + width], 10);
            dd[0] = _mm256_insert_epi16(dd[0], dI[3 * d_stride], 3);
            dd[0] = _mm256_insert_epi16(dd[0], dI[3 * d_stride + width], 11);
            dd[0] = _mm256_insert_epi16(dd[0], dI[4 * d_stride], 4);
            dd[0] = _mm256_insert_epi16(dd[0], dI[4 * d_stride + width], 12);
            dd[0] = _mm256_insert_epi16(dd[0], dI[5 * d_stride], 5);
            dd[0] = _mm256_insert_epi16(dd[0], dI[5 * d_stride + width], 13);

            ds[0] = _mm256_insert_epi16(ds[0], dJ[0 * d_stride], 0);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[0 * d_stride + width], 8);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[1 * d_stride], 1);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[1 * d_stride + width], 9);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[2 * d_stride], 2);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[2 * d_stride + width], 10);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[3 * d_stride], 3);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[3 * d_stride + width], 11);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[4 * d_stride], 4);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[4 * d_stride + width], 12);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[5 * d_stride], 5);
            ds[0] = _mm256_insert_epi16(ds[0], dJ[5 * d_stride + width], 13);

            y = 0;
            do {
                // 00s 10s 20s 30s 40s 50s 60s 70s  00e 10e 20e 30e 40e 50e 60e
                // 70e
                dd[0] = _mm256_insert_epi16(dd[0], dI[6 * d_stride], 6);
                dd[0] =
                    _mm256_insert_epi16(dd[0], dI[6 * d_stride + width], 14);
                dd[0] = _mm256_insert_epi16(dd[0], dI[7 * d_stride], 7);
                dd[0] =
                    _mm256_insert_epi16(dd[0], dI[7 * d_stride + width], 15);

                // 00s 10s 20s 30s 40s 50s 60s 70s  00e 10e 20e 30e 40e 50e 60e
                // 70e 01s 11s 21s 31s 41s 51s 61s 71s  01e 11e 21e 31e 41e 51e
                // 61e 71e
                ds[0] = _mm256_insert_epi16(ds[0], dJ[6 * d_stride], 6);
                ds[0] =
                    _mm256_insert_epi16(ds[0], dJ[6 * d_stride + width], 14);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[7 * d_stride], 7);
                ds[0] =
                    _mm256_insert_epi16(ds[0], dJ[7 * d_stride + width], 15);

                load_more_16_avx2(dI + 8 * d_stride, width, dd[0], &dd[1]);
                load_more_16_avx2(dJ + 8 * d_stride, width, ds[0], &ds[1]);
                load_more_16_avx2(dI + 9 * d_stride, width, dd[1], &dd[2]);
                load_more_16_avx2(dJ + 9 * d_stride, width, ds[1], &ds[2]);
                load_more_16_avx2(dI + 10 * d_stride, width, dd[2], &dd[3]);
                load_more_16_avx2(dJ + 10 * d_stride, width, ds[2], &ds[3]);
                load_more_16_avx2(dI + 11 * d_stride, width, dd[3], &dd[4]);
                load_more_16_avx2(dJ + 11 * d_stride, width, ds[3], &ds[4]);
                load_more_16_avx2(dI + 12 * d_stride, width, dd[4], &dd[5]);
                load_more_16_avx2(dJ + 12 * d_stride, width, ds[4], &ds[5]);
                load_more_16_avx2(dI + 13 * d_stride, width, dd[5], &dd[6]);
                load_more_16_avx2(dJ + 13 * d_stride, width, ds[5], &ds[6]);

                madd_avx2(dd[0], ds[0], &deltas[0]);
                madd_avx2(dd[0], ds[1], &deltas[1]);
                madd_avx2(dd[0], ds[2], &deltas[2]);
                madd_avx2(dd[0], ds[3], &deltas[3]);
                madd_avx2(dd[0], ds[4], &deltas[4]);
                madd_avx2(dd[0], ds[5], &deltas[5]);
                madd_avx2(dd[0], ds[6], &deltas[6]);
                madd_avx2(dd[1], ds[0], &deltas[7]);
                madd_avx2(dd[2], ds[0], &deltas[8]);
                madd_avx2(dd[3], ds[0], &deltas[9]);
                madd_avx2(dd[4], ds[0], &deltas[10]);
                madd_avx2(dd[5], ds[0], &deltas[11]);
                madd_avx2(dd[6], ds[0], &deltas[12]);

                dd[0] = _mm256_srli_si256(dd[6], 4);
                ds[0] = _mm256_srli_si256(ds[6], 4);
                dI += 8 * d_stride;
                dJ += 8 * d_stride;
                y += 8;
            } while (y < h8);

            if (bit_depth < AOM_BITS_12) {
                deltas[0] = _mm256_hadd_epi32(deltas[0], deltas[1]);
                deltas[2] = _mm256_hadd_epi32(deltas[2], deltas[3]);
                deltas[4] = _mm256_hadd_epi32(deltas[4], deltas[5]);
                deltas[6] = _mm256_hadd_epi32(deltas[6], deltas[6]);
                deltas[7] = _mm256_hadd_epi32(deltas[7], deltas[8]);
                deltas[9] = _mm256_hadd_epi32(deltas[9], deltas[10]);
                deltas[11] = _mm256_hadd_epi32(deltas[11], deltas[12]);
                deltas[0] = _mm256_hadd_epi32(deltas[0], deltas[2]);
                deltas[4] = _mm256_hadd_epi32(deltas[4], deltas[6]);
                deltas[7] = _mm256_hadd_epi32(deltas[7], deltas[9]);
                deltas[11] = _mm256_hadd_epi32(deltas[11], deltas[11]);
                const __m128i delta0 = sub_hi_lo_32_avx2(deltas[0]);
                const __m128i delta1 = sub_hi_lo_32_avx2(deltas[4]);
                const __m128i delta2 = sub_hi_lo_32_avx2(deltas[7]);
                const __m128i delta3 = sub_hi_lo_32_avx2(deltas[11]);
                deltas[0] = _mm256_inserti128_si256(
                    _mm256_castsi128_si256(delta0), delta1, 1);
                deltas[1] = _mm256_inserti128_si256(
                    _mm256_castsi128_si256(delta2), delta3, 1);
            } else {
                deltas[0] = hsub_32x8_to_64x4_avx2(deltas[0]);
                deltas[1] = hsub_32x8_to_64x4_avx2(deltas[1]);
                deltas[2] = hsub_32x8_to_64x4_avx2(deltas[2]);
                deltas[3] = hsub_32x8_to_64x4_avx2(deltas[3]);
                deltas[4] = hsub_32x8_to_64x4_avx2(deltas[4]);
                deltas[5] = hsub_32x8_to_64x4_avx2(deltas[5]);
                deltas[6] = hsub_32x8_to_64x4_avx2(deltas[6]);
                deltas[7] = hsub_32x8_to_64x4_avx2(deltas[7]);
                deltas[8] = hsub_32x8_to_64x4_avx2(deltas[8]);
                deltas[9] = hsub_32x8_to_64x4_avx2(deltas[9]);
                deltas[10] = hsub_32x8_to_64x4_avx2(deltas[10]);
                deltas[11] = hsub_32x8_to_64x4_avx2(deltas[11]);
                deltas[12] = hsub_32x8_to_64x4_avx2(deltas[12]);

                transpose_64bit_4x8_avx2(deltas + 0, deltasT);
                deltasT[0] = _mm256_add_epi64(deltasT[0], deltasT[2]);
                deltasT[4] = _mm256_add_epi64(deltasT[4], deltasT[6]);
                deltasT[1] = _mm256_add_epi64(deltasT[1], deltasT[3]);
                deltasT[5] = _mm256_add_epi64(deltasT[5], deltasT[7]);
                deltasTT[0] = _mm256_add_epi64(deltasT[0], deltasT[4]);
                deltasTT[1] = _mm256_add_epi64(deltasT[1], deltasT[5]);

                transpose_64bit_4x6_avx2(deltas + 7, deltasT);
                deltasT[0] = _mm256_add_epi64(deltasT[0], deltasT[2]);
                deltasT[4] = _mm256_add_epi64(deltasT[4], deltasT[6]);
                deltasT[1] = _mm256_add_epi64(deltasT[1], deltasT[3]);
                deltasT[5] = _mm256_add_epi64(deltasT[5], deltasT[7]);
                deltasTT[2] = _mm256_add_epi64(deltasT[0], deltasT[4]);
                deltasTT[3] = _mm256_add_epi64(deltasT[1], deltasT[5]);

                deltas[0] = _mm256_setzero_si256();
                deltas[1] = _mm256_setzero_si256();
            }

            if (h8 != height) {
                const __m256i perm = _mm256_setr_epi32(1, 2, 3, 4, 5, 6, 7, 0);

                ds[0] = _mm256_insert_epi16(ds[0], dJ[0 * d_stride], 0);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[0 * d_stride + width], 1);

                dd[2] = _mm256_insert_epi16(dd[2], -dI[1 * d_stride], 0);
                dd[2] = _mm256_insert_epi16(dd[2], dI[1 * d_stride + width], 1);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[1 * d_stride], 2);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[1 * d_stride + width], 3);

                dd[2] = _mm256_insert_epi16(dd[2], -dI[2 * d_stride], 2);
                dd[2] = _mm256_insert_epi16(dd[2], dI[2 * d_stride + width], 3);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[2 * d_stride], 4);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[2 * d_stride + width], 5);

                dd[2] = _mm256_insert_epi16(dd[2], -dI[3 * d_stride], 4);
                dd[2] = _mm256_insert_epi16(dd[2], dI[3 * d_stride + width], 5);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[3 * d_stride], 6);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[3 * d_stride + width], 7);

                dd[2] = _mm256_insert_epi16(dd[2], -dI[4 * d_stride], 6);
                dd[2] = _mm256_insert_epi16(dd[2], dI[4 * d_stride + width], 7);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[4 * d_stride], 8);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[4 * d_stride + width], 9);

                dd[2] = _mm256_insert_epi16(dd[2], -dI[5 * d_stride], 8);
                dd[2] = _mm256_insert_epi16(dd[2], dI[5 * d_stride + width], 9);
                ds[0] = _mm256_insert_epi16(ds[0], dJ[5 * d_stride], 10);
                ds[0] =
                    _mm256_insert_epi16(ds[0], dJ[5 * d_stride + width], 11);

                do {
                    dd[0] = _mm256_set1_epi16(-dI[0 * d_stride]);
                    dd[1] = _mm256_set1_epi16(dI[0 * d_stride + width]);
                    dd[0] = _mm256_unpacklo_epi16(dd[0], dd[1]);

                    ds[2] = _mm256_set1_epi16(dJ[0 * d_stride]);
                    ds[3] = _mm256_set1_epi16(dJ[0 * d_stride + width]);
                    ds[2] = _mm256_unpacklo_epi16(ds[2], ds[3]);

                    dd[2] = _mm256_insert_epi16(dd[2], -dI[6 * d_stride], 10);
                    dd[2] = _mm256_insert_epi16(
                        dd[2], dI[6 * d_stride + width], 11);
                    ds[0] = _mm256_insert_epi16(ds[0], dJ[6 * d_stride], 12);
                    ds[0] = _mm256_insert_epi16(
                        ds[0], dJ[6 * d_stride + width], 13);

                    madd_avx2(dd[0], ds[0], &deltas[0]);
                    madd_avx2(dd[2], ds[2], &deltas[1]);

                    // right shift 4 bytes
                    dd[2] = _mm256_permutevar8x32_epi32(dd[2], perm);
                    ds[0] = _mm256_permutevar8x32_epi32(ds[0], perm);
                    dI += d_stride;
                    dJ += d_stride;
                } while (++y < height);
            }

            // Writing one more H on the top edge of a square falls to the next
            // square in the same row or the first H in the next row, which
            // would be calculated later, so it won't overflow.
            if (bit_depth < AOM_BITS_12) {
                update_8_stats_avx2(
                    H + (i - 1) * wiener_win * wiener_win2 +
                        (j - 1) * wiener_win,
                    deltas[0],
                    H + i * wiener_win * wiener_win2 + j * wiener_win);

                H[(i * wiener_win + 1) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 1) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi32(deltas[1], 0);
                H[(i * wiener_win + 2) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 2) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi32(deltas[1], 1);
                H[(i * wiener_win + 3) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 3) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi32(deltas[1], 2);
                H[(i * wiener_win + 4) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 4) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi32(deltas[1], 3);
                H[(i * wiener_win + 5) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 5) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi32(deltas[1], 4);
                H[(i * wiener_win + 6) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 6) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi32(deltas[1], 5);
            } else {
                const __m256i d0 = _mm256_cvtepi32_epi64(
                    _mm256_extracti128_si256(deltas[0], 0));
                const __m256i d1 = _mm256_cvtepi32_epi64(
                    _mm256_extracti128_si256(deltas[0], 1));
                const __m256i d2 = _mm256_cvtepi32_epi64(
                    _mm256_extracti128_si256(deltas[1], 0));
                const __m256i d3 = _mm256_cvtepi32_epi64(
                    _mm256_extracti128_si256(deltas[1], 1));

                deltas[0] = _mm256_add_epi64(deltasTT[0], d0);
                deltas[1] = _mm256_add_epi64(deltasTT[1], d1);
                deltas[2] = _mm256_add_epi64(deltasTT[2], d2);
                deltas[3] = _mm256_add_epi64(deltasTT[3], d3);

                update_4_stats_highbd_avx2(
                    H + (i - 1) * wiener_win * wiener_win2 +
                        (j - 1) * wiener_win + 0,
                    deltas[0],
                    H + i * wiener_win * wiener_win2 + j * wiener_win + 0);
                update_4_stats_highbd_avx2(
                    H + (i - 1) * wiener_win * wiener_win2 +
                        (j - 1) * wiener_win + 4,
                    deltas[1],
                    H + i * wiener_win * wiener_win2 + j * wiener_win + 4);

                H[(i * wiener_win + 1) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 1) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[2], 0);
                H[(i * wiener_win + 2) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 2) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[2], 1);
                H[(i * wiener_win + 3) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 3) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[2], 2);
                H[(i * wiener_win + 4) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 4) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[2], 3);
                H[(i * wiener_win + 5) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 5) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[3], 0);
                H[(i * wiener_win + 6) * wiener_win2 + j * wiener_win] =
                    H[((i - 1) * wiener_win + 6) * wiener_win2 +
                      (j - 1) * wiener_win] +
                    _mm256_extract_epi64(deltas[3], 1);
            }
        } while (++j < wiener_win);
    } while (++i < wiener_win - 1);

    // Step 5: Derive other points of each square. No square in bottom row.
    i = 0;
    do {
        const int16_t *const dI = d + i;

        j = i + 1;
        do {
            const int16_t *const dJ = d + j;
            __m256i deltas[WIENER_WIN - 1][WIENER_WIN - 1] = {{{0}},{{0}}};
            __m256i dIs[WIENER_WIN - 1], dIe[WIENER_WIN - 1];
            __m256i dJs[WIENER_WIN - 1], dJe[WIENER_WIN - 1];

            x = 0;
            do {
                load_square_win7_avx2(
                    dI + x, dJ + x, d_stride, height, dIs, dIe, dJs, dJe);
                derive_square_win7_avx2(dIs, dIe, dJs, dJe, deltas);

                x += 16;
            } while (x < w16);

            if (w16 != width) {
                load_square_win7_avx2(
                    dI + x, dJ + x, d_stride, height, dIs, dIe, dJs, dJe);

                dIs[0] = _mm256_and_si256(dIs[0], mask);
                dIs[1] = _mm256_and_si256(dIs[1], mask);
                dIs[2] = _mm256_and_si256(dIs[2], mask);
                dIs[3] = _mm256_and_si256(dIs[3], mask);
                dIs[4] = _mm256_and_si256(dIs[4], mask);
                dIs[5] = _mm256_and_si256(dIs[5], mask);
                dIe[0] = _mm256_and_si256(dIe[0], mask);
                dIe[1] = _mm256_and_si256(dIe[1], mask);
                dIe[2] = _mm256_and_si256(dIe[2], mask);
                dIe[3] = _mm256_and_si256(dIe[3], mask);
                dIe[4] = _mm256_and_si256(dIe[4], mask);
                dIe[5] = _mm256_and_si256(dIe[5], mask);

                derive_square_win7_avx2(dIs, dIe, dJs, dJe, deltas);
            }

            if (bit_depth < AOM_BITS_12) {
                hadd_update_6_stats_avx2(
                    H + (i * wiener_win + 0) * wiener_win2 + j * wiener_win,
                    deltas[0],
                    H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_avx2(
                    H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win,
                    deltas[1],
                    H + (i * wiener_win + 2) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_avx2(
                    H + (i * wiener_win + 2) * wiener_win2 + j * wiener_win,
                    deltas[2],
                    H + (i * wiener_win + 3) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_avx2(
                    H + (i * wiener_win + 3) * wiener_win2 + j * wiener_win,
                    deltas[3],
                    H + (i * wiener_win + 4) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_avx2(
                    H + (i * wiener_win + 4) * wiener_win2 + j * wiener_win,
                    deltas[4],
                    H + (i * wiener_win + 5) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_avx2(
                    H + (i * wiener_win + 5) * wiener_win2 + j * wiener_win,
                    deltas[5],
                    H + (i * wiener_win + 6) * wiener_win2 + j * wiener_win +
                        1);
            } else {
                hadd_update_6_stats_highbd_avx2(
                    H + (i * wiener_win + 0) * wiener_win2 + j * wiener_win,
                    deltas[0],
                    H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_highbd_avx2(
                    H + (i * wiener_win + 1) * wiener_win2 + j * wiener_win,
                    deltas[1],
                    H + (i * wiener_win + 2) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_highbd_avx2(
                    H + (i * wiener_win + 2) * wiener_win2 + j * wiener_win,
                    deltas[2],
                    H + (i * wiener_win + 3) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_highbd_avx2(
                    H + (i * wiener_win + 3) * wiener_win2 + j * wiener_win,
                    deltas[3],
                    H + (i * wiener_win + 4) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_highbd_avx2(
                    H + (i * wiener_win + 4) * wiener_win2 + j * wiener_win,
                    deltas[4],
                    H + (i * wiener_win + 5) * wiener_win2 + j * wiener_win +
                        1);
                hadd_update_6_stats_highbd_avx2(
                    H + (i * wiener_win + 5) * wiener_win2 + j * wiener_win,
                    deltas[5],
                    H + (i * wiener_win + 6) * wiener_win2 + j * wiener_win +
                        1);
            }
        } while (++j < wiener_win);
    } while (++i < wiener_win - 1);

    // Step 6: Derive other points of each upper triangle along the diagonal.
    i = 0;
    do {
        const int16_t *const dI = d + i;
        __m256i deltas[WIENER_WIN * (WIENER_WIN - 1) / 2] = {0};
        __m256i dIs[WIENER_WIN - 1], dIe[WIENER_WIN - 1];

        x = 0;
        do {
            load_triangle_win7_avx2(dI + x, d_stride, height, dIs, dIe);
            derive_triangle_win7_avx2(dIs, dIe, deltas);

            x += 16;
        } while (x < w16);

        if (w16 != width) {
            load_triangle_win7_avx2(dI + x, d_stride, height, dIs, dIe);

            dIs[0] = _mm256_and_si256(dIs[0], mask);
            dIs[1] = _mm256_and_si256(dIs[1], mask);
            dIs[2] = _mm256_and_si256(dIs[2], mask);
            dIs[3] = _mm256_and_si256(dIs[3], mask);
            dIs[4] = _mm256_and_si256(dIs[4], mask);
            dIs[5] = _mm256_and_si256(dIs[5], mask);
            dIe[0] = _mm256_and_si256(dIe[0], mask);
            dIe[1] = _mm256_and_si256(dIe[1], mask);
            dIe[2] = _mm256_and_si256(dIe[2], mask);
            dIe[3] = _mm256_and_si256(dIe[3], mask);
            dIe[4] = _mm256_and_si256(dIe[4], mask);
            dIe[5] = _mm256_and_si256(dIe[5], mask);

            derive_triangle_win7_avx2(dIs, dIe, deltas);
        }

        if (bit_depth < AOM_BITS_12) {
            // Row 1: 6 points
            hadd_update_6_stats_avx2(
                H + (i * wiener_win + 0) * wiener_win2 + i * wiener_win,
                deltas,
                H + (i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1);

            const __m128i delta0 = hadd_four_32_avx2(
                deltas[15], deltas[16], deltas[17], deltas[10]);
            const __m128i delta1 = hadd_four_32_avx2(
                deltas[18], deltas[19], deltas[20], deltas[20]);
            const __m128i delta2 = _mm_cvtepi32_epi64(delta0);
            const __m128i delta3 = _mm_cvtepi32_epi64(delta1);

            // Row 2: 5 points
            hadd_update_4_stats_avx2(
                H + (i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1,
                deltas + 6,
                H + (i * wiener_win + 2) * wiener_win2 + i * wiener_win + 2);
            H[(i * wiener_win + 2) * wiener_win2 + i * wiener_win + 6] =
                H[(i * wiener_win + 1) * wiener_win2 + i * wiener_win + 5] +
                _mm_extract_epi32(delta0, 3);

            // Row 3: 4 points
            hadd_update_4_stats_avx2(
                H + (i * wiener_win + 2) * wiener_win2 + i * wiener_win + 2,
                deltas + 11,
                H + (i * wiener_win + 3) * wiener_win2 + i * wiener_win + 3);

            // Row 4: 3 points
            update_2_stats_sse2(
                H + (i * wiener_win + 3) * wiener_win2 + i * wiener_win + 3,
                delta2,
                H + (i * wiener_win + 4) * wiener_win2 + i * wiener_win + 4);
            H[(i * wiener_win + 4) * wiener_win2 + i * wiener_win + 6] =
                H[(i * wiener_win + 3) * wiener_win2 + i * wiener_win + 5] +
                _mm_extract_epi32(delta0, 2);

            // Row 5: 2 points
            update_2_stats_sse2(
                H + (i * wiener_win + 4) * wiener_win2 + i * wiener_win + 4,
                delta3,
                H + (i * wiener_win + 5) * wiener_win2 + i * wiener_win + 5);

            // Row 6: 1 points
            H[(i * wiener_win + 6) * wiener_win2 + i * wiener_win + 6] =
                H[(i * wiener_win + 5) * wiener_win2 + i * wiener_win + 5] +
                _mm_extract_epi32(delta1, 2);
        } else {
            // Row 1: 6 points
            hadd_update_6_stats_highbd_avx2(
                H + (i * wiener_win + 0) * wiener_win2 + i * wiener_win,
                deltas,
                H + (i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1);

            const __m256i delta0 = hadd_four_31_to_64_avx2(
                deltas[15], deltas[16], deltas[17], deltas[10]);
            const __m256i delta1 = hadd_four_31_to_64_avx2(
                deltas[18], deltas[19], deltas[20], deltas[20]);

            // Row 2: 5 points
            hadd_update_4_stats_highbd_avx2(
                H + (i * wiener_win + 1) * wiener_win2 + i * wiener_win + 1,
                deltas + 6,
                H + (i * wiener_win + 2) * wiener_win2 + i * wiener_win + 2);
            H[(i * wiener_win + 2) * wiener_win2 + i * wiener_win + 6] =
                H[(i * wiener_win + 1) * wiener_win2 + i * wiener_win + 5] +
                _mm256_extract_epi64(delta0, 3);

            // Row 3: 4 points
            hadd_update_4_stats_highbd_avx2(
                H + (i * wiener_win + 2) * wiener_win2 + i * wiener_win + 2,
                deltas + 11,
                H + (i * wiener_win + 3) * wiener_win2 + i * wiener_win + 3);

            // Row 4: 3 points
            update_2_stats_sse2(
                H + (i * wiener_win + 3) * wiener_win2 + i * wiener_win + 3,
                _mm256_extracti128_si256(delta0, 0),
                H + (i * wiener_win + 4) * wiener_win2 + i * wiener_win + 4);
            H[(i * wiener_win + 4) * wiener_win2 + i * wiener_win + 6] =
                H[(i * wiener_win + 3) * wiener_win2 + i * wiener_win + 5] +
                _mm256_extract_epi64(delta0, 2);

            // Row 5: 2 points
            update_2_stats_sse2(
                H + (i * wiener_win + 4) * wiener_win2 + i * wiener_win + 4,
                _mm256_extracti128_si256(delta1, 0),
                H + (i * wiener_win + 5) * wiener_win2 + i * wiener_win + 5);

            // Row 6: 1 points
            H[(i * wiener_win + 6) * wiener_win2 + i * wiener_win + 6] =
                H[(i * wiener_win + 5) * wiener_win2 + i * wiener_win + 5] +
                _mm256_extract_epi64(delta1, 2);
        }
    } while (++i < wiener_win);
}

void eb_av1_compute_stats_avx2(int32_t wiener_win, const uint8_t *dgd,
    const uint8_t *src, int32_t h_start, int32_t h_end, int32_t v_start,
    int32_t v_end, int32_t dgd_stride, int32_t src_stride, int64_t *M,
    int64_t *H) {
    const int32_t wiener_win2 = wiener_win * wiener_win;
    const int32_t wiener_halfwin = wiener_win >> 1;
    const uint8_t avg =
        find_average_avx2(dgd, h_start, h_end, v_start, v_end, dgd_stride);
    const int32_t width = h_end - h_start;
    const int32_t height = v_end - v_start;
    const int32_t d_stride = (width + 2 * wiener_halfwin + 15) & ~15;
    const int32_t s_stride = (width + 15) & ~15;
    int16_t *d, *s;

    // The maximum input size is width * height, which is
    // (9 / 4) * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX. Enlarge to
    // 3 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX considering
    // paddings.
    d = eb_aom_memalign(32,
            sizeof(*d) * 6 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX);
    s = d + 3 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX;

    assert(!(height % 2));

    sub_avg_block_avx2(src + v_start * src_stride + h_start,
                       src_stride,
                       avg,
                       width,
                       height,
                       s,
                       s_stride);
    sub_avg_block_avx2(dgd + (v_start - wiener_halfwin) * dgd_stride + h_start -
                           wiener_halfwin,
                       dgd_stride,
                       avg,
                       width + 2 * wiener_halfwin,
                       height + 2 * wiener_halfwin,
                       d,
                       d_stride);

    if (wiener_win == WIENER_WIN) {
        compute_stats_win7_avx2(
            d, d_stride, s, s_stride, width, height, M, H, 8);
    } else if (wiener_win == WIENER_WIN_CHROMA) {
        compute_stats_win5_avx2(
            d, d_stride, s, s_stride, width, height, M, H, 8);
    } else {
        assert(wiener_win == WIENER_WIN_3TAP);
        compute_stats_win3_avx2(
            d, d_stride, s, s_stride, width, height, M, H, 8);
    }

    // H is a symmetric matrix, so we only need to fill out the upper triangle.
    // We can copy it down to the lower triangle outside the (i, j) loops.
    diagonal_copy_stats_avx2(wiener_win2, H);

    eb_aom_free(d);
}

void eb_av1_compute_stats_highbd_avx2(int32_t wiener_win, const uint8_t *dgd8,
                                   const uint8_t *src8, int32_t h_start,
                                   int32_t h_end, int32_t v_start,
                                   int32_t v_end, int32_t dgd_stride,
                                   int32_t src_stride, int64_t *M, int64_t *H,
                                   AomBitDepth bit_depth) {
    const int32_t wiener_win2 = wiener_win * wiener_win;
    const int32_t wiener_halfwin = (wiener_win >> 1);
    const uint16_t *src = CONVERT_TO_SHORTPTR(src8);
    const uint16_t *dgd = CONVERT_TO_SHORTPTR(dgd8);
    const uint16_t avg = find_average_highbd_avx2(
        dgd, h_start, h_end, v_start, v_end, dgd_stride, bit_depth);
    const int32_t width = h_end - h_start;
    const int32_t height = v_end - v_start;
    const int32_t d_stride = (width + 2 * wiener_halfwin + 15) & ~15;
    const int32_t s_stride = (width + 15) & ~15;
    int32_t k;
    int16_t *d, *s;

    assert(!(height % 2));

    // The maximum input size is width * height, which is
    // (9 / 4) * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX. Enlarge to
    // 3 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX considering
    // paddings.
    d = eb_aom_memalign(32,
            sizeof(*d) * 6 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX);
    s = d + 3 * RESTORATION_UNITSIZE_MAX * RESTORATION_UNITSIZE_MAX;

    sub_avg_block_highbd_avx2(src + v_start * src_stride + h_start,
                              src_stride,
                              avg,
                              width,
                              height,
                              s,
                              s_stride);
    sub_avg_block_highbd_avx2(dgd + (v_start - wiener_halfwin) * dgd_stride +
                                  h_start - wiener_halfwin,
                              dgd_stride,
                              avg,
                              width + 2 * wiener_halfwin,
                              height + 2 * wiener_halfwin,
                              d,
                              d_stride);

    if (wiener_win == WIENER_WIN) {
        compute_stats_win7_avx2(
            d, d_stride, s, s_stride, width, height, M, H, bit_depth);
    } else if (wiener_win == WIENER_WIN_CHROMA) {
        compute_stats_win5_avx2(
            d, d_stride, s, s_stride, width, height, M, H, bit_depth);
    } else {
        assert(wiener_win == WIENER_WIN_3TAP);
        compute_stats_win3_avx2(
            d, d_stride, s, s_stride, width, height, M, H, bit_depth);
    }

    // H is a symmetric matrix, so we only need to fill out the upper triangle.
    // We can copy it down to the lower triangle outside the (i, j) loops.
    if (bit_depth == AOM_BITS_8) {
        diagonal_copy_stats_avx2(wiener_win2, H);
    } else if (bit_depth == AOM_BITS_10) {
        const int32_t k4 = wiener_win2 & ~3;

        k = 0;
        do {
            const __m256i src = _mm256_loadu_si256((__m256i *)(M + k));
            const __m256i dst = div4_avx2(src);
            _mm256_storeu_si256((__m256i *)(M + k), dst);
            H[k * wiener_win2 + k] /= 4;
            k += 4;
        } while (k < k4);

        H[k * wiener_win2 + k] /= 4;

        for (; k < wiener_win2; ++k) {
            M[k] /= 4;
        }

        div4_diagonal_copy_stats_avx2(wiener_win2, H);
    } else {
        const int32_t k4 = wiener_win2 & ~3;

        k = 0;
        do {
            const __m256i src = _mm256_loadu_si256((__m256i *)(M + k));
            const __m256i dst = div16_avx2(src);
            _mm256_storeu_si256((__m256i *)(M + k), dst);
            H[k * wiener_win2 + k] /= 16;
            k += 4;
        } while (k < k4);

        H[k * wiener_win2 + k] /= 16;

        for (; k < wiener_win2; ++k) {
            M[k] /= 16;
        }

        div16_diagonal_copy_stats_avx2(wiener_win2, H);
    }

    eb_aom_free(d);
}

static INLINE __m256i pair_set_epi16(uint16_t a, uint16_t b) {
    return _mm256_set1_epi32(
        (int32_t)(((uint16_t)(a)) | (((uint32_t)(b)) << 16)));
}

int64_t eb_av1_lowbd_pixel_proj_error_avx2(const uint8_t *src8, int32_t width,
    int32_t height, int32_t src_stride,
    const uint8_t *dat8,
    int32_t dat_stride, int32_t *flt0,
    int32_t flt0_stride, int32_t *flt1,
    int32_t flt1_stride, int32_t xq[2],
    const SgrParamsType *params) {
    const int32_t shift = SGRPROJ_RST_BITS + SGRPROJ_PRJ_BITS;
    const uint8_t *src = src8;
    const uint8_t *dat = dat8;
    int64_t err = 0;
    int32_t y = height;
    int32_t j;
    const __m256i rounding = _mm256_set1_epi32(1 << (shift - 1));
    __m256i sum64 = _mm256_setzero_si256();

    if (params->r[0] > 0 && params->r[1] > 0) {
        const __m256i xq_coeff = pair_set_epi16(xq[0], xq[1]);

        do {
            __m256i sum32 = _mm256_setzero_si256();

            for (j = 0; j <= width - 16; j += 16) {
                const __m256i d0 = _mm256_cvtepu8_epi16(xx_loadu_128(dat + j));
                const __m256i s0 = _mm256_cvtepu8_epi16(xx_loadu_128(src + j));
                const __m256i flt0_16b = _mm256_permute4x64_epi64(
                    _mm256_packs_epi32(yy_loadu_256(flt0 + j),
                        yy_loadu_256(flt0 + j + 8)),
                    0xd8);
                const __m256i flt1_16b = _mm256_permute4x64_epi64(
                    _mm256_packs_epi32(yy_loadu_256(flt1 + j),
                        yy_loadu_256(flt1 + j + 8)),
                    0xd8);
                const __m256i u0 = _mm256_slli_epi16(d0, SGRPROJ_RST_BITS);
                const __m256i flt0_0_sub_u = _mm256_sub_epi16(flt0_16b, u0);
                const __m256i flt1_0_sub_u = _mm256_sub_epi16(flt1_16b, u0);
                const __m256i v0 = _mm256_madd_epi16(
                    xq_coeff,
                    _mm256_unpacklo_epi16(flt0_0_sub_u, flt1_0_sub_u));
                const __m256i v1 = _mm256_madd_epi16(
                    xq_coeff,
                    _mm256_unpackhi_epi16(flt0_0_sub_u, flt1_0_sub_u));
                const __m256i vr0 =
                    _mm256_srai_epi32(_mm256_add_epi32(v0, rounding), shift);
                const __m256i vr1 =
                    _mm256_srai_epi32(_mm256_add_epi32(v1, rounding), shift);
                const __m256i e0 = _mm256_sub_epi16(
                    _mm256_add_epi16(_mm256_packs_epi32(vr0, vr1), d0), s0);
                const __m256i err0 = _mm256_madd_epi16(e0, e0);
                sum32 = _mm256_add_epi32(sum32, err0);
            }

            for (; j < width; ++j) {
                const int32_t u = (int32_t)(dat[j] << SGRPROJ_RST_BITS);
                int32_t v = xq[0] * (flt0[j] - u) + xq[1] * (flt1[j] - u);
                const int32_t e =
                    ROUND_POWER_OF_TWO(v, shift) + dat[j] - src[j];
                err += e * e;
            }

            dat += dat_stride;
            src += src_stride;
            flt0 += flt0_stride;
            flt1 += flt1_stride;
            const __m256i sum64_0 =
                _mm256_cvtepi32_epi64(_mm256_castsi256_si128(sum32));
            const __m256i sum64_1 =
                _mm256_cvtepi32_epi64(_mm256_extracti128_si256(sum32, 1));
            sum64 = _mm256_add_epi64(sum64, sum64_0);
            sum64 = _mm256_add_epi64(sum64, sum64_1);
        } while (--y);
    }
    else if (params->r[0] > 0 || params->r[1] > 0) {
        const int32_t xq_active = (params->r[0] > 0) ? xq[0] : xq[1];
        const __m256i xq_coeff =
            pair_set_epi16(xq_active, (-xq_active * (1 << SGRPROJ_RST_BITS)));
        const int32_t *flt = (params->r[0] > 0) ? flt0 : flt1;
        const int32_t flt_stride =
            (params->r[0] > 0) ? flt0_stride : flt1_stride;

        do {
            __m256i sum32 = _mm256_setzero_si256();

            for (j = 0; j <= width - 16; j += 16) {
                const __m256i d0 = _mm256_cvtepu8_epi16(xx_loadu_128(dat + j));
                const __m256i s0 = _mm256_cvtepu8_epi16(xx_loadu_128(src + j));
                const __m256i flt_16b = _mm256_permute4x64_epi64(
                    _mm256_packs_epi32(yy_loadu_256(flt + j),
                        yy_loadu_256(flt + j + 8)),
                    0xd8);
                const __m256i v0 = _mm256_madd_epi16(
                    xq_coeff, _mm256_unpacklo_epi16(flt_16b, d0));
                const __m256i v1 = _mm256_madd_epi16(
                    xq_coeff, _mm256_unpackhi_epi16(flt_16b, d0));
                const __m256i vr0 =
                    _mm256_srai_epi32(_mm256_add_epi32(v0, rounding), shift);
                const __m256i vr1 =
                    _mm256_srai_epi32(_mm256_add_epi32(v1, rounding), shift);
                const __m256i e0 = _mm256_sub_epi16(
                    _mm256_add_epi16(_mm256_packs_epi32(vr0, vr1), d0), s0);
                const __m256i err0 = _mm256_madd_epi16(e0, e0);
                sum32 = _mm256_add_epi32(sum32, err0);
            }

            for (; j < width; ++j) {
                const int32_t u = (int32_t)(dat[j] << SGRPROJ_RST_BITS);
                int32_t v = xq_active * (flt[j] - u);
                const int32_t e =
                    ROUND_POWER_OF_TWO(v, shift) + dat[j] - src[j];
                err += e * e;
            }

            dat += dat_stride;
            src += src_stride;
            flt += flt_stride;
            const __m256i sum64_0 =
                _mm256_cvtepi32_epi64(_mm256_castsi256_si128(sum32));
            const __m256i sum64_1 =
                _mm256_cvtepi32_epi64(_mm256_extracti128_si256(sum32, 1));
            sum64 = _mm256_add_epi64(sum64, sum64_0);
            sum64 = _mm256_add_epi64(sum64, sum64_1);
        } while (--y);
    }
    else {
        __m256i sum32 = _mm256_setzero_si256();

        do {
            for (j = 0; j <= width - 16; j += 16) {
                const __m256i d0 = _mm256_cvtepu8_epi16(xx_loadu_128(dat + j));
                const __m256i s0 = _mm256_cvtepu8_epi16(xx_loadu_128(src + j));
                const __m256i diff0 = _mm256_sub_epi16(d0, s0);
                const __m256i err0 = _mm256_madd_epi16(diff0, diff0);
                sum32 = _mm256_add_epi32(sum32, err0);
            }

            for (; j < width; ++j) {
                const int32_t e = (int32_t)(dat[j]) - src[j];
                err += e * e;
            }

            dat += dat_stride;
            src += src_stride;
        } while (--y);

        const __m256i sum64_0 =
            _mm256_cvtepi32_epi64(_mm256_castsi256_si128(sum32));
        const __m256i sum64_1 =
            _mm256_cvtepi32_epi64(_mm256_extracti128_si256(sum32, 1));
        sum64 = _mm256_add_epi64(sum64_0, sum64_1);
    }

    return err + _mm_cvtsi128_si64(hadd_64_avx2(sum64));
}

int64_t eb_av1_highbd_pixel_proj_error_avx2(
    const uint8_t *src8, int32_t width, int32_t height, int32_t src_stride,
    const uint8_t *dat8, int32_t dat_stride, int32_t *flt0, int32_t flt0_stride,
    int32_t *flt1, int32_t flt1_stride, int32_t xq[2], const SgrParamsType *params) {
    int32_t i, j, k;
    const int32_t shift = SGRPROJ_RST_BITS + SGRPROJ_PRJ_BITS;
    const __m256i rounding = _mm256_set1_epi32(1 << (shift - 1));
    __m256i sum64 = _mm256_setzero_si256();
    const uint16_t *src = CONVERT_TO_SHORTPTR(src8);
    const uint16_t *dat = CONVERT_TO_SHORTPTR(dat8);
    int64_t err = 0;
    if (params->r[0] > 0 && params->r[1] > 0) {  // Both filters are enabled
        const __m256i xq0 = _mm256_set1_epi32(xq[0]);
        const __m256i xq1 = _mm256_set1_epi32(xq[1]);
        for (i = 0; i < height; ++i) {
            __m256i sum32 = _mm256_setzero_si256();
            for (j = 0; j <= width - 16; j += 16) {  // Process 16 pixels at a time
              // Load 16 pixels each from source image and corrupted image
                const __m256i s0 = yy_loadu_256(src + j);
                const __m256i d0 = yy_loadu_256(dat + j);
                // s0 = [15 14 13 12 11 10 9 8] [7 6 5 4 3 2 1 0] as u16 (indices)

                // Shift-up each pixel to match filtered image scaling
                const __m256i u0 = _mm256_slli_epi16(d0, SGRPROJ_RST_BITS);

                // Split u0 into two halves and pad each from u16 to i32
                const __m256i u0l = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(u0));
                const __m256i u0h =
                    _mm256_cvtepu16_epi32(_mm256_extracti128_si256(u0, 1));
                // u0h, u0l = [15 14 13 12] [11 10 9 8], [7 6 5 4] [3 2 1 0] as u32

                // Load 16 pixels from each filtered image
                const __m256i flt0l = yy_loadu_256(flt0 + j);
                const __m256i flt0h = yy_loadu_256(flt0 + j + 8);
                const __m256i flt1l = yy_loadu_256(flt1 + j);
                const __m256i flt1h = yy_loadu_256(flt1 + j + 8);
                // flt?l, flt?h = [15 14 13 12] [11 10 9 8], [7 6 5 4] [3 2 1 0] as u32

                // Subtract shifted corrupt image from each filtered image
                const __m256i flt0l_subu = _mm256_sub_epi32(flt0l, u0l);
                const __m256i flt0h_subu = _mm256_sub_epi32(flt0h, u0h);
                const __m256i flt1l_subu = _mm256_sub_epi32(flt1l, u0l);
                const __m256i flt1h_subu = _mm256_sub_epi32(flt1h, u0h);

                // Multiply basis vectors by appropriate coefficients
                const __m256i v0l = _mm256_mullo_epi32(flt0l_subu, xq0);
                const __m256i v0h = _mm256_mullo_epi32(flt0h_subu, xq0);
                const __m256i v1l = _mm256_mullo_epi32(flt1l_subu, xq1);
                const __m256i v1h = _mm256_mullo_epi32(flt1h_subu, xq1);

                // Add together the contributions from the two basis vectors
                const __m256i vl = _mm256_add_epi32(v0l, v1l);
                const __m256i vh = _mm256_add_epi32(v0h, v1h);

                // Right-shift v with appropriate rounding
                const __m256i vrl =
                    _mm256_srai_epi32(_mm256_add_epi32(vl, rounding), shift);
                const __m256i vrh =
                    _mm256_srai_epi32(_mm256_add_epi32(vh, rounding), shift);
                // vrh, vrl = [15 14 13 12] [11 10 9 8], [7 6 5 4] [3 2 1 0]

                // Saturate each i32 to an i16 then combine both halves
                // The permute (control=[3 1 2 0]) fixes weird ordering from AVX lanes
                const __m256i vr =
                    _mm256_permute4x64_epi64(_mm256_packs_epi32(vrl, vrh), 0xd8);
                // intermediate = [15 14 13 12 7 6 5 4] [11 10 9 8 3 2 1 0]
                // vr = [15 14 13 12 11 10 9 8] [7 6 5 4 3 2 1 0]

                // Add twin-subspace-sgr-filter to corrupt image then subtract source
                const __m256i e0 = _mm256_sub_epi16(_mm256_add_epi16(vr, d0), s0);

                // Calculate squared error and add adjacent values
                const __m256i err0 = _mm256_madd_epi16(e0, e0);

                sum32 = _mm256_add_epi32(sum32, err0);
            }

            const __m256i sum32l =
                _mm256_cvtepu32_epi64(_mm256_castsi256_si128(sum32));
            sum64 = _mm256_add_epi64(sum64, sum32l);
            const __m256i sum32h =
                _mm256_cvtepu32_epi64(_mm256_extracti128_si256(sum32, 1));
            sum64 = _mm256_add_epi64(sum64, sum32h);

            // Process remaining pixels in this row (modulo 16)
            for (k = j; k < width; ++k) {
                const int32_t u = (int32_t)(dat[k] << SGRPROJ_RST_BITS);
                int32_t v = xq[0] * (flt0[k] - u) + xq[1] * (flt1[k] - u);
                const int32_t e = ROUND_POWER_OF_TWO(v, shift) + dat[k] - src[k];
                err += e * e;
            }
            dat += dat_stride;
            src += src_stride;
            flt0 += flt0_stride;
            flt1 += flt1_stride;
        }
    }
    else if (params->r[0] > 0 || params->r[1] > 0) {  // Only one filter enabled
        const int32_t xq_on = (params->r[0] > 0) ? xq[0] : xq[1];
        const __m256i xq_active = _mm256_set1_epi32(xq_on);
        const __m256i xq_inactive =
            _mm256_set1_epi32(-xq_on * (1 << SGRPROJ_RST_BITS));
        const int32_t *flt = (params->r[0] > 0) ? flt0 : flt1;
        const int32_t flt_stride = (params->r[0] > 0) ? flt0_stride : flt1_stride;
        for (i = 0; i < height; ++i) {
            __m256i sum32 = _mm256_setzero_si256();
            for (j = 0; j <= width - 16; j += 16) {
                // Load 16 pixels from source image
                const __m256i s0 = yy_loadu_256(src + j);
                // s0 = [15 14 13 12 11 10 9 8] [7 6 5 4 3 2 1 0] as u16

                // Load 16 pixels from corrupted image and pad each u16 to i32
                const __m256i d0 = yy_loadu_256(dat + j);
                const __m256i d0h =
                    _mm256_cvtepu16_epi32(_mm256_extracti128_si256(d0, 1));
                const __m256i d0l = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(d0));
                // d0 = [15 14 13 12 11 10 9 8] [7 6 5 4 3 2 1 0] as u16
                // d0h, d0l = [15 14 13 12] [11 10 9 8], [7 6 5 4] [3 2 1 0] as i32

                // Load 16 pixels from the filtered image
                const __m256i flth = yy_loadu_256(flt + j + 8);
                const __m256i fltl = yy_loadu_256(flt + j);
                // flth, fltl = [15 14 13 12] [11 10 9 8], [7 6 5 4] [3 2 1 0] as i32

                const __m256i flth_xq = _mm256_mullo_epi32(flth, xq_active);
                const __m256i fltl_xq = _mm256_mullo_epi32(fltl, xq_active);
                const __m256i d0h_xq = _mm256_mullo_epi32(d0h, xq_inactive);
                const __m256i d0l_xq = _mm256_mullo_epi32(d0l, xq_inactive);

                const __m256i vh = _mm256_add_epi32(flth_xq, d0h_xq);
                const __m256i vl = _mm256_add_epi32(fltl_xq, d0l_xq);

                // Shift this down with appropriate rounding
                const __m256i vrh =
                    _mm256_srai_epi32(_mm256_add_epi32(vh, rounding), shift);
                const __m256i vrl =
                    _mm256_srai_epi32(_mm256_add_epi32(vl, rounding), shift);
                // vrh, vrl = [15 14 13 12] [11 10 9 8], [7 6 5 4] [3 2 1 0] as i32

                // Saturate each i32 to an i16 then combine both halves
                // The permute (control=[3 1 2 0]) fixes weird ordering from AVX lanes
                const __m256i vr =
                    _mm256_permute4x64_epi64(_mm256_packs_epi32(vrl, vrh), 0xd8);
                // intermediate = [15 14 13 12 7 6 5 4] [11 10 9 8 3 2 1 0] as u16
                // vr = [15 14 13 12 11 10 9 8] [7 6 5 4 3 2 1 0] as u16

                // Subtract twin-subspace-sgr filtered from source image to get error
                const __m256i e0 = _mm256_sub_epi16(_mm256_add_epi16(vr, d0), s0);

                // Calculate squared error and add adjacent values
                const __m256i err0 = _mm256_madd_epi16(e0, e0);

                sum32 = _mm256_add_epi32(sum32, err0);
            }

            const __m256i sum32l =
                _mm256_cvtepu32_epi64(_mm256_castsi256_si128(sum32));
            sum64 = _mm256_add_epi64(sum64, sum32l);
            const __m256i sum32h =
                _mm256_cvtepu32_epi64(_mm256_extracti128_si256(sum32, 1));
            sum64 = _mm256_add_epi64(sum64, sum32h);

            // Process remaining pixels in this row (modulo 16)
            for (k = j; k < width; ++k) {
                const int32_t u = (int32_t)(dat[k] << SGRPROJ_RST_BITS);
                int32_t v = xq_on * (flt[k] - u);
                const int32_t e = ROUND_POWER_OF_TWO(v, shift) + dat[k] - src[k];
                err += e * e;
            }
            dat += dat_stride;
            src += src_stride;
            flt += flt_stride;
        }
    }
    else {  // Neither filter is enabled
        for (i = 0; i < height; ++i) {
            __m256i sum32 = _mm256_setzero_si256();
            for (j = 0; j <= width - 32; j += 32) {
                // Load 2x16 u16 from source image
                const __m256i s0l = yy_loadu_256(src + j);
                const __m256i s0h = yy_loadu_256(src + j + 16);

                // Load 2x16 u16 from corrupted image
                const __m256i d0l = yy_loadu_256(dat + j);
                const __m256i d0h = yy_loadu_256(dat + j + 16);

                // Subtract corrupted image from source image
                const __m256i diffl = _mm256_sub_epi16(d0l, s0l);
                const __m256i diffh = _mm256_sub_epi16(d0h, s0h);

                // Square error and add adjacent values
                const __m256i err0l = _mm256_madd_epi16(diffl, diffl);
                const __m256i err0h = _mm256_madd_epi16(diffh, diffh);

                sum32 = _mm256_add_epi32(sum32, err0l);
                sum32 = _mm256_add_epi32(sum32, err0h);
            }

            const __m256i sum32l =
                _mm256_cvtepu32_epi64(_mm256_castsi256_si128(sum32));
            sum64 = _mm256_add_epi64(sum64, sum32l);
            const __m256i sum32h =
                _mm256_cvtepu32_epi64(_mm256_extracti128_si256(sum32, 1));
            sum64 = _mm256_add_epi64(sum64, sum32h);

            // Process remaining pixels (modulu 16)
            for (k = j; k < width; ++k) {
                const int32_t e = (int32_t)(dat[k]) - src[k];
                err += e * e;
            }
            dat += dat_stride;
            src += src_stride;
        }
    }

    // Sum 4 values from sum64l and sum64h into err
    int64_t sum[4];
    yy_storeu_256(sum, sum64);
    err += sum[0] + sum[1] + sum[2] + sum[3];
    return err;
}
// clang-format on
