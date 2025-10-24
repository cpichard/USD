#!/pxrpythonsubst
#
# Copyright 2023 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

import unittest

from pxr import Pcp, Sdf, Tf

INCREMENTAL_CHANGES = Tf.GetEnvSetting(
    'PCP_ENABLE_MINIMAL_CHANGES_FOR_LAYER_OPERATIONS')

def LoadPcpCache(rootLayer, sessionLayer = None):
    l = Sdf.Layer.FindOrOpen(rootLayer)
    sl = None
    if sessionLayer is not None:
        sl = Sdf.Layer.FindOrOpen(sessionLayer)

    return Pcp.Cache(Pcp.LayerStackIdentifier(l, sl))

class TestPcpExpressionComposition(unittest.TestCase):
    def AssertVariables(self, pcpCache, path, expected, errorsExpected = False):
        '''Helper function for verifying the expected value of 
        expression variables in the layer stacks throughout the prim index
        for the prim at the given path. See Pcp._TestPrimIndex for more info.

        If "errorsExpected" is False, then this function will return false if
        any composition errors are generated when computing the prim index.
        '''
        pi, err = pcpCache.ComputePrimIndex(path)
        if errorsExpected:
            self.assertTrue(err, "Composition errors expected")
        else:
            self.assertFalse(err, "Unexpected composition errors: {}".format(
                ",".join(str(e) for e in err)))

        for node, entry in Pcp._TestPrimIndex(pi, expected):
            expectedRootLayerId, expectedVariables = entry

            self.assertEqual(
                node.layerStack.identifier.rootLayer,
                Sdf.Layer.Find(expectedRootLayerId))

            self.assertEqual(
                node.layerStack.expressionVariables.GetVariables(),
                expectedVariables,
                "Unexpected expression variables for layer stack {}"
                .format(node.layerStack.identifier))

        return pi

    def test_BasicSublayers(self):
        pcpCache = LoadPcpCache('sublayers/root.usda')
        rootLayer = pcpCache.GetLayerStackIdentifier().rootLayer

        aLayer = Sdf.Layer.FindOrOpen('sublayers/A.usda')
        bLayer = Sdf.Layer.FindOrOpen('sublayers/B.usda')
        bSubLayer = Sdf.Layer.FindOrOpen('sublayers/B_sub.usda')
        cLayer = Sdf.Layer.FindOrOpen('sublayers/C.usda')

        # Verify initial state.
        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test'),
             aLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(layerStack.localErrors, [])
        self.assertEqual(
            layerStack.layers, 
            [rootLayer, 
             aLayer])

        # Modify the expression variable to X=B. This should drop A.usda and load
        # B.usda as a sublayer. Since B.usda is not empty, this should incur a
        # significant resync.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'X':'B'}
            self.assertEqual(changes.GetSignificantChanges(), ['/'])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test'),
             bLayer.GetPrimAtPath('/Test'),
             bSubLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(layerStack.localErrors, [])
        self.assertEqual(
            layerStack.layers, 
            [rootLayer, 
             bLayer,
             bSubLayer])

        # Modify the expression variable to X=C. This drops B.usda and loads
        # C.usda as a sublayer. This incurs a significant resync even though
        # C.usda is empty -- see comment in 
        # PcpChanges::_DidChangeLayerStackExpressionVariables
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'X':'C'}
            self.assertEqual(changes.GetSignificantChanges(), ['/'])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(len(layerStack.localErrors), 0)
        self.assertEqual(
            layerStack.layers, 
            [rootLayer,
             cLayer])

        # Modify the expression variable to X=BAD. This should drop C.usda and
        # attempt to load BAD.usda as a sublayer, but fail to do so since
        # BAD.usda does not exist.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'X':'BAD'}
            self.assertEqual(changes.GetSignificantChanges(), ['/'])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(len(layerStack.localErrors), 1)
        self.assertEqual(
            layerStack.layers, 
            [rootLayer])

        # Modify the expression variable to X=A. This should resolve the error
        # and load A.usda again.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'X':'A'}
            self.assertEqual(changes.GetSignificantChanges(), ['/'])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test'),
             aLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(layerStack.localErrors, [])
        self.assertEqual(
            layerStack.layers, 
            [rootLayer, 
             aLayer])

        # Author a new expression variable Y=B. This should cause no changes
        # since nothing relies on that variable.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'X':'A', 'Y':'B'}
            self.assertEqual(changes.GetSignificantChanges(), [])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test'),
             aLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(layerStack.localErrors, [])
        self.assertEqual(
            layerStack.layers, 
            [rootLayer, 
             aLayer])

    def test_SublayerAuthoring(self):
        pcpCache = LoadPcpCache('sublayers/root.usda')
        rootLayer = pcpCache.GetLayerStackIdentifier().rootLayer

        aLayer = Sdf.Layer.FindOrOpen('sublayers/A.usda')
        bLayer = Sdf.Layer.FindOrOpen('sublayers/B.usda')
        bSubLayer = Sdf.Layer.FindOrOpen('sublayers/B_sub.usda')
        cLayer = Sdf.Layer.FindOrOpen('sublayers/C.usda')

        # Verify initial state.
        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test'),
             aLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(layerStack.localErrors, [])
        self.assertEqual(
            layerStack.layers, 
            [rootLayer, 
             aLayer])

        # Add a new sublayer using an expression that evaluates to B.usda.
        # Since B.usda is not empty, this should incur a significant resync.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.subLayerPaths.append('`"./B.usda"`')

            if INCREMENTAL_CHANGES:
                self.assertEqual(changes.GetSignificantChanges(), ['/Test'])
            else:
                self.assertEqual(changes.GetSignificantChanges(), ['/'])

            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test'),
             aLayer.GetPrimAtPath('/Test'),
             bLayer.GetPrimAtPath('/Test'),
             bSubLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(layerStack.localErrors, [])
        self.assertEqual(
            layerStack.layers, 
            [rootLayer, 
             aLayer,
             bLayer,
             bSubLayer])

        # Remove the sublayer we just added to reverse the changes.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            del rootLayer.subLayerPaths[-1]

            if INCREMENTAL_CHANGES:
                self.assertEqual(changes.GetSignificantChanges(), ['/Test'])
            else:
                self.assertEqual(changes.GetSignificantChanges(), ['/'])

            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test'),
             aLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(layerStack.localErrors, [])
        self.assertEqual(
            layerStack.layers, 
            [rootLayer, 
             aLayer])

        # Add a new sublayer using an expression that evaluates to C.usda.
        # Since C.usda is empty, this should not incur any changes to /Test.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.subLayerPaths.append('`"./C.usda"`')
            self.assertEqual(changes.GetSignificantChanges(), [])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test'),
             aLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(layerStack.localErrors, [])
        self.assertEqual(
            layerStack.layers, 
            [rootLayer, 
             aLayer,
             cLayer])

        # Remove the sublayer we just added to reverse the changes.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            del rootLayer.subLayerPaths[-1]
            self.assertEqual(changes.GetSignificantChanges(), [])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test'),
             aLayer.GetPrimAtPath('/Test')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(layerStack.localErrors, [])
        self.assertEqual(
            layerStack.layers, 
            [rootLayer, 
             aLayer])

    def test_SublayerAuthoringAndVariableChange(self):
        pcpCache = LoadPcpCache('multi_sublayer_auth/base_ref.usda')

        rootLayer = pcpCache.GetLayerStackIdentifier().rootLayer
        aLayer = Sdf.Layer.FindOrOpen('multi_sublayer_auth/A.usda')
        bLayer = Sdf.Layer.FindOrOpen('multi_sublayer_auth/B.usda')

        # Verify initial state.
        pi, err = pcpCache.ComputePrimIndex('/BaseRef')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/BaseRef')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(
            layerStack.layers, 
            [rootLayer])

        # Add a new sublayer expression and the variable it depends on
        # in the same change block. The newly-authored variable should
        # be used when evaluating the sublayer, which should be loaded
        # successfully and cause the appropriate resyncs.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            with Sdf.ChangeBlock():
                rootLayer.expressionVariables = {'X':'B'}
                rootLayer.subLayerPaths.append('`"./${X}.usda"`')

            if INCREMENTAL_CHANGES:
                self.assertEqual(changes.GetSignificantChanges(), ['/BaseRef'])
            else:
                self.assertEqual(changes.GetSignificantChanges(), ['/'])

            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/BaseRef')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/BaseRef'),
             bLayer.GetPrimAtPath('/BaseRef')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(
            layerStack.layers, 
            [rootLayer,
             bLayer])

        # Undo the changes in the same change block to reverse the
        # effects.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            with Sdf.ChangeBlock():
                rootLayer.ClearExpressionVariables()
                del rootLayer.subLayerPaths[-1]

            self.assertEqual(changes.GetSignificantChanges(), ['/'])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/BaseRef')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/BaseRef')])

        layerStack = pi.rootNode.layerStack
        self.assertEqual(
            layerStack.layers, 
            [rootLayer])

    def test_SublayerAuthoringWithNoEffectiveChange(self):
        """Tests behavior when updating a sublayer variable expression
        without actually changing the sublayers in the layer stack."""

        def _test(usd):
            subLayer = Sdf.Layer.CreateAnonymous("sub")
            subLayer.ImportFromString('''#usda 1.0

            def "Def"
            {
            }
        
            over "Over"
            {
            }
            ''')

            rootLayer = Sdf.Layer.CreateAnonymous()
            rootLayer.ImportFromString(f"""#usda 1.0
            (
                subLayers = [
                    @`if(contains(["foo", "bar"], ${{VAR}}), "{subLayer.identifier}")`@
                ]
                expressionVariables = {{
                    string "VAR" = "foo"
                }}
            )

            def "Def"
            {{
            }}

            over "Over"
            {{
            }}
            """)

            layerStackId = Pcp.LayerStackIdentifier(rootLayer)
            pcp = Pcp.Cache(layerStackId, usd=usd)

            pi, err = pcp.ComputePrimIndex('/Def')
            self.assertEqual(err, [])
            self.assertTrue(pi.rootNode.hasSpecs)
            self.assertEqual(
                pi.primStack,
                [rootLayer.GetPrimAtPath('/Def'), 
                 subLayer.GetPrimAtPath('/Def')])

            pi, err = pcp.ComputePrimIndex('/Over')
            self.assertEqual(err, [])
            self.assertTrue(pi.rootNode.hasSpecs)
            self.assertEqual(
                pi.primStack,
                [rootLayer.GetPrimAtPath('/Over'),
                 subLayer.GetPrimAtPath('/Over')])

            # Change the condition in the sublayer expression such that it
            # still passes and the sublayer remains loaded.
            # 
            # XXX: In the future Pcp could detect this and not issue any
            # invalidations, in which case this test would need to be updated.
            with Pcp._TestChangeProcessor(pcp) as cp:
                rootLayer.subLayerPaths[0] = \
                    f'`if(contains(["foo"], ${{VAR}}), "{subLayer.identifier}")`'

                if INCREMENTAL_CHANGES:
                    self.assertEqual(cp.GetSignificantChanges(), ['/Def'])
                    self.assertEqual(cp.GetSpecChanges(), ['/Over'])
                else:
                    self.assertEqual(cp.GetSignificantChanges(), ['/'])
                    self.assertEqual(cp.GetSpecChanges(), [])

            pi, err = pcp.ComputePrimIndex('/Def')
            self.assertEqual(err, [])
            self.assertTrue(pi.rootNode.hasSpecs)
            self.assertEqual(
                pi.primStack,
                [rootLayer.GetPrimAtPath('/Def'), 
                 subLayer.GetPrimAtPath('/Def')])

            pi, err = pcp.ComputePrimIndex('/Over')
            self.assertEqual(err, [])
            self.assertTrue(pi.rootNode.hasSpecs)
            self.assertEqual(
                pi.primStack,
                [rootLayer.GetPrimAtPath('/Over'),
                 subLayer.GetPrimAtPath('/Over')])

        _test(usd=False)
        _test(usd=True)

    def test_SublayerAuthoringAffectingMultipleLayerStacks(self):
        pcpCache = LoadPcpCache('multi_sublayer_auth/root.usda')

        rootLayer = pcpCache.GetLayerStackIdentifier().rootLayer
        ref1Layer = Sdf.Layer.FindOrOpen('multi_sublayer_auth/ref1.usda')
        ref2Layer = Sdf.Layer.FindOrOpen('multi_sublayer_auth/ref2.usda')
        baseRefLayer = Sdf.Layer.FindOrOpen('multi_sublayer_auth/base_ref.usda')

        aLayer = Sdf.Layer.FindOrOpen('multi_sublayer_auth/A.usda')
        bLayer = Sdf.Layer.FindOrOpen('multi_sublayer_auth/B.usda')

        # Verify initial state.
        pi, err = pcpCache.ComputePrimIndex('/Test_1')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test_1'),
             ref1Layer.GetPrimAtPath('/Ref_1'),
             baseRefLayer.GetPrimAtPath('/BaseRef')])

        baseRefLayerStack = pi.rootNode.children[0].children[0].layerStack
        self.assertEqual(baseRefLayerStack.identifier.rootLayer, baseRefLayer)
        self.assertEqual(
            baseRefLayerStack.layers, 
            [baseRefLayer])

        pi, err = pcpCache.ComputePrimIndex('/Test_2')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test_2'),
             ref2Layer.GetPrimAtPath('/Ref_2'),
             baseRefLayer.GetPrimAtPath('/BaseRef')])

        baseRefLayerStack = pi.rootNode.children[0].children[0].layerStack
        self.assertEqual(baseRefLayerStack.identifier.rootLayer, baseRefLayer)
        self.assertEqual(
            baseRefLayerStack.layers, 
            [baseRefLayer])

        # Add a sublayer that refers to the expression variable X in
        # base_ref.usda. This variable is assigned different values in the
        # various referenced layer stacks:
        #
        #   - In ref1.usda, X=A, which should load insignificant sublayer
        #     A.usda, causing no changes for /Test_1.
        #
        #   - In ref2.usda, X=B, which should load significant sublayer
        #     B.usda, causing a significant resync for /Test_2.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            baseRefLayer.subLayerPaths.append('`"./${X}.usda"`')
            self.assertEqual(changes.GetSignificantChanges(), ['/Test_2'])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test_1')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test_1'),
             ref1Layer.GetPrimAtPath('/Ref_1'),
             baseRefLayer.GetPrimAtPath('/BaseRef')])

        baseRefLayerStack = pi.rootNode.children[0].children[0].layerStack
        self.assertEqual(baseRefLayerStack.identifier.rootLayer, baseRefLayer)
        self.assertEqual(
            baseRefLayerStack.layers, 
            [baseRefLayer,
             aLayer])

        pi, err = pcpCache.ComputePrimIndex('/Test_2')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test_2'),
             ref2Layer.GetPrimAtPath('/Ref_2'),
             baseRefLayer.GetPrimAtPath('/BaseRef'),
             bLayer.GetPrimAtPath('/BaseRef')])

        baseRefLayerStack = pi.rootNode.children[0].children[0].layerStack
        self.assertEqual(baseRefLayerStack.identifier.rootLayer, baseRefLayer)
        self.assertEqual(
            baseRefLayerStack.layers, 
            [baseRefLayer,
             bLayer])

        # Remove the sublayer to reverse the changes.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            del baseRefLayer.subLayerPaths[-1]
            self.assertEqual(changes.GetSignificantChanges(), ['/Test_2'])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Test_1')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test_1'),
             ref1Layer.GetPrimAtPath('/Ref_1'),
             baseRefLayer.GetPrimAtPath('/BaseRef')])

        baseRefLayerStack = pi.rootNode.children[0].children[0].layerStack
        self.assertEqual(baseRefLayerStack.identifier.rootLayer, baseRefLayer)
        self.assertEqual(
            baseRefLayerStack.layers, 
            [baseRefLayer])

        pi, err = pcpCache.ComputePrimIndex('/Test_2')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Test_2'),
             ref2Layer.GetPrimAtPath('/Ref_2'),
             baseRefLayer.GetPrimAtPath('/BaseRef')])

        baseRefLayerStack = pi.rootNode.children[0].children[0].layerStack
        self.assertEqual(baseRefLayerStack.identifier.rootLayer, baseRefLayer)
        self.assertEqual(
            baseRefLayerStack.layers, 
            [baseRefLayer])

    def test_BasicReferencesAndPayloads(self):
        pcpCache = LoadPcpCache('refs_and_payloads/root.usda')
        rootLayer = pcpCache.GetLayerStackIdentifier().rootLayer

        # Load payloads.
        pcpCache.RequestPayloads(['/Payload', '/NoExpressionPayload'], [])

        # Verify initial state.
        self.AssertVariables(
            pcpCache, '/Ref',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'A'}), [
                    ('refs_and_payloads/A.usda', {'REF':'A'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Payload',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'A'}), [
                    ('refs_and_payloads/A.usda', {'REF':'A'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/NoExpressionRef',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'A'}), [
                    ('refs_and_payloads/A.usda', {'REF':'A'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/NoExpressionPayload',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'A'}), [
                    ('refs_and_payloads/A.usda', {'REF':'A'}), [
                    ],
                ]
            ])

        # Author a new 'OTHER_REF' variable in the root layer. No prims
        # rely on this variable in a reference arc, so this does not result
        # in any resyncs.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'REF':'A', 'OTHER_REF':'A'}
            self.assertEqual(changes.GetSignificantChanges(), [])

        self.AssertVariables(
            pcpCache, '/Ref',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'A', 'OTHER_REF':'A'}), [
                    ('refs_and_payloads/A.usda', {'REF':'A', 'OTHER_REF':'A'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Payload',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'A', 'OTHER_REF':'A'}), [
                    ('refs_and_payloads/A.usda', {'REF':'A', 'OTHER_REF':'A'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/NoExpressionRef',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'A', 'OTHER_REF':'A'}), [
                    ('refs_and_payloads/A.usda', {'REF':'A', 'OTHER_REF':'A'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/NoExpressionPayload',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'A', 'OTHER_REF':'A'}), [
                    ('refs_and_payloads/A.usda', {'REF':'A', 'OTHER_REF':'A'}), [
                    ],
                ]
            ])

        # Change the value of the 'REF' variable. Since both /Ref and /Payload
        # depend on this variable, they should be resynced.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'REF':'B'}
            self.assertEqual(
                changes.GetSignificantChanges(), ['/Payload', '/Ref'])

        self.AssertVariables(
            pcpCache, '/Ref',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'B'}), [
                    ('refs_and_payloads/B.usda', {'REF':'B'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Payload',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'B'}), [
                    ('refs_and_payloads/B.usda', {'REF':'B'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/NoExpressionRef',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'B'}), [
                    ('refs_and_payloads/A.usda', {'REF':'B'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/NoExpressionPayload',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'B'}), [
                    ('refs_and_payloads/A.usda', {'REF':'B'}), [
                    ],
                ]
            ])

        # Change the value of the 'REF' variable to an invalid expression.
        # Both /Ref and /Payload should be resynced but raise composition
        # errors due to the invalid expression.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'REF':'`${BAD`'}
            self.assertEqual(
                changes.GetSignificantChanges(), ['/Payload', '/Ref'])

        pi = self.AssertVariables(
            pcpCache, '/Ref',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'`${BAD`'}), [
                ]
            ],
            errorsExpected = True)

        self.assertIsInstance(
            pi.localErrors[0], Pcp.ErrorVariableExpressionError)

        pi = self.AssertVariables(
            pcpCache, '/Payload',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'`${BAD`'}), [
                ]
            ],
            errorsExpected = True)

        self.assertIsInstance(
            pi.localErrors[0], Pcp.ErrorVariableExpressionError)

        self.AssertVariables(
            pcpCache, '/NoExpressionRef',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'`${BAD`'}), [
                    ('refs_and_payloads/A.usda', {'REF':'`${BAD`'}), [
                    ],
                ]
            ])

        self.AssertVariables(
            pcpCache, '/NoExpressionPayload',
            expected = [
                ('refs_and_payloads/root.usda', {'REF':'`${BAD`'}), [
                    ('refs_and_payloads/A.usda', {'REF':'`${BAD`'}), [
                    ],
                ]
            ])

    def test_ExpressionVarChanges_MultipleReferences(self):
        """Test expression variable changes involving multiple references on
        a single prim."""
        pcpCache = LoadPcpCache('multi_ref/root.usda')
        rootLayer = Sdf.Layer.FindOrOpen('multi_ref/root.usda')
        ref1Layer = Sdf.Layer.FindOrOpen('multi_ref/ref1.usda')
        ref2Layer = Sdf.Layer.FindOrOpen('multi_ref/ref2.usda')

        self.AssertVariables(
            pcpCache, '/MultiRef',
            expected = [
                ('multi_ref/root.usda', {}), [
                    ('multi_ref/ref1.usda', {'SOURCE':'ref1'}), [
                        ('multi_ref/base_ref.usda', {'SOURCE':'ref1'}), [
                        ]
                    ],
                    ('multi_ref/ref2.usda', {'SOURCE':'ref2'}), [
                        ('multi_ref/base_ref.usda', {'SOURCE':'ref2'}), [
                        ]
                    ],
                ]
            ])

        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'SOURCE':'root'}
            self.assertEqual(changes.GetSignificantChanges(), ['/MultiRef'])

        multiRef = self.AssertVariables(
            pcpCache, '/MultiRef',
            expected = [
                ('multi_ref/root.usda', {'SOURCE':'root'}), [
                    ('multi_ref/ref1.usda', {'SOURCE':'root'}), [
                        ('multi_ref/base_ref.usda', {'SOURCE':'root'}), [
                        ]
                    ],
                    ('multi_ref/ref2.usda', {'SOURCE':'root'}), [
                        ('multi_ref/base_ref.usda', {'SOURCE':'root'}), [
                        ]
                    ],
                ]
            ])

        self.assertEqual(
            multiRef.rootNode.children[0].children[0].layerStack,
            multiRef.rootNode.children[1].children[0].layerStack)

        with Pcp._TestChangeProcessor(pcpCache) as changes:
            with Sdf.ChangeBlock():
                ref1Layer.expressionVariables = {'A':'B'}
                ref2Layer.expressionVariables = {'A':'C'}

            self.assertEqual(changes.GetSignificantChanges(), ['/MultiRef'])

        multiRef = self.AssertVariables(
            pcpCache, '/MultiRef',
            expected = [
                ('multi_ref/root.usda', {'SOURCE':'root'}), [
                    ('multi_ref/ref1.usda', {'SOURCE':'root', 'A':'B'}), [
                        ('multi_ref/base_ref.usda', {'SOURCE':'root', 'A':'B'}), [
                        ]
                    ],
                    ('multi_ref/ref2.usda', {'SOURCE':'root', 'A':'C'}), [
                        ('multi_ref/base_ref.usda', {'SOURCE':'root', 'A':'C'}), [
                        ]
                    ],
                ]
            ])
        
        self.assertNotEqual(
            multiRef.rootNode.children[0].children[0].layerStack,
            multiRef.rootNode.children[1].children[0].layerStack)
        
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'SOURCE':'root', 'A':'D'}
            self.assertEqual(changes.GetSignificantChanges(), ['/MultiRef'])

        multiRef = self.AssertVariables(
            pcpCache, '/MultiRef',
            expected = [
                ('multi_ref/root.usda', {'SOURCE':'root', 'A':'D'}), [
                    ('multi_ref/ref1.usda', {'SOURCE':'root', 'A':'D'}), [
                        ('multi_ref/base_ref.usda', {'SOURCE':'root', 'A':'D'}), [
                        ]
                    ],
                    ('multi_ref/ref2.usda', {'SOURCE':'root', 'A':'D'}), [
                        ('multi_ref/base_ref.usda', {'SOURCE':'root', 'A':'D'}), [
                        ]
                    ],
                ]
            ])

        self.assertEqual(
            multiRef.rootNode.children[0].children[0].layerStack,
            multiRef.rootNode.children[1].children[0].layerStack)

    def test_ExpressionVarChanges_CommonReference(self):
        """Test that changes to authored expression variables invalidate the
        appropriate layer stacks and prim pcpCache."""
        pcpCache = LoadPcpCache('common_ref/root.usda')
        ref1Layer = Sdf.Layer.FindOrOpen('common_ref/ref1.usda')
        ref2Layer = Sdf.Layer.FindOrOpen('common_ref/ref2.usda')
        
        ref1 = self.AssertVariables(
            pcpCache, '/A',
            expected = [
                ('common_ref/root.usda', {'A':'B'}), [
                    ('common_ref/ref1.usda', {'A':'B'}), [
                        ('common_ref/base_ref.usda', {'A':'B'}), [
                        ]
                    ]
                ]
            ])

        ref2 = self.AssertVariables(
            pcpCache, '/B',
            expected = [
                ('common_ref/root.usda', {'A':'B'}), [
                    ('common_ref/ref2.usda', {'A':'B'}), [
                        ('common_ref/base_ref.usda', {'A':'B'}), [
                        ]
                    ]
                ]
            ])

        # We expect the root.usda and base_ref.usda layer stacks to be shared
        # across the two prim indexes, even though base_ref.usda is referenced
        # from different layer stacks.
        self.assertEqual(ref1.rootNode.layerStack, ref2.rootNode.layerStack)
        self.assertNotEqual(ref1.rootNode.children[0].layerStack, 
                            ref2.rootNode.children[0].layerStack)
        self.assertEqual(ref1.rootNode.children[0].children[0].layerStack, 
                         ref2.rootNode.children[0].children[0].layerStack)

        # Author new expression variables in ref1.usda. This should affect only
        # /A and not /B, since /B does not reference ref1.usda. The new
        # expression variables should show up in the ref1.usda and base_ref.usda
        # layer stacks.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            ref1Layer.expressionVariables = {'X':'Y'}
            self.assertEqual(changes.GetSignificantChanges(), ['/A'])

        ref1 = self.AssertVariables(
            pcpCache, '/A',
            expected = [
                ('common_ref/root.usda', {'A':'B'}), [
                    ('common_ref/ref1.usda', {'A':'B', 'X':'Y'}), [
                        ('common_ref/base_ref.usda', {'A':'B', 'X':'Y'}), [
                        ]
                    ]
                ]
            ])

        ref2 = self.AssertVariables(
            pcpCache, '/B',
            expected = [
                ('common_ref/root.usda', {'A':'B'}), [
                    ('common_ref/ref2.usda', {'A':'B'}), [
                        ('common_ref/base_ref.usda', {'A':'B'}), [
                        ]
                    ]
                ]
            ])

        # At this point, the base_ref.usda layer stacks must differ between
        # the two prim indexes since they have different composed expression
        # variables.
        self.assertEqual(ref1.rootNode.layerStack, ref2.rootNode.layerStack)
        self.assertNotEqual(ref1.rootNode.children[0].layerStack, 
                            ref2.rootNode.children[0].layerStack)
        self.assertNotEqual(ref1.rootNode.children[0].children[0].layerStack, 
                            ref2.rootNode.children[0].children[0].layerStack)

        # Remove the authored opinion and verify everything goes back to
        # how it was.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            ref1Layer.ClearExpressionVariables()
            self.assertEqual(changes.GetSignificantChanges(), ['/A'])

        ref1 = self.AssertVariables(
            pcpCache, '/A',
            expected = [
                ('common_ref/root.usda', {'A':'B'}), [
                    ('common_ref/ref1.usda', {'A':'B'}), [
                        ('common_ref/base_ref.usda', {'A':'B'}), [
                        ]
                    ]
                ]
            ])

        ref2 = self.AssertVariables(
            pcpCache, '/B',
            expected = [
                ('common_ref/root.usda', {'A':'B'}), [
                    ('common_ref/ref2.usda', {'A':'B'}), [
                        ('common_ref/base_ref.usda', {'A':'B'}), [
                        ]
                    ]
                ]
            ])

        self.assertEqual(ref1.rootNode.layerStack, ref2.rootNode.layerStack)
        self.assertNotEqual(ref1.rootNode.children[0].layerStack, 
                            ref2.rootNode.children[0].layerStack)
        self.assertEqual(ref1.rootNode.children[0].children[0].layerStack, 
                         ref2.rootNode.children[0].children[0].layerStack)

        # Batch changes to expression variables in both ref1.usda and ref2.usda
        # and verify that the base_ref.usda layer stack in /A and /B have
        # different composed expression variables.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            with Sdf.ChangeBlock():
                ref1Layer.expressionVariables = {'X':'Y'}
                ref2Layer.expressionVariables = {'X':'Z'}

            self.assertEqual(changes.GetSignificantChanges(), ['/A', '/B'])

        ref1 = self.AssertVariables(
            pcpCache, '/A',
            expected = [
                ('common_ref/root.usda', {'A':'B'}), [
                    ('common_ref/ref1.usda', {'A':'B', 'X':'Y'}), [
                        ('common_ref/base_ref.usda', {'A':'B', 'X':'Y'}), [
                        ]
                    ]
                ]
            ])

        ref2 = self.AssertVariables(
            pcpCache, '/B',
            expected = [
                ('common_ref/root.usda', {'A':'B'}), [
                    ('common_ref/ref2.usda', {'A':'B', 'X':'Z'}), [
                        ('common_ref/base_ref.usda', {'A':'B', 'X':'Z'}), [
                        ]
                    ]
                ]
            ])

        # Again, we expect the base_ref.usda layer stack to differ between the
        # two prim indexes because of the different composed expression
        # variables.
        self.assertEqual(ref1.rootNode.layerStack, ref2.rootNode.layerStack)
        self.assertNotEqual(ref1.rootNode.children[0].layerStack, 
                            ref2.rootNode.children[0].layerStack)
        self.assertNotEqual(ref1.rootNode.children[0].children[0].layerStack, 
                            ref2.rootNode.children[0].children[0].layerStack)

    def test_ExpressionVarChanges_ChainedReferences(self):
        """Test that changes to expression variables propagates to downstream
        layer stacks in cases where multiple references have been chained
        together"""
        pcpCache = LoadPcpCache('chained_ref/root.usda')
        ref1Layer = Sdf.Layer.FindOrOpen('chained_ref/ref1.usda')
        ref2Layer = Sdf.Layer.FindOrOpen('chained_ref/ref2.usda')

        self.AssertVariables(
            pcpCache, '/Root1',
            expected = [
                ('chained_ref/root.usda', {'A':'B'}), [
                    ('chained_ref/ref1.usda', {'A':'B'}), [
                        ('chained_ref/ref2.usda', {'A':'B'}), [
                            ('chained_ref/ref3.usda', {'A':'B'}), [
                            ]
                        ]
                    ]
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Root2',
            expected = [
                ('chained_ref/root.usda', {'A':'B'}), [
                    ('chained_ref/ref2.usda', {'A':'B'}), [
                        ('chained_ref/ref3.usda', {'A':'B'}), [
                        ]
                    ]
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Root3',
            expected = [
                ('chained_ref/root.usda', {'A':'B'}), [
                    ('chained_ref/ref3.usda', {'A':'B'}), [
                    ]
                ]
            ])
        
        # Batch together changes to the expression variables in ref1.usda and
        # ref2.usda and verify they propagate to downstream layer stacks
        # in all prim indexes.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            with Sdf.ChangeBlock():
                ref1Layer.expressionVariables = {'I':'J'}
                ref2Layer.expressionVariables = {'X':'Y'}

            self.assertEqual(changes.GetSignificantChanges(),
                             ['/Root1', '/Root2'])

        self.AssertVariables(
            pcpCache, '/Root1',
            expected = [
                ('chained_ref/root.usda', {'A':'B'}), [
                    ('chained_ref/ref1.usda', {'A':'B', 'I':'J'}), [
                        ('chained_ref/ref2.usda', {'A':'B', 'I':'J', 'X':'Y'}), [
                            ('chained_ref/ref3.usda', {'A':'B', 'I':'J', 'X':'Y'}), [
                            ]
                        ]
                    ]
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Root2',
            expected = [
                ('chained_ref/root.usda', {'A':'B'}), [
                    ('chained_ref/ref2.usda', {'A':'B', 'X':'Y'}), [
                        ('chained_ref/ref3.usda', {'A':'B', 'X':'Y'}), [
                        ]
                    ]
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Root3',
            expected = [
                ('chained_ref/root.usda', {'A':'B'}), [
                    ('chained_ref/ref3.usda', {'A':'B'}), [
                    ]
                ]
            ])

    def test_ExpressionVarChanges_SignificantLayerStackChanges(self):
        """Test scenarios involving significant changes to layer stacks
        combined with expression variable changes"""
        pcpCache = LoadPcpCache('sig_changes/root.usda')
        rootLayer = Sdf.Layer.FindOrOpen('sig_changes/root.usda')
        ref1Layer = Sdf.Layer.FindOrOpen('sig_changes/ref1.usda')

        self.AssertVariables(
            pcpCache, '/Root1',
            expected = [
                ('sig_changes/root.usda', {'A':'B'}), [
                    ('sig_changes/ref1.usda', {'A':'B'}), [
                        ('sig_changes/ref2.usda', {'A':'B'}), [
                        ]
                    ]
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Root2',
            expected = [
                ('sig_changes/root.usda', {'A':'B'}), [
                    ('sig_changes/ref2.usda', {'A':'B'}), [
                    ]
                ]
            ])

        # Batch together a change to the expression variables in the root layer
        # stack and a significant layer stack change. The variable change
        # should still propagate to all downstream layer stacks.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            with Sdf.ChangeBlock():
                rootLayer.expressionVariables = {'A':'C'}
                rootLayer.subLayerPaths.append('sig_changes/sub.usda')

            if INCREMENTAL_CHANGES:
                self.assertEqual(changes.GetSignificantChanges(), ['/Dummy'])
            else:
                self.assertEqual(changes.GetSignificantChanges(), ['/'])

        self.AssertVariables(
            pcpCache, '/Root1',
            expected = [
                ('sig_changes/root.usda', {'A':'C'}), [
                    ('sig_changes/ref1.usda', {'A':'C'}), [
                        ('sig_changes/ref2.usda', {'A':'C'}), [
                        ]
                    ]
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Root2',
            expected = [
                ('sig_changes/root.usda', {'A':'C'}), [
                    ('sig_changes/ref2.usda', {'A':'C'}), [
                    ]
                ]
            ])

        # Batch together a change to the expression variables in root.usda and
        # ref1.usda, along with a significant layer stack change. We expect
        # only /Root1 to be resynced since its the only index that references
        # ref1.usda, and variable changes should be propagated appropriately.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            with Sdf.ChangeBlock():
                rootLayer.expressionVariables = {'A':'D'}
                ref1Layer.expressionVariables = {'X':'Y'}
                ref1Layer.subLayerPaths.append('sig_changes/sub1.usda')

            self.assertEqual(changes.GetSignificantChanges(), ['/Root1'])
        
        self.AssertVariables(
            pcpCache, '/Root1',
            expected = [
                ('sig_changes/root.usda', {'A':'D'}), [
                    ('sig_changes/ref1.usda', {'A':'D', 'X':'Y'}), [
                        ('sig_changes/ref2.usda', {'A':'D', 'X':'Y'}), [
                        ]
                    ]
                ]
            ])

        self.AssertVariables(
            pcpCache, '/Root2',
            expected = [
                ('sig_changes/root.usda', {'A':'D'}), [
                    ('sig_changes/ref2.usda', {'A':'D'}), [
                    ]
                ]
            ])

    def test_BasicVariantSelections(self):
        """Test expressions in variant selections."""
        pcpCache = LoadPcpCache('variants/root.usda')
        rootLayer = pcpCache.GetLayerStackIdentifier().rootLayer

        # Verify initial state
        pi, err = pcpCache.ComputePrimIndex('/Basic')
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Basic'),
             rootLayer.GetPrimAtPath('/Basic{v=x_sel}')])
        self.assertEqual(
            pi.ComposeAuthoredVariantSelections(), {'v':'x_sel'})
        self.assertEqual(
            pi.GetSelectionAppliedForVariantSet('v'), 'x_sel')

        # Author expression variable ROOT=y, which affects the variant selection
        # on /BasicVariantSelections and should cause it to be resynced.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'ROOT':'y'}

            self.assertEqual(changes.GetSignificantChanges(), ['/Basic'])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Basic')
        self.assertEqual(len(err), 0)
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Basic'),
             rootLayer.GetPrimAtPath('/Basic{v=y_sel}')])
        self.assertEqual(
            pi.ComposeAuthoredVariantSelections(), {'v':'y_sel'})
        self.assertEqual(
            pi.GetSelectionAppliedForVariantSet('v'), 'y_sel')

    def test_VariantSelectionInReference(self):
        """Test variant selection expressions across references."""
        pcpCache = LoadPcpCache('variants/root.usda')
        rootLayer = pcpCache.GetLayerStackIdentifier().rootLayer
        refLayer = Sdf.Layer.FindOrOpen('variants/ref.usda')

        # Verify initial state
        pi, err = pcpCache.ComputePrimIndex('/Reference')
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Reference'),
             refLayer.GetPrimAtPath('/Ref'),
             refLayer.GetPrimAtPath('/Ref{v=x_sel}')])
        self.assertEqual(
            pi.ComposeAuthoredVariantSelections(), {'v':'x_sel'})
        self.assertEqual(
            pi.GetSelectionAppliedForVariantSet('v'), 'x_sel')

        # Author expression variable REF=y in the referenced layer ref.usda.
        # This affects the variant selection used by /Reference, so it should
        # cause a resync.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            refLayer.expressionVariables = {'REF':'y'}

            self.assertEqual(changes.GetSignificantChanges(), ['/Reference'])
            self.assertEqual(changes.GetSpecChanges(), [])
        
        pi, err = pcpCache.ComputePrimIndex('/Reference')
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Reference'),
             refLayer.GetPrimAtPath('/Ref'),
             refLayer.GetPrimAtPath('/Ref{v=y_sel}')])
        self.assertEqual(
            pi.ComposeAuthoredVariantSelections(), {'v':'y_sel'})
        self.assertEqual(
            pi.GetSelectionAppliedForVariantSet('v'), 'y_sel')

        # Author expression variable REF=z in the root layer stack. This
        # should override the variable in ref.usda and affect the variant
        # selection on /Reference again.
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.expressionVariables = {'REF':'z'}

            self.assertEqual(changes.GetSignificantChanges(), ['/Reference'])
            self.assertEqual(changes.GetSpecChanges(), [])

        pi, err = pcpCache.ComputePrimIndex('/Reference')
        self.assertEqual(
            pi.primStack,
            [rootLayer.GetPrimAtPath('/Reference'),
             refLayer.GetPrimAtPath('/Ref'),
             refLayer.GetPrimAtPath('/Ref{v=z_sel}')])
        self.assertEqual(
            pi.ComposeAuthoredVariantSelections(), {'v':'z_sel'})
        self.assertEqual(
            pi.GetSelectionAppliedForVariantSet('v'), 'z_sel')

    def test_NoChanges(self):
        """Test scenarios where no recomputations are expected."""
        pcpCache = LoadPcpCache('no_changes/root.usda')
        rootLayer = pcpCache.GetLayerStackIdentifier().rootLayer

        # Authoring expression variables in the root layer stack should
        # not incur any significant changes if nothing depends on those
        # expression variables.
        pi, err = pcpCache.ComputePrimIndex('/A')
        with Pcp._TestChangeProcessor(pcpCache) as changes:
            rootLayer.pseudoRoot.SetInfo('expressionVariables', {'X':'A'})

            self.assertEqual(changes.GetSignificantChanges(), [])
            self.assertEqual(changes.GetSpecChanges(), [])
            self.assertEqual(changes.GetPrimChanges(), [])

    def test_ExpressionVariableCompositionInLayerSTack(self):
        """Test expression variable composition in layer stacks"""
        sessionSublayer = Sdf.Layer.CreateAnonymous('session-sublayer')
        sessionSublayer.ImportFromString('''
        #usda 1.0
        (
            expressionVariables = {
                string SESSION_SUBLAYER_ONLY = "session-sublayer"
            }
        )
        '''.strip())
        
        sessionLayer = Sdf.Layer.CreateAnonymous('session')
        sessionLayer.ImportFromString('''
        #usda 1.0
        (
            expressionVariables = {{
                string SESSION_ONLY = "session"
                string SESSION_OVERRIDE = "session"
            }}
            subLayers = [
                @{sub}@
            ]
        )
        '''.format(sub=sessionSublayer.identifier).strip())

        subLayer = Sdf.Layer.CreateAnonymous('sublayer')
        subLayer.ImportFromString('''
        #usda 1.0
        (
            expressionVariables = {
                string SUBLAYER_ONLY = "sublayer"
            }
        )
        '''.strip())
        
        rootLayer = Sdf.Layer.CreateAnonymous('root')
        rootLayer.ImportFromString('''
        #usda 1.0
        (
            expressionVariables = {{
                string ROOT_ONLY = "root"
                string SESSION_OVERRIDE = "root"
            }}
            subLayers = [
                @{sub}@
            ]
        )
        '''.format(sub=subLayer.identifier).strip())

        rootId = Pcp.LayerStackIdentifier(rootLayer, sessionLayer)
        pcpCache = Pcp.Cache(rootId)

        rootLayerStack, _ = pcpCache.ComputeLayerStack(rootId)
        self.assertEqual(rootLayerStack.expressionVariables.GetVariables(), 
                         {'SESSION_ONLY':'session', 
                          'SESSION_OVERRIDE':'session',
                          'ROOT_ONLY':'root'})

if __name__ == "__main__":
    unittest.main()
