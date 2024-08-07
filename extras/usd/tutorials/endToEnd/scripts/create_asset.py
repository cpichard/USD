#!/pxrpythonsubst
#
# Copyright 2016 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#

'''
Creates an asset file that consists of a top level layer and sublayers for
shading and geometry.
'''

from __future__ import print_function

import os
from pxr import Kind, Sdf, Usd, UsdGeom

def main():
    import optparse

    descr = __doc__.strip()
    usage = 'usage: %prog [options] asset'
    parser = optparse.OptionParser(description=descr, usage=usage)
    parser.add_option('-k', '--kind', default=Kind.Tokens.component,
                      help="sets asset's model kind")
    parser.add_option('-o', '--outputDir',
            help='directory to create assets.  if none specified, will use modelName.')
    parser.add_option('-s', '--shadingVariantLayer',
            help='add <asset>.shadingVariants.usda to model references.', default=False,
            action='store_true')
    parser.add_option('-f', '--force', default=False, action='store_true',
            help='if False, this will error if [outputDir] exists')
    options, args = parser.parse_args()

    if len(args) != 1:
        parser.error("No asset specified")

    asset = args[0]
    force = options.force

    outputDir = options.outputDir
    if not outputDir:
        outputDir = asset

    if os.path.exists(outputDir):
        if not force:
            parser.error('outputDir "%s" exists.  Use -f to override' % outputDir)
    else:
        os.makedirs(outputDir)

    _CreateAsset(asset, outputDir, options.kind, options.shadingVariantLayer)

def _CreateAsset(assetName, assetDir, assetKind, addShadingVariantLayer):
    assetFilePath = os.path.join(assetDir, '%s.usd' % assetName)

    print("Creating asset at %s" % assetFilePath)
    # Make the layer ascii - good for readability, plus the file is small
    rootLayer = Sdf.Layer.CreateNew(assetFilePath, args = {'format':'usda'})
    assetStage = Usd.Stage.Open(rootLayer)

    # Define a prim for the asset and make it the default for the stage.
    assetPrim = UsdGeom.Xform.Define(assetStage, '/%s' % assetName).GetPrim()
    assetStage.SetDefaultPrim(assetPrim)
    # Lets viewing applications know how to orient a free camera properly
    UsdGeom.SetStageUpAxis(assetStage, UsdGeom.Tokens.y)

    # Usually we will "loft up" the kind authored into the exported geometry
    # layer rather than re-stamping here; we'll leave that for a later
    # tutorial, and just be explicit here.
    model = Usd.ModelAPI(assetPrim)
    if assetKind:
        model.SetKind(assetKind)
    
    model.SetAssetName(assetName)
    model.SetAssetIdentifier('%s/%s.usd' % (assetName, assetName))
    
    refs = []
    if addShadingVariantLayer:
        # if we're going to add it, then shading is stronger than geom and needs
        # to be added first
        refs.append('./%s.shadingVariants.usda' % assetName)

    refs.append('./%s.maya.usd' % assetName)

    _CreateAndReferenceLayers(assetPrim, assetDir, refs)

    assetStage.GetRootLayer().Save()

def _CreateAndReferenceLayers(assetPrim, assetDir, refs):
    from pxr import Usd
    for refLayerPath in refs:
        referencedStage = Usd.Stage.CreateNew(os.path.join(assetDir, refLayerPath))
        referencedAssetPrim = referencedStage.DefinePrim(assetPrim.GetPath())
        referencedStage.SetDefaultPrim(referencedAssetPrim)
        referencedStage.GetRootLayer().Save()

        assetPrim.GetReferences().AddReference(refLayerPath)

    # If you want to print things out, you can do:
    #print(rootLayer.ExportToString())

if __name__ == '__main__':
    main()
