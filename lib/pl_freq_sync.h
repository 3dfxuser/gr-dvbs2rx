/* -*- c++ -*- */
/*
 * Copyright (c) 2019-2021 Igor Freire
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_DVBS2RX_PL_FREQ_SYNC_H
#define INCLUDED_DVBS2RX_PL_FREQ_SYNC_H

#include <gnuradio/gr_complex.h>
#include <dvbs2rx/api.h>
#include <volk/volk_alloc.hh>

const double fine_foffset_corr_range = 3.268e-4;
/* When only the pilot blocks are use for fine offset estimation, the normalized
 * frequency offset that the fine frequency estimator can "observe" is upper
 * limited to:
 *
 * 1/(2*(1440 + 36)) = 3.3875e-4
 *
 * When the SOF/PLHEADER phase is also used within the fine offset estimation,
 * the upper limit changes. The rationale is that the first phase difference
 * interval spans (1440 + 90) symbol periods. Consequently, the maximum
 * frequency offset that this interval can observe becomes:
 *
 * 1/(2*(1440 + 90)) = 3.268e-4
 *
 * Here, we adopt the latter approach. When the frequency offset exceeds
 * 3.268e-4, the phase changes by more than +-pi from pilot segment to pilot
 * segment. Consequently, the fine estimation approach does not work.
 */

namespace gr {
namespace dvbs2rx {

/**
 * @brief Frequency Synchronizer
 *
 * Provides methods to estimate the coarse and fine frequency offsets disturbing DVB-S2
 * frames, as well as methods to estimate the phases of various frame segments (SOF,
 * PLHEADER, and pilot blocks). These methods are meant to be used in conjunction with an
 * external frequency correction (or de-rotator/rotator) block. This class supplies the
 * frequency offset estimates, while the external block applies the corrections, an
 * operation denominated "closed-loop mode". In other words, this class is not responsible
 * for frequency offset correction. Instead, it focuses on estimation only.
 *
 * Due to the closed-loop operation, when estimating the phases of the SOF, PLHEADER, and
 * pilot blocks, this class assumes the symbols are not rotating. This assumption holds
 * closely as soon as the external rotator block converges to an accurate frequency
 * correction. Thus, the phase estimates are obtained by assuming the symbols are only
 * disturbed by white Gaussian noise. The only exception is on the `derotate_plheader()`
 * method, which offers an "open-loop" option, documented there.
 *
 * Once the frequency offset estimates are accurate enough, the external derotator block
 * applies accurate corrections and the frequency offset observed by this block becomes
 * sufficiently low. Moreover, once the normalized frequency offset magnitude falls below
 * 3.268e-4, this class infers the system is already "coarse-corrected", and the
 * corresponding state can be fetched through the `is_coarse_corrected()` method. At this
 * point, it makes sense to start computing the fine frequency offset estimate. Before
 * that, the fine frequency offset estimates are not reliable.
 *
 * Once a fine frequency offset becomes available, this class returns true on method
 * `has_fine_foffset_est()`. As of this version, a fine offset can be computed whenever
 * the processed DVB-S2 frames contain pilot blocks and the system is already
 * coarse-corrected. The estimate is based on the independent phases of each pilot block
 * composing the frame, each estimated through method `estimate_pilot_phase()`. After all
 * pilot block phases have been estimated, the fine frequency offset estimate can be
 * obtained by calling method `estimate_fine_pilot_mode()`.
 *
 * In contrast, the coarse frequency offset can be computed regardless of the presence of
 * pilots. Also, unlike the fine frequency offset estimation, which is computed and
 * refreshed on every frame, the coarse estimation is based on several consecutive frames.
 * The number of frames considered in the computation is determined by the `period`
 * parameter provided to the constructor.
 *
 * In any case, the most recent coarse and fine frequency offset estimates can be fetched
 * independently through the `get_coarse_foffset()` and `get_fine_foffset()` methods.
 *
 */
class DVBS2RX_API freq_sync
{
private:
    /* Parameters */
    int debug_level;     /**< debug level */
    unsigned int period; /**< estimation periodicity in frames */

    /* Coarse frequency offset estimation state */
    double coarse_foffset; /**< most recent freq. offset estimate */
    unsigned int i_frame;  /**< frame counter */
    unsigned int N;        /**< "preamble" length */
    unsigned int L;        /**< used phase differentials (<= N) */
    bool coarse_corrected; /**< residual offset is sufficiently low */

    /* NOTE: In principle, we could make N equal to the SOF length (26) and L =
     * N-1 (i.e. 25), in which case coarse frequency offset estimation would be
     * based on the SOF symbols only and would not require decoding of the
     * PLSC. However, this would waste all the other 64 known PLHEADER symbols,
     * which can improve coarse estimation performance substantially. So N in
     * the end will be set as 90 and L to 89. Nonetheless, the variables are
     * kept here for flexibility on experiments. */

    /* Fine frequency offset estimation state */
    double fine_foffset;
    float w_angle_avg;   /**< weighted angle average */
    bool fine_est_ready; /**< whether a fine estimate is available/initialized */

    /* Volk buffers */
    volk::vector<gr_complex> plheader_conj; /**< conjugate of PLHEADER symbols */
    volk::vector<gr_complex> pilot_mod_rm;  /**< modulation-removed received pilots */
    volk::vector<gr_complex> pp_sof;        /**< derotated SOF symbols */
    volk::vector<gr_complex> pp_plheader;   /**< derotated PLHEADER symbols */

    /* Coarse estimation only */
    volk::vector<gr_complex> pilot_corr;   /**< mod-removed autocorrelation */
    volk::vector<float> angle_corr;        /**< autocorrelation angles */
    volk::vector<float> angle_diff;        /**< angle differences */
    volk::vector<float> w_window_f;        /**< weight window for the full PLHEADER */
    volk::vector<float> w_window_s;        /**< weight window for the SOF only  */
    volk::vector<float> w_angle_diff;      /**< weighted angle differences */
    volk::vector<gr_complex> unmod_pilots; /**< conjugate of un-modulated pilots */

    /* Fine estimation only */
    volk::vector<float> angle_pilot;  /**< average angle of pilot segments */
    volk::vector<float> angle_diff_f; /**< diff of average pilot angles */

public:
    /**
     * \brief Construct the frequency synchronizer object.
     *
     * \param period Interval in PLFRAMEs between coarse frequency offset
     *               estimations.
     * \param debug_level Debugging log level (0 disables logs).
     */
    freq_sync(unsigned int period, int debug_level);

    /**
     * \brief Data-aided coarse frequency offset estimation.
     *
     * The implementation accumulates `period` frames before outputting an
     * estimate, where `period` comes from the parameter provided to the
     * constructor.
     *
     * \param in (gr_complex *) Pointer to the start of frame.
     * \param full (bool) Whether to use the full PLHEADER for the
     *                    estimation. When set to false, only the SOF symbols
     *                    are used. Otherwise, the full PLHEADER is used and the
     *                    PLSC dataword must be indicated so that the correct
     *                    PLHEADER sequence is used by the data-aided estimator.
     * \param plsc (uint8_t) PLSC corresponding to the PLHEADER being
     *                       processed. Must be within the range from 0 to 127.
     *                       It is ignored if full=false.
     * \return (bool) Whether a new estimate was computed in this iteration.
     *
     * \note The coarse frequency offset estimate is kept internally. It can be
     * fetched using the `get_coarse_foffset()` method.
     */
    bool estimate_coarse(const gr_complex* in, bool full, uint8_t plsc = 0);

    /**
     * \brief Estimate the average phase of the SOF.
     * \param in (gr_complex *) Pointer to the SOF symbol array.
     * \return (float) The phase estimate in radians within -pi to +pi.
     */
    float estimate_sof_phase(const gr_complex* in);

    /**
     * \brief Estimate the average phase of the PLHEADER.
     * \param in (gr_complex *) Pointer to the PLHEADER symbol array.
     * \param plsc (uint8_t) PLSC corresponding to the PLHEADER being
     *                       processed. Must be within the range from 0 to 127.
     * \return (float) The phase estimate within -pi to +pi.
     *
     * \note plsc indicates the expected PLHEADER symbols so that the
     * phase estimation can be fully data-aided.
     */
    float estimate_plheader_phase(const gr_complex* in, uint8_t plsc);

    /**
     * \brief Estimate the average phase of a pilot block.
     * \param in (gr_complex *) Pointer to the pilot symbol array.
     * \param i_blk (int) Index of this pilot block within the PLFRAME
     * \return (float) The phase estimate within -pi to +pi.
     *
     * \note The gr_complex array pointed `in` is expected to contain
     * the PLHEADER within its first 90 positions, then all 36-symbol
     * pilot blocks consecutively in the indexes that follow. The pilot
     * block index `i_blk` is used internally in order to fetch the
     * correct input pilots for phase estimation. The result will be
     * stored in an internal pilot angle buffer.
     */
    void estimate_pilot_phase(const gr_complex* in, int i_blk);

    /**
     * \brief Pilot-aided fine frequency offset estimation.
     *
     * Should be executed only for PLFRAMEs containing pilot symbols.
     *
     * \param n_pilot_blks (uint8_t) Number of pilot blocks in the PLFRAME being
     *                               processed.
     * \return Void.
     *
     * \note The fine frequency offset estimate is kept internally. It can be
     * fetched using the `get_fine_foffset()` method.
     */
    void estimate_fine_pilot_mode(uint8_t n_pilot_blks);

    /**
     * \brief De-rotate PLHEADER symbols.
     *
     * \param in (const gr_complex *) Input rotated PLHEADER buffer.
     * \param open_loop (bool) Whether to assume this block is running in open
     * loop, without an external frequency correction block. In this case, it is
     * assumed the most recent frequency offset estimate is still uncorrected
     * and disturbing the input PLHEADER, so this method attempts to compensate
     * for this frequency offset when derotating the PLHEADER.
     *
     * \return Void.
     *
     * \note The de-rotated PLHEADER is saved internally and can be accessed
     * using the `get_plheader()` method.
     *
     * \note The open-loop option is useful when there is too much uncertainty about the
     * the frequency offset estimate, for example while still searching for a DVB-S2
     * signal. By running `derotate_plheader()` in open loop, only the PLHEADER will be
     * derotated based on the internal frequency offset estimate, with no need to send the
     * estimate to an external rotator block. At a minimum, if this derotation is
     * successful, it can be determinant for a successful PLSC decoding, which then leads
     * to frame locking. After that, the caller can be more certain about the frequency
     * offset estimates being valid and switch to the usual closed-loop operation, while
     * sending the frequency offset estimates to the external rotator block.
     */
    void derotate_plheader(const gr_complex* in, bool open_loop = false);

    /**
     * \brief Get the last PLHEADER phase estimate.
     *
     * The estimate is kept internally after a call to the
     * `estimate_plheader_phase()` method.
     *
     * \return (float) Last PLHEADER phase estimate in radians within -pi to +pi.
     */
    float get_plheader_phase() { return angle_pilot[0]; }

    /**
     * \brief Get the phase estimate corresponding to a pilot block.
     * \param i_blk (int) Pilot block index from 0 up to 21.
     * \return (float) Phase estimate in radians within -pi to +pi.
     */
    float get_pilot_phase(int i_blk) { return angle_pilot[i_blk + 1]; }

    /**
     * \brief Get the last coarse frequency offset estimate.
     *
     * The estimate is kept internally after a call to the
     * `estimate_coarse()` method.
     *
     * \return (double) Last normalized coarse frequency offset estimate.
     */
    double get_coarse_foffset() { return coarse_foffset; }

    /**
     * \brief Get the last fine frequency offset estimate.
     *
     * The estimate is kept internally after a call to the
     * `estimate_fine_pilot_mode()` method.
     *
     * \return (double) Last normalized fine frequency offset estimate.
     */
    double get_fine_foffset() { return fine_foffset; }

    /**
     * \brief Check whether the coarse frequency correction has been achieved.
     *
     * The coarse corrected state is considered achieved when the coarse
     * frequency offset estimate falls within the fine frequency offset
     * estimation range.
     *
     * \return (bool) Coarse corrected state.
     */
    bool is_coarse_corrected() { return coarse_corrected; }

    /**
     * \brief Check whether a fine frequency offset estimate is available already.
     *
     * An estimate becomes available internally after a call to the
     * `estimate_fine_pilot_mode()` method.
     *
     * \return (bool) True when a fine frequency offset estimate is available.
     */
    bool has_fine_foffset_est() { return fine_est_ready; }

    /**
     * \brief Get the post-processed/de-rotated PLHEADER kept internally.
     *
     * A de-rotated version of the PLHEADER is stored internally after a call to
     * the `derotate_plheader()` method.
     *
     * \return (const gr_complex*) Pointer to the de-rotated PLHEADER.
     */
    const gr_complex* get_plheader() { return pp_plheader.data(); }
};

} // namespace dvbs2rx
} // namespace gr

#endif /* INCLUDED_DVBS2RX_PL_FREQ_SYNC_H */
