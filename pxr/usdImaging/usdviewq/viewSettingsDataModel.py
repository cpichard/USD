#
# Copyright 2018 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#

from .qt import QtCore
from pxr import UsdGeom, Sdf
from pxr.UsdAppUtils.complexityArgs import RefinementComplexities

from .common import (RenderModes, ColorCorrectionModes, PickModes, 
                     SelectionHighlightModes, CameraMaskModes, 
                     PrintWarning)

from . import settings
from .settings import StateSource
from pxr.UsdUtils.constantsGroup import ConstantsGroup
from .freeCamera import FreeCamera
from .common import ClearColors, HighlightColors


# Map of clear color names to rgba color tuples in linear space.
_CLEAR_COLORS_DICT = {
    ClearColors.BLACK:       (0.0, 0.0, 0.0, 1.0),
    ClearColors.DARK_GREY:   (0.07074, 0.07074, 0.07074, 1.0),
    ClearColors.LIGHT_GREY:  (0.45626, 0.45626, 0.45626, 1.0),
    ClearColors.WHITE:       (1.0, 1.0, 1.0, 1.0)}


# Map of highlight color names to rgba color tuples in linear space.
_HIGHLIGHT_COLORS_DICT = {
    HighlightColors.WHITE:   (1.0, 1.0, 1.0, 0.5),
    HighlightColors.YELLOW:  (1.0, 1.0, 0.0, 0.5),
    HighlightColors.CYAN:    (0.0, 1.0, 1.0, 0.5)}


# Default values for default material components.
DEFAULT_AMBIENT = 0.2
DEFAULT_SPECULAR = 0.1


def visibleViewSetting(f):
    def wrapper(self, *args, **kwargs):
        f(self, *args, **kwargs)
        # If f raises an exception, the signal is not emitted.
        self.signalVisibleSettingChanged.emit()
        self.signalSettingChanged.emit()
    return wrapper


def invisibleViewSetting(f):
    def wrapper(self, *args, **kwargs):
        f(self, *args, **kwargs)
        # If f raises an exception, the signal is not emitted.
        self.signalSettingChanged.emit()
    return wrapper


def freeCameraViewSetting(f):
    def wrapper(self, *args, **kwargs):
        f(self, *args, **kwargs)
        # If f raises an exception, the signal is not emitted.
        self.signalFreeCameraSettingChanged.emit()
        self.signalSettingChanged.emit()
    return wrapper


class OCIOSettings():
    """Class to hold OCIO display, view, and colorSpace config settings
    as strings."""

    def __init__(self, display="", view="", colorSpace=""):
        self._display = display
        self._view = view
        self._colorSpace = colorSpace

    @property
    def display(self):
        return self._display

    @property
    def view(self):
        return self._view

    @property
    def colorSpace(self):
        return self._colorSpace

class ViewSettingsDataModel(StateSource, QtCore.QObject):
    """Data model containing settings related to the rendered view of a USD
    file.
    """

    # emitted when any view setting changes
    signalSettingChanged = QtCore.Signal()

    # emitted when any view setting which may affect the rendered image changes
    signalVisibleSettingChanged = QtCore.Signal()

    # emitted when any view setting that affects the free camera changes. This
    # signal allows clients to switch to the free camera whenever its settings
    # are modified. Some operations may cause this signal to be emitted multiple
    # times.
    signalFreeCameraSettingChanged = QtCore.Signal()

    # emitted when autoClipping changes value, so that clients can initialize
    # it efficiently.  This signal will be emitted *before* 
    # signalVisibleSettingChanged when autoClipping changes.
    signalAutoComputeClippingPlanesChanged = QtCore.Signal()

    # emitted when any aspect of the defaultMaterial changes
    signalDefaultMaterialChanged = QtCore.Signal()

    # emitted when any setting affecting the GUI style changes
    signalStyleSettingsChanged = QtCore.Signal()

    def __init__(self, rootDataModel, parent):
        QtCore.QObject.__init__(self)
        StateSource.__init__(self, parent, "model")

        self._rootDataModel = rootDataModel

        self._cameraMaskColor = tuple(self.stateProperty("cameraMaskColor", default=[0.1, 0.1, 0.1, 1.0]))
        self._cameraReticlesColor = tuple(self.stateProperty("cameraReticlesColor", default=[0.0, 0.7, 1.0, 1.0]))
        self._defaultMaterialAmbient = self.stateProperty("defaultMaterialAmbient", default=DEFAULT_AMBIENT)
        self._defaultMaterialSpecular = self.stateProperty("defaultMaterialSpecular", default=DEFAULT_SPECULAR)
        self._redrawOnScrub = self.stateProperty("redrawOnScrub", default=True)
        self._stepSize = self.stateProperty("stepSize", default=1.0)
        self._renderMode = self.stateProperty("renderMode", default=RenderModes.SMOOTH_SHADED)
        self._freeCameraFOV = self.stateProperty("freeCameraFOV", default=60.0)
        self._freeCameraAspect = self.stateProperty("freeCameraAspect", default=1.0)
        # For freeCameraOverrideNear/Far, Use -inf as a sentinel value to mean
        # None. (We cannot use None directly because that would cause a type-
        # checking error in Settings.)
        self._clippingPlaneNoneValue = float('-inf')
        self._freeCameraOverrideNear = self.stateProperty("freeCameraOverrideNear", default=self._clippingPlaneNoneValue)
        self._freeCameraOverrideFar = self.stateProperty("freeCameraOverrideFar", default=self._clippingPlaneNoneValue)
        if self._freeCameraOverrideNear == self._clippingPlaneNoneValue:
            self._freeCameraOverrideNear = None
        if self._freeCameraOverrideFar == self._clippingPlaneNoneValue:
            self._freeCameraOverrideFar = None
        self._lockFreeCameraAspect = self.stateProperty("lockFreeCameraAspect", default=False)
        self._colorCorrectionMode = self.stateProperty("colorCorrectionMode", default=ColorCorrectionModes.SRGB)
        self._ocioSettings = OCIOSettings()
        self._pickMode = self.stateProperty("pickMode", default=PickModes.PRIMS)

        # We need to store the trinary selHighlightMode state here,
        # because the stageView only deals in True/False (because it
        # cannot know anything about playback state).
        self._selHighlightMode = self.stateProperty("selectionHighlightMode", default=SelectionHighlightModes.ONLY_WHEN_PAUSED)

        # We store the highlightColorName so that we can compare state during
        # initialization without inverting the name->value logic
        self._highlightColorName = self.stateProperty("highlightColor", default="Yellow")
        self._ambientLightOnly = self.stateProperty("cameraLightEnabled", default=True)
        self._domeLightEnabled = self.stateProperty("domeLightEnabled", default=False)
        self._domeLightTexturesVisible = self.stateProperty("domeLightTexturesVisible", default=True)
        self._clearColorText = self.stateProperty("backgroundColor", default="Grey (Dark)")
        self._autoComputeClippingPlanes = self.stateProperty("autoComputeClippingPlanes", default=False)
        self._showBBoxPlayback = self.stateProperty("showBBoxesDuringPlayback", default=False)
        self._showBBoxes = self.stateProperty("showBBoxes", default=True)
        self._showAABBox = self.stateProperty("showAABBox", default=True)
        self._showOBBox = self.stateProperty("showOBBox", default=True)
        self._displayGuide = self.stateProperty("displayGuide", default=False)
        self._displayProxy = self.stateProperty("displayProxy", default=True)
        self._displayRender = self.stateProperty("displayRender", default=False)
        self._enableSceneMaterials = self.stateProperty("enableSceneMaterials", default=True)
        self._enableSceneLights = self.stateProperty("enableSceneLights", default=True)
        self._cullBackfaces = self.stateProperty("cullBackfaces", default=False)
        self._showInactivePrims = self.stateProperty("showInactivePrims", default=True)

        showAllMasterPrims = self.stateProperty("showAllMasterPrims", default=False)
        self._showAllPrototypePrims = self.stateProperty("showAllPrototypePrims", default=False)
        # XXX: For backwards compatibility, we use the "showAllMasterPrims"
        # saved state to drive the new "showAllPrototypePrims" state. We
        # can remove "showAllMasterPrims" in a later release.
        self._showAllPrototypePrims = showAllMasterPrims

        self._showUndefinedPrims = self.stateProperty("showUndefinedPrims", default=False)
        self._showAbstractPrims = self.stateProperty("showAbstractPrims", default=False)
        self._showPrimDisplayNames = self.stateProperty("showPrimDisplayNames", default=True)
        self._rolloverPrimInfo = self.stateProperty("rolloverPrimInfo", default=False)
        self._displayCameraOracles = self.stateProperty("cameraOracles", default=False)
        self._cameraMaskMode = self.stateProperty("cameraMaskMode", default=CameraMaskModes.NONE)
        self._showMask_Outline = self.stateProperty("cameraMaskOutline", default=False)
        self._showReticles_Inside = self.stateProperty("cameraReticlesInside", default=False)
        self._showReticles_Outside = self.stateProperty("cameraReticlesOutside", default=False)
        self._showHUD = self.stateProperty("showHUD", default=True)

        self._showHUD_Info = self.stateProperty("showHUDInfo", default=False)
        # XXX Until we can make the "Subtree Info" stats-gathering faster
        # we do not want the setting to persist from session to session.
        self._showHUD_Info = False

        self._showHUD_Complexity = self.stateProperty("showHUDComplexity", default=True)
        self._showHUD_Performance = self.stateProperty("showHUDPerformance", default=True)
        self._showHUD_GPUstats = self.stateProperty("showHUDGPUStats", default=False)

        self._complexity = RefinementComplexities.LOW
        self._freeCamera = None
        self._cameraPath = None
        self._fontSize = self.stateProperty("fontSize", default=10)

    def onSaveState(self, state):
        state["cameraMaskColor"] = list(self._cameraMaskColor)
        state["cameraReticlesColor"] = list(self._cameraReticlesColor)
        state["defaultMaterialAmbient"] = self._defaultMaterialAmbient
        state["defaultMaterialSpecular"] = self._defaultMaterialSpecular
        state["redrawOnScrub"] = self._redrawOnScrub
        state["stepSize"] = self._stepSize
        state["renderMode"] = self._renderMode
        state["freeCameraFOV"] = self._freeCameraFOV
        freeCameraOverrideNear = self._freeCameraOverrideNear
        if freeCameraOverrideNear is None:
            freeCameraOverrideNear = self._clippingPlaneNoneValue
        state["freeCameraOverrideNear"] = freeCameraOverrideNear
        freeCameraOverrideFar = self._freeCameraOverrideFar
        if freeCameraOverrideFar is None:
            freeCameraOverrideFar = self._clippingPlaneNoneValue
        state["freeCameraOverrideFar"] = freeCameraOverrideFar
        state["freeCameraAspect"] = self._freeCameraAspect
        state["lockFreeCameraAspect"] = self._lockFreeCameraAspect
        state["colorCorrectionMode"] = self._colorCorrectionMode
        state["pickMode"] = self._pickMode
        state["selectionHighlightMode"] = self._selHighlightMode
        state["highlightColor"] = self._highlightColorName
        state["cameraLightEnabled"] = self._ambientLightOnly
        state["domeLightEnabled"] = self._domeLightEnabled
        state["domeLightTexturesVisible"] = self._domeLightTexturesVisible
        state["backgroundColor"] = self._clearColorText
        state["autoComputeClippingPlanes"] = self._autoComputeClippingPlanes
        state["showBBoxesDuringPlayback"] = self._showBBoxPlayback
        state["showBBoxes"] = self._showBBoxes
        state["showAABBox"] = self._showAABBox
        state["showOBBox"] = self._showOBBox
        state["displayGuide"] = self._displayGuide
        state["displayProxy"] = self._displayProxy
        state["displayRender"] = self._displayRender
        state["enableSceneMaterials"] = self._enableSceneMaterials
        state["enableSceneLights"] = self._enableSceneLights
        state["cullBackfaces"] = self._cullBackfaces
        state["showInactivePrims"] = self._showInactivePrims
        state["showAllPrototypePrims"] = self._showAllPrototypePrims
        state["showAllMasterPrims"] = self._showAllPrototypePrims
        state["showUndefinedPrims"] = self._showUndefinedPrims
        state["showAbstractPrims"] = self._showAbstractPrims
        state["showPrimDisplayNames"] = self._showPrimDisplayNames
        state["rolloverPrimInfo"] = self._rolloverPrimInfo
        state["cameraOracles"] = self._displayCameraOracles
        state["cameraMaskMode"] = self._cameraMaskMode
        state["cameraMaskOutline"] = self._showMask_Outline
        state["cameraReticlesInside"] = self._showReticles_Inside
        state["cameraReticlesOutside"] = self._showReticles_Outside
        state["showHUD"] = self._showHUD
        state["showHUDInfo"] = self._showHUD_Info
        state["showHUDComplexity"] = self._showHUD_Complexity
        state["showHUDPerformance"] = self._showHUD_Performance
        state["showHUDGPUStats"] = self._showHUD_GPUstats
        state["fontSize"] = self._fontSize

    @property
    def cameraMaskColor(self):
        return self._cameraMaskColor

    @cameraMaskColor.setter
    @visibleViewSetting
    def cameraMaskColor(self, color):
        self._cameraMaskColor = color

    @property
    def cameraReticlesColor(self):
        return self._cameraReticlesColor

    @cameraReticlesColor.setter
    @visibleViewSetting
    def cameraReticlesColor(self, color):
        self._cameraReticlesColor = color

    @property
    def defaultMaterialAmbient(self):
        return self._defaultMaterialAmbient

    @defaultMaterialAmbient.setter
    @visibleViewSetting
    def defaultMaterialAmbient(self, value):
        if value != self._defaultMaterialAmbient:
            self._defaultMaterialAmbient = value
            self.signalDefaultMaterialChanged.emit()

    @property
    def defaultMaterialSpecular(self):
        return self._defaultMaterialSpecular

    @defaultMaterialSpecular.setter
    @visibleViewSetting
    def defaultMaterialSpecular(self, value):
        if value != self._defaultMaterialSpecular:
            self._defaultMaterialSpecular = value
            self.signalDefaultMaterialChanged.emit()

    @visibleViewSetting
    def setDefaultMaterial(self, ambient, specular):
        if (ambient != self._defaultMaterialAmbient
                or specular != self._defaultMaterialSpecular):
            self._defaultMaterialAmbient = ambient
            self._defaultMaterialSpecular = specular
            self.signalDefaultMaterialChanged.emit()

    def resetDefaultMaterial(self):
        self.setDefaultMaterial(DEFAULT_AMBIENT, DEFAULT_SPECULAR)

    @property
    def complexity(self):
        return self._complexity

    @complexity.setter
    @visibleViewSetting
    def complexity(self, value):
        if value not in RefinementComplexities.ordered():
            raise ValueError("Expected Complexity, got: '{}'.".format(value))
        self._complexity = value

    @property
    def renderMode(self):
        return self._renderMode

    @renderMode.setter
    @visibleViewSetting
    def renderMode(self, value):
        self._renderMode = value

    @property
    def freeCameraFOV(self):
        return self._freeCameraFOV

    @freeCameraFOV.setter
    @freeCameraViewSetting
    def freeCameraFOV(self, value):
        if self._freeCamera:
            # Setting the freeCamera's fov will trigger our own update
            self._freeCamera.fov = value
        else:
            self._freeCameraFOV = value

    @property
    def freeCameraOverrideNear(self):
        """Returns the free camera's near clipping plane value, if it has been
        overridden by the user. Returns None if there is no user-defined near
        clipping plane."""
        return self._freeCameraOverrideNear

    @freeCameraOverrideNear.setter
    @freeCameraViewSetting
    def freeCameraOverrideNear(self, value):
        """Sets the near clipping plane to the given value. Passing in None will
        clear the current override."""
        self._freeCameraOverrideNear = value
        if self._freeCamera:
            self._freeCamera.overrideNear = value

    @property
    def freeCameraOverrideFar(self):
        """Returns the free camera's far clipping plane value, if it has been
        overridden by the user. Returns None if there is no user-defined far
        clipping plane."""
        return self._freeCameraOverrideFar

    @freeCameraOverrideFar.setter
    @freeCameraViewSetting
    def freeCameraOverrideFar(self, value):
        """Sets the far clipping plane to the given value. Passing in None will
        clear the current override."""
        self._freeCameraOverrideFar = value
        if self._freeCamera:
            self._freeCamera.overrideFar = value

    @property
    def freeCameraAspect(self):
        return self._freeCameraAspect
    
    @freeCameraAspect.setter
    @freeCameraViewSetting
    def freeCameraAspect(self, value):
        if self._freeCamera:
            # Setting the freeCamera's aspect ratio will trigger our own update
            self._freeCamera.aspectRatio = value
        else:
            self._freeCameraAspect = value

    def _frustumChanged(self):
        """
        Needed when updating any camera setting (including movements). Will not
        update the property viewer.
        """
        self.signalFreeCameraSettingChanged.emit()

    def _frustumSettingsChanged(self):
        """
        Needed when updating specific camera settings (e.g., aperture). See
        _updateFreeCameraData for the full list of dependent settings. Will
        update the property viewer.
        """
        self._updateFreeCameraData()
        self.signalSettingChanged.emit()

    def _updateFreeCameraData(self):
        '''Updates member variables with the current free camera view settings.
        '''
        if self._freeCamera:
            self._freeCameraFOV = self.freeCamera.fov
            self._freeCameraOverrideNear = self.freeCamera.overrideNear
            self._freeCameraOverrideFar = self.freeCamera.overrideFar
            if self._lockFreeCameraAspect:
                self._freeCameraAspect = self.freeCamera.aspectRatio

    @property
    def lockFreeCameraAspect(self):
        return self._lockFreeCameraAspect
    
    @lockFreeCameraAspect.setter
    @visibleViewSetting
    def lockFreeCameraAspect(self, value):
        self._lockFreeCameraAspect = value

        if value and not self.showMask:
            # Make sure the camera mask is turned on so the locked aspect ratio
            # is visible in the viewport.
            self.cameraMaskMode = CameraMaskModes.FULL

    @property
    def colorCorrectionMode(self):
        return self._colorCorrectionMode

    @colorCorrectionMode.setter
    @visibleViewSetting
    def colorCorrectionMode(self, value):
        self._colorCorrectionMode = value

    @property
    def ocioSettings(self):
        return self._ocioSettings

    @visibleViewSetting
    def setOcioSettings(self, colorSpace="", display="", view=""):
        """Specifies the OCIO settings to be used. Setting the OCIO 'display'
           requires a 'view' to be specified."""

        if colorSpace:
            self._ocioSettings._colorSpace = colorSpace

        if display:
            if view:
                self._ocioSettings._display = display
                self._ocioSettings._view = view
            else:
                PrintWarning("Cannot set a OCIO display without a view."\
                             "Using default settings instead.")
                self._ocioSettings._display = ""
                self._ocioSettings._view = ""

    @property
    def pickMode(self):
        return self._pickMode

    @pickMode.setter
    @invisibleViewSetting
    def pickMode(self, value):
        self._pickMode = value

    @property
    def showAABBox(self):
        return self._showAABBox

    @showAABBox.setter
    @visibleViewSetting
    def showAABBox(self, value):
        self._showAABBox = value

    @property
    def showOBBox(self):
        return self._showOBBox

    @showOBBox.setter
    @visibleViewSetting
    def showOBBox(self, value):
        self._showOBBox = value

    @property
    def showBBoxes(self):
        return self._showBBoxes

    @showBBoxes.setter
    @visibleViewSetting
    def showBBoxes(self, value):
        self._showBBoxes = value

    @property
    def autoComputeClippingPlanes(self):
        return self._autoComputeClippingPlanes

    @autoComputeClippingPlanes.setter
    @visibleViewSetting
    def autoComputeClippingPlanes(self, value):
        self._autoComputeClippingPlanes = value
        self.signalAutoComputeClippingPlanesChanged.emit()

    @property
    def showBBoxPlayback(self):
        return self._showBBoxPlayback

    @showBBoxPlayback.setter
    @visibleViewSetting
    def showBBoxPlayback(self, value):
        self._showBBoxPlayback = value

    @property
    def displayGuide(self):
        return self._displayGuide

    @displayGuide.setter
    @visibleViewSetting
    def displayGuide(self, value):
        self._displayGuide = value

    @property
    def displayProxy(self):
        return self._displayProxy

    @displayProxy.setter
    @visibleViewSetting
    def displayProxy(self, value):
        self._displayProxy = value

    @property
    def displayRender(self):
        return self._displayRender

    @displayRender.setter
    @visibleViewSetting
    def displayRender(self, value):
        self._displayRender = value

    @property
    def displayCameraOracles(self):
        return self._displayCameraOracles

    @displayCameraOracles.setter
    @visibleViewSetting
    def displayCameraOracles(self, value):
        self._displayCameraOracles = value

    @property
    def enableSceneMaterials(self):
        return self._enableSceneMaterials

    @enableSceneMaterials.setter
    @visibleViewSetting
    def enableSceneMaterials(self, value):
        self._enableSceneMaterials = value

    @property
    def enableSceneLights(self):
        return self._enableSceneLights

    @enableSceneLights.setter
    @visibleViewSetting
    def enableSceneLights(self, value):
        self._enableSceneLights = value

    @property
    def cullBackfaces(self):
        return self._cullBackfaces

    @cullBackfaces.setter
    @visibleViewSetting
    def cullBackfaces(self, value):
        self._cullBackfaces = value

    @property
    def showInactivePrims(self):
        return self._showInactivePrims

    @showInactivePrims.setter
    @invisibleViewSetting
    def showInactivePrims(self, value):
        self._showInactivePrims = value

    @property
    def showAllPrototypePrims(self):
        return self._showAllPrototypePrims

    @showAllPrototypePrims.setter
    @invisibleViewSetting
    def showAllPrototypePrims(self, value):
        self._showAllPrototypePrims = value

    @property
    def showUndefinedPrims(self):
        return self._showUndefinedPrims

    @showUndefinedPrims.setter
    @invisibleViewSetting
    def showUndefinedPrims(self, value):
        self._showUndefinedPrims = value

    @property
    def showAbstractPrims(self):
        return self._showAbstractPrims

    @showAbstractPrims.setter
    @invisibleViewSetting
    def showAbstractPrims(self, value):
        self._showAbstractPrims = value

    @property
    def showPrimDisplayNames(self):
        return self._showPrimDisplayNames

    @showPrimDisplayNames.setter
    @invisibleViewSetting
    def showPrimDisplayNames(self, value):
        self._showPrimDisplayNames = value

    @property
    def rolloverPrimInfo(self):
        return self._rolloverPrimInfo

    @rolloverPrimInfo.setter
    @invisibleViewSetting
    def rolloverPrimInfo(self, value):
        self._rolloverPrimInfo = value

    @property
    def cameraMaskMode(self):
        return self._cameraMaskMode

    @cameraMaskMode.setter
    @visibleViewSetting
    def cameraMaskMode(self, value):
        self._cameraMaskMode = value

    @property
    def showMask(self):
        return self._cameraMaskMode in (CameraMaskModes.FULL, CameraMaskModes.PARTIAL)

    @property
    def showMask_Opaque(self):
        return self._cameraMaskMode == CameraMaskModes.FULL

    @property
    def showMask_Outline(self):
        return self._showMask_Outline

    @showMask_Outline.setter
    @visibleViewSetting
    def showMask_Outline(self, value):
        self._showMask_Outline = value

    @property
    def showReticles_Inside(self):
        return self._showReticles_Inside

    @showReticles_Inside.setter
    @visibleViewSetting
    def showReticles_Inside(self, value):
        self._showReticles_Inside = value

    @property
    def showReticles_Outside(self):
        return self._showReticles_Outside

    @showReticles_Outside.setter
    @visibleViewSetting
    def showReticles_Outside(self, value):
        self._showReticles_Outside = value

    @property
    def showHUD(self):
        return self._showHUD

    @showHUD.setter
    @visibleViewSetting
    def showHUD(self, value):
        self._showHUD = value

    @property
    def showHUD_Info(self):
        return self._showHUD_Info

    @showHUD_Info.setter
    @visibleViewSetting
    def showHUD_Info(self, value):
        self._showHUD_Info = value

    @property
    def showHUD_Complexity(self):
        return self._showHUD_Complexity

    @showHUD_Complexity.setter
    @visibleViewSetting
    def showHUD_Complexity(self, value):
        self._showHUD_Complexity = value

    @property
    def showHUD_Performance(self):
        return self._showHUD_Performance

    @showHUD_Performance.setter
    @visibleViewSetting
    def showHUD_Performance(self, value):
        self._showHUD_Performance = value

    @property
    def showHUD_GPUstats(self):
        return self._showHUD_GPUstats

    @showHUD_GPUstats.setter
    @visibleViewSetting
    def showHUD_GPUstats(self, value):
        self._showHUD_GPUstats = value

    @property
    def ambientLightOnly(self):
        return self._ambientLightOnly

    @ambientLightOnly.setter
    @visibleViewSetting
    def ambientLightOnly(self, value):
        self._ambientLightOnly = value

    @property
    def domeLightEnabled(self):
        return self._domeLightEnabled

    @domeLightEnabled.setter
    @visibleViewSetting
    def domeLightEnabled(self, value):
        self._domeLightEnabled = value

    @property
    def domeLightTexturesVisible(self):
        return self._domeLightTexturesVisible

    @domeLightTexturesVisible.setter
    @visibleViewSetting
    def domeLightTexturesVisible(self, value):
        self._domeLightTexturesVisible = value

    @property
    def clearColorText(self):
        return self._clearColorText

    @clearColorText.setter
    @visibleViewSetting
    def clearColorText(self, value):
        if value not in ClearColors:
            raise ValueError("Unknown clear color: '{}'".format(value))
        self._clearColorText = value

    @property
    def clearColor(self):
        return _CLEAR_COLORS_DICT[self._clearColorText]

    @property
    def highlightColorName(self):
        return self._highlightColorName

    @highlightColorName.setter
    @visibleViewSetting
    def highlightColorName(self, value):
        if value not in HighlightColors:
            raise ValueError("Unknown highlight color: '{}'".format(value))
        self._highlightColorName = value

    @property
    def highlightColor(self):
        return _HIGHLIGHT_COLORS_DICT[self._highlightColorName]

    @property
    def selHighlightMode(self):
        return self._selHighlightMode

    @selHighlightMode.setter
    @visibleViewSetting
    def selHighlightMode(self, value):
        if value not in SelectionHighlightModes:
            raise ValueError("Unknown highlight mode: '{}'".format(value))
        self._selHighlightMode = value

    @property
    def redrawOnScrub(self):
        return self._redrawOnScrub

    @redrawOnScrub.setter
    @visibleViewSetting
    def redrawOnScrub(self, value):
        self._redrawOnScrub = value

    @property
    def stepSize(self):
        return self._stepSize

    @stepSize.setter
    @visibleViewSetting
    def stepSize(self, value):
        self._stepSize = value

    @property
    def freeCamera(self):
        return self._freeCamera

    @freeCamera.setter
    @visibleViewSetting
    def freeCamera(self, value):
        # ViewSettingsDataModel does not guarantee it will hold a valid
        # FreeCamera, but if one is set, we will keep the dataModel's stateful
        # free camera settings (fov, clipping planes, aspect ratio) in sync with
        # the FreeCamera

        if not isinstance(value, FreeCamera) and value != None:
            raise TypeError("Free camera must be a FreeCamera object.")
        if self._freeCamera:
            self._freeCamera.signalFrustumChanged.disconnect(
                self._frustumChanged)
            self._freeCamera.signalFrustumSettingsChanged.disconnect(
                self._frustumSettingsChanged)
        self._freeCamera = value
        if self._freeCamera:
            self._freeCamera.signalFrustumChanged.connect(
                self._frustumChanged)
            self._freeCamera.signalFrustumSettingsChanged.connect(
                self._frustumSettingsChanged)
            self._updateFreeCameraData()

    @property
    def cameraPath(self):
        return self._cameraPath

    @cameraPath.setter
    @visibleViewSetting
    def cameraPath(self, value):
        if ((not isinstance(value, Sdf.Path) or not value.IsPrimPath())
                and value is not None):
            raise TypeError("Expected prim path, got: {}".format(value))
        self._cameraPath = value

    @property
    def cameraPrim(self):
        if self.cameraPath is not None and self._rootDataModel.stage is not None:
            return self._rootDataModel.stage.GetPrimAtPath(self.cameraPath)
        else:
            return None

    @cameraPrim.setter
    def cameraPrim(self, value):
        if value is not None:
            if value.IsA(UsdGeom.Camera):
                self.cameraPath = value.GetPrimPath()
            else:
                PrintWarning("Incorrect Prim Type",
                    "Attempted to view the scene using the prim '%s', but "
                    "the prim is not a UsdGeom.Camera." % (value.GetName()))
        else:
            self.cameraPath = None

    @property
    def fontSize(self):
        return self._fontSize

    @fontSize.setter
    @visibleViewSetting
    def fontSize(self, value):
        if value != self._fontSize:
            self._fontSize = value
            self.signalStyleSettingsChanged.emit()

