//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "pxr/pxr.h"
#include "pxr/usd/pcp/debugCodes.h"
#include "pxr/base/arch/functionLite.h"
#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/stringUtils.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(PCP_CHANGES, "Pcp change processing");
    TF_DEBUG_ENVIRONMENT_SYMBOL(PCP_DEPENDENCIES, "Pcp dependencies");

    TF_DEBUG_ENVIRONMENT_SYMBOL(
        PCP_PRIM_INDEX, 
        "Print debug output to terminal during prim indexing");

    TF_DEBUG_ENVIRONMENT_SYMBOL(
        PCP_PRIM_INDEX_GRAPHS, 
        "Write graphviz 'dot' files during prim indexing "
        "(requires PCP_PRIM_INDEX)");
    
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        PCP_PRIM_INDEX_GRAPHS_MAPPINGS,
        "Include namespace mappings in graphviz files generated "
        "during prim indexing (requires PCP_PRIM_INDEX_GRAPHS)");

    TF_DEBUG_ENVIRONMENT_SYMBOL(PCP_NAMESPACE_EDIT, "Pcp namespace edits");
}

PXR_NAMESPACE_CLOSE_SCOPE
