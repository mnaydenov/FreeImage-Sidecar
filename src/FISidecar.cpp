 #include "FISidecar.h"
 #include "PluginHEIF.hpp"

 FREE_IMAGE_FORMAT DLL_CALLCONV FISidecar_RegisterPluginHEIF() {
   return FreeImage_RegisterLocalPlugin(&InitHEIF);
 }
 FREE_IMAGE_FORMAT DLL_CALLCONV FISidecar_RegisterPluginAVIF() {
   return FreeImage_RegisterLocalPlugin(&InitAVIF);
 }
