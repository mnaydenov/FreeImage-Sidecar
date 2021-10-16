#ifndef FISIDECAR_H
#define FISIDECAR_H

#include "FreeImage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief You can limit the threads, used when loading an image, by OR-ing an integer with the flags argument of Load:
 * 
 * int flags = <my flags>
 * uint8_t max_threads = 4;
 * FreeImage_Load(..., ..., flags | max_threads);
 * 
 * If the limit is not set (or 0), FISIDECAR_LOAD_MAXTHREADS_DEFAULT is used. 
 * Maximum limit is 255 by default, which is defined via FISIDECAR_LOAD_MAXTHREADS_VALUE_SIZE.
 * 
 * @note libheif must be compiled with #define ENABLE_PARALLEL_TILE_DECODING to have threaded loading in the first place.
 * It also needs to have heif_context_set_max_decoding_threads function present.
**/

const size_t FISIDECAR_LOAD_MAXTHREADS_DEFAULT    = 4; //< Default threads count, see above comment. (max 2 ^ FISIDECAR_LOAD_MAXTHREADS_VALUE_SIZE - 1)
const size_t FISIDECAR_LOAD_MAXTHREADS_VALUE_SIZE = 8; //< In bits, max 15 (FIF_LOAD_NOPIXELS) - 3 (FISIDECAR_LOAD_HEIF_TRANSFORM)

#define FISIDECAR_LOAD_HEIF_SDR                   (1 << (0 + FISIDECAR_LOAD_MAXTHREADS_VALUE_SIZE))
#define FISIDECAR_LOAD_HEIF_NCLX_TO_ICC           (1 << (1 + FISIDECAR_LOAD_MAXTHREADS_VALUE_SIZE))
#define FISIDECAR_LOAD_HEIF_TRANSFORM             (1 << (2 + FISIDECAR_LOAD_MAXTHREADS_VALUE_SIZE))
     
#define FISIDECAR_LOAD_AVIF_SDR                   FISIDECAR_LOAD_HEIF_SDR
#define FISIDECAR_LOAD_AVIF_NCLX_TO_ICC           FISIDECAR_LOAD_HEIF_NCLX_TO_ICC
#define FISIDECAR_LOAD_AVIF_TRANSFORM             FISIDECAR_LOAD_HEIF_TRANSFORM

DLL_API FREE_IMAGE_FORMAT DLL_CALLCONV FISidecar_RegisterPluginHEIF();
DLL_API FREE_IMAGE_FORMAT DLL_CALLCONV FISidecar_RegisterPluginAVIF();

#ifdef __cplusplus
}
#endif

#endif // FISIDECAR_H
