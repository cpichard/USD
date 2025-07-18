#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

# pylint: disable=range-builtin-not-iterating

from __future__ import print_function
import os, shutil, sys, unittest
from pxr import Usd, Sdf, Tf

def _CompareFlattened(layerFile, primPath, timeRange):
    composed = Usd.Stage.Open(layerFile)
    flattened = composed.Flatten()
    flatComposed = Usd.Stage.Open(flattened)

    # attributes from non flattened scene
    prim = composed.GetPrimAtPath(primPath)
    
    # attributes from flattened scene
    flatPrim = flatComposed.GetPrimAtPath(primPath)

    for attr, flatAttr in zip(prim.GetAttributes(), flatPrim.GetAttributes()):
        for i in timeRange:
            assert attr.Get(i) == flatAttr.Get(i)

    return True

def _CompareMetadata(composed, flat, indent):
    # These fields will not match, so we explicitly exclude them during
    # comparison.
    # 
    # XXX: This list is not exhaustive, additional fields may need to be added
    # if the test inputs change.
    exclude = ("active", "inheritPaths", "payload", "references", "subLayers", 
               "subLayerOffsets", "variantSelection", "variantSetNames",
               "properties", "variantSetChildren", "primChildren", 
               "targetPaths")

    cdata = composed.GetAllMetadata()
    fdata = flat.GetAllMetadata()
    for cKey in cdata.keys():
        if cKey in exclude:
            continue

        print((" " * indent) + ":",cKey)
        assert cKey in fdata, str(composed.GetPath()) + " : " + cKey
        assert composed.GetMetadata(cKey) == flat.GetMetadata(cKey), "GetMetadata -- " + str(composed.GetPath()) + " : " + cKey
        assert cdata[cKey] == fdata[cKey], str(composed.GetPath()) + " : " + cKey

class TestUsdFlatten(unittest.TestCase):
    def _ComparePaths(self, pathOne, pathTwo):
        # Use normcase here to handle differences in path casing on
        # Windows. This will alo reverse slashes on windows, so we get 
        # a consistent cross platform comparison.
        self.assertEqual(os.path.normcase(pathOne), os.path.normcase(pathTwo))

    def _ComparePathLists(self, listOne, listTwo):
        self.assertEqual(len(listOne), len(listTwo))
        for i in range(len(listOne)):
            self._ComparePaths(listOne[i], listTwo[i])

    def test_Flatten(self):
        composed = Usd.Stage.Open("root.usd")
        flatLayer = composed.Flatten()
        flat = Usd.Stage.Open(flatLayer)

        assert composed.GetPrimAtPath("/Foo").GetAttribute("size").Get(3.0) == 1.0
        assert flat.GetPrimAtPath("/Foo").GetAttribute("size").Get(3.0) == 1.0

        for pc in composed.Traverse():
            print(pc.GetPath())

            # We elide deactivated prims, so skip the check.
            if not pc.IsActive():
                continue

            pf = flat.GetPrimAtPath(pc.GetPath())
            assert pf

            _CompareMetadata(pc, pf, 1)

            for attr in pf.GetAttributes():
                print("    Attr:" , attr.GetName())
                assert attr.IsDefined()

                attrc = pc.GetAttribute(attr.GetName())

                # Compare metadata.
                _CompareMetadata(attrc, attr, 10)

                # Compare time samples.
                ts_c = attrc.GetTimeSamples()
                ts_f = attr.GetTimeSamples()
                self.assertEqual(ts_f, ts_c)
                if len(ts_c):
                    print(((" "*12) + "["), end=' ')
                for t in ts_c:
                    self.assertEqual(attrc.Get(t), attr.Get(t))
                    print(("%.1f," % t), end=' ')
                if len(ts_c):
                    print("]")

                # Compare splines if the value source is spline, otherwise
                # verify the flattened one doesn't have a spline.
                if (attrc.GetResolveInfo().GetSource() ==
                    Usd.ResolveInfoSourceSpline):
                    self.assertEqual(attrc.GetSpline(), attr.GetSpline())
                    print(" "*12, 'spline =', attr.GetSpline())
                else:
                    self.assertFalse(attr.HasSpline())

                # Compare defaults.
                self.assertEqual(attrc.Get(), attr.Get())
                print(" "*12 + 'default =', attr.Get())

            for rel in pf.GetRelationships():
                print("    Rel:" , rel.GetName())
                assert rel and rel.IsDefined()
                _CompareMetadata(pc.GetRelationship(rel.GetName()), rel, 10)

    def test_NoFallbacks(self):
        strLayer = Usd.Stage.CreateInMemory().ExportToString()
        assert "endFrame" not in strLayer

    def test_Export(self):
        # Verify that exporting to a .usd file produces the default usd file format.
        composed = Usd.Stage.Open("root.usd")
        assert composed.Export('TestExport.usd')

        newFileName = 'TestExport.' + Tf.GetEnvSetting('USD_DEFAULT_FILE_FORMAT')
        shutil.copyfile('TestExport.usd', newFileName)
        assert Sdf.Layer.FindOrOpen(newFileName)

        # Verify that exporting to a .usd file but specifying ASCII
        # via file format arguments produces an ASCII file.
        assert composed.Export('TestExport_ascii.usd', args={'format':'usda'})
        
        shutil.copyfile('TestExport_ascii.usd', 'TestExport_ascii.usda')
        assert Sdf.Layer.FindOrOpen('TestExport_ascii.usda')

        # Verify that exporting to a .usd file but specifying crate
        # via file format arguments produces a usd crate file.
        assert composed.Export('TestExport_crate.usd', args={'format':'usdc'})
        
        shutil.copyfile('TestExport_crate.usd', 'TestExport_crate.usdc')
        assert Sdf.Layer.FindOrOpen('TestExport_crate.usdc')

    def test_FlattenClips(self): 
        # This test verifies the behavior of flattening clips in 
        # two cases.

        # 1. When the clip range is uniform in its properties and prims.
        #    Meaning that it has the same topology across the scene.

        # 2. When the clips have hole's, i.e. time samples where a property
        #    is not authored at all.

        # verify that flattening a valid clip range works
        assert _CompareFlattened("clips/root.usd", 
                                 "/World/fx/Particles_Splash/points",
                                 range(101, 105))

        assert _CompareFlattened("hole_clips/root.usd", 
                                "/World/fx/Particles_Splash/points",
                                range(101, 105))

        # verify that flattening with a sparse topology works
        assert _CompareFlattened("sparse_topology_clips/root.usda",
                                "/World/fx/Particles_Splash/points",
                                range(101, 105))

    def test_FlattenBadMetadata(self):
        # Shouldn't fail with unknown fields.
        s = Usd.Stage.Open("badMetadata.usd")
        assert s
        assert s.Flatten()

    def test_FlattenAttributeWithUnregisteredType(self):
        # We should still be able to open a valid stage
        stageFile = 'badPropertyTypeNames.usd'
        stage = Usd.Stage.Open(stageFile)
        assert stage
        
        prim = stage.GetPrimAtPath('/main')
        attr = prim.GetAttribute('myAttr')
        # Ensure we are actually trying to flatten an unknown type
        assert attr.GetTypeName().type == Tf.Type.Unknown

        # Ensure flatten completes
        flattened = stage.Flatten()
        assert flattened

        # Ensure the property has been omitted in the flattened result
        assert not flattened.GetPropertyAtPath('/main.myAttr')
        # Ensure the valid properties made it through
        assert flattened.GetPropertyAtPath('/main.validAttr1')
        assert flattened.GetPropertyAtPath('/main.validAttr2')

    def test_FlattenRelationshipTargets(self):
        basePath = 'relationshipTargets/'
        stageFile = basePath+'source.usda'

        stage = Usd.Stage.Open(stageFile)
        assert stage
        prim = stage.GetPrimAtPath('/bar')
        assert prim
        rel  = prim.GetRelationship('foo')
        assert rel
        self.assertEqual(rel.GetTargets(), [Sdf.Path('/bar/baz')])

        resultFile = basePath+'result.usda'
        stage.Export(resultFile)

        resultStage = Usd.Stage.Open(resultFile)
        assert resultStage

        prim = resultStage.GetPrimAtPath('/bar')
        assert prim
        rel  = prim.GetRelationship('foo')
        assert rel
        self.assertEqual(rel.GetTargets(), [Sdf.Path('/bar/baz')])

    def test_FlattenConnections(self):
        basePath = 'connections/'
        stageFile = basePath+'source.usda'

        stage = Usd.Stage.Open(stageFile)
        assert stage
        barPrim = stage.GetPrimAtPath('/bar')
        assert barPrim
        
        fooAttr  = barPrim.GetAttribute('foo')
        assert fooAttr
        self.assertEqual(fooAttr.GetConnections(), [Sdf.Path('/bar/baz.foo')])

        bazPrim = stage.GetPrimAtPath("/bar/baz")
        assert bazPrim
        basAttr  = bazPrim.GetAttribute('bas')
        assert basAttr
        self.assertEqual(basAttr.GetConnections(), [Sdf.Path('/bar.bas')])

        resultFile = basePath+'result.usda'
        stage.Export(resultFile)

        resultStage = Usd.Stage.Open(resultFile)
        assert resultStage

        barPrim = resultStage.GetPrimAtPath('/bar')
        assert barPrim
        fooAttr = barPrim.GetAttribute('foo')
        assert fooAttr
        self.assertEqual(fooAttr.GetConnections(), [Sdf.Path('/bar/baz.foo')])

        bazPrim = resultStage.GetPrimAtPath("/bar/baz")
        assert bazPrim
        basAttr  = bazPrim.GetAttribute('bas')
        assert basAttr
        self.assertEqual(basAttr.GetConnections(), [Sdf.Path('/bar.bas')])

    def test_FlattenTimeSamplesAndDefaults(self):
        testFile = "time_samples/root.usda"
        resultFile = "time_samples/result.usda"

        stage = Usd.Stage.Open(testFile)
        resultLayer = stage.Flatten()

        # The flattened result for /StrongerDefault should only
        # have a default value, since a default value in a stronger
        # layer will override all time samples in a weaker layer.
        resultAttrSpec = \
            resultLayer.GetAttributeAtPath("/StrongerDefault.attr")
        self.assertEqual(resultAttrSpec.default, 1.0)
        self.assertEqual(
            resultLayer.ListTimeSamplesForPath(resultAttrSpec.path), [])

        # The flattened result for /StrongerTimeSamples should have
        # both time samples and a default value even though the default
        # is set in a weaker sublayer, since value resolution uses that
        # when requesting the value at the default time.
        resultAttrSpec = \
            resultLayer.GetAttributeAtPath("/StrongerTimeSamples.attr")
        self.assertEqual(resultAttrSpec.default, 1.0)
        self.assertEqual(
            resultLayer.ListTimeSamplesForPath(resultAttrSpec.path), [0.0, 1.0])
        self.assertEqual(
            resultLayer.QueryTimeSample(resultAttrSpec.path, 0.0), 100.0)
        self.assertEqual(
            resultLayer.QueryTimeSample(resultAttrSpec.path, 1.0), 101.0)

    def test_FlattenAssetPaths(self):
        testFile = "assetPaths/root.usda"

        stage = Usd.Stage.Open(testFile)
        resultLayer = stage.Flatten()

        # All asset paths in the flattened result should be anchored,
        # even though the asset being referred to does not exist.
        attr = resultLayer.GetAttributeAtPath("/AssetPathTest.assetPath")
        
        timeSamples = attr.GetInfo("timeSamples")
        self._ComparePaths(os.path.normpath(timeSamples[0].path),
                          os.path.abspath("assetPaths/asset.usda"))
        self._ComparePaths(os.path.normpath(timeSamples[1].path),
                          os.path.abspath("assetPaths/asset.usda"))
        
        self._ComparePaths(
            os.path.normpath(attr.GetInfo("default").path), 
            os.path.abspath("assetPaths/asset.usda"))
        
        attr = resultLayer.GetAttributeAtPath("/AssetPathTest.assetPathArray")
        self._ComparePathLists(
            list([os.path.normpath(p.path) for p in attr.GetInfo("default")]),
            [os.path.abspath("assetPaths/asset.usda")])

        prim = resultLayer.GetPrimAtPath("/AssetPathTest")
        metadataDict = prim.GetInfo("customData")
        self._ComparePaths(
            os.path.normpath(metadataDict["assetPath"].path),
            os.path.abspath("assetPaths/asset.usda"))
        self._ComparePathLists(
            list([os.path.normpath(p.path) 
                  for p in metadataDict["assetPathArray"]]),
            [os.path.abspath("assetPaths/asset.usda")])
            
        metadataDict = metadataDict["subDict"]
        self._ComparePaths(
            os.path.normpath(metadataDict["assetPath"].path),
            os.path.abspath("assetPaths/asset.usda"))
        self._ComparePathLists(
            list([os.path.normpath(p.path) 
                  for p in metadataDict["assetPathArray"]]),
            [os.path.abspath("assetPaths/asset.usda")])

    def test_FlattenStageMetadata(self):
        testFile = "stage_metadata/root.usda"
        sublayerFile = "stage_metadata/sub.usda"
        resultFile = "stage_metadata/result.usda"

        stage = Usd.Stage.Open(testFile)

        # Sanity check that the stage we opened has a sublayer with layer
        # metadata that we don't expect to show up in the flattened layer and 
        # that it has a TCPS of 48
        sublayer = Sdf.Layer.Find(sublayerFile)
        self.assertTrue(sublayer)
        self.assertIn(sublayer, stage.GetUsedLayers())
        self.assertEqual(sorted(sublayer.GetPrimAtPath('/').ListInfoKeys()),
            ['defaultPrim', 'endTimeCode', 'startTimeCode', 'timeCodesPerSecond'])
        self.assertEqual(sublayer.timeCodesPerSecond, 48)

        # Flatten the layer.
        resultLayer = stage.Flatten()

        # Verify that the flattened layer only contains layer metadata from
        # the root layer (and documentation written by the flatten operation
        # itself)
        resultPseudoRoot = resultLayer.GetPrimAtPath('/')
        print(str(resultPseudoRoot.ListInfoKeys()))
        self.assertEqual(sorted(resultPseudoRoot.ListInfoKeys()),
                         ['documentation', 'endTimeCode', 'startTimeCode'])
        self.assertEqual(resultPseudoRoot.GetInfo('startTimeCode'), 0)
        self.assertEqual(resultPseudoRoot.GetInfo('endTimeCode'), 24)

        # In particular verify that the timeCodesPerSecond from the sublayer
        # was not transferred over to the flattened layer.
        self.assertFalse(resultPseudoRoot.HasInfo('timeCodesPerSecond'))
        self.assertEqual(resultLayer.timeCodesPerSecond, 24)

        # Verify the time samples from the root layer were transferred to 
        # the flattened layer as is, no time mapping.
        resultRootAttrSpec = \
            resultLayer.GetAttributeAtPath("/TimeSamples.rootAttr")
        self.assertEqual(
            resultLayer.ListTimeSamplesForPath(resultRootAttrSpec.path), [0.0, 24.0])
        self.assertEqual(
            resultLayer.QueryTimeSample(resultRootAttrSpec.path, 0.0), 100.0)
        self.assertEqual(
            resultLayer.QueryTimeSample(resultRootAttrSpec.path, 24.0), 101.0)

        # Verify the time samples from the sublayer were transferred to the 
        # flattened layer and were mapped from the sublayer's 48 TCPS to the
        # root's 24 TCPS
        resultSubAttrSpec = \
            resultLayer.GetAttributeAtPath("/TimeSamples.subAttr")
        self.assertEqual(
            resultLayer.ListTimeSamplesForPath(resultSubAttrSpec.path), [12.0, 24.0])
        self.assertEqual(
            resultLayer.QueryTimeSample(resultSubAttrSpec.path, 12.0), 200.0)
        self.assertEqual(
            resultLayer.QueryTimeSample(resultSubAttrSpec.path, 24.0), 201.0)
        
    def test_FlattenPathsWithMissungUriResolvers(self):
        """Tests that when flattening, asset paths that contain URI schemes
        for which there is no registered resolver are left unmodified
        """

        rootLayer = Sdf.Layer.CreateAnonymous(".usda")
        rootLayer.ImportFromString("""
        #usda 1.0

        def "TestPrim"(
            assetInfo = {
                asset identifier = @test123://1.2.3.4/file3.txt@
                asset[] assetRefArr = [@test123://1.2.3.4/file6.txt@]
            }
        )
        {
            asset uriAssetRef = @test123://1.2.3.4/file1.txt@
            asset[] uriAssetRefArray = [@test123://1.2.3.4/file2.txt@]

            asset uriAssetRef.timeSamples = {
                0: @test123://1.2.3.4/file4.txt@,
                1: @test123://1.2.3.4/file5.txt@,
            }
                                   
            asset[] uriAssetRefArray.timeSamples = {
                0: [@test123://1.2.3.4/file6.txt@],
                1: [@test123://1.2.3.4/file7.txt@],               
            }
        }
        """.strip())
        
        stage = Usd.Stage.Open(rootLayer)
        flatStage = Usd.Stage.Open(stage.Flatten())

        propPath = "/TestPrim.uriAssetRef"
        stageProp = stage.GetPropertyAtPath(propPath)
        flatStageProp = flatStage.GetPropertyAtPath(propPath)
        self.assertEqual(stageProp.Get(), flatStageProp.Get())
        
        self.assertEqual(stageProp.GetTimeSamples(), flatStageProp.GetTimeSamples())
        for timeSample in stageProp.GetTimeSamples():
            self.assertEqual(stageProp.Get(timeSample), flatStageProp.Get(timeSample))

        arrayPath = "/TestPrim.uriAssetRefArray"
        arrayProp = stage.GetPropertyAtPath(arrayPath)
        flatArrayProp = flatStage.GetPropertyAtPath(arrayPath)
        self.assertEqual(arrayProp.Get(), flatArrayProp.Get())
            
        self.assertEqual(arrayProp.GetTimeSamples(), flatArrayProp.GetTimeSamples())
    
        for timeSample in arrayProp.GetTimeSamples():
            self.assertEqual(arrayProp.Get(timeSample), flatArrayProp.Get(timeSample))

        primPath = "/TestPrim"
        self.assertEqual(
            stage.GetPrimAtPath(primPath).GetMetadata("assetInfo").get("identifier"), 
            flatStage.GetPrimAtPath(primPath).GetMetadata("assetInfo").get("identifier"))
        
        self.assertEqual(
            stage.GetPrimAtPath(primPath).GetMetadata("assetInfo").get("assetRefArr"), 
            flatStage.GetPrimAtPath(primPath).GetMetadata("assetInfo").get("assetRefArr"))

if __name__ == "__main__":
    unittest.main()
