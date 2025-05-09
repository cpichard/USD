#!/pxrpythonsubst
#
# Copyright 2017 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

import shutil, os, unittest
from pxr import Sdf, Usd, Tf

def DiffLayers(layer1, layer2):
    assert layer1.ExportToString() == layer2.ExportToString()

############################################################
# Utility functions for determining the underlying file format of
# a .usd layer.
#
# XXX: These all rely on the given layer being saved on disk. WBN and would
#      simplify some test code a bit if that weren't the case.

def VerifyUsdFileFormat(usdFileName, underlyingFormatId):
    """In order to verify the underlying file format of the given .usd file,
    we're going to try to load the file but use an extension override to force
    the use of the expected underlying file format. If the file fails to load,
    we'll know it wasn't the right format.
    """

    # Copy the file to a temporary location and replace the extension with
    # the expected format, then try to open it.
    import tempfile
    (tmpFile, tmpFileName) = \
        tempfile.mkstemp(
            prefix=os.path.splitext(os.path.basename(usdFileName))[0],
            suffix='.' + underlyingFormatId)
    os.close(tmpFile)

    shutil.copyfile(usdFileName, tmpFileName)

    assert not Sdf.Layer.Find(tmpFileName)
    assert not Sdf.Layer.Find(usdFileName)
    layer = Sdf.Layer.FindOrOpen(tmpFileName)
    assert layer

    # NOTE: We want to delete tmpFileName but layer may have it open.
    #       On Windows the delete will fail if the file is open.
    #       Explicitly release our reference to the layer to close
    #       the file.
    del layer

    os.unlink(tmpFileName)

def VerifyUsdFileIsASCII(usdFileName):
    VerifyUsdFileFormat(usdFileName, 'usda')

def VerifyUsdFileIsCrate(usdFileName):
    VerifyUsdFileFormat(usdFileName, 'usdc')

############################################################

class TestUsdFileFormats(unittest.TestCase):
    def test_NewUsdLayer(self):
        def _TestNewLayer(layer, expectedFileFormat):
            """Check that the new layer was created successfully and its file
            format is what we expect."""
            self.assertTrue(layer)
            self.assertEqual(layer.GetFileFormat(), Sdf.FileFormat.FindById('usd'))

            # Write something to the layer to ensure it's functional.
            primSpec = Sdf.PrimSpec(layer, 'Scope', Sdf.SpecifierDef, 'Scope')
            self.assertTrue(primSpec)

            # Ensure the layer is saved to disk, then check that its
            # underlying file format matches what we expect.
            self.assertTrue(layer.Save())
            self.assertEqual(
                Sdf.UsdFileFormat.GetUnderlyingFormatForLayer(layer),
                expectedFileFormat)

        usdFileFormat = Sdf.FileFormat.FindById('usd')

        # Newly-created .usd layers default to the USD_DEFAULT_FILE_FORMAT format.
        _TestNewLayer(Sdf.Layer.CreateNew('testNewUsdLayer.usd'),
                      Tf.GetEnvSetting('USD_DEFAULT_FILE_FORMAT'))
        _TestNewLayer(Sdf.Layer.New(usdFileFormat, 'testNewUsdLayer_2.usd'),
                      Tf.GetEnvSetting('USD_DEFAULT_FILE_FORMAT'))

        # Verify that file format arguments can be used to control the
        # underlying file format for new .usd layers.
        _TestNewLayer(Sdf.Layer.CreateNew('testNewUsdLayer_text.usd',
                                         args={'format':'usda'}), 'usda')
        _TestNewLayer(Sdf.Layer.New(usdFileFormat, 'testNewUsdLayer_text_2.usd',
                                   args={'format':'usda'}), 'usda')

        _TestNewLayer(Sdf.Layer.CreateNew('testNewUsdLayer_crate.usd',
                                         args={'format':'usdc'}), 'usdc')
        _TestNewLayer(Sdf.Layer.New(usdFileFormat, 'testNewUsdLayer_crate_2.usd',
                                   args={'format':'usdc'}), 'usdc')
        
    def test_UsdLayerSave(self):
        # Verify that opening a .usd file, authoring changes, and saving it
        # maintains the same underlying file format.
        def _TestLayerOpenAndSave(srcFilename, expectedFileFormat):
            dstFilename = 'testUsdLayerSave_%s.usd' % expectedFileFormat
            shutil.copyfile(srcFilename, dstFilename)
            layer = Sdf.Layer.FindOrOpen(dstFilename)
            self.assertEqual(
                Sdf.UsdFileFormat.GetUnderlyingFormatForLayer(layer),
                expectedFileFormat)

            Sdf.PrimSpec(layer, 'TestUsdLayerSave', Sdf.SpecifierDef)
            self.assertTrue(layer.Save())

            self.assertEqual(
                Sdf.UsdFileFormat.GetUnderlyingFormatForLayer(layer),
                expectedFileFormat)

        asciiFile = 'ascii.usd'
        _TestLayerOpenAndSave(asciiFile, 'usda')

        crateFile = 'crate.usd'
        _TestLayerOpenAndSave(crateFile, 'usdc')

    def test_UsdLayerExport(self):
        def _TestExport(layer):
            self.assertTrue(layer)

            exportedLayerId = os.path.basename(
                layer.identifier).replace('.usd', '_exported.usd')
            self.assertTrue(layer.Export(exportedLayerId))

            exportedLayer = Sdf.Layer.FindOrOpen(exportedLayerId)
            self.assertTrue(exportedLayer)

            # Exporting to a new .usd file should produce a binary file,
            # since that's the default format for .usd.
            self.assertEqual(
                Sdf.UsdFileFormat.GetUnderlyingFormatForLayer(exportedLayer), 
                "usdc")

            # The contents of the exported layer should equal to the
            # contents of the original layer.
            DiffLayers(layer, exportedLayer)

        # Write some content to the layer for testing purposes.
        newLayer = Sdf.Layer.CreateNew('testUsdLayerExport.usd')
        primSpec = Sdf.PrimSpec(newLayer, 'Scope', Sdf.SpecifierDef, 'Scope')
        newLayer.Save()
        _TestExport(newLayer)

        asciiFile = 'ascii.usd'
        _TestExport(Sdf.Layer.FindOrOpen(asciiFile))

        crateFile = 'crate.usd'
        _TestExport(Sdf.Layer.FindOrOpen(crateFile))

    def test_LoadASCIIUsdLayer(self):
        asciiFile = 'ascii.usd'
        VerifyUsdFileIsASCII(asciiFile)

        l = Sdf.Layer.FindOrOpen(asciiFile)
        self.assertTrue(l)

    def test_LoadCrateUsdLayer(self):
        crateFile = 'crate.usd'
        VerifyUsdFileIsCrate(crateFile)

        l = Sdf.Layer.FindOrOpen(crateFile)
        self.assertTrue(l)

    def test_UsdLayerString(self):
        # Verify that we get non-empty string representations for binary, text, and
        # crate .usd layers.
        asciiFile = 'ascii.usd'
        asciiLayer = Sdf.Layer.FindOrOpen(asciiFile)
        asciiLayerAsString = asciiLayer.ExportToString()
        self.assertTrue(len(asciiLayerAsString) > 0)

        crateFile = 'crate.usd'
        crateLayer = Sdf.Layer.FindOrOpen(crateFile)
        crateLayerAsString = crateLayer.ExportToString()
        self.assertTrue(len(crateLayerAsString) > 0)

        # All .usd layers use the same string representation as a text .usd layer,
        # so these should match.
        self.assertEqual(crateLayerAsString, asciiLayerAsString)

        # Verify that we can import the string representation of a .usd
        # layer into another .usd layer.
        importLayer = Sdf.Layer.CreateNew('testUsdLayerString.usdc')
        self.assertTrue(importLayer.ImportFromString(asciiLayerAsString))
        DiffLayers(asciiLayer, importLayer)

    def test_BadFileName(self):
        """Previously, failing to open a layer would assert-fail in C++/Usd, this
        test makes sure it is a recoverable error."""

        with self.assertRaises(Tf.ErrorException):
            stage = Usd.Stage.Open("badFileName.usd")

    def test_UsdcRepeatedSaves(self):
        """Repeated saves used to fail due to an fopen mode bug."""
        s = Usd.Stage.CreateNew('testUsdcRepeatedSaves.usdc')
        s.DefinePrim('/foo')
        s.GetRootLayer().Save()
        s.DefinePrim('/foo/bar/baz')
        assert s.GetPrimAtPath('/foo')
        assert s.GetPrimAtPath('/foo/bar')
        assert s.GetPrimAtPath('/foo/bar/baz')
        s.GetRootLayer().Save()
        assert s.GetPrimAtPath('/foo')
        assert s.GetPrimAtPath('/foo/bar')
        assert s.GetPrimAtPath('/foo/bar/baz')
        s.GetRootLayer().Save(force=True)
        assert s.GetPrimAtPath('/foo')
        assert s.GetPrimAtPath('/foo/bar')
        assert s.GetPrimAtPath('/foo/bar/baz')

    def test_ReloadReplaceUsdUsdaWithUsdc(self):
        """Test that replacing a .usda-backed .usd file with a .usdc
        file and reloading it results in a .usdc-backed layer and not
        a .usda-backed layer with the contents of the .usdc file."""

        shutil.copyfile('ascii.usd', 'test_replace.usd')
        layer = Sdf.Layer.FindOrOpen('test_replace.usd')
        self.assertEqual(
            Sdf.UsdFileFormat.GetUnderlyingFormatForLayer(layer), 
            'usda')

        shutil.copyfile('crate.usd', 'test_replace.usd')

        # Force a reload, otherwise the reload is skipped sometimes due
        # to file mtimes not changing after copyfile.
        self.assertTrue(layer.Reload(force=True))

        self.assertEqual(
            Sdf.UsdFileFormat.GetUnderlyingFormatForLayer(layer), 
            'usdc')

    def test_Tokens(self):
        '''Test basic token wrapping'''
        self.assertEqual(Sdf.UsdFileFormat.Tokens.Target, 'usd')
        self.assertEqual(Sdf.UsdFileFormat.Tokens.FormatArg, 'format')

if __name__ == "__main__":
    unittest.main()
