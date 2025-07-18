#!/pxrpythonsubst
#
# Copyright 2019 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

from pxr import Sdf
from pxr import Usd
from pxr import UsdAppUtils
from pxr import UsdUtils

import argparse
import os
import platform
import sys
import unittest


class _NonExitingArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        # We override just this method so that we can avoid exiting and detect
        # error conditions.
        raise ValueError(message)

class TestUsdAppUtilsCmdlineArgs(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls._progName = os.path.basename(sys.argv[0])
        cls._noTimeCodeStage = Usd.Stage.Open(os.path.abspath('noTimeCode.usda'))
        cls._startTimeCodeStage = Usd.Stage.Open(os.path.abspath('startTimeCode.usda'))

    def setUp(self):
        self._parser = argparse.ArgumentParser(prog=self._progName)

    def testCameraCmdlineArgs(self):
        """
        Tests argument parsing when camera-related args are added.
        """
        UsdAppUtils.cameraArgs.AddCmdlineArgs(self._parser)

        # By default, the camera arg should contain None.
        args = self._parser.parse_args([])
        self.assertEqual(args.camera, None)

        args = self._parser.parse_args(['--camera', 'MainCamera'])
        self.assertEqual(type(args.camera), Sdf.Path)
        self.assertEqual(args.camera.pathString, 'MainCamera')

        args = self._parser.parse_args(['-cam', 'MainCamera'])
        self.assertEqual(type(args.camera), Sdf.Path)
        self.assertEqual(args.camera.pathString, 'MainCamera')

        args = self._parser.parse_args(['--camera', '/Path/To/Some/Camera'])
        self.assertEqual(type(args.camera), Sdf.Path)
        self.assertEqual(args.camera.pathString, '/Path/To/Some/Camera')

        args = self._parser.parse_args(['--camera', ''])
        self.assertEqual(type(args.camera), Sdf.Path)
        self.assertEqual(args.camera, Sdf.Path.emptyPath)

        # Test adding camera args with a different default value.
        parser = argparse.ArgumentParser(prog=self._progName)
        UsdAppUtils.cameraArgs.AddCmdlineArgs(parser,
            defaultValue='MainCamera')
        args = parser.parse_args([])
        self.assertEqual(type(args.camera), Sdf.Path)
        self.assertEqual(args.camera.pathString, 'MainCamera')

    def testColorCmdlineArgs(self):
        """
        Tests argument parsing when color-related args are added.
        """
        UsdAppUtils.colorArgs.AddCmdlineArgs(self._parser)

        args = self._parser.parse_args([])
        self.assertEqual(args.colorCorrectionMode, 'sRGB')

        args = self._parser.parse_args(['--colorCorrectionMode', 'disabled'])
        self.assertEqual(args.colorCorrectionMode, 'disabled')

        args = self._parser.parse_args(['-color', 'openColorIO'])
        self.assertEqual(args.colorCorrectionMode, 'openColorIO')

        # Test adding color args with a different default value.
        parser = argparse.ArgumentParser(prog=self._progName)
        UsdAppUtils.colorArgs.AddCmdlineArgs(parser, defaultValue='disabled')
        args = parser.parse_args([])
        self.assertEqual(args.colorCorrectionMode, 'disabled')

        # Test passing an invalid option.
        parser = _NonExitingArgumentParser(prog=self._progName)
        UsdAppUtils.colorArgs.AddCmdlineArgs(parser)
        with self.assertRaises(ValueError):
            args = parser.parse_args(['--colorCorrectionMode', 'bogus'])

    def testComplexityCmdlineArgs(self):
        """
        Tests argument parsing when complexity-related args are added.
        """
        UsdAppUtils.complexityArgs.AddCmdlineArgs(self._parser)

        args = self._parser.parse_args([])
        self.assertEqual(args.complexity,
            UsdAppUtils.complexityArgs.RefinementComplexities.LOW)

        args = self._parser.parse_args(['--complexity', 'medium'])
        self.assertEqual(args.complexity,
            UsdAppUtils.complexityArgs.RefinementComplexities.MEDIUM)

        args = self._parser.parse_args(['-c', 'high'])
        self.assertEqual(args.complexity,
            UsdAppUtils.complexityArgs.RefinementComplexities.HIGH)

        # Test adding complexity args with a different default value.
        parser = argparse.ArgumentParser(prog=self._progName)
        UsdAppUtils.complexityArgs.AddCmdlineArgs(parser,
            defaultValue='veryhigh')
        args = parser.parse_args([])
        self.assertEqual(args.complexity,
            UsdAppUtils.complexityArgs.RefinementComplexities.VERY_HIGH)

        # Test passing an invalid option.
        parser = _NonExitingArgumentParser(prog=self._progName)
        UsdAppUtils.complexityArgs.AddCmdlineArgs(parser)
        with self.assertRaises(ValueError):
            args = parser.parse_args(['--complexity', 'bogus'])

    def testFramesCmdlineArgs(self):
        """
        Tests argument parsing when frame-related args are added.
        """
        UsdAppUtils.framesArgs.AddCmdlineArgs(self._parser)

        args = self._parser.parse_args([])
        UsdAppUtils.framesArgs.ValidateCmdlineArgs(
            self._parser, args, self._noTimeCodeStage)
        self.assertEqual(args.frames, [0.0])

        args = self._parser.parse_args([])
        UsdAppUtils.framesArgs.ValidateCmdlineArgs(
            self._parser, args, self._startTimeCodeStage)
        self.assertEqual(args.frames, [5.0])

        args = self._parser.parse_args(['--defaultTime'])
        UsdAppUtils.framesArgs.ValidateCmdlineArgs(
            self._parser, args, self._noTimeCodeStage)
        self.assertEqual(args.frames, [Usd.TimeCode.Default()])

        args = self._parser.parse_args(['--d'])
        UsdAppUtils.framesArgs.ValidateCmdlineArgs(
            self._parser, args, self._noTimeCodeStage)
        self.assertEqual(args.frames, [Usd.TimeCode.Default()])

        args = self._parser.parse_args(['--frames', '1:4'])
        UsdAppUtils.framesArgs.ValidateCmdlineArgs(
            self._parser, args, self._noTimeCodeStage)
        self.assertEqual(type(args.frames),
            UsdAppUtils.framesArgs.FrameSpecIterator)
        self.assertEqual(list(args.frames),
            [Usd.TimeCode(1.0),
             Usd.TimeCode(2.0),
             Usd.TimeCode(3.0),
             Usd.TimeCode(4.0)])

        args = self._parser.parse_args(['-f', '11,13:15x2,17'])
        UsdAppUtils.framesArgs.ValidateCmdlineArgs(
            self._parser, args, self._noTimeCodeStage)
        self.assertEqual(type(args.frames),
            UsdAppUtils.framesArgs.FrameSpecIterator)
        self.assertEqual(list(args.frames),
            [Usd.TimeCode(11.0),
             Usd.TimeCode(13.0),
             Usd.TimeCode(15.0),
             Usd.TimeCode(17.0)])

        # Test that a frame format arg is correctly converted for use with
        # string.format().
        parser = argparse.ArgumentParser(prog=self._progName)
        UsdAppUtils.framesArgs.AddCmdlineArgs(parser)
        parser.add_argument('outputImagePath', action='store', type=str,
            help='output image path')
        args = parser.parse_args(['test_image.#.png', '--frames', '1:4'])
        UsdAppUtils.framesArgs.ValidateCmdlineArgs(parser, args, 
            self._noTimeCodeStage, frameFormatArgName='outputImagePath')
        self.assertEqual(args.outputImagePath, 'test_image.{frame:01.0f}.png')

        args = parser.parse_args(['test_image.###.#.png', '--frames', '1:4'])
        UsdAppUtils.framesArgs.ValidateCmdlineArgs(parser, args, 
            self._noTimeCodeStage, frameFormatArgName='outputImagePath')
        self.assertEqual(args.outputImagePath, 'test_image.{frame:05.1f}.png')

        # Test that an error is raised if the frame format arg does not contain
        # a frame placeholder when frames are given.
        parser = _NonExitingArgumentParser(prog=self._progName)
        UsdAppUtils.framesArgs.AddCmdlineArgs(parser)
        parser.add_argument('outputImagePath', action='store', type=str,
            help='output image path')
        args = parser.parse_args(['test_image.png', '--frames', '1:4'])
        with self.assertRaises(ValueError):
            UsdAppUtils.framesArgs.ValidateCmdlineArgs(parser, args,
                self._noTimeCodeStage, frameFormatArgName='outputImagePath')

        # Test that an error is raised if the frame format arg does contain
        # a frame placeholder but no frames are given.
        parser = _NonExitingArgumentParser(prog=self._progName)
        UsdAppUtils.framesArgs.AddCmdlineArgs(parser)
        parser.add_argument('outputImagePath', action='store', type=str,
            help='output image path')
        args = parser.parse_args(['test_image.#.png'])
        with self.assertRaises(ValueError):
            UsdAppUtils.framesArgs.ValidateCmdlineArgs(parser, args,
                self._noTimeCodeStage, frameFormatArgName='outputImagePath')

        args = parser.parse_args(['test_image.#.png', '--defaultTime'])
        with self.assertRaises(ValueError):
            UsdAppUtils.framesArgs.ValidateCmdlineArgs(parser, args,
                self._noTimeCodeStage, frameFormatArgName='outputImagePath')

        # Tests that an error is raised if the floating point precision
        # specified in the frame format arg is less than the minimum floating
        # point precision required for the given FrameSpecs.
        parser = _NonExitingArgumentParser(prog=self._progName)
        UsdAppUtils.framesArgs.AddCmdlineArgs(parser)
        parser.add_argument('outputImagePath', action='store', type=str,
            help='output image path')
        args = parser.parse_args(['test_image.#.png', '--frames', '1:4x0.1'])
        with self.assertRaises(ValueError):
            UsdAppUtils.framesArgs.ValidateCmdlineArgs(parser, args,
                self._noTimeCodeStage, frameFormatArgName='outputImagePath')
        args = parser.parse_args(['test_image.#.##.png', '--frames',
            '101:105x0.1,106:109x0.125,110:114x0.25'])
        with self.assertRaises(ValueError):
            UsdAppUtils.framesArgs.ValidateCmdlineArgs(parser, args,
                self._noTimeCodeStage, frameFormatArgName='outputImagePath')

    def testRendererCmdlineArgs(self):
        """
        Tests argument parsing when Hydra renderer-related args are added.
        """
        UsdAppUtils.rendererArgs.AddCmdlineArgs(self._parser)

        # No renderer plugin is specified by default.
        args = self._parser.parse_args([])
        self.assertEqual(args.rendererPlugin, None)

        args = self._parser.parse_args(['--renderer', 'GL'])
        self.assertEqual(args.rendererPlugin, 'GL')

        args = self._parser.parse_args(['-r', 'GL'])
        self.assertEqual(args.rendererPlugin, 'GL')

        # Arg "Storm" should alias to "GL"
        args = self._parser.parse_args(['--renderer', 'Storm'])
        self.assertEqual(args.rendererPlugin, 'GL')

        args = self._parser.parse_args(['-r', 'Storm'])
        self.assertEqual(args.rendererPlugin, 'GL')
   
        # Test passing an invalid option.
        parser = _NonExitingArgumentParser(prog=self._progName)
        UsdAppUtils.rendererArgs.AddCmdlineArgs(parser)
        with self.assertRaises(ValueError):
            args = parser.parse_args(['--renderer', 'bogus'])


if __name__ == "__main__":
    unittest.main()
