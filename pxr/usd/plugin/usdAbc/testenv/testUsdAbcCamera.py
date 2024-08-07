#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

from pxr import Sdf, Usd, UsdGeom, UsdAbc, UsdUtils, Gf
import unittest

class TestUsdAlembicCamera(unittest.TestCase):
    def _CompareFrustumCorners(self, c1, c2):
        for a, b in zip(c1, c2):
            self.assertTrue(Gf.IsClose(a, b, 1e-5)) 

    def _RoundTripFileNames(self, baseName):
        """
        Filenames for testing a round trip usd->abc->usd.
        First file name uses FindDataFile to locate it within a test.
        """
        oldUsdFile = baseName + '.usd'
        abcFile  = baseName + '_1.abc'
        newUsdFile  = baseName + '_2.usd'

        return oldUsdFile, abcFile, newUsdFile
    
    def test_RoundTrip(self):
        baseName = 'testShotFlattened'
        camPath = '/World/main_cam'

        oldUsdFile, abcFile, newUsdFile = (self._RoundTripFileNames(baseName))

        # USD -> ABC
        self.assertTrue(UsdAbc._WriteAlembic(oldUsdFile, abcFile))

        # ABC -> USD
        layer = Sdf.Layer.FindOrOpen(abcFile)
        self.assertTrue(layer)
        self.assertTrue(layer.Export(newUsdFile))
        layer = None

        # Open old and new USD file on stages
        oldStage = Usd.Stage.Open(oldUsdFile)
        self.assertTrue(oldStage)
        newStage = Usd.Stage.Open(newUsdFile)
        self.assertTrue(newStage)

        # Get old and new camera
        oldCam = oldStage.GetPrimAtPath(camPath)
        self.assertTrue(oldCam)
        newCam = newStage.GetPrimAtPath(camPath)
        self.assertTrue(newCam)

        # Iterate through frames
        for frame in range(
            int(oldStage.GetStartTimeCode()),
            int(oldStage.GetEndTimeCode()) + 1):

            # Convert to Gf camera
            oldGfCam = UsdGeom.Camera(oldCam).GetCamera(frame)
            newGfCam = UsdGeom.Camera(newCam).GetCamera(frame)

            # Compare frustums
            self._CompareFrustumCorners(oldGfCam.frustum.ComputeCorners(), 
                                        newGfCam.frustum.ComputeCorners())

    def test_AbcToUsd(self):
        """
        Test conversion of an ABC camera to USD.
        """

        # Open ABC file on USD stage and get camera
        abcFile = 'testAbcCamera.abc'
        self.assertTrue(abcFile)
        stage = Usd.Stage.Open(abcFile)
        self.assertTrue(stage)
        cam = stage.GetPrimAtPath('/World/main_cam')
        self.assertTrue(cam)
        
        # Convert to Gf camera to check the frustum
        gfCam = UsdGeom.Camera(cam).GetCamera(1.0)
        expected  = (
             Gf.Vec3d(-4.27349785298,       -2341.70939914119,      -10.0),
             Gf.Vec3d(4.27349785298,        -2341.70939914119,      -10.0),
             Gf.Vec3d(-4.27349785298,       -2338.29060085880,      -10.0),
             Gf.Vec3d(4.27349785298,        -2338.29060085880,      -10.0),
             Gf.Vec3d(-42734.97852984663,   -19433.99141193865,     -100000.0),
             Gf.Vec3d(42734.97852984663,    -19433.99141193865,     -100000.0),
             Gf.Vec3d(-42734.97852984663,   14753.99141193864,      -100000.0),
             Gf.Vec3d(42734.97852984663,    14753.99141193864,      -100000.0))
        self._CompareFrustumCorners(gfCam.frustum.ComputeCorners(), expected)
                     
if __name__ == "__main__":
    unittest.main()
