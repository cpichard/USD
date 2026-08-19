//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_BASE_TF_ETERNAL_STRING_H
#define PXR_BASE_TF_ETERNAL_STRING_H

/// \file

#include "pxr/pxr.h"
#include "pxr/base/tf/api.h"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>

PXR_NAMESPACE_OPEN_SCOPE

/// \class TfEternalString
///
/// An immutable string whose character data is guaranteed to remain valid at a
/// fixed address for the remainder of the process.
///
/// The point of this type is not to be a better string; it is to make a promise
/// to the functions you pass it to, in a form the compiler can check.  A callee
/// that receives a \c TfEternalString may:
///
///   \li retain \c c_str() / \c data() indefinitely without copying, and
///   \li use that address as a durable memoization key -- if the same address
///       is presented twice, it denotes the same characters, forever.
///
/// A callee may not assume the address is a content *identity*.  There is no
/// interning here.  The implication runs one way only:
///
///   \li same address   ==>  same content
///   \li same content  =/=>  same address
///
/// Two distinct \c TfEternalString objects with equal content are expected.
/// For example, \c TF_FUNC_NAME() which returns a \c TfEternalString can
/// produce them, because \c ArchGetPrettierFunctionName() truncates to collapse
/// overloads and template instantiations:
///
/// \code
///   Outer::Member<int, float>()   -->  "Outer::Member"
///   Outer::Member<char, bool>()   -->  "Outer::Member"
/// \endcode
///
/// Two call sites, two addresses, one string.  Accordingly, equality and
/// hashing on this type are defined by *content*, not by address.  Address
/// comparison is a legitimate fast path but never a uniqueness guarantee.
///
/// \section TfEternalString_cost Cost
///
/// Creating one leaks.  \c LeakCopy() allocates storage that is never freed,
/// deliberately, to satisfy the immortality guarantee.  This makes the type
/// suitable for a *bounded* set of strings fixed by the program source code --
/// one per call site, as \c TF_FUNC_NAME() does -- and actively not suitable
/// for anything dynamic or derived from data.  Never call \c LeakCopy() in a
/// loop, per prim, per frame, or on user input.  If you cannot name a static
/// bound on how many distinct strings your code creates, this is the wrong
/// type.
///
/// \section TfEternalString_vs_token Relationship to TfToken
///
/// \c TfToken is the closest existing thing, and this type deliberately agrees
/// with it about what a name *is*: both derive string identity from a
/// NUL-terminated c-string, which is why \c LeakCopy() trims.
///
/// The reason \c TfEternalString exists anyway is *not* that an immortal
/// \c TfToken is less durable.  It isn't.  Immortalizing clears the low bit of a
/// rep's reference count, and reps are reclaimed only when that count is exactly
/// 1, so an immortal rep's characters remain valid for the life of the process
/// even with no live \c TfToken referring to them.  The reasons are these:
///
///   \li That durability is undocumented, and \c TfToken::GetText() states the
///       opposite -- that its pointer is invalid once the token is destroyed.  A
///       callee retaining immortal text would be relying on an implementation
///       detail rather than on a contract.
///   \li The promise would live in a runtime bit rather than in the type.  A
///       callee handed a \c TfToken must consult \c IsImmortal() and carry a
///       fallback, and cannot know statically that retaining the pointer is
///       safe.  Handed a \c TfEternalString, it knows.
///   \li \c TfToken is not a drop-in for a string-like result.  It has no
///       \c c_str(), \c length(), or \c empty(), and no \c operator+ at all --
///       and its implicit \c std::string const & conversion does not rescue
///       concatenation, because \c std::operator+ is a template and deduction
///       does not consider user-defined conversions.  Every concatenating or
///       \c c_str() call site would have to change.
///   \li \c tf/diagnostic.h, which defines \c TF_FUNC_NAME(), is deliberately
///       light and does not include \c tf/token.h.  Making it do so would pull
///       the token table's dependencies into nearly every translation unit.
///
/// None of that rules out using the token table as the *storage* for this type's
/// characters, which would additionally make equal content share a single
/// address.  Keeping the NUL semantics aligned is what leaves that available as
/// a change with no observable effect on this interface.
///
class TfEternalString
{
public:
    /// Construct the empty eternal string.
    TF_API TfEternalString();

    /// Copy \p content into heap storage that is never freed, and return a
    /// \c TfEternalString referring to it.
    ///
    /// This is spelled as a named factory, not a converting constructor, so
    /// that callers make an explicit acknowledgement of intent at the call
    /// site.  There is deliberately no implicit conversion from \c char const *
    /// or \c std::string for the same reason.
    ///
    /// Embedded NULs are not supported.  \p content is trimmed at the first
    /// NUL, so \c LeakCopy("a\\0b") and \c LeakCopy("a") name the same string.
    /// This matches \c TfToken, which derives string identity from a
    /// NUL-terminated c-string and therefore cannot tell such a name from its
    /// prefix either.  Consequently \c size() is always \c strlen(c_str()), and
    /// this type is for names, not for arbitrary binary content.
    TF_API static TfEternalString LeakCopy(std::string_view content);

    /// \name std::string-compatible accessors
    /// @{
    char const *c_str()  const { return _str->c_str(); }
    char const *data()   const { return _str->data();  }
    size_t      size()   const { return _str->size();  }
    size_t      length() const { return _str->size();  }
    bool        empty()  const { return _str->empty(); }
    /// @}

    /// Return the underlying string.
    std::string const &GetString() const { return *_str; }

    /// Return a view of the characters.
    std::string_view GetStringView() const { return *_str; }

    /// \name Implicit conversions
    ///
    /// Both of these hand out a reference or a view of internal state, which
    /// for most types would be a dangling hazard.  Here they are safe even when
    /// the \c TfEternalString itself is a temporary, because the referent is
    /// immortal:
    /// \code
    ///     std::string const &name = TF_FUNC_NAME();   // safe
    ///     std::string_view   view = TF_FUNC_NAME();   // safe
    /// \endcode
    ///
    /// Note the \c std::string conversion yields a reference, not a value.
    /// That is what makes the durable-address promise survive being passed
    /// through an ordinary \c std::string const & parameter: no temporary is
    /// materialized, so a callee that retains \c c_str() is safe.
    ///
    /// Providing both is a deliberate trade with one known cost.  A callee
    /// overloaded on both `std::string const &` and `std::string_view` will
    /// fail to compile if passed a \c TfEternalString argument: either implicit
    /// conversion would be viable, so neither candidate is better and the call
    /// is ambiguous.
    ///
    /// If you hit that, the local fix is to pass \c GetString() or \c
    /// GetStringView() to choose one explicitly.  Overloading on both
    /// `std::string const &` and `std::string_view` is rare in practice, so
    /// providing both implicit conversions is judged worth the risk.
    /// @{
    operator std::string const &() const { return *_str; }
    operator std::string_view() const { return *_str; }
    /// @}

    /// \name Operators
    ///
    /// These are hidden friends: findable only by argument-dependent lookup on
    /// \c TfEternalString, so they are not candidates for overload resolution
    /// in unrelated expressions.  They must be written out because the
    /// corresponding \c std:: operators are templates, and template argument
    /// deduction never considers user-defined conversions -- so neither
    /// conversion above can rescue `s + " x"` or `os << s`.
    /// @{

    friend std::string operator+(TfEternalString a, std::string_view b) {
        return _Cat(a.data(), a.size(), b.data(), b.size());
    }
    friend std::string operator+(std::string_view a, TfEternalString b) {
        return _Cat(a.data(), a.size(), b.data(), b.size());
    }
    friend std::string operator+(TfEternalString a, TfEternalString b) {
        return _Cat(a.data(), a.size(), b.data(), b.size());
    }

    /// Compare by content, with an address fast path -- see the class
    /// documentation on why address equality alone is not sufficient.
    friend bool operator==(TfEternalString a, TfEternalString b) {
        return a._str == b._str || a.GetStringView() == b.GetStringView();
    }
    friend bool operator==(TfEternalString a, std::string_view b) {
        return a.GetStringView() == b;
    }
    friend bool operator==(std::string_view a, TfEternalString b) {
        return a == b.GetStringView();
    }
    friend bool operator!=(TfEternalString a, TfEternalString b) {
        return !(a == b);
    }
    friend bool operator!=(TfEternalString a, std::string_view b) {
        return !(a == b);
    }
    friend bool operator!=(std::string_view a, TfEternalString b) {
        return !(a == b);
    }

    /// Hash by content -- see the class documentation on why not by address.
    template <class HashState>
    friend void TfHashAppend(HashState &h, TfEternalString s) {
        h.Append(s.GetString());
    }

    TF_API friend std::ostream &operator<<(std::ostream &o, TfEternalString s);

    /// @}

private:
    explicit TfEternalString(std::string const *str) : _str(str) {}

    TF_API static std::string
    _Cat(char const *a, size_t aLen, char const *b, size_t bLen);

    std::string const *_str;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_BASE_TF_ETERNAL_STRING_H
