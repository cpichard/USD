#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

from pxr import Usd, UsdSkel, UsdGeom, Vt, Sdf
import unittest


class TestUsdSkelCache(unittest.TestCase):

    def test_AnimQuery(self):
        """Tests anim query retrieval."""

        cache = UsdSkel.Cache()

        stage = Usd.Stage.CreateInMemory()
         
        anim = UsdSkel.Animation.Define(stage, "/Anim")

        self.assertTrue(cache.GetAnimQuery(anim))
        # Backwards-compatibility.
        self.assertTrue(cache.GetAnimQuery(prim=anim.GetPrim()))

        self.assertFalse(cache.GetAnimQuery(UsdSkel.Animation()))

        # TODO: Test query of anim behind instancing

    
    def test_InheritedAnimBinding(self):
        """Tests for correctness in the interpretation of the inherited
           skel:animationSource binding."""
        
        testFile = "populate.usda"
        stage = Usd.Stage.Open(testFile)

        cache = UsdSkel.Cache()
        root = UsdSkel.Root(stage.GetPrimAtPath("/AnimBinding"))
        self.assertTrue(cache.Populate(root, Usd.PrimDefaultPredicate))

        query = cache.GetSkelQuery(
            UsdSkel.Skeleton.Get(stage, "/AnimBinding/Scope/Inherit"))
        self.assertTrue(query)
        self.assertEqual(query.GetAnimQuery().GetPrim().GetPath(),
                         Sdf.Path("/Anim1"))

        query = cache.GetSkelQuery(
            UsdSkel.Skeleton.Get(stage, "/AnimBinding/Scope/Override"))
        self.assertTrue(query)
        self.assertEqual(query.GetAnimQuery().GetPrim().GetPath(),
                         Sdf.Path("/Anim2"))

        query = cache.GetSkelQuery(
            UsdSkel.Skeleton.Get(stage, "/AnimBinding/Scope/Block"))
        self.assertTrue(query)
        self.assertFalse(query.GetAnimQuery())
        
        query = cache.GetSkelQuery(
            UsdSkel.Skeleton.Get(stage, "/AnimBinding/Unbound"))
        self.assertTrue(query)
        self.assertFalse(query.GetAnimQuery())

        query = cache.GetSkelQuery(
            UsdSkel.Skeleton.Get(stage, "/AnimBinding/BoundToInactiveAnim"))
        self.assertTrue(query)
        self.assertFalse(query.GetAnimQuery())

        # Ensure that the animationSource binding crosses instancing arcs.

        query = cache.GetSkelQuery(
            UsdSkel.Skeleton.Get(stage, "/AnimBinding/Instance/Inherit"))
        self.assertTrue(query)
        self.assertEqual(query.GetAnimQuery().GetPrim().GetPath(),
                         Sdf.Path("/Anim1"))

        query = cache.GetSkelQuery(
            UsdSkel.Skeleton.Get(stage, "/AnimBinding/Instance/Override"))
        self.assertTrue(query)
        self.assertTrue(query.GetAnimQuery().GetPrim().IsInPrototype())


    def test_InheritedSkeletonBinding(self):
        """Tests for correctness in the interpretation of the inherited
           skel:skeleton binding."""

        testFile = "populate.usda"
        stage = Usd.Stage.Open(testFile)

        cache = UsdSkel.Cache()
        root = UsdSkel.Root(stage.GetPrimAtPath("/SkelBinding"))
        self.assertTrue(cache.Populate(root, Usd.PrimDefaultPredicate))

        skel1 = UsdSkel.Skeleton.Get(stage, "/Skel1")
        skel2 = UsdSkel.Skeleton.Get(stage, "/Skel2")

        binding1 = cache.ComputeSkelBinding(
            root, skel1, Usd.PrimDefaultPredicate)
        self.assertEqual(binding1.GetSkeleton().GetPrim(), skel1.GetPrim())
        self.assertEqual(len(binding1.GetSkinningTargets()), 1)
        skinningQuery1 = binding1.GetSkinningTargets()[0]
        self.assertEqual(skinningQuery1.GetPrim().GetPath(),
                         Sdf.Path("/SkelBinding/Scope/Inherit"))
        # Inherited skinning properties.
        self.assertEqual(skinningQuery1.GetJointIndicesPrimvar()
                         .GetAttr().GetPath().GetPrimPath(),
                         Sdf.Path("/SkelBinding/Scope"))
        self.assertEqual(skinningQuery1.GetJointWeightsPrimvar()
                         .GetAttr().GetPath().GetPrimPath(),
                         Sdf.Path("/SkelBinding/Scope"))
        self.assertEqual(skinningQuery1.GetJointOrder(),
                         Vt.TokenArray(["scope"]))
        # Non-inherited skinning properties.
        self.assertFalse(skinningQuery1.GetBlendShapesAttr())
        self.assertFalse(skinningQuery1.GetBlendShapeTargetsRel())

        binding2 = cache.ComputeSkelBinding(
            root, skel2, Usd.PrimDefaultPredicate)
        self.assertEqual(binding2.GetSkeleton().GetPrim(), skel2.GetPrim())
        self.assertEqual(len(binding2.GetSkinningTargets()), 1)
        skinningQuery2 = binding2.GetSkinningTargets()[0]
        self.assertEqual(skinningQuery2.GetPrim().GetPath(),
                         Sdf.Path("/SkelBinding/Scope/Override"))
        # Inherited skinning properties.
        self.assertEqual(skinningQuery2.GetPrim().GetPath(),
                         Sdf.Path("/SkelBinding/Scope/Override"))
        self.assertEqual(skinningQuery2.GetJointIndicesPrimvar()
                         .GetAttr().GetPath().GetPrimPath(),
                         Sdf.Path("/SkelBinding/Scope/Override"))
        self.assertEqual(skinningQuery2.GetJointWeightsPrimvar()
                         .GetAttr().GetPath().GetPrimPath(),
                         Sdf.Path("/SkelBinding/Scope/Override"))
        self.assertEqual(skinningQuery2.GetJointOrder(),
                         Vt.TokenArray(["override"]))
        self.assertEqual(skinningQuery2.GetBlendShapeOrder(),
                         Vt.TokenArray(["shape"]))
        # Non-inherited skinning properties.
        self.assertEqual(skinningQuery2.GetBlendShapesAttr()
                         .GetPath().GetPrimPath(),
                         Sdf.Path("/SkelBinding/Scope/Override"))
        self.assertEqual(skinningQuery2.GetBlendShapeTargetsRel()
                         .GetPath().GetPrimPath(),
                         Sdf.Path("/SkelBinding/Scope/Override"))

        allBindings = cache.ComputeSkelBindings(
            root, Usd.PrimDefaultPredicate)
        # Expecting two resolved bindings. This should *not* include bindings
        # for any inactive skels or instances
        self.assertEqual(len(allBindings), 2)

        self.assertEqual(binding1.GetSkeleton().GetPrim(),
                         allBindings[0].GetSkeleton().GetPrim())
        self.assertEqual([t.GetPrim() for t in binding1.GetSkinningTargets()],
                         [t.GetPrim() for t in allBindings[0].GetSkinningTargets()])

        self.assertEqual(binding2.GetSkeleton().GetPrim(),
                         allBindings[1].GetSkeleton().GetPrim())
        self.assertEqual([t.GetPrim() for t in binding2.GetSkinningTargets()],
                         [t.GetPrim() for t in allBindings[1].GetSkinningTargets()])

    def test_InstancedSkeletonBinding(self):
        """Tests for correctness in the interpretation of the inherited
           skel:skeleton binding with instancing."""

        testFile = "populate.usda"
        stage = Usd.Stage.Open(testFile)

        cache = UsdSkel.Cache()

        root = UsdSkel.Root(stage.GetPrimAtPath("/SkelBinding"))
        self.assertTrue(cache.Populate(root, Usd.TraverseInstanceProxies()))

        skel1 = UsdSkel.Skeleton.Get(stage, "/Skel1")
        binding1 = cache.ComputeSkelBinding(
            root, skel1, Usd.TraverseInstanceProxies())
        self.assertEqual(binding1.GetSkeleton().GetPrim(), skel1.GetPrim())
        self.assertEqual(len(binding1.GetSkinningTargets()), 2)
        skinningQuery1 = binding1.GetSkinningTargets()[1]
        self.assertEqual(skinningQuery1.GetPrim().GetPath(),
                         Sdf.Path("/SkelBinding/Instance/Inherit"))
        # Inherited skinning properties.
        self.assertEqual(skinningQuery1.GetJointIndicesPrimvar()
                         .GetAttr().GetPath().GetPrimPath(),
                         Sdf.Path("/SkelBinding/Instance"))
        self.assertEqual(skinningQuery1.GetJointWeightsPrimvar()
                         .GetAttr().GetPath().GetPrimPath(),
                         Sdf.Path("/SkelBinding/Instance"))
        self.assertEqual(skinningQuery1.GetJointOrder(),
                         Vt.TokenArray(["instance"]))
        # Non-inherited skinning properties.
        self.assertFalse(skinningQuery1.GetBlendShapesAttr())
        self.assertFalse(skinningQuery1.GetBlendShapeTargetsRel())

        allBindings = cache.ComputeSkelBindings(
            root, Usd.TraverseInstanceProxies())
        # Expecting three resolved bindings. This should *not* include bindings
        # for any inactive skels, but does include instances
        self.assertEqual(len(allBindings), 3)

        skel2 = UsdSkel.Skeleton.Get(stage, "/SkelBinding/Instance/Skel")
        binding2 = cache.ComputeSkelBinding(
            root, skel2, Usd.TraverseInstanceProxies())

        self.assertEqual(binding2.GetSkeleton().GetPrim(),
                         allBindings[2].GetSkeleton().GetPrim())
        self.assertEqual([t.GetPrim() for t in binding2.GetSkinningTargets()],
                         [t.GetPrim() for t in allBindings[2].GetSkinningTargets()])

        inheritBindingMesh = stage.GetPrimAtPath(
            "/SkelBinding/Instance/Inherit")
        
        overrideBindingMesh = stage.GetPrimAtPath(
            "/SkelBinding/Instance/Override")

        # Instances should not be discoverable with a default predicate.
        cache = UsdSkel.Cache()
        cache.Populate(root, Usd.PrimDefaultPredicate)

        self.assertFalse(cache.GetSkinningQuery(inheritBindingMesh))
        self.assertFalse(cache.GetSkinningQuery(overrideBindingMesh))

        # Need to explicitly traverse instance prototypes to see these bindings.
        cache.Populate(root, Usd.TraverseInstanceProxies())

        query = cache.GetSkinningQuery(inheritBindingMesh)
        self.assertTrue(query)
        self.assertEqual(list(query.GetJointOrder()), ["instance"])

        query = cache.GetSkinningQuery(overrideBindingMesh)
        self.assertTrue(query)
        self.assertEqual(list(query.GetJointOrder()), ["override"])
        self.assertEqual(list(query.GetBlendShapeOrder()), ["override"])

    def test_InstancedBlendShape(self):
        """Tests for correctness in the interpretation of instanced 
           blend shapes."""

        testFile = "populate.usda"
        stage = Usd.Stage.Open(testFile)
        cache = UsdSkel.Cache()
        skelRoot = UsdSkel.Root(stage.GetPrimAtPath("/SkelBinding"))
        self.assertTrue(cache.Populate(skelRoot, Usd.TraverseInstanceProxies()))

        # instance proxy mesh
        mesh = stage.GetPrimAtPath("/SkelBinding/Instance/Override")
        skelBinding = UsdSkel.BindingAPI(mesh)
        self.assertEqual(list(skelBinding.GetBlendShapesAttr().Get()), 
            ["override"])

        skel = skelBinding.GetSkeleton()
        skelq = cache.GetSkelQuery(skel)
        animq = skelq.GetAnimQuery()
        self.assertEqual(list(animq.GetBlendShapeOrder()), 
            ["override", "shape"])

        skinq = cache.GetSkinningQuery(mesh)
        self.assertTrue(skinq.HasBlendShapes())
        self.assertEqual(skelBinding.GetInheritedAnimationSource(), 
            stage.GetPrimAtPath("/Anim1"))

        # bug PRES-77530
        # because skelBinding.GetBlendShapesAttr().Get() and 
        # animq.GetBlendShapeOrder() have different order,
        # the mapper should not be null.
        mapper = skinq.GetBlendShapeMapper()
        self.assertFalse(mapper.IsNull())

if __name__ == "__main__":
    unittest.main()
