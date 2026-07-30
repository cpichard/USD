#!/pxrpythonsubst
#
# Copyright 2026 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
from pxr import Sdf
from pxr.UsdAppUtils.complexityArgs import RefinementComplexities

def _modifySettings(appController):
    appController._dataModel.viewSettings.showBBoxes = False
    appController._dataModel.viewSettings.showHUD = False
    appController._dataModel.viewSettings.autoComputeClippingPlanes = True
    appController._dataModel.viewSettings.complexity = \
        RefinementComplexities.VERY_HIGH
    appController._dataModel._viewSettingsDataModel.cameraPath = \
        Sdf.Path('/main_cam')
    appController._stageView.updateView()

# Edit inputs:varname on the shader within the referenced nodegraph and re-render.
def testUsdviewInputFunction(appController):
    _modifySettings(appController)

    stage = appController._dataModel.stage

    appController._takeShot("before.png")

    shader = stage.GetPrimAtPath('/PrimvarPackage/PrimvarColor')
    shader.GetAttribute('inputs:varname').Set('myothercolor')
    appController._takeShot("after.png")
