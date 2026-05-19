#!/pxrpythonsubst
#
# Copyright 2026 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

from pxr import Gf, Sdf, Ts, Usd, InvertibleRigsExample
import unittest

class TestSwitchCompensation(unittest.TestCase):

    def test_SetSplineKnot(self):
        """
        Test a convenience function that sets a knot on a spline.
        """

        layer = Sdf.Layer.CreateAnonymous()
        layer.ImportFromString(
            '''#usda 1.0
            def "Foo" {
                double x = 0
            }
            ''')
        stage = Usd.Stage.Open(layer)
        x = stage.GetAttributeAtPath('/Foo.x')
        assert x

        time = Usd.TimeCode(0.0)
        InvertibleRigsExample.Authoring.SetSplineKnot(x, time, 1.0);
        assert x.Get(time) == 1.0

        time = Usd.TimeCode(10.0)
        InvertibleRigsExample.Authoring.SetSplineKnot(x, time, 2.0);
        assert x.Get(time) == 2.0


    def _AssertAvarValues(time, avars, expectedValues):
        """
        Asserts that the values of the given avars at the given time are close
        to the given expected values.
        """

        for avar, expectedValue in zip(avars, expectedValues):
            actualValue = avar.Get(time)
            assert Gf.IsClose(actualValue, expectedValue, 1e-6), \
                f"For avar <{avar.GetPath()} {actualValue} != {expectedValue}"

    def test_CompensateSwitch(self):
        """
        Test compensation on a switch controller.
        """

        layer = Sdf.Layer.FindOrOpen("shot.usda")
        stage = Usd.Stage.Open(layer)

        switch = stage.GetAttributeAtPath('/Root/Control/Switch.switch')
        assert switch

        joint1 = stage.GetPrimAtPath('/Root/Anim/Joint1')
        joint2 = stage.GetPrimAtPath('/Root/Anim/Joint1/Joint2')
        assert joint1 and joint2

        ry1 = joint1.GetAttribute("avars:ry")
        ry2 = joint2.GetAttribute("avars:ry")
        assert ry1 and ry2

        inputAvarNames = (
            "avars:tx", "avars:ty", "avars:tz", 
            "avars:rx", "avars:ry", "avars:rz", "avars:rspin")
        inputAvars = [prim.GetAttribute(name)
                      for prim in (joint1, joint2)
                      for name in inputAvarNames]

        authoring = InvertibleRigsExample.Authoring(stage)

        # Start at time 0 with the switch set to 'rig1' and splines on all
        # input avars with 0 valued knots.
        currentTime = Usd.TimeCode(0.0)
        switch.Set('rig1', currentTime)
        spline = Ts.Spline("double")
        spline.SetKnot(Ts.Knot(time=0.0, value=0.0, nextInterp=Ts.InterpCurve))
        for avar in inputAvars:
            avar.SetSpline(spline)

        # Author animation on rotation avars and a series of compensated changes
        # to the switch attribute that make the rig tumble by alternately
        # pivoting about one end or the other.

        currentTime = Usd.TimeCode(10.0)
        InvertibleRigsExample.Authoring.SetSplineKnot(ry1, currentTime, 90.0)
        authoring.CompensateSwitch(switch, currentTime, 'rig2')

        TestSwitchCompensation._AssertAvarValues(
            currentTime, inputAvars,
            (0, 0, 0, 0, 0, 0, 0,
             10, 0, -10, 0, 90, 0, 0))

        currentTime = Usd.TimeCode(30.0)
        InvertibleRigsExample.Authoring.SetSplineKnot(ry2, currentTime, 270.0)
        authoring.CompensateSwitch(switch, currentTime, 'rig1')

        TestSwitchCompensation._AssertAvarValues(
            currentTime, inputAvars,
            (20, 0, 0, 0, -90, 0, 0,
             0, 0, 0, 0, 0, 0, 0))

        currentTime = Usd.TimeCode(50.0)
        InvertibleRigsExample.Authoring.SetSplineKnot(ry1, currentTime, 90.0)
        authoring.CompensateSwitch(switch, currentTime, 'rig2')
    
        TestSwitchCompensation._AssertAvarValues(
            currentTime, inputAvars,
            (0, 0, 0, 0, 0, 0, 0,
             30, 0, -10, 0, 90, 0, 0))

        currentTime = Usd.TimeCode(60.0)
        InvertibleRigsExample.Authoring.SetSplineKnot(ry2, currentTime, 180.0)

        layer.Export("result.usda")
        
if __name__ == "__main__":
    unittest.main()
