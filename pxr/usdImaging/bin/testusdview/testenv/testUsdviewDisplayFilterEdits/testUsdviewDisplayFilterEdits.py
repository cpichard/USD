#!/pxrpythonsubst
#
# Copyright 2022 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
from pxr import UsdShade, Gf

# Remove any unwanted visuals from the view, and enable autoClip
def _modifySettings(appController):
    appController._dataModel.viewSettings.showBBoxes = False
    appController._dataModel.viewSettings.showHUD = False
    appController._dataModel.viewSettings.autoComputeClippingPlanes = True

# Update the connected Display Filter.
def _updateDisplayFilterTargets(filterPaths, appController):
    stage = appController._dataModel.stage
    layer = stage.GetSessionLayer()
    stage.SetEditTarget(layer)

    renderSettings = stage.GetPrimAtPath('/Render/RenderSettings')
    displayFilterRel = renderSettings.GetRelationship('ri:displayFilters')
    displayFilterRel.SetTargets(filterPaths)

def _updateDisplayFilterParam(filterPath, attrName, attrValue, appController):
    stage = appController._dataModel.stage
    layer = stage.GetSessionLayer()
    stage.SetEditTarget(layer)

    displayFilter = stage.GetPrimAtPath(filterPath)
    displayFilterParam = displayFilter.GetAttribute(attrName)
    displayFilterParam.Set(attrValue)


# Test changing the connected DisplayFilter.
def testUsdviewInputFunction(appController):
    from os import cpu_count
    print('******CPU COUNT: {}'.format(cpu_count()))
    _modifySettings(appController)

    filter1 = '/Render/DisplayFilter1'
    filter2 = '/Render/DisplayFilter2'
    bgColorAttrName = "inputs:ri:backgroundColor"

    appController._takeShot("firstFilter.png", waitForConvergence=True)
    
    _updateDisplayFilterTargets([filter2], appController)
    appController._takeShot("secondFilter.png", waitForConvergence=True)

    _updateDisplayFilterParam(filter2, bgColorAttrName, Gf.Vec3f(0, 1, 1), appController)
    appController._takeShot("secondFilter_modified.png", waitForConvergence=True)

    _updateDisplayFilterParam(filter2, bgColorAttrName, Gf.Vec3f(1, 0, 1), appController)
    _updateDisplayFilterTargets([filter1, filter2], appController)
    appController._takeShot("multiFilters1.png", waitForConvergence=True)

    _updateDisplayFilterTargets([filter2, filter1], appController)
    appController._takeShot("multiFilters2.png", waitForConvergence=True)
