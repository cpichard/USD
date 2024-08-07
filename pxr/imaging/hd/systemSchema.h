//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* **                                                                      ** */
/* ** This file is generated by a script.                                  ** */
/* **                                                                      ** */
/* ** Do not edit it directly (unless it is within a CUSTOM CODE section)! ** */
/* ** Edit hdSchemaDefs.py instead to make changes.                        ** */
/* **                                                                      ** */
/* ************************************************************************** */

#ifndef PXR_IMAGING_HD_SYSTEM_SCHEMA_H
#define PXR_IMAGING_HD_SYSTEM_SCHEMA_H

/// \file

#include "pxr/imaging/hd/api.h"

#include "pxr/imaging/hd/schema.h"

// --(BEGIN CUSTOM CODE: Includes)--
#include "pxr/base/tf/declarePtrs.h"
// --(END CUSTOM CODE: Includes)--

PXR_NAMESPACE_OPEN_SCOPE

// --(BEGIN CUSTOM CODE: Declares)--
TF_DECLARE_REF_PTRS(HdSceneIndexBase);
// --(END CUSTOM CODE: Declares)--

#define HD_SYSTEM_SCHEMA_TOKENS \
    (system) \

TF_DECLARE_PUBLIC_TOKENS(HdSystemSchemaTokens, HD_API,
    HD_SYSTEM_SCHEMA_TOKENS);

//-----------------------------------------------------------------------------

// The HdSystemSchema specifies a container that will hold "system" data. Each
// piece of system data is identified by a key within the container. A piece
// of system data is evaluated at a given location by walking up the namespace
// looking for a system container that contains the corresponding key.
//

class HdSystemSchema : public HdSchema
{
public:
    /// \name Schema retrieval
    /// @{

    HdSystemSchema(HdContainerDataSourceHandle container)
      : HdSchema(container) {}

    /// Retrieves a container data source with the schema's default name token
    /// "system" from the parent container and constructs a
    /// HdSystemSchema instance.
    /// Because the requested container data source may not exist, the result
    /// should be checked with IsDefined() or a bool comparison before use.
    HD_API
    static HdSystemSchema GetFromParent(
        const HdContainerDataSourceHandle &fromParentContainer);

    /// @}

// --(BEGIN CUSTOM CODE: Schema Methods)--

    /// Evaluates the \p key at \p fromPath.  If \p key is found, the return
    /// value will be non-null and \p foundAtPath will contain the path at
    /// which the non-null result was found.  Otherwise, this returns null.
    ///
    /// This operation will be linear in the length of \p fromPath.
    HD_API
    static HdDataSourceBaseHandle GetFromPath(
            HdSceneIndexBaseRefPtr const &inputScene,
            SdfPath const &fromPath,
            TfToken const &key,
            SdfPath *foundAtPath);

    /// Composes the system container in at \p fromPath by walking up the
    /// prim.dataSources in \p inputScene and composing any system containers
    /// it encounters.  
    ///
    /// If no system containers were found, this returns null.
    /// Otherwise, this will return a container data source with the composed
    /// system data sources. If non-null, \p foundAtPath will be the last prim
    /// at which system data was found.
    HD_API
    static HdContainerDataSourceHandle Compose(
            HdSceneIndexBaseRefPtr const &inputScene,
            SdfPath const &fromPath,
            SdfPath *foundAtPath);

    /// Similar to `Compose` but this return value would be suitable for using
    /// with HdOverlayContainerDataSource for a prim's dataSource:
    /// ```
    /// prim.dataSource = HdOverlayContainerDataSource::New(
    ///   HdSystemSchema::ComposeAsPrimDataSource(...),
    ///   prim.dataSource);
    /// ```
    HD_API
    static HdContainerDataSourceHandle ComposeAsPrimDataSource(
            HdSceneIndexBaseRefPtr const &inputScene,
            SdfPath const &fromPath,
            SdfPath *foundAtPath);

// --(END CUSTOM CODE: Schema Methods)--

    /// \name Member accessor
    /// @{ 

    /// @}

    /// \name Schema location
    /// @{

    /// Returns a token where the container representing this schema is found in
    /// a container by default.
    HD_API
    static const TfToken &GetSchemaToken();

    /// Returns an HdDataSourceLocator (relative to the prim-level data source)
    /// where the container representing this schema is found by default.
    HD_API
    static const HdDataSourceLocator &GetDefaultLocator();

    /// @} 

    /// \name Schema construction
    /// @{

    /// @}
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif