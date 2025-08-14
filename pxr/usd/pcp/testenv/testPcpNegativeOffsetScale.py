#!/pxrpythonsubst
#
# Copyright 2025 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at

import unittest

from pxr import Pcp, Sdf, Tf

def _ComposeLayersWithNegativeOffsetScale():
    refLayer = Sdf.Layer.CreateAnonymous("ref")
    refLayer.ImportFromString('''
    #usda 1.0

    def "prim" {
        double attr.timeSamples = {
            0: 1.0,
            1: 2.0,
        }
    }
    '''.strip())

    subLayer = Sdf.Layer.CreateAnonymous("sub")
    subLayer.ImportFromString(f'''
    #usda 1.0
    (
        subLayers = [
            @{refLayer.identifier}@ (scale = -1)
        ]
    )
    '''.strip())

    rootLayer = Sdf.Layer.CreateAnonymous()
    rootLayer.ImportFromString(f'''
    #usda 1.0

    def "prim" (
        references = @{subLayer.identifier}@</prim> (scale = -1)
    )
    {{
    }}
    '''.strip())

    pcpCache = Pcp.Cache(Pcp.LayerStackIdentifier(rootLayer))
    _, errs = pcpCache.ComputePrimIndex("/prim")
    return errs

class TestPcpNegativeLayerOffsetScale(unittest.TestCase):

    # Following will result in a composition error
    def test_NegativeLayerOffsetScaleNotAllowed(self):
        errs = _ComposeLayersWithNegativeOffsetScale()
        self.assertEqual(len(errs), 2)

if __name__ == "__main__":
    unittest.main()
