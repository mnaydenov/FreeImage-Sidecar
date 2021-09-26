#include "PluginHEIF.hpp"
#include <cstring>
#include <cmath> //< std::lerp
#include <cassert>
#include "libheif/heif.h"
#include <iostream>
#include "Utilities.h"
#include "FISidecar.h"

#if ! defined(FI_ADV)
#include "unique_resource.h"
#endif

#if defined(FISIDECAR_HAS_LCMS)
#include "lcms2.h"
#endif

namespace {

void addExif(FIBITMAP* dib, const void* data, size_t length) {
	FITAG* tag = FreeImage_CreateTag();
	if(tag) {
		FreeImage_SetTagKey(tag, "ExifRaw");
		FreeImage_SetTagLength(tag, length);
		FreeImage_SetTagCount(tag, length);
		FreeImage_SetTagType(tag, FIDT_BYTE);
		FreeImage_SetTagValue(tag, data);

		// store the tag
		FreeImage_SetMetadata(FIMD_EXIF_RAW, dib, FreeImage_GetTagKey(tag), tag);

		// destroy the tag
		FreeImage_DeleteTag(tag);
	}
}

void addXMP(FIBITMAP* dib, const void* data, size_t length) {
	FITAG* tag = FreeImage_CreateTag();
	if(tag) {
		FreeImage_SetTagKey(tag, "XMLPacket");
		FreeImage_SetTagLength(tag, length);
		FreeImage_SetTagCount(tag, length);
		FreeImage_SetTagType(tag, FIDT_ASCII);
		FreeImage_SetTagValue(tag, data);

		// store the tag
		FreeImage_SetMetadata(FIMD_XMP, dib, FreeImage_GetTagKey(tag), tag);

		// destroy the tag
		FreeImage_DeleteTag(tag);
	}
}

std::pair<unique_mem, size_t> get_metadata_block(const heif_image_handle& himage, heif_item_id id) {
  const auto size = heif_image_handle_get_metadata_size(&himage, id);
  auto data = malloc(size);
  if(! data) {
    return {unique_mem{nullptr}, 0};
  } 
  
  unique_mem data_storage{data};
  auto err = heif_image_handle_get_metadata(&himage, id, data);
  assert(! err.code);

  return {std::move(data_storage), size};
}

#ifdef FI_ADV
using Args = const FreeImageLoadArgs*;
#else
using Args = int;
#endif

struct output_msg_t {
Args args;
int format_id;
#ifdef FI_ADV
class Progress* progress;
template<class... Args>
void operator()(const char* fmt, Args&&... a) const { FreeImage_OutputMessageProcCB(args->cb, format_id, fmt, std::forward<Args>(a)...); }
#else
template<class... Args>
void operator()(const char* fmt, Args&&... args) const { FreeImage_OutputMessageProc(format_id, fmt, std::forward<Args>(args)...); }
#endif
};

bool convertNCLXtoICC(const heif_color_profile_nclx& nclx, void** data, unsigned long* size_, const output_msg_t& output_msg) {
#ifdef FISIDECAR_HAS_LCMS
  // The below code has the same behavior as the GIMP plugin (https://gitlab.gnome.org/GNOME/gimp/-/blob/master/plug-ins/common/file-heif.c)

  if (nclx.color_primaries == heif_color_primaries_unspecified
    || (nclx.color_primaries == heif_color_primaries_ITU_R_BT_709_5 
        && (nclx.transfer_characteristics == heif_transfer_characteristic_IEC_61966_2_1
            || nclx.transfer_characteristics == heif_transfer_characteristic_linear)))
  {
    // no profile (assume srgb for IEC_61966_2_1; linear have no idea how to handle)
    return true; 
  }

  const cmsCIExyY whitepoint{nclx.color_primary_white_x, nclx.color_primary_white_y, 1.0f};
  const cmsCIExyYTRIPLE primaries{{nclx.color_primary_red_x, nclx.color_primary_red_y, 1.0f}
                                , {nclx.color_primary_green_x, nclx.color_primary_green_y, 1.0f}
                                , {nclx.color_primary_blue_x, nclx.color_primary_blue_y, 1.0f}};

  using unique_curve = unique_ptr<cmsToneCurve, void(*)(cmsToneCurve*)>;
  cmsToneCurve* curve;
  switch (nclx.transfer_characteristics)
  {
  case heif_transfer_characteristic_ITU_R_BT_709_5:
  { 
    static const cmsFloat64Number params[5] = { 2.2, 1.0 / 1.099,  0.099 / 1.099, 1.0 / 4.5, 0.081 };
    curve = cmsBuildParametricToneCurve ({}, 4, params);
  }
  break;
  case heif_transfer_characteristic_ITU_R_BT_470_6_System_M:
    curve = cmsBuildGamma ({}, 2.2f);
  break;
  case heif_transfer_characteristic_ITU_R_BT_470_6_System_B_G:
    curve = cmsBuildGamma ({}, 2.8f);
  break;
  case heif_transfer_characteristic_linear:
    curve = cmsBuildGamma ({}, 1.0f);
  break;
  case heif_transfer_characteristic_IEC_61966_2_1:
  default:
    static const cmsFloat64Number params[5] = { 2.4, 1.0 / 1.055,  0.055 / 1.055, 1.0 / 12.92, 0.04045 };
    curve = cmsBuildParametricToneCurve ({}, 4, params);
  }
  unique_curve curve_storage{curve, &cmsFreeToneCurve};

  cmsToneCurve* const curves[3] {curve, curve, curve};
  if(auto profile = cmsCreateRGBProfile(&whitepoint, &primaries, curves)) {

    auto description = cmsMLUalloc({}, 1);
    cmsMLUsetASCII(description, "en", "US", "Created from NCLX");
    cmsWriteTag(profile, cmsSigProfileDescriptionTag, description);
    cmsMLUfree(description);

    cmsUInt32Number size{};
    if(cmsSaveProfileToMem(profile, {}, &size)) {
      *data = malloc(size);
      *size_ = size;
      if (! *data) {
        output_msg("Out of memory for color profile");
      } else if(! cmsSaveProfileToMem(profile, *data, &size)) {
        output_msg("Failed to save ICC profile");
        free(*data);
      }
    }

    cmsCloseProfile(profile);
  }

  return true;
#else
  return false;
#endif
}

struct FIIO
{
  FIIO(FreeImageIO* io, fi_handle handle) 
    : io(io)
    , handle(handle)
  {
    const auto start_pos = io->tell_proc(handle);
    io->seek_proc(handle, 0, SEEK_END);
    this->file_size = io->tell_proc(handle) - start_pos;
    io->seek_proc(handle, start_pos, SEEK_SET);
  }
  FreeImageIO* io;
  fi_handle handle;

  int64_t file_size;
};

struct FIIO_reader : heif_reader
{
  FIIO_reader() 
    : heif_reader{1, &get_position, &read, &seek, &wait_for_file_size}
  {}

  static int64_t get_position(void* userdata) {
    auto fio = static_cast<FIIO*>(userdata);
    
    return fio->io->tell_proc(fio->handle);
  }

  // The functions read(), and seek() return 0 on success.
  // Generally, libheif will make sure that we do not read past the file size.
  static int read(void* data,
               size_t size,
               void* userdata) {
    auto fio = static_cast<FIIO*>(userdata);

    return int(fio->io->read_proc(data, 1, size, fio->handle) != size); 
  }

  static int seek(int64_t position,
               void* userdata) {
    auto fio = static_cast<FIIO*>(userdata);

    return fio->io->seek_proc(fio->handle, position, SEEK_SET);
  }

  // "When calling this function, libheif wants to make sure that it can read the file
  // up to 'target_size'. This is useful when the file is currently downloaded and may
  // grow with time. You may, for example, extract the image sizes even before the actual
  // compressed image data has been completely downloaded.
  //
  // Even if your input files will not grow, you will have to implement at least
  // detection whether the target_size is above the (fixed) file length
  // (in this case, return 'size_beyond_eof')." libheif

  // Note, this function could be used to implement cached IO (reading bigger chunks instead of 1-2 bytes at a time). 
  // What complicates things is the use of seek to move around the file.
  static heif_reader_grow_status wait_for_file_size(int64_t target_size, void* userdata) {
    auto fio = static_cast<FIIO*>(userdata);
    
    return (target_size > fio->file_size) 
      ? heif_reader_grow_status_size_beyond_eof 
      : heif_reader_grow_status_size_reached;
  }
};

namespace h {

int s_format_id;

BOOL DLL_CALLCONV
Validate(FreeImageIO* io, fi_handle handle)
{
  BYTE signature[12] = {};

  io->read_proc(signature, sizeof(signature), 1, handle);

  const auto type = heif_check_filetype(signature, sizeof(signature));
  const auto brand = heif_read_main_brand(signature, sizeof(signature));

  return type != heif_filetype_no
    && (brand != heif_fourcc_to_brand("avif") && brand != heif_fourcc_to_brand("avis"));
}

} // namespace h

namespace a {

int s_format_id;

} // namespace a

BOOL DLL_CALLCONV
returnTRUE()
{
  return TRUE;
}

int flags(Args args) {
#ifdef FI_ADV
  return args ? args->flags : 0;
#else
  return args;
#endif
}

#if defined(FI_ADV)

struct Progress
{
  FIProgress* progress;
  double first_progress;
  double last_progress;
  int total_steps;
};

int start_progress(enum heif_progress_step step, int max_progress, void* progress_user_data) {
  if(step != heif_progress_step_load_tile)
    return true;
    
  auto progress = static_cast<Progress*>(progress_user_data);
  progress->total_steps = max_progress;

  return true;
}

int on_progress(enum heif_progress_step step, int tiles_processed, void* progress_user_data) {
  if(step != heif_progress_step_load_tile)
    return true;

#if __cpp_lib_interpolate
  using std::lerp;
#else
  const auto lerp = []( double a, double b, double t) noexcept { return a + t*(b - a); };
#endif

  auto progress = static_cast<Progress*>(progress_user_data);

  const auto relativeTilesProgress = double(tiles_processed) / progress->total_steps;
  return progress->progress->reportProgress(lerp(progress->first_progress, progress->last_progress, relativeTilesProgress));
}

#endif // FI_ADV

FIBITMAP* loadFromHimage(heif_image_handle* himage, output_msg_t output_msg)
{
  const auto flags = ::flags(output_msg.args);
  const auto isLoadHeaderOnly = flags & FIF_LOAD_NOPIXELS;
  const auto isLoadForcedSDR  = flags & FISIDECAR_LOAD_HEIF_SDR;

  using unique_opts = unique_ptr<heif_decoding_options, void (*)(heif_decoding_options*)>;
  using unique_img  = unique_ptr<heif_image, void (*)(const heif_image*)>;

  const auto hasAlpha = heif_image_handle_has_alpha_channel(himage);
  const auto isHDR = heif_image_handle_get_luma_bits_per_pixel(himage) > 8 
  || heif_image_handle_get_chroma_bits_per_pixel(himage) > 8;

  const auto shouldLoadAsHDR = isHDR && ! isLoadForcedSDR;

  if(shouldLoadAsHDR && ! isLoadHeaderOnly) {
    output_msg("HEIF hdr support is not implemented. Pass FISIDECAR_LOAD_HEIF_SDR to get standard 8-bit image.");
    return {};
  }

  const auto target_chroma = shouldLoadAsHDR 
#if defined(FREEIMAGE_BIGENDIAN)
  ? (hasAlpha ? heif_chroma_interleaved_RRGGBBAA_BE : heif_chroma_interleaved_RRGGBB_BE)
#else
  ? (hasAlpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE)
#endif
  : (hasAlpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB);
  
  auto* opts = heif_decoding_options_alloc();
  unique_opts opts_storage{opts, &heif_decoding_options_free};
  
#if defined(FI_ADV)
  if(output_msg.progress) {
    opts->start_progress = start_progress;
    opts->on_progress = on_progress;
    opts->progress_user_data = output_msg.progress;
  }
#endif
  opts->convert_hdr_to_8bit = isLoadForcedSDR;
  opts->ignore_transformations = ! (flags & FISIDECAR_LOAD_HEIF_TRANSFORM);
  
  // --- get image

  const auto dst_bpp = hasAlpha ? 32 : 24;

  FIBITMAP* dib{};
  unique_dib dib_storage{dib};

  if(isLoadHeaderOnly) {
    const auto width = opts->ignore_transformations ? heif_image_handle_get_ispe_width(himage) : heif_image_handle_get_width(himage);
    const auto height = opts->ignore_transformations ? heif_image_handle_get_ispe_height(himage) : heif_image_handle_get_height(himage);
    
    if(! (dib = FreeImage_AllocateHeader(true, width, height, dst_bpp))) {
      output_msg(FI_MSG_ERROR_DIB_MEMORY);
      return {};
    }
    dib_storage.reset(dib);
  } else  {
    heif_image* img;
    auto err = heif_decode_image(himage, &img, heif_colorspace_RGB, target_chroma, opts);
    if(err.code) {
      output_msg(err.message);
      return {};
    }

    unique_img img_storage{img, &heif_image_release};

    const auto width = heif_image_get_width(img, heif_channel_interleaved);
    const auto height = heif_image_get_height(img, heif_channel_interleaved);

    if(! (dib = FreeImage_Allocate(width, height, dst_bpp))) {
      output_msg(FI_MSG_ERROR_DIB_MEMORY);
      return {};
    }
    dib_storage.reset(dib);

    // --- access image data

    int src_pitch;
    const uint8_t* src_line = heif_image_get_plane_readonly(img, heif_channel_interleaved, &src_pitch);
    const auto src_bpp = heif_image_get_bits_per_pixel(img, heif_channel_interleaved);

    // --- copy image data (8bit only for now)

    const auto dst_pitch = FreeImage_GetPitch(dib);
    auto* dst_line = FreeImage_GetBits(dib) + dst_pitch * (height - 1);

    for(unsigned y=0; y < height; y++) {
      auto* dst_bits = dst_line;
      auto* src_bits = src_line;
      for(unsigned x=0; x < width; x++) {
        dst_bits[FI_RGBA_RED]   = src_bits[0];
        dst_bits[FI_RGBA_GREEN] = src_bits[1];
        dst_bits[FI_RGBA_BLUE]  = src_bits[2];
        if(hasAlpha)
          dst_bits[FI_RGBA_ALPHA] = src_bits[3];
        
        dst_bits += dst_bpp/8;
        src_bits += src_bpp/8;
      }

      dst_line -= dst_pitch;
      src_line += src_pitch;
    }
  } 

  // --- get color profile

  // Note, we get it from himage, because in real-life photos, img does not have one (libheif issue?)
  // Also, to have a profile in header-only is consitent to the other plugins

  const auto profile_type = heif_image_handle_get_color_profile_type(himage);
  switch(profile_type)
  {
    case heif_color_profile_type_not_present:
    break;
    case heif_color_profile_type_nclx:
    {
      const auto shouldConvertToICC
#ifdef FISIDECAR_HAS_LCMS
      = (flags & FISIDECAR_LOAD_HEIF_NCLX_TO_ICC);
#else
      = false;
#endif
      if(shouldConvertToICC) {
        heif_color_profile_nclx* nclx{};
        auto err = heif_image_handle_get_nclx_color_profile (himage, &nclx);
        if(err.code) {
          output_msg("Failed to get_nclx_color_profile");
        } else {
          void* data{};
          unsigned long size{};
          if(convertNCLXtoICC(*nclx, &data, &size, output_msg) && data) {
            FreeImage_CreateICCProfile(dib, data, size);
            free(data);
          }
        }
      } else {
        output_msg("NCLX color profile ignored.");
      }
    }
    break;
    case heif_color_profile_type_rICC:
    case heif_color_profile_type_prof:
    {
      const auto size = heif_image_handle_get_raw_color_profile_size(himage);
      auto data = malloc(size);
      if (!data) {
        output_msg("Out of memory for color profile");
      } else {
        unique_mem data_storage{data};
        const auto err = heif_image_handle_get_raw_color_profile(himage, data);
        assert(!err.code);
        FreeImage_CreateICCProfile(dib, data, size);
      }
    }
    break;
  }

  return dib_storage.release();
}

FIBITMAP* DLL_CALLCONV
Load(FreeImageIO* io, fi_handle handle, int page, Args args, void* data)
{
  using unique_ctx    = unique_ptr<heif_context, void (*)(heif_context*)>;
  using unique_himage = unique_ptr<heif_image_handle, void (*)(const heif_image_handle*)>;

  assert(io);
  assert(handle);

  const auto format_id = [&]{ 
    const auto start_pos = io->tell_proc(handle);
    const auto format_id = h::Validate(io, handle) ? h::s_format_id : a::s_format_id;
    io->seek_proc(handle, start_pos, SEEK_SET);
    return format_id;
  }(); //< invoke

  auto output_msg = output_msg_t{args, format_id};

  try {
#if defined(FI_ADV)
    FIProgress progress(args->cbOption, args->cb, FI_OP_LOAD, format_id);
    if(progress.isCanceled()) {
      return {};
    }
#endif
    auto* ctx = heif_context_alloc();
    unique_ctx ctx_storage{ctx, &heif_context_free};

    FIIO fio(io, handle);
    FIIO_reader fio_reader;

    // --- read file

    auto err = heif_context_read_from_reader(ctx, &fio_reader, &fio, nullptr);

    if(err.code) {
      output_msg(err.message);
      return {};
    }
    
    // --- get handle to the primary image
    
    heif_image_handle* himage;
    err = heif_context_get_primary_image_handle(ctx, &himage);

    if(err.code) {
      output_msg(err.message);
      return {};
    }
    unique_himage himage_storage{himage, &heif_image_handle_release};

    // --- decode image and get profile
#if defined(FI_ADV)
    static const auto read_end_progress = .3;
    static const auto decode_end_progress = .9;
    if(! progress.reportProgress(read_end_progress))
      return {};

    Progress progress_decode{&progress, read_end_progress, decode_end_progress}; 
    output_msg.progress = &progress_decode; 
#endif
    auto dib = loadFromHimage(himage, output_msg);
    if(! dib)
      return {};

    unique_dib dib_storage{dib};
    
    // --- get metadata

    {
      static const auto idsCount = 5; //< made-up number, really
      heif_item_id ids[idsCount];
      unique_arr<heif_item_id> ids_storage;
      heif_item_id* items = ids;

      const char* filter = {};
      const auto itemsCount = heif_image_handle_get_number_of_metadata_blocks(himage, filter);
      if(itemsCount > idsCount) {
        items = new heif_item_id[itemsCount];
        ids_storage.reset(items);
      }

      (void) heif_image_handle_get_list_of_metadata_block_IDs(himage, filter, items, itemsCount);

      for(const auto* it = items; it != items + itemsCount; it++) {
        const auto type = std::string(heif_image_handle_get_metadata_type(himage, *it)); //< We count on SSO this to be cheap                                                           //<

        if(type == "Exif") {
          auto block = get_metadata_block(*himage, *it);

          if(! block.first) {
            output_msg("Out of memory for %s block", type.c_str()); 
          } else {
            // First 4 bytes are the offset into the block where the data starts
            // (https://github.com/strukturag/libheif/issues/269#issuecomment-667149770)
            const auto size = block.second;
            const auto* const data = reinterpret_cast<unsigned char*>(block.first.get());
            assert(size > 4);
            const auto offset = unsigned(data[0] << 4) | (data[1] << 3) | (data[2] << 2) | data[3];
            
            if(size > (4 + offset)) {

              // ### However, where the data starts is the byte order (TIFF header), 
              // skiping over the "exif\0\0" signature, if any. 
              // This creates problems: 
              //  - libexif fails to load such blocks (https://github.com/libexif/libexif/issues/58)
              //  - FreeImage fails to save such blocks in JPEG and others.
              // DO THE UGLY THING and preped a signatire.

              const auto sizeofSig = sizeof("Exif\0\0") - 1;
              const auto new_size = sizeofSig + (size - (4 + offset));
              const auto new_data = reinterpret_cast<unsigned char*>(malloc(new_size));

              if(! new_data) {
                output_msg("Out of memory for %s block", type.c_str()); 
              } else {
                memcpy(new_data, "Exif\0\0", sizeofSig);
                memcpy(new_data + sizeofSig, data + (4 + offset), new_size - sizeofSig);

                block.second = new_size;
                block.first.reset(new_data);

                addExif(dib, block.first.get(), block.second);
              }
            }
          }
        } else if(type == "mime" && heif_image_handle_get_metadata_content_type(himage, *it) == std::string("application/rdf+xml")) {
          const auto block = get_metadata_block(*himage, *it);

          if(! block.first) 
            output_msg("Out of memory for XMP block"); 
          else 
            addXMP(dib, block.first.get(), block.second);
        } else {
          output_msg("metadata of type %s not implemented", type.c_str());
        }
      }
    }

    // --- get thumb

    {
      static const auto idsCount = 1; //< it is usually just one
      heif_item_id ids[idsCount];

      if(const auto thumbsCount = heif_image_handle_get_number_of_thumbnails(himage)) {
        if(thumbsCount > 1) {
          output_msg("Warning: Thumbs beyond the first are ignored.");
        }

        (void) heif_image_handle_get_list_of_thumbnail_IDs(himage, ids, idsCount);

        heif_image_handle* hthumb;
        err = heif_image_handle_get_thumbnail(himage, *ids, &hthumb);
        assert(! err.code);

        unique_himage himage_storage{hthumb, &heif_image_handle_release};

#if defined(FI_ADV)
        if(! progress.reportProgress(decode_end_progress)) {
          return {};
        }

        FreeImageLoadArgs thArgs{*args};
        thArgs.flags &= ~FIF_LOAD_NOPIXELS;
        output_msg.args = &thArgs; 
        output_msg.progress = {};  
#else
        output_msg.args &= ~FIF_LOAD_NOPIXELS;
#endif
        auto thumb = loadFromHimage(hthumb, output_msg);
        FreeImage_SetThumbnail(dib, thumb);
        FreeImage_Unload(thumb);
      }
    }

    return dib_storage.release();

  } catch (const std::exception& e) { //< std::bad_alloc to the very least, probably others fom libheif
    output_msg(e.what());
    return {};
  }
}

} // namespace

void DLL_CALLCONV
InitHEIF(Plugin* plugin, int format_id)
{
  using namespace h;
  s_format_id = format_id;

  struct impl {
    static const char* DLL_CALLCONV
    Format() { return "HEIF"; }

    static const char* DLL_CALLCONV
    Description() { return "High Efficiency Image File Format (HEIF)"; }

    static const char* DLL_CALLCONV
    Extension() { return "heic,heics,heif,heifs"; }

    static const char* DLL_CALLCONV
    MimeType() { return "image/heif"; }
  };

  plugin->format_proc = impl::Format;
  plugin->description_proc = impl::Description;
  plugin->extension_proc = impl::Extension;
#ifdef FI_ADV
  plugin->loadAdv_proc = Load;
#else
  plugin->load_proc = Load;
#endif
  plugin->validate_proc = Validate;
  plugin->mime_proc = impl::MimeType;
  plugin->supports_icc_profiles_proc = returnTRUE;
	plugin->supports_no_pixels_proc = returnTRUE;
}

void DLL_CALLCONV
InitAVIF(Plugin* plugin, int format_id)
{
  using namespace a;
  s_format_id = format_id;

  struct impl {
    static const char* DLL_CALLCONV
    Format() { return "AVIF"; }

    static const char* DLL_CALLCONV
    Description() { return "AV1 Image File Format (AVIF)"; }

    static const char* DLL_CALLCONV
    Extension() { return "avif,avifs"; }

    static const char* DLL_CALLCONV
    MimeType() { return "image/avif"; }

    static BOOL DLL_CALLCONV
    Validate(FreeImageIO* io, fi_handle handle) {
      BYTE signature[12] = {};

      io->read_proc(signature, sizeof(signature), 1, handle);

      const auto type = heif_check_filetype(signature, sizeof(signature));
      const auto brand = heif_read_main_brand(signature, sizeof(signature));

      return type != heif_filetype_no 
        && (brand == heif_fourcc_to_brand("avif") || brand == heif_fourcc_to_brand("avis"));
    }

  };

  plugin->format_proc = impl::Format;
  plugin->description_proc = impl::Description;
  plugin->extension_proc = impl::Extension;
#ifdef FI_ADV
  plugin->loadAdv_proc = Load;
#else
  plugin->load_proc = Load;
#endif
  plugin->validate_proc = impl::Validate;
  plugin->mime_proc = impl::MimeType;
  plugin->supports_icc_profiles_proc = returnTRUE;
  plugin->supports_no_pixels_proc = returnTRUE;
}
