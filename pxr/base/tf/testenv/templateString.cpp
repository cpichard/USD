//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/regTest.h"
#include "pxr/base/tf/templateString.h"
#include <string>
#include <vector>

using std::string;
using std::vector;
PXR_NAMESPACE_USING_DIRECTIVE

static string
_Replace(
    const string& template_,
    const TfTemplateString::Mapping& mapping = TfTemplateString::Mapping(),
    bool safe = false)
{
    TfTemplateString ts(template_);
    string result = safe
        ? ts.SafeSubstitute(mapping) : ts.Substitute(mapping);
    printf("'%s' -> '%s'\n", template_.c_str(), result.c_str());
    return result;
}

static bool
TestTemplateString()
{
    TF_AXIOM(TfTemplateString("").IsValid());
    TF_AXIOM(TfTemplateString("${var}").IsValid());
    TF_AXIOM(TfTemplateString("$(var)").IsValid());
    TF_AXIOM(!TfTemplateString("${}").IsValid());
    TF_AXIOM(TfTemplateString("$").IsValid());
    TF_AXIOM(TfTemplateString("$$").IsValid());
    TF_AXIOM(TfTemplateString("$.").IsValid());
    TF_AXIOM(TfTemplateString("$valid plus $").IsValid());
    TF_AXIOM(TfTemplateString("Having_no_placeholders_is_valid").IsValid());
    TF_AXIOM(TfTemplateString("#mcat $RCSfile$ $Revision$").IsValid());

    {
        TF_AXIOM(!TfTemplateString("${} ${} ${} ").IsValid());
        TF_AXIOM(TfTemplateString("${} ${} ${} ").GetParseErrors().size() == 3);
    }

    {
        TfTemplateString t("${");
        TF_AXIOM(!t.IsValid());
        TF_AXIOM(!t.GetParseErrors().empty());

        t = TfTemplateString("${foo}");
        TF_AXIOM("${foo}" == t.GetTemplate());
        TF_AXIOM(t.IsValid());
        TF_AXIOM(t.GetParseErrors().empty());
    }


    TF_AXIOM("$(var)" ==
        _Replace("$(var)", {{"var", "value"}}));

    TF_AXIOM("value" ==
        _Replace("$var", {{"var", "value"}}));

    TF_AXIOM("$var" ==
        _Replace("$$var", {{"var", "value"}}));

    TF_AXIOM("$value" ==
        _Replace("$$$var", {{"var", "value"}}));

    TF_AXIOM("$(value)" ==
        _Replace("$($var)", {{"var", "value"}}));

    TF_AXIOM("valued" ==
        _Replace("${var}d", {{"var", "value"}}));

    TF_AXIOM("value-value" ==
        _Replace("$var-value", {{"var", "value"}}));

    TF_AXIOM("0000" ==
        _Replace("$var$var$var$var", {{"var", "0"}}));

    TF_AXIOM("0.0.0.0" ==
        _Replace("${var}.${var}.${var}.${var}", {{"var", "0"}}));

    TF_AXIOM("//brave/b952/shot/b952_17/b952_17.menva" ==
        _Replace("//$unit/$prod/shot/$shot/$shot.menva", {
            {"unit", "brave"}, {"prod", "b952"}, {"shot", "b952_17"} }));

    TF_AXIOM("Please remit the $sum of $19.95" ==
        _Replace("Please remit the $$sum of $$$sum",
            {{"sum", "19.95"}}));

    {
        TfErrorMark m;
        TF_AXIOM("Unreplaced placeholders ${are} awesome" ==
            _Replace("Unreplaced placeholders ${are} awesome",
                TfTemplateString::Mapping(), true));
        TF_AXIOM(m.IsClean());
    }

    {
        fprintf(stderr, "=== Begin Expected Error ===\n");

        TfErrorMark m;
        TF_AXIOM("Unreplaced placeholders ${are} not awesome" ==
            _Replace("Unreplaced placeholders ${are} not awesome"));
        TF_AXIOM(!m.IsClean());
        m.Clear();

        fprintf(stderr, "=== End Expected Error ===\n");
    }

    {
        fprintf(stderr, "=== Begin Expected Error ===\n");

        TfErrorMark m;
        TF_AXIOM("Invalid characters in placeholders ${are not awesome" ==
            _Replace("Invalid characters in placeholders ${are not awesome"));
        TF_AXIOM(!m.IsClean());
        m.Clear();

        fprintf(stderr, "=== End Expected Error ===\n");
    }

    {
        fprintf(stderr, "=== Begin Expected Error ===\n");

        TfErrorMark m;
        TF_AXIOM("Never stop ${quoting" ==
            _Replace("Never stop ${quoting"));
        TF_AXIOM(!m.IsClean());
        m.Clear();

        fprintf(stderr, "=== End Expected Error ===\n");
    }

    {
        fprintf(stderr, "=== Begin Expected Error ===\n");

        TfErrorMark m;
        TF_AXIOM("${}" == _Replace("${}"));
        TF_AXIOM(!m.IsClean());
        m.Clear();

        fprintf(stderr, "=== End Expected Error ===\n");
    }

    {
        fprintf(stderr, "=== Begin Expected Error ===\n");

        TfErrorMark m;
        TF_AXIOM("${  }" == _Replace("${  }"));
        TF_AXIOM(!m.IsClean());
        m.Clear();

        fprintf(stderr, "=== End Expected Error ===\n");
    }

    {
        TfTemplateString t("//$unit/$prod/shot/$shot/$shot.menva");
        TfTemplateString::Mapping mapping = t.GetEmptyMapping();
        TF_AXIOM(mapping.find("unit") != mapping.end());
        TF_AXIOM(mapping.find("prod") != mapping.end());
        TF_AXIOM(mapping.find("shot") != mapping.end());
        TF_AXIOM(t.IsValid());
        TF_AXIOM(t.GetParseErrors().empty());
    }

    {
        TfTemplateString t("${ }");
        TfTemplateString::Mapping mapping = t.GetEmptyMapping();
        TF_AXIOM(mapping.empty());
        TF_AXIOM(!t.IsValid());
        TF_AXIOM(!t.GetParseErrors().empty());
    }

    return true;
}

static bool
Test_TfTemplateString()
{
    return TestTemplateString();
}

TF_ADD_REGTEST(TfTemplateString);
