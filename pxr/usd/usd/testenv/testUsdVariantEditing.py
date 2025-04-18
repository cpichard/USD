#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

from __future__ import print_function

import os, sys
from pxr import Gf, Tf, Sdf, Usd

def OpenLayer(name):
    layerFile = '%s.usda' % name
    layer = Sdf.Layer.FindOrOpen(layerFile)
    assert layer, 'failed to open layer @%s@' % layerFile
    return layer

# Open stage.
layer = OpenLayer('testAPI_var')
stage = Usd.Stage.Open(layer.identifier)
assert stage, 'failed to create stage for @%s@' % layer.identifier

# Check GetLayerStack behavior.
assert stage.GetLayerStack()[0] == stage.GetSessionLayer()

# Get LayerStack without session layer.
rootLayer = stage.GetLayerStack(includeSessionLayers=False)[0]
assert rootLayer == stage.GetRootLayer()

# Get Sarah prim.
sarah = stage.GetPrimAtPath('/Sarah')
assert sarah, 'failed to find prim /Sarah'

# Sanity check simple composition.
assert sarah.GetAttribute('color').Get() == Gf.Vec3d(1,0,0)

# Verify that the base prim does not have the custom attribute that we will
# create later in the variant.
emptyPrim = stage.OverridePrim('/Sarah/EmptyPrim')
assert emptyPrim
assert not emptyPrim.GetAttribute('newAttr')

# Should start out with EditTarget being local & root layer.
assert (stage.HasLocalLayer(stage.GetEditTarget().GetLayer()) and
        stage.GetEditTarget().GetLayer() == stage.GetRootLayer())

# Try editing a local variant.
displayColor = sarah.GetVariantSet('displayColor')
assert displayColor.GetVariantSelection() == 'red'
assert sarah.GetVariantSets().GetVariantSelection('displayColor') == 'red'
with displayColor.GetVariantEditContext():
    sarah.GetAttribute('color').Set(Gf.Vec3d(1,1,1))
    stage.DefinePrim(sarah.GetPath().AppendChild('Child'), 'Scope')

    # Bug 90706 - verify that within a VariantSet, a new attribute that does
    # not exist on the base prim returns True for IsDefined()
    over = stage.OverridePrim('/Sarah/EmptyPrim')
    assert over
    over.CreateAttribute('newAttr', Sdf.ValueTypeNames.Int)
    assert over.GetAttribute('newAttr').IsDefined()

# Test existence of the newly created attribute again, outside of the edit
# context, while we are still set to the variant selection from which we created
# the attribute.
emptyPrim = stage.OverridePrim('/Sarah/EmptyPrim')
assert emptyPrim
assert emptyPrim.GetAttribute('newAttr').IsDefined()

assert sarah.GetAttribute('color').Get() == Gf.Vec3d(1,1,1)
assert stage.GetPrimAtPath(sarah.GetPath().AppendChild('Child'))

# Switch to 'green' variant.
displayColor.SetVariantSelection('green')
assert displayColor.GetVariantSelection() == 'green'

# Should not be picking up variant opinions authored above.
assert sarah.GetAttribute('color').Get() == Gf.Vec3d(0,1,0)
assert not stage.GetPrimAtPath(sarah.GetPath().AppendChild('Scope'))
emptyPrim = stage.OverridePrim('/Sarah/EmptyPrim')
assert emptyPrim
assert not emptyPrim.GetAttribute('newAttr').IsDefined()

displayColor.ClearVariantSelection()
assert displayColor.GetVariantSelection() == ''

# Test editing a variant that doesn't yet have opinions.
sarah_ref = stage.GetPrimAtPath('/Sarah_ref')
displayColor = sarah_ref.GetVariantSet('displayColor')
displayColor.SetVariantSelection('red')
assert displayColor.GetVariantSelection() == 'red'
with displayColor.GetVariantEditContext():
    sarah_ref.GetAttribute('color').Set(Gf.Vec3d(2,2,2))
assert sarah_ref.GetAttribute('color').Get() == Gf.Vec3d(2,2,2)

def TestNewPayloadAutoLoading():
    print('TestNewPayloadAutoLoading')
    
    # Test that switching a variant that introduces a payload causes the payload
    # to be included if the parent is loaded, and vice versa.
    rootLayer = Sdf.Layer.CreateAnonymous()
    payloadLayer = Sdf.Layer.CreateAnonymous()

    Sdf.CreatePrimInLayer(rootLayer, '/main')
    Sdf.CreatePrimInLayer(payloadLayer, '/parent/child')

    stage = Usd.Stage.Open(rootLayer)
    main = stage.GetPrimAtPath('/main')
    pvs = main.GetVariantSets().AddVariantSet('payload_vset')

    withPayload = pvs.AddVariant('with_payload')
    withoutPayload = pvs.AddVariant('without_payload')

    pvs.SetVariantSelection('with_payload')
    with pvs.GetVariantEditContext():
        main.GetPayloads().AddPayload(payloadLayer.identifier, '/parent')

    pvs.SetVariantSelection('without_payload')

    # Now open the stage load all, we shouldn't have /main/child.
    stage = Usd.Stage.Open(rootLayer, load=Usd.Stage.LoadAll)
    assert stage.GetPrimAtPath('/main')
    assert not stage.GetPrimAtPath('/main/child')

    # Switching the selection should cause the payload to auto-load.
    stage.GetPrimAtPath('/main').GetVariantSet(
        'payload_vset').SetVariantSelection('with_payload')
    main = stage.GetPrimAtPath('/main')
    assert main and main.IsLoaded()
    assert stage.GetPrimAtPath('/main/child')

    # Open the stage again, but with load-none.
    stage = Usd.Stage.Open(rootLayer, load=Usd.Stage.LoadNone)
    assert stage.GetPrimAtPath('/main')
    assert not stage.GetPrimAtPath('/main/child')

    # Switching the selection should NOT cause the payload to auto-load.
    stage.GetPrimAtPath('/main').GetVariantSet(
        'payload_vset').SetVariantSelection('with_payload')
    main = stage.GetPrimAtPath('/main')
    assert main and not main.IsLoaded()
    assert not stage.GetPrimAtPath('/main/child')

TestNewPayloadAutoLoading()

def TestVariantChangeResyncNotification():
    """Test that changes to variants only resync prims that actually
    depend on the variant"""

    # /PrimVariants has a variantSet "primVariant" with two variants 
    # "one" and "two" but no variant selection set.
    # 
    # /Prim1 references /PrimVariants and sets the primVariant variant
    # selection to "one"
    #
    # /Prim2 references /PrimVariants and sets the primVariant variant
    # selection to "two"
    layer = Sdf.Layer.CreateAnonymous("layer.usda")
    layer.ImportFromString('''#usda 1.0

        def "PrimVariants" (
            variantSets = ["primVariant"]
        ) {
            variantSet "primVariant" = {
                "one" {
                    def "VariantOneChild" {
                        int variantOneChildAttr
                    }
                }
                "two" {
                    def "VariantTwoChild" {
                        int variantTwoChildAttr
                    }
                }
            }
        }
        
        def "Prim1" (
            references = </PrimVariants>
            variants = {
                string primVariant = "one"
            }
        ) {
        }
        
        def "Prim2" (
            references = </PrimVariants>
            variants = {
                string primVariant = "two"
            }
        ) {
        }
        
    ''')

    stage = Usd.Stage.Open(layer)

    # Register ObjectsChanged notice handler that prints the resynced paths.
    resyncedPaths = None
    def _OnObjectsChanged(notice, sender):
        nonlocal resyncedPaths
        resyncedPaths = notice.GetResyncedPaths()
    objectsChanged = Tf.Notice.RegisterGlobally(
        Usd.Notice.ObjectsChanged, _OnObjectsChanged)

    # Make changes to the variants in /PrimVariants{primVariant=}
    primVariantsPrim = layer.GetPrimAtPath("/PrimVariants")
    vSet = primVariantsPrim.variantSets['primVariant']

    # Add a new variant "three" that isn't selected by any prims. This should
    # result in no resyncs.
    resyncedPaths = None
    Sdf.VariantSpec(vSet, "three")
    assert resyncedPaths == []

    # Delete the varint "one" which is selected by /Prim1. /Prim1 only should
    # be resynced.
    resyncedPaths = None
    vSet.RemoveVariant( vSet.variants["one"])
    assert resyncedPaths == ["/Prim1"]

TestVariantChangeResyncNotification()

print('OK')

