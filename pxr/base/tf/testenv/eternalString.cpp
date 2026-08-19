//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/base/tf/eternalString.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/hash.h"
#include "pxr/base/tf/regTest.h"

#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

// Helpers for the interop checks below.
static std::string
_TakeStringRef(std::string const &s)
{
    return s;
}

// A callee taking only std::string_view, which is the shape that motivated the
// implicit view conversion (e.g. pxr/exec/exec/requestImpl.cpp's debug-message
// helpers).  It returns the view's data pointer so callers can check that the
// characters were not copied on the way in.
static char const *
_TakeStringView(std::string_view s)
{
    return s.data();
}

// A local stand-in for TF_FUNC_NAME(), so that this test exercises
// TfEternalString's own guarantees without depending on tf/diagnostic.h's macro
// or on how ArchGetPrettierFunctionName() formats a name.  It keeps the two
// properties that matter here: the string is computed once per call site and
// cached in an immortal static, and it describes the enclosing function.
//
// Note that __func__ is passed as a lambda *parameter*.  Expanding it inside
// the lambda body would name the lambda's own operator() instead; the naming
// checks below are what catch that mistake.
#define TEST_FUNC_NAME()                                                      \
    ([](char const *f) -> TfEternalString {                                   \
        static TfEternalString const n =                                      \
            TfEternalString::LeakCopy(std::string("fn:") + f);                \
        return n;                                                             \
    }(__func__))

// The name TEST_FUNC_NAME() must produce in the function that invokes it.
// Evaluated outside any lambda, so it is the enclosing function's name.  This
// is compared for exact equality rather than matching a substring, so the test
// does not depend on what __func__ actually spells.
#define TEST_FUNC_NAME_EXPECTED() (std::string("fn:") + __func__)

// Returns the address of the character data cached at a single call site.  Two
// calls must return the same address; that is the caching guarantee.
static char const *
_OneCallSite()
{
    return TEST_FUNC_NAME().data();
}

// Two distinct call sites inside one function.  Both describe the same
// function, so the content matches, but each site caches independently.
static void
_TwoCallSitesOneFunction(TfEternalString *first, TfEternalString *second)
{
    *first = TEST_FUNC_NAME();
    *second = TEST_FUNC_NAME();
    TF_AXIOM(*first == TEST_FUNC_NAME_EXPECTED());
    TF_AXIOM(*second == TEST_FUNC_NAME_EXPECTED());
}

// A call site inside a template.  Each instantiation gets its own closure type,
// hence its own cached string.
template <class T>
static TfEternalString
_TemplateCallSite()
{
    TfEternalString result = TEST_FUNC_NAME();
    TF_AXIOM(result == TEST_FUNC_NAME_EXPECTED());
    return result;
}

static bool
Test_TfEternalString()
{
    // Construction and basic accessors.
    {
        TfEternalString s = TfEternalString::LeakCopy("Foo::Bar");
        TF_AXIOM(s.size() == 8);
        TF_AXIOM(s.length() == 8);
        TF_AXIOM(!s.empty());
        TF_AXIOM(s.GetString() == "Foo::Bar");
        TF_AXIOM(s.GetStringView() == "Foo::Bar");
        TF_AXIOM(std::strcmp(s.c_str(), "Foo::Bar") == 0);
        // c_str() must be NUL terminated; data() and c_str() agree.
        TF_AXIOM(s.c_str()[s.size()] == '\0');
        TF_AXIOM(s.data() == s.c_str());
    }

    // The empty cases.
    {
        TfEternalString dflt;
        TF_AXIOM(dflt.empty());
        TF_AXIOM(dflt.size() == 0);
        TF_AXIOM(dflt.c_str() != nullptr);
        TF_AXIOM(dflt.c_str()[0] == '\0');
        TF_AXIOM(dflt == "");
        TF_AXIOM(dflt == std::string());

        TfEternalString empty = TfEternalString::LeakCopy("");
        TF_AXIOM(empty.empty());
        TF_AXIOM(empty == dflt);
    }

    // LeakCopy copies, so the source need not outlive the result.  This is the
    // core immortality claim: the data survives its origin.
    {
        char const *survivor = nullptr;
        {
            std::string temporary = "transient content";
            survivor = TfEternalString::LeakCopy(temporary).c_str();
        }
        TF_AXIOM(std::strcmp(survivor, "transient content") == 0);
    }

    // Embedded NULs are not supported: content is trimmed at the first NUL, so
    // that this type agrees with TfToken about what a given argument names.
    // Interning through the token table later must not change these results.
    {
        TfEternalString s =
            TfEternalString::LeakCopy(std::string_view("a\0b", 3));
        TF_AXIOM(s.size() == 1);
        TF_AXIOM(s == "a");
        TF_AXIOM(s == TfEternalString::LeakCopy("a"));

        // Content differing only past a NUL names the same string.
        TF_AXIOM(s == TfEternalString::LeakCopy(std::string_view("a\0zzz", 5)));

        // Trimming also resolves what would otherwise be an odd case: content
        // that is nothing but a NUL is the empty string, not a size-1 string
        // that prints as empty.
        TfEternalString justNul =
            TfEternalString::LeakCopy(std::string_view("\0", 1));
        TF_AXIOM(justNul.empty());
        TF_AXIOM(justNul == TfEternalString());

        // Trimming makes size() and strlen(c_str()) agree unconditionally.
        TF_AXIOM(s.size() == std::strlen(s.c_str()));
        TF_AXIOM(justNul.size() == std::strlen(justNul.c_str()));
    }

    // Copying shares the address; the type is a small, immutable handle.
    {
        TF_AXIOM(sizeof(TfEternalString) == sizeof(char const *));

        TfEternalString a = TfEternalString::LeakCopy("shared");
        TfEternalString b = a;
        TfEternalString c;
        c = a;
        TF_AXIOM(b.data() == a.data());
        TF_AXIOM(c.data() == a.data());
        TF_AXIOM(a == b && b == c);
    }

    // The contract: same address implies same content, but equal content does
    // not imply the same address.  Equality and hashing are by content.
    {
        TfEternalString x = TfEternalString::LeakCopy("Outer::Method");
        TfEternalString y = TfEternalString::LeakCopy("Outer::Method");

        // Two separate allocations...
        TF_AXIOM(x.data() != y.data());
        // ...that must still compare and hash equal.
        TF_AXIOM(x == y);
        TF_AXIOM(!(x != y));
        TF_AXIOM(TfHash()(x) == TfHash()(y));

        // Distinct content compares unequal.
        TfEternalString z = TfEternalString::LeakCopy("Outer::Other");
        TF_AXIOM(x != z);
        TF_AXIOM(!(x == z));
    }

    // Comparison against std::string and char const *, both orders.
    {
        TfEternalString s = TfEternalString::LeakCopy("cmp");
        std::string const same = "cmp";
        std::string const diff = "nope";

        TF_AXIOM(s == same);
        TF_AXIOM(same == s);
        TF_AXIOM(s != diff);
        TF_AXIOM(diff != s);
        TF_AXIOM(s == "cmp");
        TF_AXIOM("cmp" == s);
        TF_AXIOM(s != "nope");
        TF_AXIOM("nope" != s);
        TF_AXIOM(s == std::string_view("cmp"));
        TF_AXIOM(std::string_view("cmp") == s);
    }

    // Concatenation, in both directions and chained.  These exist because the
    // std:: operators are templates and will not see the conversion to
    // std::string const &.
    {
        TfEternalString s = TfEternalString::LeakCopy("Foo::Bar");

        TF_AXIOM(s + " suffix" == "Foo::Bar suffix");
        TF_AXIOM("prefix " + s == "prefix Foo::Bar");
        TF_AXIOM(s + std::string(" str") == "Foo::Bar str");
        TF_AXIOM(std::string("str ") + s == "str Foo::Bar");
        TF_AXIOM(s + s == "Foo::BarFoo::Bar");
        TF_AXIOM(s + " x " + s == "Foo::Bar x Foo::Bar");

        // The result is an ordinary std::string.
        std::string built = s + " built";
        built += "!";
        TF_AXIOM(built == "Foo::Bar built!");

        // Concatenation is length-based rather than c_str()-based, which is
        // still observable: a std::string_view operand may itself contain a
        // NUL, and those bytes must survive even though LeakCopy() trims.
        TF_AXIOM((s + std::string_view("\0z", 2)).size() == 10);
    }

    // Stream insertion.
    {
        TfEternalString s = TfEternalString::LeakCopy("streamed");
        std::ostringstream os;
        os << s << "!";
        TF_AXIOM(os.str() == "streamed!");
    }

    // Interop with std::string via the implicit conversion.
    {
        TfEternalString s = TfEternalString::LeakCopy("interop");

        // Binds to a std::string const & parameter, and to a by-value one.
        TF_AXIOM(_TakeStringRef(s) == "interop");

        // Copy-initializes a std::string.
        std::string copy = s;
        TF_AXIOM(copy == "interop");

        // Usable as a std::string-keyed container key.
        std::map<std::string, int> m;
        m[s] = 7;
        TF_AXIOM(m.find(s) != m.end());
        TF_AXIOM(m.find(s)->second == 7);

        // Storable in a vector of string.
        std::vector<std::string> v;
        v.push_back(s);
        TF_AXIOM(v[0] == s);
    }

    // Interop with std::string_view via the implicit conversion.
    {
        TfEternalString s = TfEternalString::LeakCopy("viewed");

        // Binds to a std::string_view parameter without copying: the callee
        // sees this object's own durable characters.
        TF_AXIOM(_TakeStringView(s) == s.data());

        // Copy-initializes a std::string_view, which also does not copy.
        std::string_view view = s;
        TF_AXIOM(view == "viewed");
        TF_AXIOM(view.data() == s.data());
        TF_AXIOM(view.size() == s.size());

        // Adding the view conversion must not make the comparison operators
        // ambiguous: the hidden friends are exact matches and so still win over
        // std::string_view's own comparisons.
        std::string_view other = "viewed";
        TF_AXIOM(s == other);
        TF_AXIOM(other == s);
        TF_AXIOM(!(s != other));

        // Nor may it disturb resolution for std::string, which reaches the
        // string_view-like member templates by exact deduction.
        std::string str;
        str = s;
        str.append(s);
        TF_AXIOM(str == "viewedviewed");
    }

    // Both conversions yield a reference or view of immortal storage, so
    // anything obtained from them stays valid after the TfEternalString is
    // gone.  This is what lets a callee retain c_str() or a view safely.
    {
        char const *retained = nullptr;
        std::string const *referenced = nullptr;
        std::string_view survivingView;
        {
            TfEternalString scoped = TfEternalString::LeakCopy("retained");
            std::string const &ref = scoped;
            retained = ref.c_str();
            referenced = &ref;
            survivingView = scoped;
        }
        TF_AXIOM(std::strcmp(retained, "retained") == 0);
        TF_AXIOM(*referenced == "retained");
        TF_AXIOM(survivingView == "retained");
        TF_AXIOM(survivingView.data() == retained);
    }

    // Caching an immortal string in a per-call-site static, which is how
    // TF_FUNC_NAME() is expected to use this type.
    {
        // Same call site, called twice: one string, one address.
        char const *first = _OneCallSite();
        char const *second = _OneCallSite();
        TF_AXIOM(first == second);
        TF_AXIOM(std::strlen(first) > 0);
    }

    {
        // Two sites in one function: equal content, independent storage.
        TfEternalString a, b;
        _TwoCallSitesOneFunction(&a, &b);
        TF_AXIOM(a == b);
        TF_AXIOM(a.data() != b.data());
    }

    {
        // Each template instantiation caches separately, and repeat calls to a
        // given instantiation are stable.
        TfEternalString i1 = _TemplateCallSite<int>();
        TfEternalString i2 = _TemplateCallSite<int>();
        TfEternalString c1 = _TemplateCallSite<char>();
        TF_AXIOM(i1.data() == i2.data());
        TF_AXIOM(i1.data() != c1.data());
        // Distinct instantiations of this template have the same __func__, so
        // the content matches even though the storage does not.
        TF_AXIOM(i1 == c1);
    }

    return true;
}

TF_ADD_REGTEST(TfEternalString);
