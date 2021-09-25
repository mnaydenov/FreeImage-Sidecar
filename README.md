# FreeImage-Sidecar

An add-on library to [FreeImage](https://sourceforge.net/projects/freeimage/), which adds loading support of HEIF and AVIF file format. It is an add-on, because of incompatible linceses b/w FreeImage and [libheif](https://github.com/strukturag/libheif).

# How to compile

The project uses [CMake](https://cmake.org/).  
As mentioned, you will need `libheif` in order to compile (`find_package(Libheif REQUIRED)`).  
Other than that, you need FreeImage. Because FreeImage does not support the standard CMake modules workflow, or the standard install procedure, you will have to manually point to the header and library locations. Use CMake variables `FREEIMAGE_HEADER_DIR` and `FREEIMAGE_LIBRARY_DIR` for the header and library locations respectively.

```bash
cmake -S <path-to-source> -B <path-to-binaries> -DFREEIMAGE_HEADER_DIR="path-to-FreeImage-header-folder" -DFREEIMAGE_LIBRARY_DIR="path-to-FreeImage-library-folder"
```
_Explicitly point to both header and library._

**optional**

HEIF and AVIF sometimes come with NCLX color information. In order to create ICC profiles from these, [liblcms2](https://www.littlecms.com/) is used. This library also does not use CMake modules, but can be installed. This is why an attempt is made to first look for it into `CMAKE_INSTALL_PREFIX`.  

```bash
cmake -S <path-to-source> -B <path-to-binaries> -DCMAKE_INSTALL_PREFIX="path-to-your-install-prefix"
```
_Use explicit install prefix. Otherwise defaults to `/usr/local` on UNIX and `c:/Program Files/${PROJECT_NAME}` on Windows._  

If this fails, the lookup is the same as with FreeImage and `LCMS_HEADER_DIR` and `LCMS_LIBRARY_DIR` are searched for the header and the library respectively. 
```bash
cmake -S <path-to-source> -B <path-to-binaries> -DLCMS_HEADER_DIR="path-to-lcms2-header-folder" -DLCMS_LIBRARY_DIR="path-to-lcms2-library-folder"
```
_Point to both header and library._

If you opt-out from using `liblcms2` the NCLX color information will be lost, resulting of somewhat incorrect colors. 

# How to use

Include `FISidecar.h` header and link `libfisidecar`.  

The library exposes two new functions to initialize the plugins:

 - `FISidecar_RegisterPluginHEIF` will register the HEIF plugin and return the `FREE_IMAGE_FORMAT` for it.
 - `FISidecar_RegisterPluginAVIF` will register the AVIF plugin and return the `FREE_IMAGE_FORMAT` for it.

> Both functions must be run as early as possible in your program, after FreeImage library itself is loaded, because they modify its global state.   
Basically call these right after `FreeImage_Initialise`, in case of static FreeImage library. In case of dynamic FreeImage library, `Initialise` is called internally when the library is loaded. In that case, you can trigger a load by some non-image-loading APIs like `GetVersion` and call the `FISidecar_Register*` function(s) right after that.

There are few new load flags:

 - `FISIDECAR_LOAD_HEIF_SDR` - Load 10bit+ images as 8bit. **10bit+ Loading is not implemented yet, so you really want this flag.**
 - `FISIDECAR_LOAD_HEIF_NCLX_TO_ICC` (requires `liblcms2`) - Create ICC profile, reflecting the NCLX information.
 - `FISIDECAR_LOAD_HEIF_TRANSFORM` - Similarly to the existing `JPEG_EXIFROTATE`, this flag will instruct the loader to apply all geometry transformations, described in the file. Also similarly, the metadata might become out of sync because it is not updated to reflect the changes. In contrast to `JPEG_EXIFROTATE`, the correct (transformed) dimensions are returned when loading with `FIF_LOAD_NOPIXELS`.  

 ## Metadata support

 The plugin will load EXIF and XMP. Note, however that EXIF is loaded _only_ as "ExifRaw" tag. This means no metadata will be available via the FreeImage usual metadata query routines. The reason for this is simple - FreeImage EXIF parsing is not available (not exported) for external applications to use, including plugins. 

 # FreeImage-Adv (optional)

 [FreeImage-Adv](https://github.com/mnaydenov/FreeImage-Adv) is a fork of FreeImage adding callback support for progress report and cancellation. 
 This add-on can optionally be used with this fork as well, by `#define`-ing `FI_ADV`. 
 
 >Note, this is not a CMake variable, but a compilie-time define. If you want to pass it via CMake, you can use the `CMAKE_CXX_FLAGS`, setting it to `"-DFI_ADV"`:  
`cmake -DCMAKE_CXX_FLAGS="-DFI_ADV" <other-cmake-options>`

As of this writing however, `libheif` _has, but does not use_ callbacks. The `FreeImage-Sidecar` project has a `libheif` fork as a [Git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules) in its "external" subdirectory. This fork makes use of the callbacks to add _both_ progress monitoring _and_ cancellation to the tiles loading portion of `libheif`. Tile loading is used in all iOS devices (a major HEIF producer) and allows progressive loading of the image as tiles are fetched and decoded separately from one another. **Tile progress report is implemented only for the single-threaded version of `libheif`.** 
