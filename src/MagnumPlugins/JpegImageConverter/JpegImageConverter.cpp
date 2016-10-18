/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015
              Vladimír Vondruš <mosra@centrum.cz>

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#include "JpegImageConverter.h"

#include <csetjmp>
#include <algorithm>
#include <Corrade/Containers/Array.h>
#include <Corrade/Utility/Endianness.h>
#include <Magnum/ImageView.h>
#include <Magnum/PixelFormat.h>

/* On Windows we need to circumvent conflicting definition of INT32 in
   <windows.h> (included from OpenGL headers). Problem with libjpeg-tubo only,
   libjpeg solves that already somehow. */
#ifdef CORRADE_TARGET_WINDOWS
#define XMD_H
#endif
#include <jpeglib.h>

namespace Magnum { namespace Trade {

JpegImageConverter::JpegImageConverter() = default;

JpegImageConverter::JpegImageConverter(PluginManager::AbstractManager& manager, std::string plugin): AbstractImageConverter(manager, std::move(plugin)) {}

auto JpegImageConverter::doFeatures() const -> Features { return Feature::ConvertData; }

Containers::Array<char> JpegImageConverter::doExportToData(const ImageView2D& image) {
    #ifndef MAGNUM_TARGET_GLES
    if(image.storage().swapBytes()) {
        Error() << "Trade::JpegImageConverter::exportToData(): pixel byte swap is not supported";
        return nullptr;
    }
    #endif

    #if BITS_IN_JSAMPLE == 8
    if(image.type() != PixelType::UnsignedByte) {
        Error() << "Trade::JpegImageConverter::exportToData(): cannot convert image of type" << image.type() << "into 8-bit JPEG";
        return nullptr;
    }
    #elif BITS_IN_JSAMPLE == 12
    if(image.type() != PixelType::UnsignedShort) {
        Error() << "Trade::JpegImageConverter::exportToData(): cannot convert image of type" << image.type() << "into 12-bit JPEG";
        return nullptr;
    }
    #else
    #error only 8-bit-per-sample and 12-bit-per-sample libJPEG builds are supported
    #endif

    Int components;
    J_COLOR_SPACE colorSpace;
    switch(image.format()) {
        #if !(defined(MAGNUM_TARGET_WEBGL) && defined(MAGNUM_TARGET_GLES2))
        case PixelFormat::Red:
        #endif
        #ifdef MAGNUM_TARGET_GLES2
        case PixelFormat::Luminance:
        #endif
            components = 1;
            colorSpace = JCS_GRAYSCALE;
            break;
        case PixelFormat::RGB:
            components = 3;
            colorSpace = JCS_RGB;
            break;

        #if JCS_ALPHA_EXTENSIONS == 1
        case PixelFormat::RGBA:
            components = 4;
            colorSpace = JCS_EXT_RGBA;
            break;
        #endif

        /** @todo RGB565 and other extension formats? */

        default:
            Error() << "Trade::JpegImageConverter::exportToData(): unsupported pixel format" << image.format();
            return nullptr;
    }

    /* Initialize structures. Needs to be before the setjmp crap in order to
       avoid leaks on error. */
    jpeg_compress_struct info;
    struct DestinationManager {
        jpeg_destination_mgr jpegDestinationManager;
        std::string output;
    } destinationManager;

    Containers::Array<JSAMPROW> rows;
    Containers::Array<char> data;

    /* Fugly error handling stuff */
    /** @todo Get rid of this crap */
    struct ErrorManager {
        jpeg_error_mgr jpegErrorManager;
        std::jmp_buf setjmpBuffer;
    } errorManager;
    info.err = jpeg_std_error(&errorManager.jpegErrorManager);
    errorManager.jpegErrorManager.error_exit = [](j_common_ptr info) {
        info->err->output_message(info);
        std::longjmp(reinterpret_cast<ErrorManager*>(info->err)->setjmpBuffer, 1);
    };
    if(setjmp(errorManager.setjmpBuffer)) {
        Error() << "Trade::JpegImageConverter::doExportToData(): error while writing JPEG file";

        jpeg_destroy_compress(&info);
        return nullptr;
    }

    /* Create the compression structure */
    jpeg_create_compress(&info);
    info.dest = reinterpret_cast<jpeg_destination_mgr*>(&destinationManager);
    info.dest->init_destination = [](j_compress_ptr info) {
        auto& destinationManager = *reinterpret_cast<DestinationManager*>(info->dest);
        destinationManager.output.resize(1); /* It crashes if the buffer has zero free space */
        info->dest->next_output_byte = reinterpret_cast<JOCTET*>(&destinationManager.output[0]);
        info->dest->free_in_buffer = destinationManager.output.size();
    };
    info.dest->term_destination = [](j_compress_ptr info) {
        auto& destinationManager = *reinterpret_cast<DestinationManager*>(info->dest);
        destinationManager.output.resize(destinationManager.output.size() - info->dest->free_in_buffer);
    };
    info.dest->empty_output_buffer = [](j_compress_ptr info) -> boolean {
        auto& destinationManager = *reinterpret_cast<DestinationManager*>(info->dest);
        const std::size_t oldSize = destinationManager.output.size();
        destinationManager.output.resize(oldSize*2); /* Double capacity each time it is exceeded */
        info->dest->next_output_byte = reinterpret_cast<JOCTET*>(&destinationManager.output[0] + oldSize);
        info->dest->free_in_buffer = destinationManager.output.size() - oldSize;
        return true;
    };

    /* Fill the info structure */
    info.image_width = image.size().x();
    info.image_height = image.size().y();
    info.input_components = components;
    info.in_color_space = colorSpace;

    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, 100, true);
    jpeg_start_compress(&info, true);

    /* Data properties */
    Math::Vector2<std::size_t> offset, dataSize;
    std::tie(offset, dataSize, std::ignore) = image.dataProperties();

    /* For 16bit images we need to downscale to 12bit */
    #if BITS_IN_JSAMPLE == 12
    const std::size_t rowSize = image.pixelSize()*image.size().x()/2;
    Containers::Array<UnsignedShort> row{rowSize};
    #elif BITS_IN_JSAMPLE != 8
    #error only 8-bit-per-sample and 12-bit-per-sample libJPEG builds are supported
    #endif
    while(info.next_scanline < info.image_height) {
        #if BITS_IN_JSAMPLE == 12
        std::copy_n(image.data<UnsignedShort>() + (offset.sum() + (image.size().y() - info.next_scanline - 1)*dataSize.x())/2, rowSize, row.data());
        for(UnsignedShort& i: row)
            i >>= 4;
        #elif BITS_IN_JSAMPLE != 8
        #error only 8-bit-per-sample and 12-bit-per-sample libJPEG builds are supported
        #endif

        /* libJPEG HAVE YOU EVER HEARD ABOUT CONST ARGUMENTS?! IT'S NOT 1978
           ANYMORE */
        JSAMPROW row = reinterpret_cast<JSAMPROW>(const_cast<char*>(image.data() + (image.size().y() - info.next_scanline - 1)*dataSize.x()));
        jpeg_write_scanlines(&info, &row, 1);
    }

    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);

    /* Copy the string into the output array (I would kill for having std::string::release()) */
    Containers::Array<char> fileData{destinationManager.output.size()};
    std::copy(destinationManager.output.begin(), destinationManager.output.end(), fileData.data());
    return fileData;
}

}}
