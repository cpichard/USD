#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

import os, sys, unittest
from pxr import Gf, Sdf, Usd, Plug

class TestUsdVariantFallback(unittest.TestCase):
    def test_Basic(self):
        # Register test plugin
        testenvDir = os.getcwd()
        Plug.Registry().RegisterPlugins(testenvDir)

        # We should pick up the plugInfo.json fallbacks now.
        self.assertEqual(Usd.Stage.GetGlobalVariantFallbacks()['displayColor'],
                         ['green'])

        def OpenLayer(name):
            fullName = '%s.usda' % name
            layerFile = os.path.abspath(fullName)
            self.assertTrue(layerFile, 'failed to find @%s@' % fullName)
            layer = Sdf.Layer.FindOrOpen(layerFile)
            self.assertTrue(layer, 'failed to open layer @%s@' % fullName)
            return layer

        # Open stage.
        layer = OpenLayer('testAPI_var')
        stage = Usd.Stage.Open(layer.identifier)
        self.assertTrue(stage, 
                        'failed to create stage for @%s@' % layer.identifier)
        sarah = stage.GetPrimAtPath('/Sarah')
        displayColor = sarah.GetVariantSet('displayColor')
        self.assertTrue(sarah, 'failed to find prim /Sarah')

        # Because our test has plugInfo.json that specifies a fallback of green,
        # we should see green.
        self.assertEqual(sarah.GetAttribute('color').Get(), Gf.Vec3d(0,1,0))
        self.assertEqual(displayColor.GetVariantSelection(), 'green')

        # Now override our process global variant policy and open a new stage.
        Usd.Stage.SetGlobalVariantFallbacks({'displayColor':['red']})
        self.assertEqual(Usd.Stage.GetGlobalVariantFallbacks(),
                         {'displayColor':['red']})
        stage = Usd.Stage.Open(layer.identifier)
        sarah = stage.GetPrimAtPath('/Sarah')
        displayColor = sarah.GetVariantSet('displayColor')

        # We should now get an attribute that resolves to red
        self.assertEqual(sarah.GetAttribute('color').Get(), Gf.Vec3d(1,0,0))
        self.assertEqual(displayColor.GetVariantSelection(), 'red')

        # Author a variant selection.
        displayColor .SetVariantSelection('blue')
        self.assertEqual(sarah.GetAttribute('color').Get(), Gf.Vec3d(0,0,1))
        self.assertEqual(displayColor.GetVariantSelection(), 'blue')

if __name__ == "__main__":
    unittest.main()
