#!/pxrpythonsubst
#
# Copyright 2020 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

# pylint: disable=range-builtin-not-iterating

from pxr import Sdf, Tf, Gf, Vt
import sys, unittest

# This class is derived by format-specific test cases below.
class TestSdfAttributeBase():

    def CreateAnonymous(self):
        return Sdf.Layer.CreateAnonymous(self.extension)
    
    def test_Creation(self):

        # Test SdPropertySpec abstractness
        with self.assertRaises(RuntimeError):
            nullProp = Sdf.PropertySpec()

        # Test SdAttributeSpec creation and name
        layer = self.CreateAnonymous()
        prim = Sdf.PrimSpec(layer, "test", Sdf.SpecifierDef, "bogus_type")
        attr = Sdf.AttributeSpec(prim, 'numCrvs', Sdf.ValueTypeNames.Int)
        self.assertEqual(attr.name, 'numCrvs')
        self.assertEqual(attr.typeName, Sdf.ValueTypeNames.Int)
        self.assertEqual(attr.owner, prim)
        self.assertEqual(len( prim.properties ), 1)
        self.assertTrue(attr in prim.properties)
        self.assertTrue(attr.name in prim.properties)
        self.assertEqual(prim.properties[0], attr)
        self.assertEqual(prim.properties[ attr.name ], attr)
        self.assertEqual(prim.properties[0].custom, False)

        # create a duplicate attribute: error expected
        with self.assertRaises(RuntimeError):
            dupe = Sdf.AttributeSpec(
                attr.owner, attr.name, Sdf.ValueTypeNames.Int)
            self.assertEqual(len(prim.properties), 1)

        # create a duplicate attribute via renaming: error expected
        dupe = Sdf.AttributeSpec(attr.owner, 'dupe', Sdf.ValueTypeNames.Int)
        self.assertTrue(dupe)
        with self.assertRaises(RuntimeError):
            dupe.name = attr.name
        self.assertEqual(dupe.name, 'dupe')

        # create an attribute under an expired prim: error expected
        prim2 = Sdf.PrimSpec(layer, "test2", Sdf.SpecifierDef, "bogus_type")
        self.assertEqual(prim2.name, 'test2')
        self.assertFalse(prim2.expired)
        del layer.rootPrims[prim2.name]
        self.assertTrue(prim2.expired)
        with self.assertRaises(RuntimeError):
            attr2 = Sdf.AttributeSpec(prim2, 'attr2', Sdf.ValueTypeNames.Int)

        # create an attribute on the PseudoRoot: error expected
        self.assertEqual(len(layer.pseudoRoot.properties), 0)
        with self.assertRaises(RuntimeError):
            bogus = Sdf.AttributeSpec(
                layer.pseudoRoot, 'badAttr', Sdf.ValueTypeNames.Int)
        self.assertEqual(len(layer.pseudoRoot.properties), 0)

    def test_Rename(self):
        layer = self.CreateAnonymous()
        prim = Sdf.PrimSpec(layer, "test", Sdf.SpecifierDef, "bogus_type")
        attr = Sdf.AttributeSpec(prim, 'numCrvs', Sdf.ValueTypeNames.Int)
        attr.name = 'numberOfCurves'
        self.assertEqual(attr.name, 'numberOfCurves')

        # Check invalid cases.
        for badName in ['', 'a.b', '<ab>', 'a[]', 'a/']:
            oldName = attr.name
            self.assertNotEqual(oldName, '')
            self.assertNotEqual(oldName, badName)
            with self.assertRaises(Tf.ErrorException):
                attr.name = badName
            self.assertEqual(attr.name, oldName)

    def test_InvalidName(self):
        layer = self.CreateAnonymous()
        prim = Sdf.PrimSpec(layer, "test", Sdf.SpecifierDef, "bogus_type")
        for badName in ['', 'a.b', '<ab>', 'a[]', 'a/']:
            with self.assertRaises(RuntimeError):
                badAttr = Sdf.AttributeSpec(
                    prim, badName, Sdf.ValueTypeNames.Int)

    def test_Metadata(self):
        layer = self.CreateAnonymous()
        prim = Sdf.PrimSpec(layer, "test", Sdf.SpecifierDef, "bogus_type")
        attr = Sdf.AttributeSpec(prim, 'numCrvs', Sdf.ValueTypeNames.Int)

        # assign a default value, otherwise after ClearInfo, attr will expire.
        attr.default = 1

        self.assertEqual(attr.custom, False)
        attr.custom = True
        self.assertEqual(attr.custom, True)
        attr.custom = False
        self.assertEqual(attr.custom, False)

        self.assertFalse(attr.HasInfo('customData'))
        self.assertTrue(len(attr.customData) == 0)
        attr.customData = {'attrCustomData': {'attrBool': True}}
        self.assertTrue(attr.HasInfo('customData'))
        self.assertTrue(len(attr.customData) == 1)
        self.assertEqual(attr.customData['attrCustomData'], {'attrBool': True})
        attr.customData = {'attrCustomData': {'attrFloat': 1.0}}
        self.assertTrue(attr.HasInfo('customData'))
        self.assertTrue(len(attr.customData) == 1)
        self.assertEqual(attr.customData['attrCustomData'], {'attrFloat': 1.0})
        attr.ClearInfo('customData')
        self.assertFalse(attr.HasInfo('customData'))
        self.assertTrue(len(attr.customData) == 0)
        with self.assertRaises(Tf.ErrorException):
            # This dict's Gf.Range1d values are not supported as metadata in
            # scene description.
            attr.customData = { 'foo': { 'bar': Gf.Range1d(1.0, 2.0) },
                                'bar': Gf.Range1d(1.0, 2.0) }
        self.assertTrue(len(attr.customData) == 0)

        # Setting doc so that we can clear default without expiring.
        attr.documentation = 'some documentation'
        attr.ClearInfo('default')
        self.assertFalse(attr.HasInfo('default'))
        self.assertFalse(attr.HasDefaultValue())
        self.assertTrue(attr.default is None)
        for val in range(-10,10):
            attr.default = val
            attr.default = val # do this twice to cover the SetDefault
                               # short-circuit
            self.assertTrue(attr.HasInfo('default'))
            self.assertTrue(attr.HasDefaultValue())
            self.assertEqual(attr.default, val)
            self.assertFalse(attr.default is None)
            self.assertFalse(hasattr(attr.default, '_isVtArray'))
        attr.ClearDefaultValue()
        self.assertFalse(attr.HasInfo('default'))
        self.assertFalse (attr.HasDefaultValue())
        self.assertTrue(attr.default is None)

        shapedAttr = Sdf.AttributeSpec(
            prim, 'testShapedAttr', Sdf.ValueTypeNames.IntArray)
        self.assertFalse(shapedAttr.HasInfo('default'))
        self.assertTrue(shapedAttr.default is None)

        # set documentation so that we can clear without expiring.
        shapedAttr.documentation = 'some shaped documentation'

        for dim in range(1,10):
            val = Vt.IntArray(dim)
            val[...] = range(dim)
            self.assertEqual(len(val), dim)
            shapedAttr.default = val
            self.assertTrue(shapedAttr.HasInfo('default'))
            self.assertEqual(shapedAttr.default, val)
            self.assertFalse(shapedAttr.default is None)
            self.assertTrue(hasattr(shapedAttr.default, '_isVtArray'))
        shapedAttr.ClearInfo('default')
        self.assertFalse(shapedAttr.HasInfo('default'))
        self.assertTrue(shapedAttr.default is None)

        self.assertEqual(attr.comment, '')
        attr.comment = 'foo'
        self.assertEqual(attr.comment, 'foo')
        attr.comment = 'bar'
        self.assertEqual(attr.comment, 'bar')
        attr.comment = ''
        self.assertEqual(attr.comment, '')

        attr.ClearInfo('documentation')
        self.assertFalse(attr.HasInfo('documentation'))
        self.assertEqual(attr.documentation, '')
        attr.documentation = 'foo'
        self.assertTrue(attr.HasInfo('documentation'))
        self.assertEqual(attr.documentation, 'foo')
        attr.documentation = 'bar'
        self.assertTrue(attr.HasInfo('documentation'))
        self.assertEqual(attr.documentation, 'bar')
        attr.ClearInfo('documentation')
        self.assertFalse(attr.HasInfo('documentation'))
        self.assertEqual(attr.documentation, '')

        self.assertFalse(attr.HasInfo('displayGroup'))
        self.assertEqual(attr.displayGroup, '')
        attr.displayGroup = 'foo'
        self.assertTrue(attr.HasInfo('displayGroup'))
        self.assertEqual(attr.displayGroup, 'foo')
        attr.displayGroup = 'bar'
        self.assertTrue(attr.HasInfo('displayGroup'))
        self.assertEqual(attr.displayGroup, 'bar')
        attr.ClearInfo('displayGroup')
        self.assertFalse(attr.HasInfo('displayGroup'))
        self.assertEqual(attr.displayGroup, '')

    def test_ClearUnexpectedField(self):
        layer = self.CreateAnonymous()
        layer.ImportFromString(
'''#usda 1.0
def Sphere "Foo"
{
    double radius (
        displayName = "Radius"
        unrecognized = "Test"
    )
}
''')

        spec = layer.GetPropertyAtPath("/Foo.radius")
        self.assertTrue(spec.HasInfo("unrecognized"))
        spec.ClearInfo("unrecognized")
        self.assertFalse(spec.HasInfo("unrecognized"))

    def test_Connections(self):
        layer = self.CreateAnonymous()
        prim = Sdf.PrimSpec(layer, "test", Sdf.SpecifierDef, "bogus_type")
        attr = Sdf.AttributeSpec(prim, 'numCrvs', Sdf.ValueTypeNames.Int)

        self.assertFalse(attr.HasInfo('connectionPaths'))
        self.assertFalse(attr.connectionPathList.isExplicit)
        self.assertEqual(len( attr.connectionPathList.explicitItems ), 0)
        testPath = Sdf.Path('test.attr')
        attr.connectionPathList.explicitItems.append( testPath )
        testPath = testPath.MakeAbsolutePath(attr.path.GetPrimPath())
        self.assertTrue(testPath in attr.connectionPathList.explicitItems)
        self.assertTrue(attr.HasInfo('connectionPaths'))
        self.assertTrue(attr.connectionPathList.isExplicit)
        testPath2 = Sdf.Path('test2.attr')
        attr.connectionPathList.explicitItems.append( testPath2 )
        testPath2 = testPath2.MakeAbsolutePath(attr.path.GetPrimPath())
        self.assertTrue(testPath2 in attr.connectionPathList.explicitItems)
        self.assertEqual(attr.connectionPathList.explicitItems,
                         [testPath, testPath2])
        attr.connectionPathList.explicitItems.clear()
        self.assertTrue(attr.HasInfo('connectionPaths'))
        self.assertTrue(testPath not in attr.connectionPathList.explicitItems)
        self.assertTrue(testPath2 not in attr.connectionPathList.explicitItems)
        self.assertEqual(attr.connectionPathList.explicitItems, [])
        attr.connectionPathList.explicitItems[:] = [testPath, testPath2]
        self.assertTrue(attr.HasInfo('connectionPaths'))
        self.assertTrue(testPath in attr.connectionPathList.explicitItems)
        self.assertTrue(testPath2 in attr.connectionPathList.explicitItems)
        self.assertEqual(attr.connectionPathList.explicitItems,
                         [testPath, testPath2])

        # adding connection to invalid path: errors expected
        bogus = Sdf.Path('/Not/A{Valid=Connection}Path/to/test.attribute')
        self.assertTrue(bogus not in attr.connectionPathList.explicitItems)
        with self.assertRaises(Tf.ErrorException):
            attr.connectionPathList.explicitItems.append(bogus)
        self.assertTrue(bogus not in attr.connectionPathList.explicitItems)

        # inserting connection paths at various indices
        testPath1 = Sdf.Path(
            'test.foo').MakeAbsolutePath(attr.path.GetPrimPath())
        testPath2 = Sdf.Path(
            'test2.foo').MakeAbsolutePath(attr.path.GetPrimPath())
        attr.connectionPathList.explicitItems[:] = [testPath1, testPath2]
        testPath_middle = Sdf.Path(
            'middle.foo').MakeAbsolutePath(attr.path.GetPrimPath())
        attr.connectionPathList.explicitItems.insert( 1, testPath_middle )
        testPath_first = Sdf.Path(
            'first.foo').MakeAbsolutePath(attr.path.GetPrimPath())
        attr.connectionPathList.explicitItems.insert(0, testPath_first)
        with self.assertRaises(IndexError):
            attr.connectionPathList.explicitItems.insert(1000, 'last.foo')
        testPath_penultimate = Sdf.Path(
            'penultimate.foo').MakeAbsolutePath(attr.path.GetPrimPath())
        attr.connectionPathList.explicitItems.insert(-1, testPath_penultimate)
        self.assertEqual(attr.connectionPathList.explicitItems,
                         [testPath_first, testPath1, testPath_middle,
                          testPath_penultimate, testPath2])

        # removing connection paths by value
        testPath_dead = Sdf.Path(
            'dead.foo').MakeAbsolutePath(attr.path.GetPrimPath())
        self.assertTrue(
            testPath_dead not in attr.connectionPathList.explicitItems)
        attr.connectionPathList.explicitItems.insert(2, testPath_dead)
        self.assertTrue(testPath_dead in attr.connectionPathList.explicitItems)
        del attr.connectionPathList.explicitItems[testPath_dead]
        self.assertTrue(
            testPath_dead not in attr.connectionPathList.explicitItems)

        # adding duplicate connection path
        testPath = attr.connectionPathList.explicitItems[0]
        self.assertEqual(
            attr.connectionPathList.explicitItems.count(testPath), 1)
        attr.connectionPathList.explicitItems.append(testPath)
        self.assertEqual(
            attr.connectionPathList.explicitItems.count(testPath), 1)

        # modifying connection paths via info API (disallowed)
        with self.assertRaises(Tf.ErrorException):
            attr.SetInfo('connectionPaths', Sdf.PathListOp())
        with self.assertRaises(Tf.ErrorException):
            attr.ClearInfo('connectionPaths')

        # adding variant-selection connection path: error expected
        varSelPath =  Sdf.Path("/Variant{vset=vsel}Is/Not/Allowed")
        connectionPathListEditor = attr.connectionPathList.explicitItems
        with self.assertRaises(Tf.ErrorException):
            connectionPathListEditor.append(varSelPath)

        # changing paths for connection paths'
        testPath_shlep = Sdf.Path(
            'shlep.foo').MakeAbsolutePath(attr.path.GetPrimPath())
        attr.connectionPathList.explicitItems[:] = [testPath1, testPath2]
        attr.connectionPathList.ReplaceItemEdits( testPath1, testPath_shlep )
        self.assertEqual(
            attr.connectionPathList.explicitItems, [testPath_shlep, testPath2])

        attr.connectionPathList.ClearEditsAndMakeExplicit()
        self.assertTrue(attr.connectionPathList.isExplicit)
        self.assertEqual(len(attr.connectionPathList.explicitItems), 0)
        self.assertEqual(len(attr.connectionPathList.addedItems), 0)
        self.assertEqual(len(attr.connectionPathList.deletedItems), 0)
        self.assertEqual(len(attr.connectionPathList.orderedItems), 0)

        attr.connectionPathList.ClearEdits()
        self.assertFalse(attr.connectionPathList.isExplicit)
        self.assertEqual(len(attr.connectionPathList.explicitItems), 0)
        self.assertEqual(len(attr.connectionPathList.addedItems), 0)
        self.assertEqual(len(attr.connectionPathList.deletedItems), 0)
        self.assertEqual(len(attr.connectionPathList.orderedItems), 0)

        testPath3 = Sdf.Path(
            'test3.foo').MakeAbsolutePath(attr.path.GetPrimPath())
        testPath4 = Sdf.Path(
            'test4.foo').MakeAbsolutePath(attr.path.GetPrimPath())
        testPath_shlep2 = Sdf.Path(
            'shlep2.foo').MakeAbsolutePath(attr.path.GetPrimPath())

        attr.connectionPathList.addedItems[:] = [testPath1, testPath2]
        attr.connectionPathList.deletedItems[:] = [testPath3, testPath4]
        attr.connectionPathList.orderedItems[:] = [testPath2, testPath1]

        attr.connectionPathList.ReplaceItemEdits(testPath2, testPath_shlep)
        attr.connectionPathList.ReplaceItemEdits(testPath3, testPath_shlep2)
        self.assertEqual(
            attr.connectionPathList.addedItems, [testPath1, testPath_shlep])
        self.assertEqual(
            attr.connectionPathList.deletedItems, [testPath_shlep2, testPath4])
        self.assertEqual(
            attr.connectionPathList.orderedItems, [testPath_shlep, testPath1])

        attr.connectionPathList.ClearEdits()
        self.assertFalse(attr.connectionPathList.isExplicit)
        self.assertEqual(len(attr.connectionPathList.explicitItems), 0)
        self.assertEqual(len(attr.connectionPathList.addedItems), 0)
        self.assertEqual(len(attr.connectionPathList.deletedItems), 0)
        self.assertEqual(len(attr.connectionPathList.orderedItems), 0)

        # removing connection paths via the list editor's RemoveItemEdits API
        attr.connectionPathList.explicitItems[:] = [testPath1, testPath2]
        attr.connectionPathList.RemoveItemEdits( testPath2 )
        self.assertEqual(attr.connectionPathList.explicitItems, [testPath1])

        attr.connectionPathList.ClearEdits()

        attr.connectionPathList.addedItems[:] = [testPath1, testPath2]
        attr.connectionPathList.deletedItems[:] = [testPath1, testPath2]
        attr.connectionPathList.orderedItems[:] = [testPath1, testPath2]

        attr.connectionPathList.RemoveItemEdits( testPath1 )
        self.assertEqual(attr.connectionPathList.addedItems, [testPath2])
        self.assertEqual(attr.connectionPathList.deletedItems, [testPath2])
        self.assertEqual(attr.connectionPathList.orderedItems, [testPath2])

        attr.connectionPathList.ClearEdits()

    def test_Path(self):
        layer = self.CreateAnonymous()
        prim = Sdf.PrimSpec(layer, "test", Sdf.SpecifierDef, "bogus_type")
        attr = Sdf.AttributeSpec(prim, 'numCrvs', Sdf.ValueTypeNames.Int)
        self.assertEqual(attr.path,
                         Sdf.Path('/' + prim.name + '.' + attr.name))
        self.assertEqual(prim.GetAttributeAtPath(
            Sdf.Path('.' + attr.name)), attr)
        self.assertEqual(prim.GetObjectAtPath(
            Sdf.Path('.' + attr.name)), attr)
        self.assertEqual(layer.GetAttributeAtPath(attr.path), attr)
        self.assertEqual(layer.GetObjectAtPath(attr.path), attr)
        # Try a bad path... access a relational attribute of an attribute!
        self.assertEqual(prim.GetAttributeAtPath(
            Sdf.Path('.' + attr.name + '[targ].attr')), None)

        # dormant object path, for code coverage
        del prim.properties[attr.name]
        self.assertTrue(attr.expired)

    def test_Inertness(self):
        # Test attribute-specific 'IsInert()' and 'hasOnlyRequiredFields'
        # behavior.
        #
        # Having any connections render the spec non-inert and having more than
        # only required fields. This is important due to the 'remove if inert'
        # cleanup step that automatically runs after any call to ClearInfo.

        layer = self.CreateAnonymous()
        prim = Sdf.PrimSpec(layer, "test", Sdf.SpecifierDef, "bogus_type")
        attr = Sdf.AttributeSpec(prim, 'testAttr', Sdf.ValueTypeNames.Int)
        self.assertTrue(attr)
        self.assertFalse(attr.IsInert())
        self.assertTrue(attr.hasOnlyRequiredFields)
        attr.connectionPathList.explicitItems.append('/connection.path')
        self.assertFalse(attr.IsInert())
        self.assertFalse(attr.hasOnlyRequiredFields)

        attr.connectionPathList.ClearEdits()
        self.assertEqual(len(attr.connectionPathList.explicitItems), 0)
        self.assertFalse(attr.IsInert())
        self.assertTrue(attr.hasOnlyRequiredFields)

    def test_TimeSamples(self):
        # Test interaction with time samples on an attribute
        timeSamplesLayer = self.CreateAnonymous()
        timeSamplesLayer.ImportFromString(
'''#usda 1.0
def Scope "Scope"
{
    custom double empty = 0
    double empty.timeSamples = {
    }
    custom double radius = 1.0
    double radius.timeSamples = {
        1.23: 5,
        3.23: 10,
        6: 5,
    }
    custom string desc = ""
    string desc.timeSamples = {
        1.23: "foo",
        3.23: "bar",
        6: "baz",
    }
}
''')
        self.assertTrue(timeSamplesLayer)
        prim = timeSamplesLayer.GetPrimAtPath('/Scope')
        self.assertTrue(prim)
        self.assertEqual(prim.attributes['empty'].GetInfo('timeSamples'), {})
        self.assertEqual(prim.attributes['radius'].GetInfo('timeSamples'),
                         {1.23: 5, 3.23: 10, 6: 5})
        self.assertEqual(prim.attributes['desc'].GetInfo('timeSamples'),
                         {1.23: 'foo', 3.23: 'bar', 6: 'baz'})

        prim.attributes['radius'].SetTimeSample(4.0, 2.0)
        self.assertEqual(prim.attributes['radius'].QueryTimeSample(4.0), 2.0)

        prim.attributes['desc'].SetTimeSample(10, 'boom')
        self.assertEqual(prim.attributes['desc'].GetNumTimeSamples(), 4)
        self.assertEqual(prim.attributes['desc'].ListTimeSamples(), [1.23, 3.23, 6, 10])
        prim.attributes['desc'].EraseTimeSample(10)
        self.assertEqual(prim.attributes['desc'].GetNumTimeSamples(), 3)
        self.assertEqual(prim.attributes['desc'].GetBracketingTimeSamples(2.0), (True, 1.23, 3.23))

    def test_Spline(self):
        splineLayer = self.CreateAnonymous()
        splineLayer.ImportFromString(
'''#usda 1.0
def Xform "Prim"
{
    double a.spline = {
        1: 5; post held,
        2: 18; post held,
    }
    double a2
    float b.spline = {
        1: 10; post held,
    }
    double c.timeSamples = {
        6: 5,
    }
    int e
}
''')
        # Test HasSpline, GetSpline, SetSpline and ClearSpline works on
        # compatible types
        self.assertTrue(splineLayer)
        a = splineLayer.GetAttributeAtPath('/Prim.a')
        splineA = a.GetSpline()
        self.assertTrue(splineA)
        self.assertTrue(a.HasInfo('spline'))
        self.assertTrue(a.HasSpline())
        self.assertFalse(splineA.IsEmpty())
        self.assertEqual(len(splineA.GetKnots()), 2)
        self.assertEqual(splineA.GetValueTypeName(), "double")
        self.assertEqual(a.typeName.cppTypeName, "double")
        a2 = splineLayer.GetAttributeAtPath('/Prim.a2')
        self.assertTrue(a2)
        self.assertFalse(a2.HasInfo('spline'))
        self.assertFalse(a2.HasSpline())
        self.assertTrue(a2.GetSpline().IsEmpty())
        a2.SetSpline(splineA)
        splineA2 = a2.GetSpline()
        self.assertTrue(splineA2)
        self.assertTrue(a2.HasInfo('spline'))
        self.assertTrue(a2.HasSpline())
        self.assertFalse(splineA2.IsEmpty())
        self.assertEqual(len(splineA2.GetKnots()), 2)
        b = splineLayer.GetAttributeAtPath('/Prim.b')
        splineB = b.GetSpline()
        self.assertTrue(splineB)
        self.assertTrue(b.HasInfo('spline'))
        self.assertTrue(b.HasSpline())
        self.assertFalse(splineB.IsEmpty())
        self.assertEqual(len(splineB.GetKnots()), 1)
        self.assertEqual(splineB.GetValueTypeName(), "float")
        self.assertEqual(b.typeName.cppTypeName, "float")
        a.ClearSpline()
        b.ClearSpline()
        self.assertFalse(a.HasInfo('spline'))
        self.assertFalse(a.HasSpline())
        self.assertFalse(b.HasInfo('spline'))
        self.assertFalse(b.HasSpline())

        # Test that SetSpline results in a coding error when trying to set a 
        # float spline on a double attribute.
        self.assertEqual(a.typeName.cppTypeName, "double")
        with self.assertRaises(Tf.ErrorException):
            a.SetSpline(splineB)
        # No spline gets set on a
        self.assertFalse(a.HasInfo('spline'))
        self.assertFalse(a.HasSpline())

        # Test that HasInfo returns false when spline is not set, yet GetSpline
        # returns an empty spline.
        c = splineLayer.GetAttributeAtPath('/Prim.c')
        self.assertTrue(c)
        self.assertFalse(c.HasInfo('spline'))
        self.assertFalse(c.HasSpline())
        self.assertTrue(c.GetSpline().IsEmpty())

        # Test that SetSpline results in a coding error when trying to set a
        # spline on an attribute that doesn't support splines.
        from pxr import Ts
        e = splineLayer.GetAttributeAtPath('/Prim.e')
        self.assertFalse(Ts.Spline.IsSupportedValueType(e.typeName.type))
        self.assertFalse(e.HasInfo('spline'))
        self.assertFalse(e.HasSpline())
        with self.assertRaises(RuntimeError):
            e.SetSpline(splineA)
        # No spline was set on e
        self.assertFalse(e.HasInfo('spline'))
        self.assertFalse(e.HasSpline())

    def test_Limits(self):
        """
        Exercise basic API for the limits metadata field
        """
        layer = self.CreateAnonymous()
        layer.ImportFromString(
'''#usda 1.0
def Sphere "Foo"
{
    double radius (
        limits = {
            dictionary "hard" = {
                double minimum = 0.0
                double maximum = 10.0
                string foo = "bar"
            }
        }
    )
}
''')

        spec = layer.GetAttributeAtPath("/Foo.radius")

        # Check initial value
        myLimits = {
            'hard' : {
                'minimum' : 0.0,
                'maximum' : 10.0,
                'foo' : 'bar'
            }
        }

        self.assertTrue(spec.HasLimits())
        self.assertEqual(spec.limits, myLimits)

        # Modify
        myLimits['hard']['minimumValue'] = 5.5
        myLimits['hard']['foo'] = 'baz'

        spec.limits = myLimits
        self.assertTrue(spec.HasLimits())
        self.assertEqual(spec.limits, myLimits)

        # Clear
        spec.ClearLimits()
        self.assertFalse(spec.HasLimits())
        self.assertEqual(spec.limits, {})

    def test_OpaqueNoAuthoredDefault(self):
        """
        Attempting to set the default value of an opaque attribute should fail.
        """
        layer = self.CreateAnonymous()
        prim = Sdf.PrimSpec(layer, "Test", Sdf.SpecifierDef, "TestType")
        attr = Sdf.AttributeSpec(prim, "Attr", Sdf.ValueTypeNames.Opaque)
        self.assertEqual(attr.default, None)
        with self.assertRaises(Tf.ErrorException):
            attr.default = Sdf.OpaqueValue()
        self.assertEqual(attr.default, None)

    def test_GroupNoAuthoredDefault(self):
        """
        Attempting to set the default value of a group attribute should fail.
        """
        layer = self.CreateAnonymous()
        prim = Sdf.PrimSpec(layer, "Test", Sdf.SpecifierDef, "TestType")
        attr = Sdf.AttributeSpec(prim, "Attr", Sdf.ValueTypeNames.Group)
        self.assertEqual(attr.default, None)
        with self.assertRaises(Tf.ErrorException):
            attr.default = Sdf.OpaqueValue()
        self.assertEqual(attr.default, None)

    def test_ArrayEditAuthoring(self):
        """
        Test authoring array edit values.
        """
        layer = self.CreateAnonymous()

        prim = Sdf.CreatePrimInLayer(layer, '/prim')
        
        ia = Sdf.AttributeSpec(prim, 'ia', Sdf.ValueTypeNames.IntArray)
        fa = Sdf.AttributeSpec(prim, 'fa', Sdf.ValueTypeNames.FloatArray)
        sa = Sdf.AttributeSpec(prim, 'sa', Sdf.ValueTypeNames.StringArray)
        aa = Sdf.AttributeSpec(prim, 'aa', Sdf.ValueTypeNames.AssetArray)
        p3fa = Sdf.AttributeSpec(prim, 'p3fa', Sdf.ValueTypeNames.Point3fArray)

        # Create values.
        iaDefault = (Vt.IntArrayEditBuilder()
                     .Append(123)
                     .InsertRef(0, -1)
                     .SetSize(1024)
                     .FinalizeAndReset())
        faDefault = (Vt.FloatArrayEditBuilder()
                     .Append(1.23)
                     .InsertRef(-1, -2)
                     .SetSize(99)
                     .FinalizeAndReset())
        saDefault = (Vt.StringArrayEditBuilder()
                     .Append('123')
                     .InsertRef(-2, 0)
                     .MinSize(999, 'fill')
                     .FinalizeAndReset())
        aaDefault = (Sdf.AssetPathArrayEditBuilder()
                     .Append(Sdf.AssetPath('woody'))
                     .Prepend(Sdf.AssetPath('buzz'))
                     .MaxSize(3)
                     .FinalizeAndReset())
        p3faDefault = (Vt.Vec3fArrayEditBuilder()
                       .Append((1,2,3))
                       .Insert((2,3,4), 1)
                       .SetSize(2048, (7,7,7))
                       .FinalizeAndReset())
        
        # Author defaults.
        ia.default = iaDefault
        fa.default = faDefault
        sa.default = saDefault
        aa.default = aaDefault
        p3fa.default = p3faDefault

        # Author time samples.
        iaSample = Vt.IntArrayEditBuilder().Prepend(123).FinalizeAndReset()
        faSample = Vt.FloatArrayEditBuilder().Insert(
            1.23, 0).FinalizeAndReset()
        saSample =  Vt.StringArrayEditBuilder().MinSize(
            123, '123').FinalizeAndReset()
        aaSample = Sdf.AssetPathArrayEditBuilder().Prepend(
            Sdf.AssetPath('123')).FinalizeAndReset()
        p3faSample = Vt.Vec3fArrayEditBuilder().Prepend(
            (1,2,3)).FinalizeAndReset()
        
        ia.SetTimeSample(1.0, iaSample)
        fa.SetTimeSample(1.0, faSample)
        sa.SetTimeSample(1.0, saSample)
        aa.SetTimeSample(1.0, aaSample)
        p3fa.SetTimeSample(1.0, p3faSample)

        # Check that authoring didn't modify the values.
        self.assertEqual(iaDefault, ia.default)
        self.assertEqual(faDefault, fa.default)
        self.assertEqual(saDefault, sa.default)
        self.assertEqual(aaDefault, aa.default)
        self.assertEqual(p3faDefault, p3fa.default)

        self.assertEqual(iaSample, ia.QueryTimeSample(1.0))
        self.assertEqual(faSample, fa.QueryTimeSample(1.0))
        self.assertEqual(saSample, sa.QueryTimeSample(1.0))
        self.assertEqual(aaSample, aa.QueryTimeSample(1.0))
        self.assertEqual(p3faSample, p3fa.QueryTimeSample(1.0))

        # Export to file & string, open and compare.
        fname = 'testSdfAttributeArrayEditAuthoring.' + self.extension
        layer.Export(fname)

        fileLayer = Sdf.Layer.FindOrOpen(fname)
        self.assertTrue(fileLayer)

        strLayer = self.CreateAnonymous()
        self.assertTrue(strLayer.ImportFromString(layer.ExportToString()))

        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.ia').default,
            ia.default)
        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.fa').default,
            fa.default)
        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.sa').default,
            sa.default)
        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.aa').default,
            aa.default)
        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.p3fa').default,
            p3fa.default)
                         
        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.ia').QueryTimeSample(1.0),
            ia.QueryTimeSample(1.0))
        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.fa').QueryTimeSample(1.0),
            fa.QueryTimeSample(1.0))
        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.sa').QueryTimeSample(1.0),
            sa.QueryTimeSample(1.0))
        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.aa').QueryTimeSample(1.0),
            aa.QueryTimeSample(1.0))
        self.assertEqual(
            fileLayer.GetAttributeAtPath('/prim.p3fa').QueryTimeSample(1.0),
            p3fa.QueryTimeSample(1.0))

        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.ia').default,
            ia.default)
        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.fa').default,
            fa.default)
        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.sa').default,
            sa.default)
        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.aa').default,
            aa.default)
        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.p3fa').default,
            p3fa.default)
                         
        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.ia').QueryTimeSample(1.0),
            ia.QueryTimeSample(1.0))
        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.fa').QueryTimeSample(1.0),
            fa.QueryTimeSample(1.0))
        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.sa').QueryTimeSample(1.0),
            sa.QueryTimeSample(1.0))
        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.aa').QueryTimeSample(1.0),
            aa.QueryTimeSample(1.0))
        self.assertEqual(
            strLayer.GetAttributeAtPath('/prim.p3fa').QueryTimeSample(1.0),
            p3fa.QueryTimeSample(1.0))


class TestSdfAttributeUsda(TestSdfAttributeBase, unittest.TestCase):
    extension = 'usda'

class TestSdfAttributeUsdc(TestSdfAttributeBase, unittest.TestCase):
    extension = 'usdc'

if __name__ == '__main__':
    unittest.main()
