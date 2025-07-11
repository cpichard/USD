#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

'''
This test exercises the Pcp dependency API in specific scenarios
that represent interesting cases that it needs to handle.

It cherry picks particular examples from the Pcp museum
to exercise.
'''
from __future__ import print_function

from pxr import Sdf, Pcp, Tf
import functools, os, unittest

class TestPcpDependencies(unittest.TestCase):
    # Fully populate a PcpCache so we can examine its full dependencies.
    def _ForcePopulateCache(self, cache):
        def walk(cache, path=Sdf.Path('/')):
            primIndex, primIndexErrors = cache.ComputePrimIndex(path)
            childNames, prohibitedChildNames = primIndex.ComputePrimChildNames()
            for childName in childNames:
                walk(cache, path.AppendChild(childName))
        walk(cache)

    # Wrapper to query deps on a given site.
    def _FindSiteDeps(self, rootLayerPath, siteLayerPath, sitePath,
                      cacheInUsdMode = False,
                      depMask = Pcp.DependencyTypeAnyNonVirtual,
                      recurseOnSite = False,
                      recurseOnIndex = False):
        sitePath = Sdf.Path(sitePath)
        self.assertFalse(sitePath.isEmpty)
        rootLayer = Sdf.Layer.FindOrOpen(rootLayerPath)
        self.assertTrue(rootLayer)
        siteLayer = Sdf.Layer.FindOrOpen(siteLayerPath)
        self.assertTrue(siteLayer)
        cache = Pcp.Cache( Pcp.LayerStackIdentifier(rootLayer), 
                          usd = cacheInUsdMode )
        siteLayerStack = cache.ComputeLayerStack(
            Pcp.LayerStackIdentifier(siteLayer))[0]
        self.assertTrue(siteLayerStack)
        self._ForcePopulateCache(cache)
        return cache.FindSiteDependencies( siteLayerStack, sitePath, depMask,
                                           recurseOnSite, recurseOnIndex,
                                           False )

    # Helper to compare dep results and print diffs.
    def _AssertDepsEqual(self, deps_lhs, deps_rhs):
        def _LessThan(a, b):
            if a.indexPath < b.indexPath:
                return -1
            if a.indexPath > b.indexPath:
                return 1
            if a.sitePath < b.sitePath:
                return -1
            if a.sitePath > b.sitePath:
                return 1
            if id(a.mapFunc) < id(b.mapFunc):
                return -1
            if id(a.mapFunc) > id(b.mapFunc):
                return 1
            return 0
        # functools is used here to support python 2 and 3. In Python 3 there
        # is no cmp parameter, only a function that gives a key value. This
        # functools function exists to convert a cmp function to a key function.
        a = sorted(deps_lhs, key = functools.cmp_to_key(_LessThan))
        b = sorted(deps_rhs, key = functools.cmp_to_key(_LessThan))
        if a != b:
            print('Only in a:')
            for i in a:
                if i not in b:
                    print(i)
            print('Only in b:')
            for i in b:
                if i not in a:
                    print(i)
            self.assertEqual(a,b)

    def test_Basic(self):
        ########################################################################
        # A basic reference arc.
        #
        self._AssertDepsEqual( self._FindSiteDeps(
             'BasicReference/root.usda',
             'BasicReference/ref.usda',
             '/PrimA'),
            [
            Pcp.Dependency(
                '/PrimWithReferences',
                '/PrimA',
                Pcp.MapFunction({'/PrimA': '/PrimWithReferences'})
                )
            ])

        ########################################################################
        # A reference to a defaultPrim.
        #
        self._AssertDepsEqual( self._FindSiteDeps(
             'BasicReference/root.usda',
             'BasicReference/defaultRef.usda',
             '/Default'),
            [
            Pcp.Dependency(
                '/PrimWithDefaultReferenceTarget',
                '/Default',
                Pcp.MapFunction({
                    '/Default': '/PrimWithDefaultReferenceTarget'})
                )
            ])

        ########################################################################
        # A reference case showing the distinction between ancestral and
        # direct dependencies.
        #
        self._AssertDepsEqual( self._FindSiteDeps(
             'BasicAncestralReference/root.usda',
             'BasicAncestralReference/A.usda',
             '/A'),
            [
            Pcp.Dependency(
                '/A',
                '/A',
                Pcp.MapFunction({
                    '/A': '/A'})
                )
            ])
        self._AssertDepsEqual( self._FindSiteDeps(
             'BasicAncestralReference/root.usda',
             'BasicAncestralReference/B.usda',
             '/B',
             depMask = Pcp.DependencyTypeDirect | Pcp.DependencyTypeNonVirtual
             ),
            [
            Pcp.Dependency(
                '/A/B',
                '/B',
                Pcp.MapFunction({
                    '/B': '/A/B'})
                )
            ])
        self._AssertDepsEqual( self._FindSiteDeps(
             'BasicAncestralReference/root.usda',
             'BasicAncestralReference/A.usda',
             '/A/B',
             depMask = Pcp.DependencyTypeAncestral | Pcp.DependencyTypeNonVirtual
             ),
            [
            Pcp.Dependency(
                '/A/B',
                '/A/B',
                Pcp.MapFunction({
                    '/A': '/A'})
                )
            ])

        ########################################################################
        # An example showing how we can use existing structure to reason about
        # deps that would apply to sites that do not contain any specs yet.
        # This case is important for change processing the addition of a new
        # child prim.
        #
        self._AssertDepsEqual( self._FindSiteDeps(
             'BasicAncestralReference/root.usda',
             'BasicAncestralReference/A.usda',
             '/A.HypotheticalProperty',
             ),
            [
            Pcp.Dependency(
                '/A.HypotheticalProperty',
                '/A.HypotheticalProperty',
                Pcp.MapFunction({
                    '/A': '/A'})
                )
            ])
        self._AssertDepsEqual( self._FindSiteDeps(
             'BasicAncestralReference/root.usda',
             'BasicAncestralReference/A.usda',
             '/A.HypotheticalProperty',
             recurseOnSite = True, # This should have no effect on the results.
             ),
            [
            Pcp.Dependency(
                '/A.HypotheticalProperty',
                '/A.HypotheticalProperty',
                Pcp.MapFunction({
                    '/A': '/A'})
                )
            ])
        self._AssertDepsEqual( self._FindSiteDeps(
             'BasicAncestralReference/root.usda',
             'BasicAncestralReference/A.usda',
             '/A/B/HypotheticalChildPrim',
             depMask = Pcp.DependencyTypeAncestral | Pcp.DependencyTypeNonVirtual
             ),
            [
            Pcp.Dependency(
                '/A/B/HypotheticalChildPrim',
                '/A/B/HypotheticalChildPrim',
                Pcp.MapFunction({
                    '/A': '/A'})
                )
            ])

        ########################################################################
        # Using recurseOnSite to find inbound dependencies on child sites.
        # This is important for the case of making a significant change to
        # a prim that contains children inherited elsewhere.
        self._AssertDepsEqual( self._FindSiteDeps(
            'BasicLocalAndGlobalClassCombination/root.usda',
            'BasicLocalAndGlobalClassCombination/model.usda',
            '/_class_Model/_class_Nested',
            depMask = Pcp.DependencyTypeAnyIncludingVirtual,
            recurseOnSite = True
            ),
            [
            Pcp.Dependency(
                '/Model_1/_class_Nested',
                '/_class_Model/_class_Nested',
                Pcp.MapFunction({
                    '/_class_Model': '/Model_1',
                    })
                ),
            Pcp.Dependency(
                '/Model_1/_class_Nested/Left',
                '/_class_Model/_class_Nested/Sym',
                Pcp.MapFunction({
                    '/_class_Model': '/Model_1',
                    '/_class_Model/_class_Nested/Sym': '/Model_1/_class_Nested/Left',
                    })
                ),
            Pcp.Dependency(
                '/Model_1/Instance',
                '/_class_Model/_class_Nested',
                Pcp.MapFunction({
                    '/_class_Model': '/Model_1',
                    '/_class_Model/_class_Nested': '/Model_1/Instance',
                    })
                ),
            Pcp.Dependency(
                '/Model_1/Instance/Left',
                '/_class_Model/_class_Nested/Sym',
                Pcp.MapFunction({
                    '/_class_Model': '/Model_1',
                    '/_class_Model/_class_Nested': '/Model_1/Instance',
                    '/_class_Model/_class_Nested/Sym': '/Model_1/Instance/Left',
                    })
                ),
            ])

        ########################################################################
        # Only prim2 that select a variant depend on that particular variant's 
        # site. Here /Prim1 selects primVariant=one.
        self._AssertDepsEqual(self._FindSiteDeps(
                'BasicVariant/root.usda',
                'BasicVariant/root.usda',
                '/PrimVariants{primVariant=one}',
                recurseOnSite = True,
                cacheInUsdMode = True
            ),
            [
            Pcp.Dependency(
                '/Prim1', 
                '/PrimVariants{primVariant=one}', 
                Pcp.MapFunction({
                    '/': '/',
                    '/PrimVariants': '/Prim1'})
                ),
            Pcp.Dependency(
                '/Prim1', 
                '/PrimVariants{primVariant=one}',
                Pcp.MapFunction({
                     '/': '/', 
                     '/PrimVariants': '/Prim1'})
                )
            ])

        # And /Prim2 selects primVariant=two.
        self._AssertDepsEqual(self._FindSiteDeps(
                'BasicVariant/root.usda',
                'BasicVariant/root.usda',
                '/PrimVariants{primVariant=two}',
                recurseOnSite = True,
                cacheInUsdMode = True
            ),
            [
            Pcp.Dependency(
                '/Prim2', 
                '/PrimVariants{primVariant=two}', 
                Pcp.MapFunction({
                    '/': '/',
                    '/PrimVariants': '/Prim2'})
                ),
            Pcp.Dependency(
                '/Prim2', 
                '/PrimVariants{primVariant=two}',
                Pcp.MapFunction({
                     '/': '/', 
                     '/PrimVariants': '/Prim2'})
                )
            ])

        ########################################################################
        # XXX: In non-USD mode we still have the bug where prims can have 
        # dependencies to unselected variants that they don't actually depend 
        # on. This bug is in place until downstream dependencies can be updated
        # to account for the fix to the bug.
        self._AssertDepsEqual(self._FindSiteDeps(
                'BasicVariant/root.usda',
                'BasicVariant/root.usda',
                '/PrimVariants{primVariant=one}',
                recurseOnSite = True,
                cacheInUsdMode = False
            ),
            [
            Pcp.Dependency(
                '/Prim1', 
                '/PrimVariants{primVariant=one}', 
                Pcp.MapFunction({
                    '/': '/',
                    '/PrimVariants': '/Prim1'})
                ),
            Pcp.Dependency(
                '/Prim1', 
                '/PrimVariants{primVariant=one}',
                Pcp.MapFunction({
                     '/': '/', 
                     '/PrimVariants': '/Prim1'})
                ), 
            Pcp.Dependency(
                '/Prim1', 
                '/PrimVariants{primVariant=one}',
                Pcp.MapFunction({
                    '/': '/',
                    '/PrimVariants': '/Prim1'})
                ),
            Pcp.Dependency(
                '/Prim2',
                '/PrimVariants{primVariant=one}',
                Pcp.MapFunction({
                    '/': '/', 
                    '/PrimVariants': '/Prim2'})
                )
            ])

        ########################################################################
        # Because deps are analyzed in terms of prim arcs, recurseOnSite needs
        # to be careful to work correctly when querying deps on a property path.
        self._AssertDepsEqual( self._FindSiteDeps(
                'BasicVariantWithConnections/root.usda',
                'BasicVariantWithConnections/camera.usda',
                '/camera.HypotheticalProperty',
                recurseOnSite = True,
            ),
            [
            Pcp.Dependency(
                '/main_cam.HypotheticalProperty',
                '/camera.HypotheticalProperty',
                Pcp.MapFunction({
                    '/camera': '/main_cam'})
                )
            ])


        ########################################################################
        # Because deps are analyzed in terms of prim arcs, recurseOnSite needs
        # to be careful to work correctly when querying deps on a property path.
        self._AssertDepsEqual( self._FindSiteDeps(
                'BasicVariantWithConnections/root.usda',
                'BasicVariantWithConnections/camera.usda',
                '/camera.HypotheticalProperty',
                recurseOnSite = True,
            ),
            [
            Pcp.Dependency(
                '/main_cam.HypotheticalProperty',
                '/camera.HypotheticalProperty',
                Pcp.MapFunction({
                    '/camera': '/main_cam'})
                )
            ])

        ########################################################################
        # A reference arc, using recurseOnIndex to pick up child indexes
        # that do not introduce any new deps.
        #
        self._AssertDepsEqual( self._FindSiteDeps(
             'BasicReference/root.usda',
             'BasicReference/ref2.usda',
             '/PrimB',
             recurseOnIndex = True),
            [
            Pcp.Dependency(
                '/PrimWithReferences',
                '/PrimB',
                Pcp.MapFunction({'/PrimB': '/PrimWithReferences'})
                ),
            Pcp.Dependency(
                '/PrimWithReferences/PrimA_Child',
                '/PrimB/PrimA_Child',
                Pcp.MapFunction({'/PrimB': '/PrimWithReferences'})
                ),
            Pcp.Dependency(
                '/PrimWithReferences/PrimB_Child',
                '/PrimB/PrimB_Child',
                Pcp.MapFunction({'/PrimB': '/PrimWithReferences'})
                ),
            Pcp.Dependency(
                '/PrimWithReferences/PrimC_Child',
                '/PrimB/PrimC_Child',
                Pcp.MapFunction({'/PrimB': '/PrimWithReferences'})
                )
            ])

        ########################################################################
        # A relocation that affects a connection target path.
        # This requires path translation below the dependency arc.
        #
        self._AssertDepsEqual( self._FindSiteDeps(
             'TrickyConnectionToRelocatedAttribute/root.usda',
             'TrickyConnectionToRelocatedAttribute/eye_rig.usda',
             '/EyeRig/rig/Mover.bar[/EyeRig/Anim.foo]'),
            [
            Pcp.Dependency(
                '/HumanRig/rig/Face/rig/LEyeRig/rig/Mover.bar[/HumanRig/Anim/Face/LEye.foo]',
                '/EyeRig/rig/Mover.bar[/EyeRig/Anim.foo]',
                Pcp.MapFunction({
                    '/EyeRig': '/HumanRig/rig/Face/rig/LEyeRig',
                    '/EyeRig/Anim': '/HumanRig/Anim/Face/LEye',
                    })
                ),
            Pcp.Dependency(
                '/HumanRig/rig/Face/rig/REyeRig/rig/Mover.bar[/HumanRig/Anim/Face/REye.foo]',
                '/EyeRig/rig/Mover.bar[/EyeRig/Anim.foo]',
                Pcp.MapFunction({
                    '/EyeRig': '/HumanRig/rig/Face/rig/REyeRig',
                    '/EyeRig/Anim': '/HumanRig/Anim/Face/REye',
                    })
                ),
            Pcp.Dependency(
                '/HumanRig/rig/Face/rig/SymEyeRig/rig/Mover.bar[/HumanRig/rig/Face/rig/SymEyeRig/Anim.foo]',
                '/EyeRig/rig/Mover.bar[/EyeRig/Anim.foo]',
                Pcp.MapFunction({
                    '/EyeRig': '/HumanRig/rig/Face/rig/SymEyeRig',
                    })
                ),
            ])

        ########################################################################
        # A relocation that causes a virtual (aka spooky) dependency.
        #
        # Virtual deps are not returned when not requested:
        self._AssertDepsEqual( self._FindSiteDeps(
            'TrickyConnectionToRelocatedAttribute/root.usda',
            'TrickyConnectionToRelocatedAttribute/root.usda',
            '/HumanRig/rig/Face/rig/LEyeRig/Anim',
            depMask = (Pcp.DependencyTypeDirect | Pcp.DependencyTypeAncestral
                        | Pcp.DependencyTypeNonVirtual)
            ),
            [])

        # Virtual deps are introduced by relocations:
        self._AssertDepsEqual( self._FindSiteDeps(
            'TrickyConnectionToRelocatedAttribute/root.usda',
            'TrickyConnectionToRelocatedAttribute/root.usda',
            '/HumanRig/rig/Face/rig/LEyeRig/Anim',
            depMask = (Pcp.DependencyTypeDirect | Pcp.DependencyTypeAncestral
                        | Pcp.DependencyTypeVirtual)
            ),
            # XXX These deps are duplicated 2x due to the presence of relocates
            # nodes representing virtual dependencies.  Leaving here for now,
            # but this raises an interesting question w/r/t whether we should
            # unique the deps.
            [
            Pcp.Dependency(
                '/HumanRig/Anim/Face/LEye',
                '/HumanRig/rig/Face/rig/LEyeRig/Anim',
                Pcp.MapFunction.Identity()),
            Pcp.Dependency(
                '/HumanRig/Anim/Face/LEye',
                '/HumanRig/rig/Face/rig/LEyeRig/Anim',
                Pcp.MapFunction.Identity()),
            Pcp.Dependency(
                '/HumanRig/Anim/Face/LEye',
                '/HumanRig/rig/Face/rig/LEyeRig/Anim',
                Pcp.MapFunction.Identity()),
            Pcp.Dependency(
                '/HumanRig/Anim/Face/LEye',
                '/HumanRig/rig/Face/rig/LEyeRig/Anim',
                Pcp.MapFunction.Identity()),
            ])

if __name__ == "__main__":
    unittest.main()
