//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/references.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/stageCache.h"
#include "pxr/usd/usd/stageCacheContext.h"

#include "pxr/usd/sdf/pathExpression.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

// Test that relationship target and attribute connection specs
// created in the .usd file formats have the appropriate spec
// type.
void TestTargetSpecs()
{
    for (const std::string fmt : {"usda", "usdc"}) {
        UsdStageRefPtr stage = 
            UsdStage::CreateInMemory("TestTargetSpecs." + fmt);
        
        UsdPrim prim = stage->DefinePrim(SdfPath("/Test"));

        UsdAttribute attr = prim.CreateAttribute(
            TfToken("attr"), SdfValueTypeNames->Int);
        TF_AXIOM(attr.AddConnection(SdfPath("/Test.dummy")));

        SdfSpecType connSpecType = stage->GetRootLayer()->GetSpecType(
            attr.GetPath().AppendTarget(SdfPath("/Test.dummy")));
        TF_AXIOM(connSpecType == SdfSpecTypeConnection);                

        UsdRelationship rel = prim.CreateRelationship(TfToken("rel"));
        TF_AXIOM(rel.AddTarget(SdfPath("/Test.dummy")));

        SdfSpecType relSpecType = stage->GetRootLayer()->GetSpecType(
            rel.GetPath().AppendTarget(SdfPath("/Test.dummy")));
        TF_AXIOM(relSpecType == SdfSpecTypeRelationshipTarget);
    }
}

// Tests the behavior and return values of the GetConnections for attributes 
// and GetTargets/GetForwardedTargets for relationships. The boolean return
// values are not part of the python API so we test them for the C++ API.
void TestGetTargetsAndConnections()
{
    for (const std::string fmt : {"usda", "usdc"}) {
        UsdStageRefPtr stage = 
            UsdStage::CreateInMemory("TestGetTargets." + fmt);
        
        // Add an attribute to test connections first.        
        UsdPrim attrPrim = stage->DefinePrim(SdfPath("/TestAttr"));
        UsdAttribute attr = attrPrim.CreateAttribute(
            TfToken("attr"), SdfValueTypeNames->Int);
        SdfPathVector conns;
        // No connections to start, GetConnections returns false when there
        // are no authored connections.
        TF_AXIOM(!attr.GetConnections(&conns));
        TF_AXIOM(conns.empty());
        // Add a connection, GetConnections returns true when there are authored
        // connections
        TF_AXIOM(attr.AddConnection(SdfPath("/TestAttr.dummy")));
        TF_AXIOM(attr.GetConnections(&conns));
        TF_AXIOM(conns == SdfPathVector({SdfPath("/TestAttr.dummy")}));

        // Add a relationship on a new prim to test targets.
        UsdPrim relPrim = stage->DefinePrim(SdfPath("/TestRel"));
        UsdRelationship rel = relPrim.CreateRelationship(TfToken("rel"));
        SdfPathVector targets;
        // No targets to start, GetTargets and GetForwardedTargets return false 
        // when there are no authored targets.
        TF_AXIOM(!rel.GetTargets(&targets));
        TF_AXIOM(targets.empty());
        TF_AXIOM(!rel.GetForwardedTargets(&targets));
        TF_AXIOM(targets.empty());

        // Add another relationship to test relationship forwarding
        UsdRelationship forwardingRel = 
            relPrim.CreateRelationship(TfToken("forwardingRel"));
        // Add a target to the previous relationship, GetTargets 
        // returns true and gets the targeted relationship. However 
        // GetForwardedTargets returns false because the only target is a 
        // relationship that has no authored targets.
        TF_AXIOM(forwardingRel.AddTarget(SdfPath("/TestRel.rel")));
        TF_AXIOM(forwardingRel.GetTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestRel.rel")}));
        TF_AXIOM(!forwardingRel.GetForwardedTargets(&targets));
        TF_AXIOM(targets.empty());

        // Add a target, GetTargets on the first relationship returns true when
        // there are authored targets.
        TF_AXIOM(rel.AddTarget(SdfPath("/TestAttr.dummy")));
        TF_AXIOM(rel.GetTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestAttr.dummy")}));
        TF_AXIOM(rel.GetForwardedTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestAttr.dummy")}));
        // GetForwardedTargets on the forwarding relationship also returns true 
        // because its targeted  relation now has targets.
        TF_AXIOM(forwardingRel.GetForwardedTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestAttr.dummy")}));

        // To test the effect of composition errors, add a new prim with a
        // reference to the prim we defined the prior relationships on. There
        // will be a composition error when building targets for the
        // relationship "rel" because the defined target path "/TestAttr.dummy"
        // can't be mapped across the reference from "/TestRef" to "/TestRel"
        UsdPrim refPrim = stage->DefinePrim(SdfPath("/TestRef"));
        refPrim.GetReferences().AddReference(stage->GetRootLayer()->GetIdentifier(),
                                             relPrim.GetPath());
        // "rel" on the referencing prim will have authored targets, but
        // GetTargets will return false because of the composition error.
        UsdRelationship refRel = refPrim.GetRelationship(TfToken("rel"));
        TF_AXIOM(refRel.HasAuthoredTargets());
        TF_AXIOM(!refRel.GetTargets(&targets));
        TF_AXIOM(targets.empty());
        // Add another valid target. Still returns false because of the other
        // composition errors.
        refRel.AddTarget(SdfPath("/TestAttr.dummy"));
        TF_AXIOM(refRel.HasAuthoredTargets());
        TF_AXIOM(!refRel.GetTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestAttr.dummy")}));

        // "forwardingRel" on the referencing prim will still return true for
        // GetTargets because there are no errors mapping the relationship
        // it targets across the reference.
        UsdRelationship refForwardingRel =
            refPrim.GetRelationship(TfToken("forwardingRel"));
        TF_AXIOM(refForwardingRel.HasAuthoredTargets());
        TF_AXIOM(refForwardingRel.GetTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestRef.rel")}));
        // However, GetForwardedTargets will return false because of the
        // target composition errors on "/TestRef.rel"
        TF_AXIOM(!refForwardingRel.GetForwardedTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestAttr.dummy")}));

        // Add another valid target directly to "forwardingRel" on the
        // referencing prim. GetForwardedTargets still returns false because
        // of the downstream composition errors, but it does still get any
        // valid forwarded targets it found along the way.
        TF_AXIOM(refForwardingRel.AddTarget(SdfPath("/TestAttr.attr")));
        TF_AXIOM(!refForwardingRel.GetForwardedTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestAttr.attr"),
                                           SdfPath("/TestAttr.dummy")}));

        // We do this part after the other test cases so we don't have to set
        // up the state again afterwards.
        // Clear the targets on the original relationship. GetTargets returns
        // false again because there are no authored targets.
        TF_AXIOM(rel.ClearTargets(false));
        TF_AXIOM(!rel.GetTargets(&targets));
        TF_AXIOM(targets.empty());
        // GetForwardedTargets on forwarding rel also returns false
        TF_AXIOM(!forwardingRel.GetForwardedTargets(&targets));
        TF_AXIOM(targets.empty());

        // Now explicitly set empty targets for the original relationship.
        // GetTargets returns true because there is an authored opinion even
        // though there are no targets.
        TF_AXIOM(rel.SetTargets({}));
        TF_AXIOM(rel.GetTargets(&targets));
        TF_AXIOM(targets.empty());
        // GetForwardedTargets on forwarding rel also returns true because the
        // targeted relationship has an explicitly authored opinion.
        TF_AXIOM(forwardingRel.GetForwardedTargets(&targets));
        TF_AXIOM(targets.empty());

        // Clear targets on the first rel again and add a non-relationship 
        // target to forwardingRel. Confirm that GetForwardingTargets returns
        // true because the forwardingRel is no longer "purely forwarding" even
        // even the forwarded relationship has no opinions
        TF_AXIOM(rel.ClearTargets(false));
        TF_AXIOM(forwardingRel.AddTarget(SdfPath("/TestAttr.dummy")));
        // rel has no target opinion
        TF_AXIOM(!rel.GetTargets(&targets));
        TF_AXIOM(targets.empty());
        // forwardRel has a relationship target and an attribute target.
        TF_AXIOM(forwardingRel.GetTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestRel.rel"),
                                           SdfPath("/TestAttr.dummy")}));
        // forwardingRel returns true for GetForwardedTargets.
        TF_AXIOM(forwardingRel.GetForwardedTargets(&targets));
        TF_AXIOM(targets == SdfPathVector({SdfPath("/TestAttr.dummy")}));
    }
}

static void
_CheckNoSpecForOpaqueValues(const std::string &formatExtension)
{
    printf("%s %s\n", TF_FUNC_NAME().c_str(), formatExtension.c_str());

    const UsdStageRefPtr stage =
        UsdStage::CreateNew("test_opaque_values." + formatExtension);
    const SdfLayerHandle layer = stage->GetRootLayer();

    const UsdPrim prim = stage->DefinePrim(SdfPath("/Prim"));
    const UsdAttribute attr = prim.CreateAttribute(
        TfToken("attr"), SdfValueTypeNames->Opaque);

    TF_AXIOM(!attr.HasAuthoredValue());

    layer->SetField(
        SdfPath("/Prim.attr"),
        SdfFieldKeys->Default,
        SdfOpaqueValue());
    // We've set a default value, so this should return true.
    TF_AXIOM(attr.HasAuthoredValue());

    {
        TfErrorMark mark;
        mark.SetMark();
        layer->Save();
        // Should have emitted an error about trying to author an opaque value.
        TF_AXIOM(!mark.IsClean());
    }

    // Once we reload the layer from disk, that authored opinion should be gone.
    layer->Reload(/* force = */ true);
    TF_AXIOM(!attr.HasAuthoredValue());
}

void
TestOpaqueValueFileIO()
{
    _CheckNoSpecForOpaqueValues("usda");
    _CheckNoSpecForOpaqueValues("usdc");
}

void
TestOutParamterIgnoredForComposingValues()
{
    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    const SdfLayerHandle layer = stage->GetRootLayer();

    const UsdPrim prim = stage->DefinePrim(SdfPath {"/Prim"});

    // Test that a PathExpression out-paramter's value is ignored, and not
    // composed over authored opinions.
    const UsdAttribute pathExprAttr = prim.CreateAttribute(
        TfToken("pathExpr"), SdfValueTypeNames->PathExpression);
    
    SdfPathExpression outPathExpr { "/out %_" };
    
    // Calling Get() with no authored value should leave the out parameter
    // untouched.
    TF_AXIOM(!pathExprAttr.Get(&outPathExpr));
    TF_AXIOM(outPathExpr == SdfPathExpression { "/out %_" });
    
    // Calling Get() should ignore the value in the out parameter.
    pathExprAttr.Set(SdfPathExpression {"/authored"});
    TF_AXIOM(pathExprAttr.Get(&outPathExpr));
    TF_AXIOM(outPathExpr == SdfPathExpression { "/authored" });
}

void
TestStageCacheUniqueReference()
{
    UsdStageCache cache;
    UsdStageCacheContext context(cache);

    UsdStage const *rawStage;
    UsdStagePtr weakStage;
    {
        // Create a stage, which will be added to `cache`.
	UsdStageRefPtr stage = UsdStage::CreateNew("root.usda");

        // It's not uniquely owned here, since both `stage` and `cache` own
        // references.
        TF_AXIOM(!stage->IsUnique());

        rawStage = get_pointer(stage);
        weakStage = stage;
    }

    // Here the stage should be alive, and uniquely owned by a single reference
    // in `cache`.
    TF_AXIOM(!weakStage.IsExpired());
    TF_AXIOM(rawStage->IsUnique());
}

}

int 
main(int argc, char** argv)
{
    TestTargetSpecs();
    TestGetTargetsAndConnections();
    TestOpaqueValueFileIO();
    TestOutParamterIgnoredForComposingValues();
    TestStageCacheUniqueReference();
    return 0;
}
