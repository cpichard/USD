//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/base/gf/color.h"
#include "pxr/base/gf/colorSpace.h"
#include "pxr/base/tf/diagnostic.h"
#include "../nc/nanocolor.h"
#include "../colorSpace_data.h"
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

class GfColorTest : public GfColor {
public:
    // wrap the constructors
    GfColorTest() : GfColor() {}
    GfColorTest(const GfColorSpace& colorSpace) : GfColor(colorSpace) {}
    GfColorTest(const GfVec3f &rgb, const GfColorSpace& colorSpace) 
        : GfColor(rgb, colorSpace) {}
    GfColorTest(const GfColor& srcColor, const GfColorSpace& dstColorSpace) 
        : GfColor(srcColor, dstColorSpace) {}
    GfColorTest(const GfColor& srcColor) : GfColor(srcColor) {}

    // Get the CIEXY coordinate of the color in the chromaticity chart.
    GfVec2f GetChromaticity() const {
        return _GetChromaticity();
    }

    // Set the color from a CIEXY coordinate in the chromaticity chart.
    void SetFromChromaticity(const GfVec2f& xy) {
        _SetFromChromaticity(xy);
    }

};


bool ColorApproxEq(const GfColorTest& c1, const GfColorTest& c2)
{
    return GfIsClose(c1.GetRGB(), c2.GetRGB(), 1e-5f);
}


// Calculate barycentric coordinates of a point p with respect to a triangle formed by vertices v0, v1, and v2
bool PointInTriangle(const GfVec2f& p, const GfVec2f& v0, const GfVec2f& v1, const GfVec2f& v2) {
    GfVec2f v0v1 = v1 - v0;
    GfVec2f v0v2 = v2 - v0;
    GfVec2f vp = p - v0;

    float dot00 = GfDot(v0v1, v0v1);
    float dot01 = GfDot(v0v1, v0v2);
    float dot02 = GfDot(v0v1, vp);
    float dot11 = GfDot(v0v2, v0v2);
    float dot12 = GfDot(v0v2, vp);

    // Compute barycentric coordinates
    float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
    float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
    float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

    // Check if point p is inside the triangle
    return (u >= 0) && (v >= 0) && (u + v <= 1);
}

/*
    Note that by necessity, GfColor & GfColorSpace are tested together.
 */

void basicColorTests() {
    GfColorSpace csSRGB(GfColorSpaceNames->SRGBRec709);
    GfColorSpace csLinearSRGB(GfColorSpaceNames->LinearRec709);
    GfColorSpace csLinearRec709(GfColorSpaceNames->LinearRec709);
    GfColorSpace csG22Rec709(GfColorSpaceNames->G22Rec709);
    GfColorSpace csAp0(GfColorSpaceNames->LinearAP0);
    GfColorSpace csSRGBP3(GfColorSpaceNames->SRGBP3D65);
    GfColorSpace csLinearRec2020(GfColorSpaceNames->LinearRec2020);

    GfColor mauveLinear(GfVec3f(0.5f, 0.25f, 0.125f), csLinearRec709);
    GfColor mauveGamma(mauveLinear, csG22Rec709);

    GfVec2f cr_baseline_linear = GfColorTest(mauveLinear).GetChromaticity();
    GfVec2f cr_baseline_curve = GfColorTest(mauveGamma).GetChromaticity();
    GfVec2f wpD65xy = GfColorTest(GfVec3f(1.0f, 1.0f, 1.0f), csLinearRec709).GetChromaticity();

    GfVec2f ap0Primaries[3] = {
        { 0.7347, 0.2653  },
        { 0.0000, 1.0000  },
        { 0.0001, -0.0770 },
    };
    GfVec2f rec2020Primaries[3] = {
        { 0.708, 0.292 },
        { 0.170, 0.797 },
        { 0.131, 0.046 },
    };
    GfVec2f rec709Primaries[3] = {
        { 0.640, 0.330 },
        { 0.300, 0.600 },
        { 0.150, 0.060 },
    };

    // test the color space names
    {
        TF_AXIOM(GfColorSpaceNames->LinearAP1 == TfToken("lin_ap1_scene"));
        TF_AXIOM(GfColorSpaceNames->LinearAP0 == TfToken("lin_ap0_scene"));
        TF_AXIOM(GfColorSpaceNames->LinearRec709 == TfToken("lin_rec709_scene"));
        TF_AXIOM(GfColorSpaceNames->LinearP3D65 == TfToken("lin_p3d65_scene"));
        TF_AXIOM(GfColorSpaceNames->LinearRec2020 == TfToken("lin_rec2020_scene"));
        TF_AXIOM(GfColorSpaceNames->LinearAdobeRGB == TfToken("lin_adobergb_scene"));
        TF_AXIOM(GfColorSpaceNames->LinearCIEXYZD65 == TfToken("lin_ciexyzd65_scene"));
        TF_AXIOM(GfColorSpaceNames->SRGBRec709 == TfToken("srgb_rec709_scene"));
        TF_AXIOM(GfColorSpaceNames->G22Rec709 == TfToken("g22_rec709_scene"));
        TF_AXIOM(GfColorSpaceNames->G18Rec709 == TfToken("g18_rec709_scene"));
        TF_AXIOM(GfColorSpaceNames->SRGBAP1 == TfToken("srgb_ap1_scene"));
        TF_AXIOM(GfColorSpaceNames->G22AP1 == TfToken("g22_ap1_scene"));
        TF_AXIOM(GfColorSpaceNames->SRGBP3D65 == TfToken("srgb_p3d65_scene"));
        TF_AXIOM(GfColorSpaceNames->G22AdobeRGB == TfToken("g22_adobergb_scene"));
        TF_AXIOM(GfColorSpaceNames->Identity == TfToken("identity"));
        TF_AXIOM(GfColorSpaceNames->Data == TfToken("data"));
        TF_AXIOM(GfColorSpaceNames->Unknown == TfToken("unknown"));
        TF_AXIOM(GfColorSpaceNames->CIEXYZ == TfToken("lin_ciexyzd65_scene"));
        TF_AXIOM(GfColorSpaceNames->LinearDisplayP3 == TfToken("lin_p3d65_scene"));
    }

    // test that all the color space names in the enumeration are valid colorspaces
    {
        for (const TfToken& token : GfColorSpaceNames->allTokens) {
            TF_AXIOM(GfColorSpace::IsValid(token));
        }
    }

    // test default construction
    {
        GfColor c;
        TF_AXIOM(c.GetColorSpace() == csLinearRec709);
        TF_AXIOM(c.GetRGB() == GfVec3f(0, 0, 0));
    }
    // test copy construction
    {
        GfColor c(GfVec3f(0.5f, 0.5f, 0.5f), csSRGB);
        GfColor c2(c);
        TF_AXIOM(c2.GetColorSpace() == csSRGB);
        TF_AXIOM(c2.GetRGB() == GfVec3f(0.5f, 0.5f, 0.5f));
    }
    // test construction with color space
    {
        GfColor c(csSRGB);
        TF_AXIOM(c.GetColorSpace() == csSRGB);
        TF_AXIOM(c.GetRGB() == GfVec3f(0, 0, 0));
    }
    // test construction with color space and RGB
    {
        GfColor c(GfVec3f(0.5f, 0.5f, 0.5f), csSRGB);
        TF_AXIOM(c.GetColorSpace() == csSRGB);
        TF_AXIOM(c.GetRGB() == GfVec3f(0.5f, 0.5f, 0.5f));
    }
    {
        // test that a EOTF curve <-> Linear works
        GfColor c1(mauveLinear, csSRGB);    // convert linear to SRGB
        GfColor c2(c1, csLinearSRGB);
        TF_AXIOM(GfIsClose(mauveLinear, c2, 1e-6f));
        GfColor c3(c2, csSRGB);
        TF_AXIOM(GfIsClose(c1, c3, 1e-7f));
    }
    {
        // test round tripping to Rec2020
        GfColor c1(mauveLinear, csLinearRec2020);
        GfColor c2(c1, csLinearSRGB);
        TF_AXIOM(GfIsClose(mauveLinear, c2, 1e-5f));
    }
    // test CIE XY equality, and thus also GetChromaticity
    {
        // MauveLinear and Gamma are both have a D65 white point.
        GfColorTest col_SRGB(mauveLinear, csSRGB);
        GfColorTest col_ap0(col_SRGB, csAp0);       // different white point
        GfColorTest col_SRGBP3(col_ap0, csSRGBP3);  // adapt to d65 for comparison
        GfColorTest col_SRGB_2(col_ap0, csSRGB);
        GfColorTest col_SRGB_3(col_SRGBP3, csSRGB);

        GfVec2f cr_SRGB = col_SRGB.GetChromaticity();
        GfVec2f cr_SRGB_2 = col_SRGB_2.GetChromaticity();
        GfVec2f cr_SRGB_3 = col_SRGB_3.GetChromaticity();

        TF_AXIOM(GfIsClose(cr_baseline_linear, cr_baseline_curve, 1e-5f));
        TF_AXIOM(GfIsClose(cr_baseline_linear, cr_SRGB, 1e-5f));
        TF_AXIOM(GfIsClose(cr_SRGB_2, cr_SRGB_3, 2e-2f));
        TF_AXIOM(GfIsClose(cr_baseline_linear, cr_SRGB_2, 5e-2f));
        TF_AXIOM(GfIsClose(cr_baseline_linear, cr_SRGB_3, 2e-2f));
    }
    // test construction with conversion
    {
        //print out all the values as we go and report to Rick

        GfColorTest colG22Rec709(mauveLinear, csG22Rec709);
        TF_AXIOM(GfIsClose(colG22Rec709, mauveGamma, 1e-5f));
        GfColorTest colLinRec709(colG22Rec709, csLinearRec709);
        TF_AXIOM(GfIsClose(colLinRec709, mauveLinear, 1e-5f));

        // verify assignment didn't mutate cs
        TF_AXIOM(colG22Rec709.GetColorSpace() == csG22Rec709);

        TF_AXIOM(colLinRec709.GetColorSpace() == csLinearRec709);
        GfColorTest colSRGB_2(colLinRec709, csSRGB);
        GfVec2f rec709_chroma_1 = colG22Rec709.GetChromaticity();
        GfVec2f rec709_chroma_2 = colSRGB_2.GetChromaticity();
        TF_AXIOM(GfIsClose(rec709_chroma_1, rec709_chroma_2, 1e-5f));
        GfColorTest colAp0(colSRGB_2, csAp0);
        //GfVec2f ap0_chroma = colAp0.GetChromaticity();
        GfColorTest colSRGB_3(colAp0, csSRGB);
        GfVec2f rec709_chroma_3 = colSRGB_2.GetChromaticity();
        TF_AXIOM(GfIsClose(rec709_chroma_1, rec709_chroma_3, 3e-2f));
        GfColorTest col_SRGBP3(colSRGB_3, csSRGBP3);
        //GfVec2f xy5 = col_SRGBP3.GetChromaticity();

        // all the way back to rec709
        GfColorTest colLinRec709_2(col_SRGBP3, csLinearRec709);
        TF_AXIOM(GfIsClose(colLinRec709_2, colLinRec709, 1e-5f));
    }
    // test move constructor
    {
        GfColor c1(GfVec3f(0.5f, 0.25f, 0.125f), csAp0);
        GfColor c2(std::move(c1));
        GfColorSpace c1cs = c1.GetColorSpace();
        TF_AXIOM(c2.GetColorSpace() == csAp0);
        TF_AXIOM(GfIsClose(c2.GetRGB(), GfVec3f(0.5f, 0.25f, 0.125f), 1e-5f));
    }
    // test copy assignment
    {
        GfColor c1(GfVec3f(0.5f, 0.25f, 0.125f), csSRGB);
        GfColor c2 = c1;
        TF_AXIOM(GfIsClose(c1, c2, 1e-5f));
        TF_AXIOM(c1.GetColorSpace() == c2.GetColorSpace());

        // move assignment, overwriting existing values
        GfColor c3(GfVec3f(0.5f, 0.25f, 0.125f), csAp0);
        GfColor c4(GfVec3f(0.25f, 0.5f, 0.125f), csSRGB);
        c3 = std::move(c4);
        TF_AXIOM(GfIsClose(c3.GetRGB(), GfVec3f(0.25f, 0.5f, 0.125f), 1e-5f));
        TF_AXIOM(c3.GetColorSpace() == csSRGB);
    }
    // test color space inequality
    {
        TF_AXIOM(csSRGB != csLinearSRGB);
        TF_AXIOM(csSRGB != csLinearRec709);
        TF_AXIOM(csSRGB != csG22Rec709);
        TF_AXIOM(csSRGB != csAp0);
        TF_AXIOM(csSRGB != csSRGBP3);
        TF_AXIOM(csSRGB != csLinearRec2020);
    }
    // test that constructing from Kelvin at 6504 is in the near vicinity of
    // the chromaticity of D65, even though spectrally, they are unrelated.
    // nb. 6504 is the CCT match to D65
    {
        GfColorTest c;
        c.SetFromPlanckianLocus(6504, 1.0f);
        GfVec2f xy = c.GetChromaticity();
        TF_AXIOM(GfIsClose(xy, wpD65xy, 1e-2f));
    }
    // test that primaries correspond to unit vectors in their color space
    {
        GfColorTest c1(csAp0);
        c1.SetFromChromaticity(ap0Primaries[0]);
        GfColorTest c2(csAp0);
        c2.SetFromChromaticity(ap0Primaries[1]);
        GfColorTest c3(csAp0);
        c3.SetFromChromaticity(ap0Primaries[2]);

        GfColorTest c4(csLinearRec2020);
        c4.SetFromChromaticity(rec2020Primaries[0]);
        GfColorTest c5(csLinearRec2020);
        c5.SetFromChromaticity(rec2020Primaries[1]);
        GfColorTest c6(csLinearRec2020);
        c6.SetFromChromaticity(rec2020Primaries[2]);
        TF_AXIOM(GfIsClose(c4, GfColorTest(GfVec3f(1, 0, 0), csLinearRec2020), 1e-5f));
        TF_AXIOM(GfIsClose(c5, GfColorTest(GfVec3f(0, 1, 0), csLinearRec2020), 1e-5f));
        TF_AXIOM(GfIsClose(c6, GfColorTest(GfVec3f(0, 0, 1), csLinearRec2020), 1e-5f));

        GfColorTest c7(csLinearRec709);
        c7.SetFromChromaticity(rec709Primaries[0]);
        GfColorTest c8(csLinearRec709);
        c8.SetFromChromaticity(rec709Primaries[1]);
        GfColorTest c9(csLinearRec709);
        c9.SetFromChromaticity(rec709Primaries[2]);
        TF_AXIOM(GfIsClose(c7, GfColorTest(GfVec3f(1, 0, 0), csLinearRec709), 1e-5f));
        TF_AXIOM(GfIsClose(c8, GfColorTest(GfVec3f(0, 1, 0), csLinearRec709), 1e-5f));
        TF_AXIOM(GfIsClose(c9, GfColorTest(GfVec3f(0, 0, 1), csLinearRec709), 1e-5f));
    }

    // permute the rec709 primaries through rec2020 and ap0
    // and verify that at each point the converted colors are contained
    // within the gamut of the target color space by testing that the
    // converted color is within the triangle formed by the target color
    // space's primaries
    {
        // Create converted colors
        GfColorTest red709(GfVec3f(1.0f, 0.0f, 0.0f), csLinearRec709);
        GfColorTest green709(GfVec3f(0.0f, 1.0f, 0.0f), csLinearRec709);
        GfColorTest blue709(GfVec3f(0.0f, 0.0f, 1.0f), csLinearRec709);
        GfColorTest red2020(GfVec3f(1.0f, 0.0f, 0.0f), csLinearRec2020);
        GfColorTest green2020(GfVec3f(0.0f, 1.0f, 0.0f), csLinearRec2020);
        GfColorTest blue2020(GfVec3f(0.0f, 0.0f, 1.0f), csLinearRec2020);
        GfColorTest redAp0(GfVec3f(1.0f, 0.0f, 0.0f), csAp0);
        GfColorTest greenAp0(GfVec3f(0.0f, 1.0f, 0.0f), csAp0);
        GfColorTest blueAp0(GfVec3f(0.0f, 0.0f, 1.0f), csAp0);

        // Verify that converted 709 colors are within rec2020 gamut
        TF_AXIOM(PointInTriangle(red709.GetChromaticity(),
                                 red2020.GetChromaticity(), 
                                 green2020.GetChromaticity(),
                                 blue2020.GetChromaticity()));
        TF_AXIOM(PointInTriangle(green709.GetChromaticity(),
                                 red2020.GetChromaticity(), 
                                 green2020.GetChromaticity(),
                                 blue2020.GetChromaticity()));
        TF_AXIOM(PointInTriangle(blue709.GetChromaticity(),
                                 red2020.GetChromaticity(), 
                                 green2020.GetChromaticity(),
                                 blue2020.GetChromaticity()));

        // Verify that converted 709 colors are within ap0 gamut
        TF_AXIOM(PointInTriangle(red709.GetChromaticity(),
                                 redAp0.GetChromaticity(), 
                                 greenAp0.GetChromaticity(),
                                 blueAp0.GetChromaticity()));
        TF_AXIOM(PointInTriangle(green709.GetChromaticity(),
                                 redAp0.GetChromaticity(), 
                                 greenAp0.GetChromaticity(),
                                 blueAp0.GetChromaticity()));
        TF_AXIOM(PointInTriangle(blue709.GetChromaticity(),
                                 redAp0.GetChromaticity(), 
                                 greenAp0.GetChromaticity(),
                                 blueAp0.GetChromaticity()));
    }
}

void testColorSpace() {
    GfVec2f redChroma(0.64f, 0.33f);
    GfVec2f greenChroma(0.30f, 0.60f);
    GfVec2f blueChroma(0.15f, 0.06f);
    GfVec2f whitePoint(0.3127f, 0.3290f); // D65
    float gamma = 2.2f;
    float linearBias = 0.05f;
    
    TfToken customName("custom_getter_test");
    GfColorSpace customSpace(customName, redChroma, greenChroma, blueChroma, 
                             whitePoint, gamma, linearBias);
    
    TF_AXIOM(customName == customSpace.GetName());
    auto primaries = customSpace.GetPrimariesAndWhitePoint();
    TF_AXIOM(std::get<0>(primaries) == redChroma);
    TF_AXIOM(std::get<1>(primaries) == greenChroma);
    TF_AXIOM(std::get<2>(primaries) == blueChroma);
    TF_AXIOM(std::get<3>(primaries) == whitePoint);
    TF_AXIOM(customSpace.GetGamma() == gamma);
    TF_AXIOM(customSpace.GetLinearBias() == linearBias);

    auto transferParams = customSpace.GetTransferFunctionParams();
    bool transferParamsExist = (transferParams.first != 0.0f || transferParams.second != 0.0f);
    TF_AXIOM(transferParamsExist);

    // Define a custom RGB to XYZ matrix (Rec.709 matrix)
    GfMatrix3f rgbToXYZ(
        0.4124f, 0.3576f, 0.1805f,
        0.2126f, 0.7152f, 0.0722f,
        0.0193f, 0.1192f, 0.9505f
    );
    
    // Construct custom space from matrix
    TfToken matrixSpaceName("matrix_space");
    GfColorSpace matrixSpace(matrixSpaceName, rgbToXYZ, gamma, linearBias);
    TF_AXIOM(matrixSpaceName == matrixSpace.GetName());
    GfMatrix3f returnedMatrix = matrixSpace.GetRGBToXYZ();
    TF_AXIOM(GfIsClose(returnedMatrix, rgbToXYZ, 1e-5f));
    TF_AXIOM(GfIsClose(matrixSpace.GetGamma(), gamma, 1e-6f));
    TF_AXIOM(GfIsClose(matrixSpace.GetLinearBias(), linearBias, 1e-6f));
}

void testPlanckianLocus() {
    // Test Kelvin to Yxy conversion for values that are 1000K apart from
    // 1000 to 15000 Kelvin
    {
        GfColorSpace csIdentity(GfColorSpaceNames->Data);
        GfVec2f tableOfKnownValues[] = {
            { 0.6530877f, 0.3446811f },
            { 0.5266493f, 0.4133117f },
            { 0.4370493f, 0.4043753f },
            { 0.3804111f, 0.3765993f },
            { 0.3450407f, 0.3512992f },
            { 0.3220662f, 0.3315561f },
            { 0.3064031f, 0.3165002f },
            { 0.2952405f, 0.3049043f },
            { 0.2869792f, 0.2958082f },
            { 0.2806694f, 0.2885335f },
            { 0.2757214f, 0.2826093f },
            { 0.2717545f, 0.2777060f },
            { 0.2685138f, 0.2735892f },
            { 0.2658236f, 0.2700888f },
            { 0.2635591f, 0.2670793f },
        };
        // test against known values. Although the approximation returns
        // values between 1000 and 2000, they are slightly divergent from
        // canonical values.
        for (int kelvin = 1000; kelvin <= 15000; kelvin += 1000) {
            GfColorTest c(csIdentity);
            c.SetFromPlanckianLocus(kelvin, 1.0f);
            GfVec2f xy = c.GetChromaticity();
            int index = (kelvin - 1000) / 1000;
            GfVec2f known = tableOfKnownValues[index];
            TF_AXIOM(GfIsClose(xy, known, 1e-3f));
        }
    }
    
    // Test invalid Kelvin values (out of range)
    GfColor lowKelvinColor(GfColorSpace(GfColorSpaceNames->LinearRec709));
    lowKelvinColor.SetFromPlanckianLocus(999.0f, 1.0f);  // Below 1000K
    
    // Should clamp to valid range and produce finite values
    GfVec3f lowKelvinRGB = lowKelvinColor.GetRGB();
    bool lowKelvinValid = !std::isnan(lowKelvinRGB[0]) && !std::isnan(lowKelvinRGB[1]) && 
                          !std::isnan(lowKelvinRGB[2]) && !std::isinf(lowKelvinRGB[0]) && 
                          !std::isinf(lowKelvinRGB[1]) && !std::isinf(lowKelvinRGB[2]);
    TF_AXIOM(lowKelvinValid);

    GfColor highKelvinColor(GfColorSpace(GfColorSpaceNames->LinearRec709));
    highKelvinColor.SetFromPlanckianLocus(15001.0f, 1.0f);  // Above 15000K
    
    // Should clamp to valid range and produce finite values
    GfVec3f highKelvinRGB = highKelvinColor.GetRGB();
    bool highKelvinValid = !std::isnan(highKelvinRGB[0]) && !std::isnan(highKelvinRGB[1]) && 
                           !std::isnan(highKelvinRGB[2]) && !std::isinf(highKelvinRGB[0]) && 
                           !std::isinf(highKelvinRGB[1]) && !std::isinf(highKelvinRGB[2]);
    TF_AXIOM(highKelvinValid);
}

void testSpanConversions() {
    std::vector<TfToken> specialSpaces = {
        GfColorSpaceNames->Identity,
        GfColorSpaceNames->Data,
        GfColorSpaceNames->Raw,
        GfColorSpaceNames->Unknown
    };
    
    // Regular color spaces for comparison
    std::vector<TfToken> regularSpaces = {
        GfColorSpaceNames->LinearRec709,
        GfColorSpaceNames->G22AP1
    };
    
    const float epsilon = 1e-6f;
    for (const auto& specialToken : specialSpaces) {
        for (const auto& regularToken : regularSpaces) {
            // Create color spaces
            GfColorSpace specialSpace(specialToken);
            GfColorSpace regularSpace(regularToken);
            
            // Create a span; a prime number of three tuples.
            std::vector<float> rgbValues(3*37);
            for (size_t i = 0; i < rgbValues.size(); ++i) {
                rgbValues[i] = static_cast<float>(i % 256) / 255.0f;
            }

            // Create a copy for validation
            std::vector<float> originalValues = rgbValues;

            // convert the test values in place to the special space
            TfSpan<float> rgbSpan(rgbValues.data(), rgbValues.size());
            regularSpace.ConvertRGBSpan(specialSpace, rgbSpan);

            // Verify values are didn't go out of bounds
            for (size_t i = 0; i < rgbValues.size(); ++i) {
                if (!TF_AXIOM(rgbValues[i] >= -epsilon && rgbValues[i] <= 1.0f + epsilon)) {
                    break;
                }
            }

            // convert the test values in place to the regular space
            specialSpace.ConvertRGBSpan(regularSpace, rgbSpan);
            
            // Verify values are preserved within tolerance
            for (size_t i = 0; i < rgbValues.size(); ++i) {
                TF_AXIOM(rgbValues[i] >= -epsilon && rgbValues[i] <= 1.0f + epsilon);
                if (!TF_AXIOM(GfIsClose(rgbValues[i], originalValues[i], 1e-5f))) {
                    break;
                }
            }
        }
    }
    
    for (const auto& specialToken : specialSpaces) {
        for (const auto& regularToken : regularSpaces) {
            // Create color spaces
            GfColorSpace specialSpace(specialToken);
            GfColorSpace regularSpace(regularToken);
            
            // Create a span; a prim number of 4 tuples (RGBA)
            std::vector<float> rgbaValues(4 * 37);
            for (size_t i = 0; i < rgbaValues.size(); ++i) {
                rgbaValues[i] = static_cast<float>(i % 256) / 255.0f;
            }
            
            // Create a copy for validation
            std::vector<float> originalValues = rgbaValues;
            
            TfSpan<float> rgbaSpan(rgbaValues.data(), rgbaValues.size());
            
            // Convert from special to regular
            regularSpace.ConvertRGBASpan(specialSpace, rgbaSpan);
            
            // Convert back to special
            specialSpace.ConvertRGBASpan(regularSpace, rgbaSpan);
            
            // Verify RGB values are preserved within tolerance
            for (size_t i = 0; i < rgbaValues.size(); ++i) {
                // Skip alpha values (every 4th value)
                if (i % 4 == 3) {
                    // Alpha should be exactly preserved
                    if (!TF_AXIOM(rgbaValues[i] >= 0.0f && rgbaValues[i] <= 1.0f)) {
                        break;
                    }
                    if (!TF_AXIOM(rgbaValues[i] == originalValues[i])) {
                        break;
                    }
                } 
                else {
                    // RGB should be preserved within tolerance
                    const float epsilon = 1e-6f;
                    TF_AXIOM(rgbaValues[i] >= -epsilon && rgbaValues[i] <= 1.0f + epsilon);
                    if (!TF_AXIOM(GfIsClose(rgbaValues[i], originalValues[i], 1e-5f))) {
                        break;
                    }
                }
            }
        }
    }
}

void testKnownColorSpanConversion() {
    GfColorSpace csLinearRec709(GfColorSpaceNames->LinearRec709);
    GfColor linear_rec709_scene(GfVec3f(0.5f, 0.25f, 0.125f), csLinearRec709);
    GfColorSpace csAp0(GfColorSpaceNames->LinearAP0);
    GfColor lin_ap0_scene(GfVec3f(0.337736f, 0.260346f, 0.145521f), csAp0);
    GfColorSpace csSRGBP3(GfColorSpaceNames->SRGBP3D65);
    GfColor srgb_p3d65_scene(GfVec3f(0.7053029f, 0.545210f, 0.410651f), csSRGBP3);

    // Create a span with the linear rec709 color, repeated 47 times
    std::vector<float> rgbValues(3 * 47);
    for (size_t i = 0; i < rgbValues.size(); ++i) {
        rgbValues[i] = linear_rec709_scene.GetRGB()[i % 3];
    }
    TfSpan<float> rgbSpan(rgbValues.data(), rgbValues.size());
    // Convert the span to AP0
    csLinearRec709.ConvertRGBSpan(csAp0, rgbSpan);
    // Verify the conversion
    for (size_t i = 0; i < rgbValues.size(); i += 3) {
        GfVec3f expected(lin_ap0_scene.GetRGB()[0], 
                         lin_ap0_scene.GetRGB()[1],
                         lin_ap0_scene.GetRGB()[2]);
        TF_AXIOM(GfIsClose(GfVec3f(rgbValues[i], rgbValues[i + 1], rgbValues[i + 2]), 
                           expected, 1e-5f));
    }
    // Now convert the span to P3D65
    csAp0.ConvertRGBSpan(csSRGBP3, rgbSpan);
    // Verify the conversion
    for (size_t i = 0; i < rgbValues.size(); i += 3) {
        GfVec3f expected(srgb_p3d65_scene.GetRGB()[0], 
                         srgb_p3d65_scene.GetRGB()[1],
                         srgb_p3d65_scene.GetRGB()[2]);
        TF_AXIOM(GfIsClose(GfVec3f(rgbValues[i], rgbValues[i + 1], rgbValues[i + 2]), 
                           expected, 1e-4f));
    }
    // Now convert the span back to Linear Rec709
    csSRGBP3.ConvertRGBSpan(csLinearRec709, rgbSpan);
    // Verify the conversion
    for (size_t i = 0; i < rgbValues.size(); i += 3) {
        GfVec3f expected(linear_rec709_scene.GetRGB()[0], 
                         linear_rec709_scene.GetRGB()[1],
                         linear_rec709_scene.GetRGB()[2]);
        TF_AXIOM(GfIsClose(GfVec3f(rgbValues[i], rgbValues[i + 1], rgbValues[i + 2]), 
                           expected, 1e-5f));
    }
}

void testKnownColors() {
    GfColorSpace csSRGB(GfColorSpaceNames->SRGBRec709);
    GfColorSpace csLinearSRGB(GfColorSpaceNames->LinearRec709);
    GfColorSpace csLinearRec709(GfColorSpaceNames->LinearRec709);
    GfColorSpace csG22Rec709(GfColorSpaceNames->G22Rec709);
    GfColorSpace csAp0(GfColorSpaceNames->LinearAP0);
    GfColorSpace csSRGBP3(GfColorSpaceNames->SRGBP3D65);
    GfColorSpace csLinearRec2020(GfColorSpaceNames->LinearRec2020);
    GfColorSpace csLinearP3D65(GfColorSpaceNames->LinearP3D65);

    // test colors; all corresponding to the same value
    GfColor linear_rec709_scene(GfVec3f(0.5f, 0.25f, 0.125f), csLinearRec709);
    GfColor g22_rec709_scene(GfVec3f(0.729740f, 0.532521f, 0.388602f), csG22Rec709);
    GfColor srgb_rec709_scene(GfVec3f(0.735357f, 0.537099f, 0.388573f), csSRGB);
    GfColor lin_ap0_scene(GfVec3f(0.337736f, 0.260346f, 0.145521f), csAp0);
    GfColor srgb_p3d65_scene(GfVec3f(0.7053029f, 0.545210f, 0.410651f), csSRGBP3);

    GfColor check_ap0(linear_rec709_scene, csAp0);
    TF_AXIOM(GfIsClose(check_ap0, lin_ap0_scene, 1e-5f));
    GfColor check_linear_p3d65(linear_rec709_scene, csLinearP3D65);

    // test each of the known colors converted to linear rec709
    {
        GfColor t1(g22_rec709_scene, csLinearRec709);
        TF_AXIOM(GfIsClose(t1, linear_rec709_scene, 1e-5f));
        GfColor t2(srgb_rec709_scene, csLinearRec709);
        TF_AXIOM(GfIsClose(t2, linear_rec709_scene, 1e-5f));
        GfColor t3(lin_ap0_scene, csLinearRec709);
        TF_AXIOM(GfIsClose(t3, linear_rec709_scene, 1e-5f));
        GfColor t4(srgb_p3d65_scene, csLinearRec709);
        TF_AXIOM(GfIsClose(t4, linear_rec709_scene, 1e-3f));

        // convert back to G22Rec709
        GfColor t5(t1, csG22Rec709);
        TF_AXIOM(GfIsClose(t5, g22_rec709_scene, 1e-5f));
        GfColor t6(t2, csG22Rec709);
        TF_AXIOM(GfIsClose(t6, g22_rec709_scene, 1e-5f));
        GfColor t7(t3, csG22Rec709);
        TF_AXIOM(GfIsClose(t7, g22_rec709_scene, 1e-5f));
        GfColor t8(t4, csG22Rec709);
        TF_AXIOM(GfIsClose(t8, g22_rec709_scene, 1e-3f));
    }

    // test each of the known colors converted to csSRGB
    {
        GfColor t1(g22_rec709_scene, csSRGB);
        TF_AXIOM(GfIsClose(t1, srgb_rec709_scene, 1e-5f));
        GfColor t2(srgb_rec709_scene, csSRGB);
        TF_AXIOM(GfIsClose(t2, srgb_rec709_scene, 1e-5f));
        GfColor t3(lin_ap0_scene, csSRGB);
        TF_AXIOM(GfIsClose(t3, srgb_rec709_scene, 1e-5f));
        GfColor t4(srgb_p3d65_scene, csSRGB);
        TF_AXIOM(GfIsClose(t4, srgb_rec709_scene, 1e-4f));

        // convert back to G22Rec709
        GfColor t5(t1, csG22Rec709);
        TF_AXIOM(GfIsClose(t5, g22_rec709_scene, 1e-5f));
        GfColor t6(t2, csG22Rec709);
        TF_AXIOM(GfIsClose(t6, g22_rec709_scene, 1e-5f));
        GfColor t7(t3, csG22Rec709);
        TF_AXIOM(GfIsClose(t7, g22_rec709_scene, 1e-5f));
        GfColor t8(t4, csG22Rec709);
        TF_AXIOM(GfIsClose(t8, g22_rec709_scene, 1e-4f));
    }
}

void testLinRec709ToAP0Matrix() {
    /* 
        The expected matrix for converting from Rec.709 to AP0 is:
            0.43963298 0.38298870 0.17737832
            0.08977644 0.81343943 0.09678413
            0.01754117 0.11154655 0.87091228
    */
    GfColorSpace csLinearRec709(GfColorSpaceNames->LinearRec709);
    GfColorSpace csAp0(GfColorSpaceNames->LinearAP0);
    GfMatrix3f expectedMatrix(
        0.43963298f, 0.38298870f, 0.17737832f,
        0.08977644f, 0.81343943f, 0.09678413f,
        0.01754117f, 0.11154655f, 0.87091228f
    );

    GfMatrix3f rec709toXYZ = csLinearRec709.GetRGBToXYZ();
    GfMatrix3f ap0toXYZ = csAp0.GetRGBToXYZ();
    GfMatrix3f XYZtoAP0 = ap0toXYZ.GetInverse();
    GfMatrix3f conversionMatrix = XYZtoAP0 * rec709toXYZ;

    GfMatrix3f validateMatrix = csAp0.GetRGBToRGB(csLinearRec709);

    TF_AXIOM(GfIsClose(conversionMatrix, expectedMatrix, 1e-5f));
    TF_AXIOM(GfIsClose(conversionMatrix, validateMatrix, 1e-5));
}

int
main(int argc, char *argv[])
{
    basicColorTests();
    testColorSpace();
    testKnownColors();
    testKnownColorSpanConversion();
    testSpanConversions();
    testLinRec709ToAP0Matrix();
    testPlanckianLocus();
    std::cout << "testGfColorCpp Complete" << std::endl;
    return 0;
}
