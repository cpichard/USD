#
# Copyright 2025 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
[
    dict(
        SCHEMA_NAME = 'ALL_SCHEMAS',
        LIBRARY_PATH = 'pxr/usdImaging/usdSkelImaging',
    ),

    #--------------------------------------------------------------------------
    # usdSkelImaging/animation
    dict(
        SCHEMA_NAME = 'Animation',
        SCHEMA_TOKEN = 'skelAnimation',
        ADD_DEFAULT_LOCATOR = True,
        DOC = '''Corresponds to UsdSkelAnimation.''',
        MEMBERS = [
            ('ALL_MEMBERS', '', dict(ADD_LOCATOR=True)),

            ('joints', T_TOKENARRAY, {}),
            ('translations', T_VEC3FARRAY, {}),
            ('rotations', T_QUATFARRAY, {}),
            ('scales', T_VEC3HARRAY, {}),

            ('blendShapes', T_TOKENARRAY, {}),
            ('blendShapeWeights', T_FLOATARRAY, {})
        ],
    ),
]
