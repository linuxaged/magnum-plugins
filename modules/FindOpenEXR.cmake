#.rst:
# Find OpenEXR
# ------------
#
# Finds the OpenEXR library, either by delegating to the OpenEXR CMake config
# (if found), or by falling back to plain filesystem search. This module
# supplements both IlmBase and OpenEXR config files and defines:
#
#  OpenEXR_FOUND        - True if the OpenEXR library is found
#  OpenEXR::IlmImf      - libIlmImf imported target
#
#  IlmBase::Half        - libHalf imported target
#  IlmBase::Iex         - libIex imported target
#  IlmBase::IlmThread   - libIlmThread imported target
#  IlmBase::Imath       - libImath imported target
#
# The IexMath library is not being searched for as it's not needed to be linked
# to and it no longer exists in OpenEXR 3.0.0. Additionally these variables are
# defined for internal usage:
#
#  OPENEXR_ILMINF_LIBRARY - libIlmInf library
#  OPENEXR_INCLUDE_DIR    - Include dir
#
#  ILMBASE_HALF_LIBRARY - libHalf library
#  ILMBASE_IEX_LIBRARY  - libIex library
#  ILMBASE_ILMTHREAD_LIBRARY - libIlmThread library
#  ILMBASE_IMATH_LIBRARY - libImath library
#

#
#   This file is part of Magnum.
#
#   Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019,
#               2020, 2021 Vladimír Vondruš <mosra@centrum.cz>
#
#   Permission is hereby granted, free of charge, to any person obtaining a
#   copy of this software and associated documentation files (the "Software"),
#   to deal in the Software without restriction, including without limitation
#   the rights to use, copy, modify, merge, publish, distribute, sublicense,
#   and/or sell copies of the Software, and to permit persons to whom the
#   Software is furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included
#   in all copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
#   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#   DEALINGS IN THE SOFTWARE.
#

# Try to find OpenEXR via its config files. These are available only since 2.5
# (2.4?), for older versions we'll have to fall back to finding ourselves.
find_package(OpenEXR CONFIG QUIET)
if(OpenEXR_FOUND)
    # Just to make FPHSA print some meaningful location, nothing else
    get_target_property(_OPENEXR_INTERFACE_INCLUDE_DIRECTORIES OpenEXR::IlmImf INTERFACE_INCLUDE_DIRECTORIES)
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(OpenEXR DEFAULT_MSG
        _OPENEXR_INTERFACE_INCLUDE_DIRECTORIES)

    return()
endif()

# ZLIB and threads are a dependency. Can't use CMakeFindDependencyMacro because
# "The call to return() makes this macro unsuitable to call from Find Modules".
# Thanks for nothing, CMake.
if(OpenEXR_FIND_QUIETLY)
    set(_OPENEXR_FIND_QUIET QUIET)
else()
    set(_OPENEXR_FIND_QUIET )
endif()
if(OpenEXR_FIND_REQUIRED)
    set(_OPENEXR_FIND_REQUIRED REQUIRED)
else()
    set(_OPENEXR_FIND_REQUIRED )
endif()
find_package(ZLIB ${_OPENEXR_FIND_QUIET} ${_OPENEXR_FIND_REQUIRED})
find_package(Threads ${_OPENEXR_FIND_QUIET} ${_OPENEXR_FIND_REQUIRED})

# All the libraries. Don't bother with debug/release distinction, if people
# have that, there's a high chance they have a non-ancient version with CMake
# configs as well
find_library(OPENEXR_ILMIMF_LIBRARY NAMES IlmImf)
find_library(ILMBASE_HALF_LIBRARY NAMES Half)
find_library(ILMBASE_IEX_LIBRARY NAMES Iex)
find_library(ILMBASE_ILMTHREAD_LIBRARY NAMES IlmThread)
find_library(ILMBASE_IMATH_LIBRARY NAMES Imath)

# Include dir
find_path(OPENEXR_INCLUDE_DIR NAMES OpenEXR/ImfHeader.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenEXR DEFAULT_MSG
    OPENEXR_ILMIMF_LIBRARY
    OPENEXR_INCLUDE_DIR
    ILMBASE_HALF_LIBRARY
    ILMBASE_IEX_LIBRARY
    ILMBASE_ILMTHREAD_LIBRARY
    ILMBASE_IMATH_LIBRARY
    ZLIB_FOUND)

mark_as_advanced(FORCE
    OPENEXR_ILMIMF_LIBRARY
    OPENEXR_INCLUDE_DIR
    ILMBASE_HALF_LIBRARY
    ILMBASE_IEX_LIBRARY
    ILMBASE_ILMTHREAD_LIBRARY
    ILMBASE_IMATH_LIBRARY)

# Dependency treee reverse-engineered from the 2.5 config files. They also have
# OpenEXR::IlmInfConfig / IlmBase::IlmBaseConfig which do just an include path
# setup, I find that unnecessary, the include path gets added to the Half / Iex
# libraries instead, which are needed by everything else.

if(NOT TARGET IlmBase::Half)
    add_library(IlmBase::Half UNKNOWN IMPORTED)
    set_target_properties(IlmBase::Half PROPERTIES
        IMPORTED_LOCATION ${ILMBASE_HALF_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${OPENEXR_INCLUDE_DIR})
endif()

if(NOT TARGET IlmBase::Iex)
    add_library(IlmBase::Iex UNKNOWN IMPORTED)
    set_target_properties(IlmBase::Iex PROPERTIES
        IMPORTED_LOCATION ${ILMBASE_IEX_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${OPENEXR_INCLUDE_DIR})
endif()

if(NOT TARGET IlmBase::IlmThread)
    add_library(IlmBase::IlmThread UNKNOWN IMPORTED)
    set_target_properties(IlmBase::IlmThread PROPERTIES
        IMPORTED_LOCATION ${ILMBASE_ILMTHREAD_LIBRARY}
        INTERFACE_LINK_LIBRARIES "IlmBase::Iex;Threads::Threads")
endif()

if(NOT TARGET IlmBase::Imath)
    add_library(IlmBase::Imath UNKNOWN IMPORTED)
    set_target_properties(IlmBase::Imath PROPERTIES
        IMPORTED_LOCATION ${ILMBASE_IMATH_LIBRARY}
        # The 2.5 config links to IexMath, which then transitively links to
        # Iex, but ldd has a different opinion -- the IexMath isn't needed at
        # all, it seems, so skip it.
        INTERFACE_LINK_LIBRARIES "IlmBase::Half;IlmBase::Iex")
endif()

if(NOT TARGET OpenEXR::IlmImf)
    add_library(OpenEXR::IlmImf UNKNOWN IMPORTED)
    set_target_properties(OpenEXR::IlmImf PROPERTIES
        IMPORTED_LOCATION ${OPENEXR_ILMIMF_LIBRARY}
        INTERFACE_LINK_LIBRARIES "IlmBase::Half;IlmBase::Iex;IlmBase::IlmThread;IlmBase::Imath;ZLIB::ZLIB")
endif()
