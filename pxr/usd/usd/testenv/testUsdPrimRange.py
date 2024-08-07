#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

# pylint: disable=map-builtin-not-iterating

import unittest
from pxr import Sdf, Tf, Usd

allFormats = ['usd' + x for x in 'ac']

class TestUsdPrimRange(unittest.TestCase):
    def test_PrimIsDefined(self):
        for fmt in allFormats:
            s = Usd.Stage.CreateInMemory('TestPrimIsDefined.'+fmt)
            pseudoRoot = s.GetPrimAtPath("/")
            foo = s.DefinePrim("/Foo", "Mesh")
            bar = s.OverridePrim("/Bar")
            faz = s.DefinePrim("/Foo/Faz", "Mesh")
            baz = s.DefinePrim("/Bar/Baz", "Mesh")

            # Change specifier so that /Bar is an over prim with no type.
            s.GetRootLayer().GetPrimAtPath("/Bar").specifier = Sdf.SpecifierOver

            # Default tree iterator should not hit undefined prims.
            x = list(Usd.PrimRange(pseudoRoot))
            self.assertEqual(x, [pseudoRoot, foo, faz])

            # Create a tree iterator and ensure that /Bar and its descendants aren't
            # traversed by it.
            x = list(Usd.PrimRange(pseudoRoot, Usd.PrimIsDefined))
            self.assertEqual(x, [pseudoRoot, foo, faz])

            # When we ask for undefined prim rooted at /Bar, verify that bar and baz
            # are returned.
            x = list(Usd.PrimRange(bar, ~Usd.PrimIsDefined))
            self.assertEqual(x, [bar, baz])

    def test_PrimHasDefiningSpecifier(self):
        for fmt in allFormats:
            stageFile = 'testHasDefiningSpecifier.' + fmt
            stage = Usd.Stage.Open(stageFile)

            # In the case of nested overs and defs, the onus is
            # on the client to manually prune their results either by
            # restarting iteration upon hitting an over, or by iterating
            # through all prims.
            root = stage.GetPrimAtPath('/a1')
            actual = []
            expected = [stage.GetPrimAtPath(x) for x in ['/a1', '/a1/a2']]
            for prim in Usd.PrimRange.AllPrims(root):
                if prim.HasDefiningSpecifier():
                    actual.append(prim)
            self.assertEqual(actual, expected)

            root = stage.GetPrimAtPath('/b1')
            actual = []
            expected = [stage.GetPrimAtPath(x) for x in 
                        ['/b1/b2', '/b1/b2/b3/b4/b5/b6']]
            for prim in Usd.PrimRange(root, Usd.PrimIsActive):
                if prim.HasDefiningSpecifier():
                    actual.append(prim)
            self.assertEqual(actual, expected)

            # Note that the over is not included in our traversal.
            root = stage.GetPrimAtPath('/c1')
            actual = list(Usd.PrimRange(root, Usd.PrimHasDefiningSpecifier))
            expected = [stage.GetPrimAtPath(x) for x in 
                        ['/c1', '/c1/c2', '/c1/c2/c3']]
            self.assertEqual(actual, expected)

    def test_PrimIsActive(self):
        for fmt in allFormats:
            s = Usd.Stage.CreateInMemory('TestPrimIsActive.'+fmt)
            foo = s.DefinePrim("/Foo", "Mesh")
            bar = s.DefinePrim("/Foo/Bar", "Mesh")
            baz = s.DefinePrim("/Foo/Bar/Baz", "Mesh")
            baz.SetActive(False)

            # Create a tree iterator and ensure that /Bar and its descendants aren't
            # traversed by it.
            x1 = list(Usd.PrimRange(foo, Usd.PrimIsActive))
            self.assertEqual(x1, [foo, bar])

            # Test to make sure the predicate is checked on the iteration root.
            x2 = list(Usd.PrimRange(baz, Usd.PrimIsActive))
            self.assertEqual(x2, [])

            x3 = list(Usd.PrimRange(baz, ~Usd.PrimIsActive))
            self.assertEqual(x3, [baz])

            x4 = list(Usd.PrimRange(bar, Usd.PrimIsActive))
            self.assertEqual(x4, [bar])
       
    def test_PrimIsModelOrGroup(self):
        for fmt in allFormats:
            s = Usd.Stage.CreateInMemory('TestPrimIsModelOrGroup.'+fmt)
            group = s.DefinePrim("/Group", "Xform")
            Usd.ModelAPI(group).SetKind('group')
            model = s.DefinePrim("/Group/Model", "Model")
            Usd.ModelAPI(model).SetKind('model')
            mesh = s.DefinePrim("/Group/Model/Sbdv", "Mesh")

            x1 = list(Usd.PrimRange(group, Usd.PrimIsModel))
            self.assertEqual(x1, [group, model])

            x2 = list(Usd.PrimRange(group, Usd.PrimIsGroup))
            self.assertEqual(x2, [group])

            x3 = list(Usd.PrimRange(group, ~Usd.PrimIsModel))
            self.assertEqual(x3, [])

            x4 = list(Usd.PrimRange(mesh, ~Usd.PrimIsModel))
            self.assertEqual(x4, [mesh])

            x5 = list(Usd.PrimRange(group, 
                Usd.PrimIsModel | Usd.PrimIsGroup))
            self.assertEqual(x5, [group, model])

            x6 = list(Usd.PrimRange(group, 
                Usd.PrimIsModel & Usd.PrimIsGroup))
            self.assertEqual(x6, [group])

    def test_PrimIsAbstract(self):
        for fmt in allFormats:
            s = Usd.Stage.CreateInMemory('TestPrimIsAbstract.'+fmt)
            group = s.DefinePrim("/Group", "Xform")
            c = s.CreateClassPrim("/class_Model")

            x1 = list(Usd.PrimRange(group, Usd.PrimIsAbstract))
            self.assertEqual(x1, [])

            x2 = list(Usd.PrimRange(group, ~Usd.PrimIsAbstract))
            self.assertEqual(x2, [group])

            x3 = list(Usd.PrimRange(c, Usd.PrimIsAbstract))
            self.assertEqual(x3, [c])

            x4 = list(Usd.PrimRange(c, ~Usd.PrimIsAbstract))
            self.assertEqual(x4, [])

    def test_PrimIsLoaded(self):
        for fmt in allFormats:
            payloadStage = Usd.Stage.CreateInMemory("payload."+fmt)
            p = payloadStage.DefinePrim("/Payload", "Scope")

            stage = Usd.Stage.CreateInMemory("scene."+fmt)
            foo = stage.DefinePrim("/Foo")
            foo.GetPayloads().AddPayload(
                payloadStage.GetRootLayer().identifier, "/Payload")

            self.assertEqual(stage.GetLoadSet(), ["/Foo"])

            stage.Unload("/Foo")

            x1 = list(Usd.PrimRange(foo, ~Usd.PrimIsLoaded))
            self.assertEqual(x1, [foo])

            x2 = list(Usd.PrimRange(foo, Usd.PrimIsLoaded))
            self.assertEqual(x2, [])

            stage.Load("/Foo")

            x3 = list(Usd.PrimRange(foo, ~Usd.PrimIsLoaded))
            self.assertEqual(x3, [])

            x4 = list(Usd.PrimRange(foo, Usd.PrimIsLoaded))
            self.assertEqual(x4, [foo])

    def test_PrimIsInstanceOrPrototypeOrRoot(self):
        for fmt in allFormats:
            refStage = Usd.Stage.CreateInMemory("reference."+fmt)
            refStage.DefinePrim("/Ref/Child")

            stage = Usd.Stage.CreateInMemory("scene."+fmt)
            root = stage.DefinePrim("/Root")

            i = stage.DefinePrim("/Root/Instance")
            i.GetReferences().AddReference(refStage.GetRootLayer().identifier, "/Ref")
            i.SetMetadata("instanceable", True)

            n = stage.DefinePrim("/Root/NonInstance")
            n.GetReferences().AddReference(refStage.GetRootLayer().identifier, "/Ref")
            nChild = stage.GetPrimAtPath('/Root/NonInstance/Child')

            # Test Usd.PrimIsInstance
            self.assertEqual(list(Usd.PrimRange(i, Usd.PrimIsInstance)), 
                        [i])
            self.assertEqual(list(Usd.PrimRange(i, ~Usd.PrimIsInstance)), [])

            self.assertEqual(list(Usd.PrimRange(n, Usd.PrimIsInstance)), [])
            self.assertEqual(list(Usd.PrimRange(n, ~Usd.PrimIsInstance)), 
                        [n, nChild])

            self.assertEqual(list(Usd.PrimRange(root, Usd.PrimIsInstance)), 
                        [])
            self.assertEqual(list(Usd.PrimRange(root, ~Usd.PrimIsInstance)), 
                        [root, n, nChild])

    def test_RoundTrip(self):
        for fmt in allFormats:
            stage = Usd.Stage.CreateInMemory('TestRoundTrip.'+fmt)
            prims = map(stage.DefinePrim, ['/foo', '/bar', '/baz'])

            treeRange = Usd.PrimRange(stage.GetPseudoRoot())
            tripped = Usd._TestPrimRangeRoundTrip(treeRange)
            self.assertEqual(treeRange, tripped)
            self.assertEqual(list(treeRange), list(tripped))

            treeRange = Usd.PrimRange.PreAndPostVisit(stage.GetPseudoRoot())
            tripped = Usd._TestPrimRangeRoundTrip(treeRange)
            self.assertEqual(treeRange, tripped)
            self.assertEqual(list(treeRange), list(tripped))

    def test_StageTraverse(self):
        for fmt in allFormats:
            s = Usd.Stage.CreateInMemory('TestStageTraverse.'+fmt)
            pseudoRoot = s.GetPrimAtPath("/")
            foo = s.DefinePrim("/Foo", "Mesh")
            bar = s.OverridePrim("/Bar")
            faz = s.DefinePrim("/Foo/Faz", "Mesh")
            baz = s.DefinePrim("/Bar/Baz", "Mesh")

            # Change speicifer so that /Bar is an over with no type.
            s.GetRootLayer().GetPrimAtPath("/Bar").specifier = Sdf.SpecifierOver

            # Default traverse should not hit undefined prims.
            x = list(s.Traverse())
            self.assertEqual(x, [foo, faz])
            x = list(Usd.PrimRange.Stage(s))
            self.assertEqual(x, [foo, faz])

            # Traverse all.
            x = list(s.TraverseAll())
            self.assertEqual(x, [foo, faz, bar, baz])
            x = list(Usd.PrimRange.Stage(s, predicate=Usd.PrimAllPrimsPredicate))
            self.assertEqual(x, [foo, faz, bar, baz])

            # Traverse undefined prims.
            x = list(s.Traverse(~Usd.PrimIsDefined))
            self.assertEqual(x, [bar, baz])
            x = list(Usd.PrimRange.Stage(s, predicate=~Usd.PrimIsDefined))
            self.assertEqual(x, [bar, baz])

    def test_WithInstancing(self):
        for fmt in allFormats:
            refStage = Usd.Stage.CreateInMemory("reference."+fmt)
            refStage.DefinePrim("/Ref/Child")

            stage = Usd.Stage.CreateInMemory("scene."+fmt)
            root = stage.DefinePrim("/Root")

            i = stage.DefinePrim("/Root/Instance")
            i.GetReferences().AddReference(refStage.GetRootLayer().identifier, "/Ref")
            i.SetMetadata("instanceable", True)

            n = stage.DefinePrim("/Root/NonInstance")
            n.GetReferences().AddReference(refStage.GetRootLayer().identifier, "/Ref")
            nChild = stage.GetPrimAtPath('/Root/NonInstance/Child')

            prototype = stage.GetPrototypes()[0]
            prototypeChild = prototype.GetChild('Child')

            # A default traversal of a stage with instances should not descend into 
            # instance prototypes
            self.assertEqual(list(Usd.PrimRange.Stage(stage)), [root, i, n, nChild])
            self.assertEqual(list(Usd.PrimRange(stage.GetPseudoRoot())),
                        [stage.GetPseudoRoot(), root, i, n, nChild])

            # But the tree iterator should allow traversal of the prototypes if
            # explicitly specified.
            self.assertEqual(list(Usd.PrimRange(prototype)), 
                             [prototype, prototypeChild])

    def test_PrimIterationOnExpiredStage(self):
        for fmt in allFormats:
            rootLayer = Sdf.Layer.CreateAnonymous('.' + fmt)
            for p in Sdf.Path('/Something/To/Iterate/Over').GetPrefixes():
                Sdf.PrimSpec(
                    rootLayer.GetPrimAtPath(p.GetParentPath()), p.name,
                    Sdf.SpecifierDef)

            # Check that a Usd.PrimRange raises a Python exception when
            # used with a dead stage.
            stage = Usd.Stage.Open(rootLayer)
            primRange = Usd.PrimRange.Stage(stage)
            del stage
            with self.assertRaises(RuntimeError):
                prims = list(primRange)

            with self.assertRaises(RuntimeError):
                prims = list(Usd.PrimRange.Stage(Usd.Stage.Open(rootLayer)))

            with self.assertRaises(RuntimeError):
                prims = list(Usd.Stage.Open(rootLayer).Traverse())

if __name__ == "__main__":
    unittest.main()
