#pragma once

#include <memory>
#include "FreeImage.h"

template<class T, class Deleter> 
using unique_ptr = std::unique_ptr<T, Deleter>;

template<class T> 
using unique_arr = std::unique_ptr<T[], std::default_delete<T>>;

template<class T> 
using unique_obj = std::unique_ptr<T, std::default_delete<T>>;

class unique_mem : public unique_ptr<void, decltype(&std::free)>
{
public:
	explicit unique_mem(pointer p) : unique_ptr<void, decltype(&std::free)>(p, &std::free)
	{}
};

class unique_dib : public unique_ptr<FIBITMAP, void(DLL_CALLCONV*)(FIBITMAP*)>
{
public:
	explicit unique_dib(pointer p) : unique_ptr<FIBITMAP, void(DLL_CALLCONV*)(FIBITMAP*)>(p, &FreeImage_Unload)
	{}
};

class unique_fimem : public unique_ptr<FIMEMORY, void(DLL_CALLCONV*)(FIMEMORY*)>
{
public:
	explicit unique_fimem(pointer p) : unique_ptr<FIMEMORY, void(DLL_CALLCONV*)(FIMEMORY*)>(p, &FreeImage_CloseMemory)
	{}
};
