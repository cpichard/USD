#
# Copyright 2016 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#

from .qt import QtGui, QtWidgets, QtCore
from pxr import Sdf
from .usdviewContextMenuItem import UsdviewContextMenuItem
from .common import (PropertyViewIndex, PropertyViewDataRoles,
                     PrimNotFoundException, PropertyNotFoundException)

#
# Specialized context menu for running commands in the attribute viewer.
#
class AttributeViewContextMenu(QtWidgets.QMenu):

    def __init__(self, parent, item, dataModel):
        QtWidgets.QMenu.__init__(self, parent)
        self._menuItems = _GetContextMenuItems(item, dataModel)

        for menuItem in self._menuItems:
            # create menu actions
            if menuItem.isValid() and menuItem.ShouldDisplay():
                action = self.addAction(menuItem.GetText(), menuItem.RunCommand)

                # set enabled
                if not menuItem.IsEnabled():
                    action.setEnabled(False)


def _GetContextMenuItems(item, dataModel):
    return [ # Root selection methods
             CopyAttributeNameMenuItem(dataModel, item),
             CopyAttributeValueMenuItem(dataModel, item),
             CopyAllTargetPathsMenuItem(dataModel, item),
             SelectAllTargetPathsMenuItem(dataModel, item),

             # Individual/multi target selection menus
             CopyTargetPathMenuItem(dataModel, item),
             SelectTargetPathMenuItem(dataModel, item),
             ViewSplineMenuItem(dataModel, item),]

def _selectPrimsAndProps(dataModel, paths):
    prims = []
    props = []
    for path in paths:

        primPath = path.GetAbsoluteRootOrPrimPath()
        prim = dataModel.stage.GetPrimAtPath(primPath)
        if not prim:
            raise PrimNotFoundException(primPath)
        prims.append(prim)

        if path.IsPropertyPath():
            prop = prim.GetProperty(path.name)
            if not prop:
                raise PropertyNotFoundException(path)
            props.append(prop)

    with dataModel.selection.batchPrimChanges:
        dataModel.selection.clearPrims()
        for prim in prims:
            dataModel.selection.addPrim(prim)

    with dataModel.selection.batchPropChanges:
        dataModel.selection.clearProps()
        for prop in props:
            dataModel.selection.addProp(prop)

    dataModel.selection.clearComputedProps()

#
# The base class for propertyview context menu items.
#
class AttributeViewContextMenuItem(UsdviewContextMenuItem):
    def __init__(self, dataModel, item):
        self._dataModel = dataModel
        self._item = item
        self._role = self._item.data(PropertyViewIndex.TYPE, QtCore.Qt.ItemDataRole.WhatsThisRole)
        self._name = self._item.text(PropertyViewIndex.NAME) if self._item else ""
        self._value = self._item.text(PropertyViewIndex.VALUE) if self._item else ""

    def IsEnabled(self):
        return True

    def ShouldDisplay(self):
        return True

    def GetText(self):
        return ""

    def RunCommand(self):
        pass

#
# Copy the attribute's name to clipboard.
#
class CopyAttributeNameMenuItem(AttributeViewContextMenuItem):
    def ShouldDisplay(self):
        return self._role not in (PropertyViewDataRoles.TARGET, PropertyViewDataRoles.CONNECTION)

    def GetText(self):
        return "Copy Property Name"

    def RunCommand(self):
        if self._name == "":
            return

        cb = QtWidgets.QApplication.clipboard()
        cb.setText(self._name, QtGui.QClipboard.Selection)
        cb.setText(self._name, QtGui.QClipboard.Clipboard)

#
# Copy the attribute's value to clipboard.
#
class CopyAttributeValueMenuItem(AttributeViewContextMenuItem):
    def ShouldDisplay(self):
        return self._role not in (PropertyViewDataRoles.TARGET, PropertyViewDataRoles.CONNECTION)

    def GetText(self):
        return "Copy Property Value"

    def RunCommand(self):
        # We display relationships targets as:
        #    /f, /g/a ...
        # But when we ask to copy the value, we'd like to get back:
        #    [Sdf.Path('/f'), Sdf.Path('/g/a')]
        # Which is useful for pasting into a python interpreter.
        if self._role == PropertyViewDataRoles.RELATIONSHIP_WITH_TARGETS:
            valueStr = str([Sdf.Path("".join(p.split())) \
                          for p in self._value.split(",")])
        else:
            valueStr = self._value
            if self._item:
                rawVal = self._item.rawValue
                if rawVal is not None:
                    valueStr = str(rawVal)

        cb = QtWidgets.QApplication.clipboard()
        cb.setText(valueStr, QtGui.QClipboard.Selection)
        cb.setText(valueStr, QtGui.QClipboard.Clipboard)

# --------------------------------------------------------------------
# Individual target selection menus
# --------------------------------------------------------------------

#
# Copy the target path to clipboard.
#
class CopyTargetPathMenuItem(AttributeViewContextMenuItem):
    def ShouldDisplay(self):
        return self._role in (PropertyViewDataRoles.TARGET, PropertyViewDataRoles.CONNECTION)

    def GetText(self):
        return "Copy Target Path As Text"

    def GetSelectedOfType(self):
        getRole = lambda s: s.data(PropertyViewIndex.TYPE, QtCore.Qt.ItemDataRole.WhatsThisRole)
        return [s for s in self._item.treeWidget().selectedItems() \
                    if getRole(s) in (PropertyViewDataRoles.TARGET, PropertyViewDataRoles.CONNECTION)]

    def RunCommand(self):
        if not self._item:
            return

        value = ", ".join([s.text(PropertyViewIndex.NAME) for s in self.GetSelectedOfType()])
        cb = QtWidgets.QApplication.clipboard()
        cb.setText(value, QtGui.QClipboard.Selection)
        cb.setText(value, QtGui.QClipboard.Clipboard)

#
# Jump to the target path in the prim browser
# This will include all other highlighted paths of this type, if any.
#
class SelectTargetPathMenuItem(CopyTargetPathMenuItem):
    def GetText(self):
        return "Select Target Path"

    def RunCommand(self):
        paths = [Sdf.Path(s.text(PropertyViewIndex.NAME))
            for s in self.GetSelectedOfType()]

        _selectPrimsAndProps(self._dataModel, paths)

# --------------------------------------------------------------------
# Target owning property selection menus
# --------------------------------------------------------------------

def _GetTargetPathsForItem(item):
    paths = [Sdf.Path(item.child(i).text(PropertyViewIndex.NAME)) for 
                 i in range (0, item.childCount())]
    # If there are no children, the value column must hold a valid path
    # (since the command is enabled).
    if len(paths) == 0:
        itemText = item.text(PropertyViewIndex.VALUE)
        if len(itemText) > 0:
            paths.append(Sdf.Path(itemText))
    return paths

#
# Select all target paths under the selected attribute
#
class SelectAllTargetPathsMenuItem(AttributeViewContextMenuItem):
    def ShouldDisplay(self):
        return (self._role == PropertyViewDataRoles.RELATIONSHIP_WITH_TARGETS
                or self._role == PropertyViewDataRoles.ATTRIBUTE_WITH_CONNNECTIONS)

    def IsEnabled(self):
        if not self._item:
            return False
        
        # Enable the menu item if there are one or more targets for this 
        # rel/attribute connection
        if self._item.childCount() > 0:
            return True

        # Enable the menu if the item holds a valid SdfPath.
        itemText = self._item.text(2)
        return Sdf.Path.IsValidPathString(itemText)

    def GetText(self):
        return "Select Target Path(s)"

    def RunCommand(self):
        if not self._item:
            return

        _selectPrimsAndProps(self._dataModel, 
                _GetTargetPathsForItem(self._item))

#
# Copy all target paths under the currently selected relationship to the clipboard
#
class CopyAllTargetPathsMenuItem(SelectAllTargetPathsMenuItem):
    def GetText(self):
        return "Copy Target Path(s) As Text"

    def RunCommand(self):
        if not self._item:
            return

        value = ", ".join(_GetTargetPathsForItem(self._item))
        
        cb = QtWidgets.QApplication.clipboard()
        cb.setText(value, QtGui.QClipboard.Selection)
        cb.setText(value, QtGui.QClipboard.Clipboard)

class ViewSplineMenuItem(AttributeViewContextMenuItem):
    _viewerByAttrPath = {}

    def ShouldDisplay(self):
        # Only show for attributes (not connections or targets)
        return self._role == PropertyViewDataRoles.ATTRIBUTE

    def IsEnabled(self):
        from pxr import Usd
        # Check if this attributes has a spline value source
        attr = self._getUsdAttribute()
        return (
            attr and 
            attr.GetResolveInfo().GetSource() == Usd.ResolveInfoSourceSpline)

    def GetText(self):
        return "View Spline"

    def RunCommand(self):
        from .splineViewer import SplineViewer
        attr = self._getUsdAttribute()
        if not attr:
            return

        # Activate existing spline viewer if one exists for this spline values
        # attribute
        pathStr = str(attr.GetPath())
        if viewer := self._viewerByAttrPath.get(pathStr):
            viewer.raise_()
            viewer.activateWindow()
            return

        # Show a minimal dialog box with info
        splineViewer = SplineViewer(
            attr, currentFrame = self._dataModel.currentFrame)
        splineViewer.setAttribute(QtCore.Qt.WA_DeleteOnClose)
        splineViewer.SetStartAndEndTime(
            self._dataModel.frameRangeBegin,
            self._dataModel.frameRangeEnd)
        # Store the viewer in a dictionary to keep it alive
        # until the user closes it
        self._viewerByAttrPath[pathStr] = splineViewer
        self._dataModel.currentFrameChanged.connect(
            splineViewer.SetCurrentFrame)
        self._dataModel.frameRangeChanged.connect(
            splineViewer.SetStartAndEndTime)
        # Remove the viewer from the dictionary when it is closed
        splineViewer.destroyed.connect(
            lambda: self._viewerByAttrPath.pop(pathStr, None))
        splineViewer.show()


    def _getUsdAttribute(self):
        stage = self._dataModel.stage
        if len(self._dataModel.selection.getPropPaths()) != 1:
            return None
        path = self._dataModel.selection.getPropPaths()[0]
        return stage.GetAttributeAtPath(path)
