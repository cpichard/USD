#!/pxrpythonsubst
#
# Copyright 2018 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

import os
os.environ['PXR_MTLX_STDLIB_SEARCH_PATHS'] = os.getcwd()

from pxr import Tf, Sdr
import unittest

class TestParser(unittest.TestCase):
    def test_NodeParser(self):
        """
        Test MaterialX node parser.
        """
        # Find our nodes.
        nodes = Sdr.Registry().GetShaderNodesByFamily('UsdMtlxTestNode')
        self.assertEqual(sorted([node.GetName() for node in nodes]), [
            'UsdMtlxTestNamespace:nd_boolean',
            'UsdMtlxTestNamespace:nd_color3',
            'UsdMtlxTestNamespace:nd_color4',
            'UsdMtlxTestNamespace:nd_customtype',
            'UsdMtlxTestNamespace:nd_float',
            'UsdMtlxTestNamespace:nd_integer',
            'UsdMtlxTestNamespace:nd_string',
            'UsdMtlxTestNamespace:nd_surface',
            'UsdMtlxTestNamespace:nd_vector',
        ])

        # Verify common info.
        for node in nodes:
            implementationUri = node.GetResolvedImplementationURI()
            self.assertEqual(os.path.normcase(implementationUri), 
                    os.path.normcase(os.path.abspath("test.mtlx")))
            self.assertEqual(node.GetSourceType(), "mtlx")
            self.assertEqual(node.GetFamily(), "UsdMtlxTestNode")
            self.assertEqual(sorted(node.GetShaderInputNames()), ["in", "note"])
            self.assertEqual(node.GetShaderOutputNames(), ['out'])

        # Verify some metadata:
        node = Sdr.Registry().GetShaderNodeByIdentifier(
            'UsdMtlxTestNamespace:nd_vector')
        self.assertEqual(node.GetHelp(), "Vector help")
        # Properties without a Page metadata end up in an unnamed page. This
        # means that all MaterialX outputs will be assigned to the unnamed page
        # when the metadata is used.
        self.assertEqual(node.GetPages(), ["UI Page", ""])
        self.assertEqual(node.GetPropertyNamesForPage("UI Page"), ["in",])
        self.assertEqual(node.GetPropertyNamesForPage(""), ["note", "out"])
        input = node.GetShaderInput("in")
        self.assertEqual(input.GetHelp(), "Property help")
        self.assertEqual(input.GetLabel(), "UI Vector")
        self.assertEqual(input.GetPage(), "UI Page")
        self.assertEqual(input.GetOptions(),
            [("X Label", "1, 0, 0"), ("Y Label", "0, 1, 0"), ("Z Label", "0, 0, 1")])

        node = Sdr.Registry().GetShaderNodeByIdentifier(
            'UsdMtlxTestNamespace:nd_float')
        expected = {
            "uimin": "-360.0",
            "uimax": "360.0",
            "uisoftmin": "0.0",
            "uisoftmax": "180.0",
            "uistep": "1.0",
            "unittype": "angle",
            "unit": "degree"
        }
        input = node.GetShaderInput("in")
        self.assertEqual(input.GetHelp(), "Unit is degree.")
        hints = input.GetHints()
        metadata = input.GetMetadata()
        for key in expected.keys():
            self.assertEqual(hints[key], expected[key])
            self.assertEqual(metadata[key], expected[key])

        # Verify converted types.
        typeNameMap = {
            'boolean': 'bool',
            'color3': 'color',
            'color4': 'color4',
            'customtype': 'customtype',
            'float': 'float',
            'integer': 'int',
            'string': 'string',
            'surface': 'string',
            'vector': 'float',   # vector actually becomes a float[3], but the
                                 # array size is not represented in the Type
        }
        for node in nodes:
            # Strip leading UsdMtlxTestNamespace:nd_ from name.
            name = node.GetName()[24:]

            # Get the input.
            prop = node.GetShaderInput('in')

            # Verify type.
            self.assertEqual(prop.GetType(), typeNameMap.get(name))

if __name__ == '__main__':
    unittest.main()
