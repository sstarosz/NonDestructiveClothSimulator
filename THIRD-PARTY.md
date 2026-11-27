# What else is in the package

Twintailor is under the MIT licence (`LICENSE`). The Windows package also
holds the work of others, listed here with its licence and where its source
is. The licence texts are in `licenses/`.

## Qt 6.12.0

Files: `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, and in `plugins/`
`platforms/qwindows.dll` and `styles/qmodernwindowsstyle.dll`.

Copyright (C) The Qt Company Ltd. and other contributors. Used under the GNU
Lesser General Public License version 3 (`licenses/LGPL-3.0.txt`, which adds
to `licenses/GPL-3.0.txt`).

The libraries are unchanged, as Qt's installer provides them, and are loaded
as separate files: you may replace them with your own build of Qt 6.12.
Their source is at <https://download.qt.io/official_releases/qt/6.12/6.12.0/>.

Qt itself contains code of others. Its notices are in
`licenses/qt-third-party.txt`. Among them: this software is based in part on
the work of the FreeType Team.

## Microsoft Visual C++ runtime

Files: `msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll`.

Copyright (C) Microsoft Corporation. Redistributed as "distributable code"
under the Visual Studio licence terms
(<https://visualstudio.microsoft.com/license-terms/>); see
<https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files>.

## CDT (Constrained Delaunay Triangulation) 1.4.4

Compiled into `twintailor.exe`, unchanged. Mozilla Public License 2.0
(`licenses/MPL-2.0.txt`). Source:
<https://github.com/artem-ogre/CDT/tree/1.4.4>.

CDT brings two pieces of other authors:

- `portable_nth_element.hpp`, part of the LLVM Project, under the Apache
  License 2.0 with LLVM Exceptions (<https://llvm.org/LICENSE.txt>).
- `predicates.h` (<https://github.com/wlenthe/GeometricPredicates>):

      Copyright (c) 2019, William C. Lenthe
      All rights reserved.

      Redistribution and use in source and binary forms, with or without
      modification, are permitted provided that the following conditions are
      met:

      1. Redistributions of source code must retain the above copyright
         notice, this list of conditions and the following disclaimer.

      2. Redistributions in binary form must reproduce the above copyright
         notice, this list of conditions and the following disclaimer in the
         documentation and/or other materials provided with the distribution.

      3. Neither the name of the copyright holder nor the names of its
         contributors may be used to endorse or promote products derived from
         this software without specific prior written permission.

      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
      "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
      LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
      PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
      HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
      SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
      LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
      DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
      THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
      (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
      OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Vulkan headers and Vulkan-Hpp

Compiled into `twintailor.exe`. Source:
<https://github.com/KhronosGroup/Vulkan-Hpp> and
<https://github.com/KhronosGroup/Vulkan-Headers>. Offered under the Apache
License 2.0 or the MIT licence; used here under the MIT licence:

    Copyright 2015-2026 The Khronos Group Inc.

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to use, copy, modify, merge, publish,
    distribute, sublicense, and/or sell copies of the Software, and to permit
    persons to whom the Software is furnished to do so, subject to the
    following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
    OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
    NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
    DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
    OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
    USE OR OTHER DEALINGS IN THE SOFTWARE.

The Vulkan loader (`vulkan-1.dll`) is not in the package: it comes with the
graphics card's driver.

## The avatar

`assets/avatarV1.obj`: generated with AI. Shipped under Twintailor's MIT
licence (see `LICENSE`).
