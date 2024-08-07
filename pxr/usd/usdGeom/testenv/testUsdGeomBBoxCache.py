#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

from __future__ import print_function

from pxr import Tf, Usd, UsdGeom, Gf, Vt
import sys

# Direct TF_DEBUG output to stderr.
Tf.Debug.SetOutputFile(sys.__stderr__)

def AssertBBoxesClose(cachedBox, directBox, msg):
    cachedRange = cachedBox.ComputeAlignedRange()
    directRange = directBox.ComputeAlignedRange()
    assert Gf.IsClose(cachedRange.min, directRange.min, 1e-5), msg
    assert Gf.IsClose(cachedRange.max, directRange.max, 1e-5), msg

def TestAtCurTime(stage, bboxCache):
    p = stage.GetPrimAtPath("/parent/primWithLocalXform")
    assert bboxCache.ComputeWorldBound(p) == bboxCache.ComputeWorldBound(p)
    bboxCache.SetIncludedPurposes([UsdGeom.Tokens.default_])
    print()
    print("Untransformed bound:", p)
    print(bboxCache.ComputeUntransformedBound(p))
    print()
    print(bboxCache.ComputeUntransformedBound(p).ComputeAlignedRange())
    print()
    # The baseline is predicated on TfDebug output.  We do not care to clutter
    # it up with the duplicate computation that calling the UsdGeom.Imageable
    # API will induce, so here and below we disable TfDebug output while
    # testing
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    db = UsdGeom.Imageable(p).ComputeUntransformedBound(bboxCache.GetTime(),
                                                        UsdGeom.Tokens.default_)
    AssertBBoxesClose(bboxCache.ComputeUntransformedBound(p), db, "Untransformed")
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 1)
                      
    print()
    print("Local bound:", p)
    print(bboxCache.ComputeLocalBound(p))
    print()
    print(bboxCache.ComputeLocalBound(p).ComputeAlignedRange())
    print()
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    db = UsdGeom.Imageable(p).ComputeLocalBound(bboxCache.GetTime(),
                                                UsdGeom.Tokens.default_)
    AssertBBoxesClose(bboxCache.ComputeLocalBound(p), db, "Local")
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 1)

    print()
    print("World bound:", p)
    print(bboxCache.ComputeWorldBound(p))
    print()
    print(bboxCache.ComputeWorldBound(p).ComputeAlignedRange())
    print()
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    db = UsdGeom.Imageable(p).ComputeWorldBound(bboxCache.GetTime(),
                                                UsdGeom.Tokens.default_)
    AssertBBoxesClose(bboxCache.ComputeWorldBound(p), db, "World")
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 1)

    print()
    pp = stage.GetPrimAtPath(str(p.GetPath()) + "/InvisibleChild")
    print("Invisible Bound:", pp)
    print(bboxCache.ComputeWorldBound(pp))
    print()
    print(bboxCache.ComputeWorldBound(pp).ComputeAlignedRange())
    print()
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    db = UsdGeom.Imageable(pp).ComputeWorldBound(bboxCache.GetTime(),
                                                 UsdGeom.Tokens.default_)
    AssertBBoxesClose(bboxCache.ComputeWorldBound(pp), db, "Invis World")
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 1)


    print()
    print("Visit Guides:", p)
    bboxCache.SetIncludedPurposes([UsdGeom.Tokens.guide])
    assert bboxCache.GetIncludedPurposes() == [UsdGeom.Tokens.guide]
    print(bboxCache.ComputeWorldBound(p))
    print()
    print(bboxCache.ComputeWorldBound(p).ComputeAlignedRange())
    print()
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    db = UsdGeom.Imageable(p).ComputeWorldBound(bboxCache.GetTime(),
                                                UsdGeom.Tokens.guide)
    AssertBBoxesClose(bboxCache.ComputeWorldBound(p), db, "World Guide")
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 1)


    print()
    print("Visit Render:", p)
    bboxCache.SetIncludedPurposes([UsdGeom.Tokens.render])
    assert bboxCache.GetIncludedPurposes() == [UsdGeom.Tokens.render]
    print(bboxCache.ComputeWorldBound(p))
    print()
    print(bboxCache.ComputeWorldBound(p).ComputeAlignedRange())
    print()
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    db = UsdGeom.Imageable(p).ComputeWorldBound(bboxCache.GetTime(),
                                                UsdGeom.Tokens.render)
    AssertBBoxesClose(bboxCache.ComputeWorldBound(p), db, "World Render")
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 1)


    print()
    print("Visit Proxy:", p)
    bboxCache.SetIncludedPurposes([UsdGeom.Tokens.proxy])
    assert bboxCache.GetIncludedPurposes() == [UsdGeom.Tokens.proxy]
    print(bboxCache.ComputeWorldBound(p))
    print()
    print(bboxCache.ComputeWorldBound(p).ComputeAlignedRange())
    print()
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    db = UsdGeom.Imageable(p).ComputeWorldBound(bboxCache.GetTime(),
                                                UsdGeom.Tokens.proxy)
    AssertBBoxesClose(bboxCache.ComputeWorldBound(p), db, "World Proxy")

    # Test multi-purpose
    bboxCache.SetIncludedPurposes([UsdGeom.Tokens.default_,
                                   UsdGeom.Tokens.proxy,
                                   UsdGeom.Tokens.render])
    assert bboxCache.GetIncludedPurposes() == [UsdGeom.Tokens.default_,
                                               UsdGeom.Tokens.proxy,
                                               UsdGeom.Tokens.render]
    db = UsdGeom.Imageable(p).ComputeWorldBound(bboxCache.GetTime(),
                                                UsdGeom.Tokens.default_,
                                                UsdGeom.Tokens.proxy,
                                                UsdGeom.Tokens.render)
    AssertBBoxesClose(bboxCache.ComputeWorldBound(p), db, "Multi-purpose")
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 1)


    p = stage.GetPrimAtPath("/Rotated")
    print()
    print("Visit Rotated:", p)
    bboxCache.SetIncludedPurposes([UsdGeom.Tokens.default_])
    assert bboxCache.GetIncludedPurposes() == [UsdGeom.Tokens.default_]
    print(bboxCache.ComputeWorldBound(p))
    print()
    print(bboxCache.ComputeWorldBound(p).ComputeAlignedRange())
    print()
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    db = UsdGeom.Imageable(p).ComputeWorldBound(bboxCache.GetTime(),
                                                UsdGeom.Tokens.default_)
    AssertBBoxesClose(bboxCache.ComputeWorldBound(p), db, "Rotated")
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 1)


    p = stage.GetPrimAtPath("/Rotated/Rotate135AndTranslate/Rot45")
    print()
    print("Visit Rotated:", p)
    bboxCache.SetIncludedPurposes([UsdGeom.Tokens.default_])
    assert bboxCache.GetIncludedPurposes() == [UsdGeom.Tokens.default_]
    print(bboxCache.ComputeWorldBound(p))
    print()
    print(bboxCache.ComputeWorldBound(p).ComputeAlignedRange())
    print()
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    db = UsdGeom.Imageable(p).ComputeWorldBound(bboxCache.GetTime(),
                                                UsdGeom.Tokens.default_)
    AssertBBoxesClose(bboxCache.ComputeWorldBound(p), db, "Rotated Twice")
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 1)

    p = Usd.Prim()
    assert not p 
    
    try:
        bboxCache.ComputeWorldBound(p)
    except RuntimeError:
        pass
    else:
        assert False, 'Failed to raise expected exception.'
    
    try:
        bboxCache.ComputeLocalBound(p)
    except RuntimeError:
        pass
    else:
       assert False, 'Failed to raise expected exception.'

    try:
        bboxCache.ComputeUntransformedBound(p)
    except RuntimeError:
        pass
    else:
        assert False, 'Failed to raise expected exception.'

def Main():
    stage = Usd.Stage.Open("cubeSbdv.usda")

    bboxCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), 
                                  includedPurposes=[UsdGeom.Tokens.default_])
    print("-"*80)
    print("Running tests at UsdTimeCode::Default()")
    print("-"*80)
    print("UseExtentsHint is %s" % bboxCache.GetUseExtentsHint())
    TestAtCurTime(stage, bboxCache)
    print("-"*80)
    print() 
    print("-"*80)
    print("Running tests at UsdTimeCode(1.0)")
    print("-"*80)
    bboxCache.SetTime(1.0)
    TestAtCurTime(stage, bboxCache)

    # Test the use of cached extents hint.
    bboxCache2 = UsdGeom.BBoxCache(Usd.TimeCode.Default(), 
                                   includedPurposes=[UsdGeom.Tokens.default_],
                                   useExtentsHint=True)
    print("-"*80)
    print("Running tests at UsdTimeCode::Default()")
    print("-"*80)
    print("useExtentsHint is %s" % bboxCache2.GetUseExtentsHint())
    TestAtCurTime(stage, bboxCache2)
    print("-"*80)
    print() 
    print("-"*80)
    print("Running tests at UsdTimeCode(1.0)")
    print("-"*80)
    bboxCache2.SetTime(1.0)
    TestAtCurTime(stage, bboxCache2)

def TestInstancedStage(stage, bboxCache):
    print("UseExtentsHint is %s" % bboxCache.GetUseExtentsHint())

    instancedPrim = stage.GetPrimAtPath("/instanced_parent")
    uninstancedPrim = stage.GetPrimAtPath("/uninstanced_parent")

    instancedRotated = stage.GetPrimAtPath("/instanced_Rotated")
    uninstancedRotated = stage.GetPrimAtPath("/uninstanced_Rotated")

    print()
    print("Bound for instance prim /instanced_parent:", \
        bboxCache.ComputeWorldBound(instancedPrim))
    print()
    print("Bound for prim /uninstanced_parent:", \
        bboxCache.ComputeWorldBound(uninstancedPrim))

    AssertBBoxesClose(bboxCache.ComputeWorldBound(instancedPrim),
                      bboxCache.ComputeWorldBound(uninstancedPrim), "Instanced")

    print()
    print("Bound for instance prim /instanced_Rotated:", \
        bboxCache.ComputeWorldBound(instancedRotated))
    print()
    print("Bound for prim /uninstanced_Rotated:", \
        bboxCache.ComputeWorldBound(uninstancedRotated))

    AssertBBoxesClose(bboxCache.ComputeWorldBound(instancedRotated),
                      bboxCache.ComputeWorldBound(uninstancedRotated), 
                      "Instanced")

def TestWithInstancing():
    stage = Usd.Stage.Open("cubeSbdv_instanced.usda")
    bboxCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), 
                                  includedPurposes=[UsdGeom.Tokens.default_])

    print("-"*80)
    print("Testing bounding boxes on instanced prims")
    print("-"*80)

    print("-"*80)
    print("Running tests at UsdTimeCode::Default()")
    print("-"*80)
    TestInstancedStage(stage, bboxCache)

    print() 
    print("-"*80)
    print("Running tests at UsdTimeCode(1.0)")
    print("-"*80)
    bboxCache.SetTime(1.0)
    TestInstancedStage(stage, bboxCache)

    # Test the use of cached extents hint.
    bboxCache2 = UsdGeom.BBoxCache(Usd.TimeCode.Default(), 
                                   includedPurposes=[UsdGeom.Tokens.default_],
                                   useExtentsHint=True)
    
    print("-"*80)
    print("Running tests at UsdTimeCode::Default()")
    print("-"*80)
    TestInstancedStage(stage, bboxCache2)

    print() 
    print("-"*80)
    print("Running tests at UsdTimeCode(1.0)")
    print("-"*80)
    bboxCache.SetTime(1.0)
    TestInstancedStage(stage, bboxCache2)

def TestBug113044():
    stage = Usd.Stage.Open("animVis.usda")
    bboxCache = UsdGeom.BBoxCache(Usd.TimeCode(0.0),
                                  includedPurposes=[UsdGeom.Tokens.default_])
    pseudoRoot = stage.GetPrimAtPath("/")
    assert bboxCache.ComputeWorldBound(pseudoRoot).GetRange().IsEmpty()

    # The cube is visible at frame 1. This should invalidate the bounds at '/'
    # and cause the bbox to be non-empty.
    bboxCache.SetTime(1.0)
    assert not bboxCache.ComputeWorldBound(pseudoRoot).GetRange().IsEmpty()

    bboxCache.SetTime(2.0)
    assert bboxCache.ComputeWorldBound(pseudoRoot).GetRange().IsEmpty()

    bboxCache.SetTime(3.0)
    assert not bboxCache.ComputeWorldBound(pseudoRoot).GetRange().IsEmpty()

def TestExtentCalculation():
    stage = Usd.Stage.Open(
        "pointsAndCurves.usda")
    bboxCache = UsdGeom.BBoxCache(Usd.TimeCode(0.0), 
        includedPurposes=[UsdGeom.Tokens.default_])

    print("-"*80)
    print("Testing extent calculations on prims")
    print("-"*80)
    print() 

    validPrims = stage.GetPrimAtPath("/ValidPrims")
    warningPrims = stage.GetPrimAtPath("/WarningPrims")
    errorPrims = stage.GetPrimAtPath("/ErrorPrims")

    print("Visit Extent: " + str(validPrims))
    for prim in validPrims.GetChildren():
        print(str(prim) + ": " + \
            str(bboxCache.ComputeWorldBound(prim).GetRange()))
    print()

    print("Visit Extent: " + str(warningPrims))
    for prim in warningPrims.GetChildren():
        print(str(prim) + ": " + \
            str(bboxCache.ComputeWorldBound(prim).GetRange()))
    print()

    print("Visit Extent: " + str(errorPrims))
    for prim in errorPrims.GetChildren():
        bboxRange = bboxCache.ComputeWorldBound(prim).GetRange()
        print(str(prim) + ": " + \
            str(bboxCache.ComputeWorldBound(prim).GetRange()))
            # Note: The bbox has cached the result, and will not throw 
            #   another error here.

def TestUnloadedExtentsHints():
    stage = Usd.Stage.Open(
        "unloadedCubeModel.usda",
        load = Usd.Stage.LoadNone)
    bboxCacheNo = UsdGeom.BBoxCache(Usd.TimeCode(0.0), 
        includedPurposes=[UsdGeom.Tokens.default_], useExtentsHint=False)
    bboxCacheYes = UsdGeom.BBoxCache(Usd.TimeCode(0.0), 
        includedPurposes=[UsdGeom.Tokens.default_], useExtentsHint=True)

    print("-"*80)
    print("Testing aggregate bounds with unloaded child prims")
    print("-"*80)
    print() 

    prim = stage.GetPseudoRoot()
    bboxNo  = bboxCacheNo.ComputeWorldBound(prim)
    bboxYes = bboxCacheYes.ComputeWorldBound(prim)
    
    assert bboxNo.GetRange().IsEmpty()
    assert not bboxYes.GetRange().IsEmpty()

def TestIgnoredPrims():
    stage = Usd.Stage.Open("cubeSbdv.usda")

    bboxCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), 
                                  includedPurposes=[UsdGeom.Tokens.default_])

    print("-"*80)
    print("Testing computation for undefined, inactive and abstract prims")
    print("-"*80)
    print()

    undefinedPrim = stage.GetPrimAtPath("/undefinedCube1")
    assert bboxCache.ComputeWorldBound(undefinedPrim).GetRange().IsEmpty()

    inactivePrim = stage.GetPrimAtPath("/inactiveCube1")
    assert bboxCache.ComputeWorldBound(inactivePrim).GetRange().IsEmpty()

    abstractPrim = stage.GetPrimAtPath("/_class_UnitCube")
    assert bboxCache.ComputeWorldBound(abstractPrim).GetRange().IsEmpty()

def TestIgnoreVisibility():
    stage = Usd.Stage.Open("animVis.usda")
    bboxCache = UsdGeom.BBoxCache(Usd.TimeCode(0.0),
                                  includedPurposes=[UsdGeom.Tokens.default_],
                                  useExtentsHint=True,
                                  ignoreVisibility=True)
    pseudoRoot = stage.GetPrimAtPath("/")
    assert not bboxCache.ComputeWorldBound(pseudoRoot).GetRange().IsEmpty()
    
def TestBug125048():
    stage = Usd.Stage.Open("testBug125048.usda")
    bboxCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), 
                                  includedPurposes=[UsdGeom.Tokens.default_],
                                  useExtentsHint=True)
    modelPrim = stage.GetPrimAtPath("/Model")
    geomPrim = stage.GetPrimAtPath("/Model/Geom/cube")
    bboxCache.ComputeUntransformedBound(modelPrim)
    # The following computation used to trip a verify.
    bboxCache.ComputeUntransformedBound(geomPrim)

def TestBug127801():
    """ Test that typeless defs are included in the traversal when computing 
        bounds. 
    """
    stage = Usd.Stage.CreateInMemory()
    world = stage.DefinePrim("/World")
    char = stage.DefinePrim("/World/anim/char")
    sphere = UsdGeom.Sphere.Define(stage, 
        char.GetPath().AppendChild('sphere'))

    bboxCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), 
                                  includedPurposes=[UsdGeom.Tokens.default_],
                                  useExtentsHint=True)
    bbox = bboxCache.ComputeUntransformedBound(world)
    assert not bbox.GetRange().IsEmpty()

def TestUsd4957():
    """ Tests the case in which a prim has an xform directly on it and its
        bounding box relative to one of its ancestors is computed using
        ComputeRelativeBound().
    """
    s = Usd.Stage.Open("testUSD4957.usda")
    b = s.GetPrimAtPath("/A/B")
    c = s.GetPrimAtPath("/A/B/C")
    bc = UsdGeom.BBoxCache(Usd.TimeCode.Default(), ['default', 'render'])
    
    # Call the function being tested
    relativeBbox = bc.ComputeRelativeBound(c, b)
    
    # Compare the result with the bbox of C in its local space
    cExtent = UsdGeom.Boundable(c).GetExtentAttr().Get()
    cRange = Gf.Range3d(Gf.Vec3d(cExtent[0]), Gf.Vec3d(cExtent[1]))
    cLocalXform = UsdGeom.Xformable(c).GetLocalTransformation()
    cBbox = Gf.BBox3d(cRange, cLocalXform)
    
    AssertBBoxesClose(relativeBbox, cBbox,
                      "ComputeRelativeBound produced a wrong bbox.")

def TestPurposeWithInstancing():
    """ Tests that purpose filtered bounding boxes work correctly with native
        instancing and inherited purpose.
    """

    # Our test stages should all produce the exact same default and render 
    # purpose bounding boxes even though they have different permutations of 
    # instancing prims.

    # Note that since extent value is not authored the bbox extents will be
    # computed. Using the computed values for tests instead of what fallback
    # extent would have provided.
    defaultBBox = Gf.BBox3d(Gf.Range3d(
        Gf.Vec3d(-1.5, -1.5, -1.5), Gf.Vec3d(1.5, 1.5, 10.5)))
    renderBBox = Gf.BBox3d(Gf.Range3d(
        Gf.Vec3d(-11.0, -1.5, -16.5), Gf.Vec3d(1.5, 1.5, 10.5)))
    defaultCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), ['default'])
    renderCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), ['default', 'render'])

    # Stage with all instancing disabled.
    stage = Usd.Stage.Open("disableAllInstancing.usda")
    assert len(stage.GetPrototypes()) == 0
    root = stage.GetPrimAtPath("/Root")

    assert defaultCache.ComputeWorldBound(root) == defaultBBox
    assert renderCache.ComputeWorldBound(root) == renderBBox

    # Stage with one set of instances.
    stage = Usd.Stage.Open("disableInnerInstancing.usda")
    assert len(stage.GetPrototypes()) == 1
    root = stage.GetPrimAtPath("/Root")

    assert defaultCache.ComputeWorldBound(root) == defaultBBox
    assert renderCache.ComputeWorldBound(root) == renderBBox

    # Stage with one different set of instances.
    stage = Usd.Stage.Open("disableOuterInstancing.usda")
    assert len(stage.GetPrototypes()) == 1
    root = stage.GetPrimAtPath("/Root")

    assert defaultCache.ComputeWorldBound(root) == defaultBBox
    assert renderCache.ComputeWorldBound(root) == renderBBox

    # Stage with both sets of instances which are nested.
    stage = Usd.Stage.Open("nestedInstanceTest.usda")
    assert len(stage.GetPrototypes()) == 2
    root = stage.GetPrimAtPath("/Root")

    assert defaultCache.ComputeWorldBound(root) == defaultBBox
    assert renderCache.ComputeWorldBound(root) == renderBBox

def TestMeshBounds():
    """
    Test to see different scenarios for Mesh bounds computation. These scenarios
    include edge cases like a Mesh missing points, extent not authored and 
    combinations of the above.
    """
    stage = Usd.Stage.Open("meshBounds.usda")
    noExtentButPoints = \
        UsdGeom.Boundable(stage.GetPrimAtPath('/NoExtentButPoints'))
    assert noExtentButPoints
    expectedExtent = Vt.Vec3fArray(2, \
            (Gf.Vec3f(-2.0, -2.0, -2.0), Gf.Vec3f(2.0, -2.0, 2.0)))
    assert noExtentButPoints.ComputeExtent(Usd.TimeCode.Default()) == \
        expectedExtent
    noExtentNoPoints = \
        UsdGeom.Boundable(stage.GetPrimAtPath('/NoExtentNoPoints'))
    assert noExtentNoPoints
    assert not noExtentNoPoints.ComputeExtent(Usd.TimeCode.Default())
    noExtentEmptyPoints = \
        UsdGeom.Boundable(stage.GetPrimAtPath('/NoExtentEmptyPoints'))
    assert noExtentEmptyPoints
    # Note that the following returns an empty extent as Max_Float, Min_Float
    # signifying an empty range as max < min!
    floatMax = Gf.Range1d().min
    floatMin = Gf.Range1d().max
    expectedExtent = Vt.Vec3fArray(2, \
            (Gf.Vec3f(floatMax, floatMax, floatMax), \
                Gf.Vec3f(floatMin, floatMin, floatMin)))
    assert noExtentEmptyPoints.ComputeExtent(Usd.TimeCode.Default()) == \
            expectedExtent
    

if __name__ == "__main__":
    Main()
    TestWithInstancing()
    TestExtentCalculation()
    TestUnloadedExtentsHints()
    TestIgnoredPrims()
    TestIgnoreVisibility()
    TestPurposeWithInstancing()
    
    # Turn off debug symbol for these regression tests.
    Tf.Debug.SetDebugSymbolsByName("USDGEOM_BBOX", 0)
    TestBug113044()
    TestBug125048()
    TestBug127801()
    TestUsd4957()
    TestMeshBounds()

