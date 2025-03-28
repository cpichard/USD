#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

from pxr.Usdviewq.qt import QtWidgets

# positions and names of our variants
CAPSULE = (1, 'capsule')
CONE = (2, 'cone')
CUBE = (3, 'cube')
CYLINDER = (4, 'cylinder')

VARIANT_INFO_POS = 0
VARIANT_INFO_NAME = 1

# Identifiers for variants in our stage
FIRST_VARIANT = 'a_shapeVariant'
SECOND_VARIANT = 'b_shapeVariant'

def _makeFileName(variantInfo, index):
    return variantInfo[1] + str(index) + '.png'

def _modifySettings(appController):
    appController._dataModel.viewSettings.showBBoxes = False
    appController._dataModel.viewSettings.showHUD = False
    appController._dataModel.viewSettings.selHighlightMode = 'Never'

def _setupWidgets(appController):
    # Select our prim with the variant authored
    appController._ui.primViewLineEdit.setText('Shapes')
    appController._primViewFindNext()

def _getVariantSelector(appController, whichVariant):
    # Select the metadata tab in the lower right corner
    propertyInspector = appController._ui.propertyInspector
    propertyInspector.setCurrentIndex(1)

    # Grab the rows of our metadata tab and select the set containing
    # our variant selection
    metadataTable = propertyInspector.currentWidget().findChildren(QtWidgets.QTableWidget)[0]

    for i in range(0, metadataTable.rowCount()):
        currentName = metadataTable.item(i,0).text()
        if str(currentName).startswith(whichVariant):
            return metadataTable.cellWidget(i,1) 

    return None

def _selectVariant(appController, variantPos, whichVariant):
    selector = _getVariantSelector(appController, whichVariant)
    selector.setCurrentIndex(variantPos)

    # Variant selection changes the USD stage. Since all stage changes are
    # coalesced using the guiResetTimer, we need to process events here to give
    # the timer a chance to fire.
    QtWidgets.QApplication.processEvents()

def _testBasic(appController):
    # select items from the first variant

    # select capsule
    _selectVariant(appController, CAPSULE[VARIANT_INFO_POS], FIRST_VARIANT)
    appController._takeShot(_makeFileName(CAPSULE, 1))

    # select cone
    _selectVariant(appController, CONE[VARIANT_INFO_POS], FIRST_VARIANT)
    appController._takeShot(_makeFileName(CONE, 1))

    # select cube
    _selectVariant(appController, CUBE[VARIANT_INFO_POS], FIRST_VARIANT)
    appController._takeShot(_makeFileName(CUBE, 1))

    # select cylinder 
    _selectVariant(appController, CYLINDER[VARIANT_INFO_POS], FIRST_VARIANT)
    appController._takeShot(_makeFileName(CYLINDER, 1))

    # select items from the second variant

    # select capsule
    _selectVariant(appController, CAPSULE[VARIANT_INFO_POS], SECOND_VARIANT)
    appController._takeShot(_makeFileName(CAPSULE, 2))

    # select cone
    _selectVariant(appController, CONE[VARIANT_INFO_POS], SECOND_VARIANT)
    appController._takeShot(_makeFileName(CONE, 2))

    # select cube
    _selectVariant(appController, CUBE[VARIANT_INFO_POS], SECOND_VARIANT)
    appController._takeShot(_makeFileName(CUBE, 2))

    # select cylinder 
    _selectVariant(appController, CYLINDER[VARIANT_INFO_POS], SECOND_VARIANT)
    appController._takeShot(_makeFileName(CYLINDER, 2))

def testUsdviewInputFunction(appController):
    _modifySettings(appController)
    _setupWidgets(appController)
    
    _testBasic(appController)
