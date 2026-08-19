//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/base/tf/eternalString.h"

#include <ostream>

PXR_NAMESPACE_OPEN_SCOPE

// The one and only place this type leaks.  Storage allocated here is
// intentionally never reclaimed; see the class doc for rationale.
//
// Content is trimmed at the first embedded NUL.  That is deliberate rather than
// incidental: TfToken establishes string identity from a NUL-terminated
// c-string, so a name with an embedded NUL is indistinguishable there from its
// prefix.  Trimming here makes TfEternalString agree with TfToken about what a
// given argument names, which keeps the door open to interning through the token
// table later without changing observable behavior.  It also means
// size() == strlen(c_str()) always holds.
static std::string const *
_Leak(std::string_view content)
{
    // find() returns npos when there is no NUL, and substr() treats an npos
    // count as "to the end", so this is a no-op for well-formed content.
    return new std::string(content.substr(0, content.find('\0')));
}

TfEternalString::TfEternalString()
    : _str([]() {
          static std::string const *empty = _Leak(std::string_view());
          return empty;
      }())
{
}

TfEternalString
TfEternalString::LeakCopy(std::string_view content)
{
    return TfEternalString(_Leak(content));
}

std::string
TfEternalString::_Cat(char const *a, size_t aLen, char const *b, size_t bLen)
{
    std::string result;
    result.reserve(aLen + bLen);
    result.append(a, aLen);
    result.append(b, bLen);
    return result;
}

std::ostream &
operator<<(std::ostream &o, TfEternalString s)
{
    return o << s.GetString();
}

PXR_NAMESPACE_CLOSE_SCOPE
