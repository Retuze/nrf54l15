cmake_minimum_required(VERSION 3.16)
project(opus_cm33 LANGUAGES C)

# ============================================================
# Path to the unmodified Opus source tree
# ============================================================
set(OPUS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../audioopus")

# ============================================================
# CELT core sources (no ARM/x86/MIPS intrinsics)
# ============================================================
set(CELT_SOURCES
    ${OPUS_ROOT}/celt/bands.c
    ${OPUS_ROOT}/celt/celt.c
    ${OPUS_ROOT}/celt/celt_encoder.c
    ${OPUS_ROOT}/celt/celt_decoder.c
    ${OPUS_ROOT}/celt/cwrs.c
    ${OPUS_ROOT}/celt/entcode.c
    ${OPUS_ROOT}/celt/entdec.c
    ${OPUS_ROOT}/celt/entenc.c
    ${OPUS_ROOT}/celt/kiss_fft.c
    ${OPUS_ROOT}/celt/laplace.c
    ${OPUS_ROOT}/celt/mathops.c
    ${OPUS_ROOT}/celt/mdct.c
    ${OPUS_ROOT}/celt/modes.c
    ${OPUS_ROOT}/celt/pitch.c
    ${OPUS_ROOT}/celt/celt_lpc.c
    ${OPUS_ROOT}/celt/quant_bands.c
    ${OPUS_ROOT}/celt/rate.c
    ${OPUS_ROOT}/celt/vq.c
)

# ============================================================
# SILK common sources (no _FIX, no platform intrinsics)
# ============================================================
set(SILK_COMMON_SOURCES
    ${OPUS_ROOT}/silk/CNG.c
    ${OPUS_ROOT}/silk/code_signs.c
    ${OPUS_ROOT}/silk/init_decoder.c
    ${OPUS_ROOT}/silk/decode_core.c
    ${OPUS_ROOT}/silk/decode_frame.c
    ${OPUS_ROOT}/silk/decode_parameters.c
    ${OPUS_ROOT}/silk/decode_indices.c
    ${OPUS_ROOT}/silk/decode_pulses.c
    ${OPUS_ROOT}/silk/decoder_set_fs.c
    ${OPUS_ROOT}/silk/dec_API.c
    ${OPUS_ROOT}/silk/enc_API.c
    ${OPUS_ROOT}/silk/encode_indices.c
    ${OPUS_ROOT}/silk/encode_pulses.c
    ${OPUS_ROOT}/silk/gain_quant.c
    ${OPUS_ROOT}/silk/interpolate.c
    ${OPUS_ROOT}/silk/LP_variable_cutoff.c
    ${OPUS_ROOT}/silk/NLSF_decode.c
    ${OPUS_ROOT}/silk/NSQ.c
    ${OPUS_ROOT}/silk/NSQ_del_dec.c
    ${OPUS_ROOT}/silk/PLC.c
    ${OPUS_ROOT}/silk/shell_coder.c
    ${OPUS_ROOT}/silk/tables_gain.c
    ${OPUS_ROOT}/silk/tables_LTP.c
    ${OPUS_ROOT}/silk/tables_NLSF_CB_NB_MB.c
    ${OPUS_ROOT}/silk/tables_NLSF_CB_WB.c
    ${OPUS_ROOT}/silk/tables_other.c
    ${OPUS_ROOT}/silk/tables_pitch_lag.c
    ${OPUS_ROOT}/silk/tables_pulses_per_block.c
    ${OPUS_ROOT}/silk/VAD.c
    ${OPUS_ROOT}/silk/control_audio_bandwidth.c
    ${OPUS_ROOT}/silk/quant_LTP_gains.c
    ${OPUS_ROOT}/silk/VQ_WMat_EC.c
    ${OPUS_ROOT}/silk/HP_variable_cutoff.c
    ${OPUS_ROOT}/silk/NLSF_encode.c
    ${OPUS_ROOT}/silk/NLSF_VQ.c
    ${OPUS_ROOT}/silk/NLSF_unpack.c
    ${OPUS_ROOT}/silk/NLSF_del_dec_quant.c
    ${OPUS_ROOT}/silk/process_NLSFs.c
    ${OPUS_ROOT}/silk/stereo_LR_to_MS.c
    ${OPUS_ROOT}/silk/stereo_MS_to_LR.c
    ${OPUS_ROOT}/silk/check_control_input.c
    ${OPUS_ROOT}/silk/control_SNR.c
    ${OPUS_ROOT}/silk/init_encoder.c
    ${OPUS_ROOT}/silk/control_codec.c
    ${OPUS_ROOT}/silk/A2NLSF.c
    ${OPUS_ROOT}/silk/ana_filt_bank_1.c
    ${OPUS_ROOT}/silk/biquad_alt.c
    ${OPUS_ROOT}/silk/bwexpander_32.c
    ${OPUS_ROOT}/silk/bwexpander.c
    ${OPUS_ROOT}/silk/debug.c
    ${OPUS_ROOT}/silk/decode_pitch.c
    ${OPUS_ROOT}/silk/inner_prod_aligned.c
    ${OPUS_ROOT}/silk/lin2log.c
    ${OPUS_ROOT}/silk/log2lin.c
    ${OPUS_ROOT}/silk/LPC_analysis_filter.c
    ${OPUS_ROOT}/silk/LPC_inv_pred_gain.c
    ${OPUS_ROOT}/silk/table_LSF_cos.c
    ${OPUS_ROOT}/silk/NLSF2A.c
    ${OPUS_ROOT}/silk/NLSF_stabilize.c
    ${OPUS_ROOT}/silk/NLSF_VQ_weights_laroia.c
    ${OPUS_ROOT}/silk/pitch_est_tables.c
    ${OPUS_ROOT}/silk/resampler.c
    ${OPUS_ROOT}/silk/resampler_down2_3.c
    ${OPUS_ROOT}/silk/resampler_down2.c
    ${OPUS_ROOT}/silk/resampler_private_AR2.c
    ${OPUS_ROOT}/silk/resampler_private_down_FIR.c
    ${OPUS_ROOT}/silk/resampler_private_IIR_FIR.c
    ${OPUS_ROOT}/silk/resampler_private_up2_HQ.c
    ${OPUS_ROOT}/silk/resampler_rom.c
    ${OPUS_ROOT}/silk/sigm_Q15.c
    ${OPUS_ROOT}/silk/sort.c
    ${OPUS_ROOT}/silk/sum_sqr_shift.c
    ${OPUS_ROOT}/silk/stereo_decode_pred.c
    ${OPUS_ROOT}/silk/stereo_encode_pred.c
    ${OPUS_ROOT}/silk/stereo_find_predictor.c
    ${OPUS_ROOT}/silk/stereo_quant_pred.c
    ${OPUS_ROOT}/silk/LPC_fit.c
)

# ============================================================
# SILK float sources (FLP path — not fixed point)
# ============================================================
set(SILK_FLOAT_SOURCES
    ${OPUS_ROOT}/silk/float/apply_sine_window_FLP.c
    ${OPUS_ROOT}/silk/float/corrMatrix_FLP.c
    ${OPUS_ROOT}/silk/float/encode_frame_FLP.c
    ${OPUS_ROOT}/silk/float/find_LPC_FLP.c
    ${OPUS_ROOT}/silk/float/find_LTP_FLP.c
    ${OPUS_ROOT}/silk/float/find_pitch_lags_FLP.c
    ${OPUS_ROOT}/silk/float/find_pred_coefs_FLP.c
    ${OPUS_ROOT}/silk/float/LPC_analysis_filter_FLP.c
    ${OPUS_ROOT}/silk/float/LTP_analysis_filter_FLP.c
    ${OPUS_ROOT}/silk/float/LTP_scale_ctrl_FLP.c
    ${OPUS_ROOT}/silk/float/noise_shape_analysis_FLP.c
    ${OPUS_ROOT}/silk/float/process_gains_FLP.c
    ${OPUS_ROOT}/silk/float/regularize_correlations_FLP.c
    ${OPUS_ROOT}/silk/float/residual_energy_FLP.c
    ${OPUS_ROOT}/silk/float/warped_autocorrelation_FLP.c
    ${OPUS_ROOT}/silk/float/wrappers_FLP.c
    ${OPUS_ROOT}/silk/float/autocorrelation_FLP.c
    ${OPUS_ROOT}/silk/float/burg_modified_FLP.c
    ${OPUS_ROOT}/silk/float/bwexpander_FLP.c
    ${OPUS_ROOT}/silk/float/energy_FLP.c
    ${OPUS_ROOT}/silk/float/inner_product_FLP.c
    ${OPUS_ROOT}/silk/float/k2a_FLP.c
    ${OPUS_ROOT}/silk/float/LPC_inv_pred_gain_FLP.c
    ${OPUS_ROOT}/silk/float/pitch_analysis_core_FLP.c
    ${OPUS_ROOT}/silk/float/scale_copy_vector_FLP.c
    ${OPUS_ROOT}/silk/float/scale_vector_FLP.c
    ${OPUS_ROOT}/silk/float/schur_FLP.c
    ${OPUS_ROOT}/silk/float/sort_FLP.c
)

# ============================================================
# Top-level Opus sources (trimmed: no multistream, no projection)
# Keep: core encode/decode, analysis, repacketizer
# ============================================================
set(OPUS_SOURCES
    ${OPUS_ROOT}/src/opus.c
    ${OPUS_ROOT}/src/opus_encoder.c
    ${OPUS_ROOT}/src/opus_decoder.c
    ${OPUS_ROOT}/src/analysis.c
    ${OPUS_ROOT}/src/mlp.c
    ${OPUS_ROOT}/src/mlp_data.c
    ${OPUS_ROOT}/src/repacketizer.c
    ${OPUS_ROOT}/src/extensions.c
)

# ============================================================
# Library target
# ============================================================
add_library(opus_cm33 STATIC
    ${CELT_SOURCES}
    ${SILK_COMMON_SOURCES}
    ${SILK_FLOAT_SOURCES}
    ${OPUS_SOURCES}
)

# ============================================================
# Include paths
# ============================================================
target_include_directories(opus_cm33 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}           # port/config.h (HAVE_CONFIG_H)
    ${OPUS_ROOT}/include                  # public API (opus.h, etc.)
)

target_include_directories(opus_cm33 PRIVATE
    ${OPUS_ROOT}/celt                     # CELT internals
    ${OPUS_ROOT}/silk                     # SILK internals
    ${OPUS_ROOT}/silk/float               # SILK float variant
    ${OPUS_ROOT}/src                      # opus_private.h
)

# ============================================================
# Compile definitions
# ============================================================
target_compile_definitions(opus_cm33 PRIVATE
    OPUS_BUILD
    HAVE_CONFIG_H
)

# ============================================================
# Cortex-M33 compile flags (only when cross-compiling)
# Unset OPUS_CROSS_COMPILE to build natively for PC testing
# ============================================================
option(OPUS_CROSS_COMPILE "Cross-compile for ARM Cortex-M33" ON)

if(OPUS_CROSS_COMPILE)
    # Memory allocation: redirect opus_alloc/opus_free to RT-Thread heap.
    # Requires <rtthread.h> to be included BEFORE any opus header.
    # If using static buffers with opus_encoder_init/opus_decoder_init
    # instead of opus_encoder_create/opus_decoder_create, these can
    # be removed and the internal allocators will be dead-code eliminated.
    target_compile_definitions(opus_cm33 PRIVATE
        OVERRIDE_OPUS_ALLOC=rt_malloc
        OVERRIDE_OPUS_FREE=rt_free
        OVERRIDE_OPUS_REALLOC=rt_realloc
        OVERRIDE_OPUS_ALLOC_SCRATCH=rt_malloc
    )

    target_compile_options(opus_cm33 PRIVATE
        -mcpu=cortex-m33
        -mthumb
        -mfloat-abi=hard
        -mfpu=fpv5-sp-d16
        -ffunction-sections
        -fdata-sections
        -fno-common
        -ffreestanding
        -Os
        -Wall -Wextra
        -Wno-unused-parameter
    )

    target_link_options(opus_cm33 INTERFACE
        -mcpu=cortex-m33
        -mthumb
        -mfloat-abi=hard
        -mfpu=fpv5-sp-d16
        -Wl,--gc-sections
        --specs=nano.specs
        --specs=nosys.specs
    )
else()
    # Native build for PC verification
    target_compile_options(opus_cm33 PRIVATE
        -Wall -Wextra
        -Wno-unused-parameter
    )
endif()
