#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
from pxr import Sdf, Usd, Vt, Gf, Tf, UsdAbc
import sys, os, tempfile, unittest

class TestUsdAbcAlembicData(unittest.TestCase):
    def test_Basic(self):
        stage = Usd.Stage.Open('testasset.abc')
        self.assertTrue(stage)
        prim = stage.GetPrimAtPath('/World/fx/Particles_Splash')
        self.assertTrue(prim)
        attr = prim.GetAttribute('tester_i')
        self.assertTrue(attr)
        self.assertEqual(attr.Get(Usd.TimeCode.EarliestTime()), 15)

        # Confirm that fps data from alembic files is stored as
        # usd timeCodesPerSeconds
        stage = Usd.Stage.Open('testusdtimecode.abc')
        self.assertTrue(stage)
        self.assertTrue(stage.GetRootLayer().HasTimeCodesPerSecond())
        self.assertEqual(stage.GetTimeCodesPerSecond(), 30)

    def test_Write(self):
        # Create a usda and abc temporary files.
        # NOTE: This files will automatically be deleted when the test quits,
        # if you want to keep them around for debugging, pass in delete=False
        with tempfile.NamedTemporaryFile(suffix='.abc') as tempAbcFile, \
             tempfile.NamedTemporaryFile(suffix='.usda') as tempUsdFile:
            tempAbcFile.close()
            tempUsdFile.close()

            # Create a USD file that we'll save out as .abc
            stage = Usd.Stage.CreateNew(tempUsdFile.name)
            stage.OverridePrim('/Prim')
            stage.GetRootLayer().Save()

            # Write out the USD file as .abc
            UsdAbc._WriteAlembic(tempUsdFile.name, tempAbcFile.name)

            # Read it back in and expect to read back a prim.
            roundTrippedStage = Usd.Stage.Open(tempAbcFile.name)
            prim = roundTrippedStage.GetPrimAtPath('/Prim')
            self.assertTrue(prim)

            # Verify that timeCodesPerSecond and framesPerSecond values are preserved 
            # when round-tripping.
            self.assertTrue(roundTrippedStage.GetRootLayer().HasTimeCodesPerSecond())
            self.assertEqual(roundTrippedStage.GetTimeCodesPerSecond(), 
                             stage.GetTimeCodesPerSecond())

            self.assertTrue(roundTrippedStage.GetRootLayer().HasFramesPerSecond())
            self.assertEqual(roundTrippedStage.GetFramesPerSecond(), 
                             stage.GetTimeCodesPerSecond())

            # NOTE: tempAbcFile and tempUsdFile will want to delete the
            #       underlying filse on __exit__ from the context manager.
            #       But roundTrippedStage and stage may have the file open.
            #       If so the deletion will  fail on Windows.  Explicitly
            #       release our references to the stages to close the file.
            del stage
            del roundTrippedStage

    def test_Types(self):
        # Create a usda and abc temporary files.
        # NOTE: This files will automatically be deleted when the test quits,
        # if you want to keep them around for debugging, pass in delete=False
        with tempfile.NamedTemporaryFile(suffix='.abc') as tempAbcFile, \
             tempfile.NamedTemporaryFile(suffix='.usda') as tempUsdFile:
            tempAbcFile.close()
            tempUsdFile.close()

            stage = Usd.Stage.CreateNew(tempUsdFile.name)
            prim = stage.OverridePrim('/Prim')

            # Note these test cases come from testSdTypes.py
            usdValuesToTest = [
                ("hello", 'string', 'myString'),
                (True,'bool', 'myBool'),
                (1,'uchar', 'myUChar'),
                (1,'int', 'myInt'),
                (1,'uint', 'myUInt'),
                (1,'int64', 'myInt64'), 
                (1,'uint64', 'myUInt64'), 
                (1.0,'half', 'myHalf'), 
                (1.0,'float', 'myFloat'), 
                (1.0,'double', 'myDouble'), 
                (Gf.Vec2d(1,2),'double2', 'myVec2d'),
                (Gf.Vec2f(1,2),'float2', 'myVec2f'), 
                (Gf.Vec2h(1,2),'half2', 'myVec2h'), 
                (Gf.Vec2i(1,2),'int2', 'myVec2i'), 
                (Gf.Vec3d(1,2,3),'double3', 'myVec3d'), 
                (Gf.Vec3f(1,2,3),'float3', 'myVec3f'), 
                (Gf.Vec3h(1,2,3),'half3', 'myVec3h'), 
                (Gf.Vec3i(1,2,3),'int3', 'myVec3i'), 
                (Gf.Vec4d(1,2,3,4),'double4', 'myVec4d'), 
                (Gf.Vec4f(1,2,3,4),'float4', 'myVec4f'), 
                (Gf.Vec4h(1,2,3,4),'half4', 'myVec4h'), 
                (Gf.Vec4i(1,2,3,4),'int4', 'myVec4i'), 
                (Gf.Matrix4d(3),'matrix4d', 'myMatrix4d'), 
                (Gf.Quatf(1.0, [2.0, 3.0, 4.0]), 'quatf', 'myQuatf'),
                (Gf.Quatd(1.0, [2.0, 3.0, 4.0]), 'quatd', 'myQuatd'),
                (Vt.StringArray(),'string[]', 'myStringArray'), 
                (Vt.Vec2dArray(),'double2[]', 'myVec2dArray'), 
                (Vt.Vec2fArray(),'float2[]', 'myVec2fArray'), 
                (Vt.Vec2hArray(),'half2[]', 'myVec2hArray'), 
                (Vt.Vec2iArray(),'int2[]', 'myVec2iArray'), 
            ]

            for value, typeName, attrName in usdValuesToTest:
                prim.CreateAttribute(attrName, Sdf.ValueTypeNames.Find(typeName))
                prim.GetAttribute(attrName).Set(value)

            stage.GetRootLayer().Save()

            # Write out the USD file as .abc
            UsdAbc._WriteAlembic(tempUsdFile.name, tempAbcFile.name)

            # Read it back in and expect the same attributes and values
            stage = Usd.Stage.Open(tempAbcFile.name)
            prim = stage.GetPrimAtPath('/Prim')
            self.assertTrue(prim)

            for value, typeName, attrName in usdValuesToTest:
                attr = prim.GetAttribute(attrName)
                self.assertTrue(attr)
                self.assertEqual(attr.GetTypeName(), typeName)
                self.assertEqual(attr.Get(), value)

            # NOTE: tempAbcFile will want to delete the underlying file
            #       on __exit__ from the context manager.  But stage
            #       may have the file open.  If so the deletion will
            #       fail on Windows.  Explicitly release our reference
            #       to the stage to close the file.
            del stage

    def test_Capabilities(self):
        self.assertTrue(Sdf.FileFormat.FormatSupportsReading('.abc'))
        self.assertTrue(Sdf.FileFormat.FormatSupportsWriting('.abc'))
        self.assertFalse(Sdf.FileFormat.FormatSupportsEditing('.abc'))

if __name__ == "__main__":
    unittest.main()
