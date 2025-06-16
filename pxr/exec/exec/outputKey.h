//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_EXEC_EXEC_OUTPUT_KEY_H
#define PXR_EXEC_EXEC_OUTPUT_KEY_H

#include "pxr/pxr.h"

#include "pxr/exec/exec/api.h"

#include "pxr/exec/exec/computationDefinition.h"

#include "pxr/exec/esf/object.h"
#include "pxr/base/tf/smallVector.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Specifies an output to compile.
///
/// Compilation uses output keys to identify outputs to compile along with
/// parameters needed for their compilation.
/// 
class Exec_OutputKey
{
public:
    Exec_OutputKey(
        const EsfObject &providerObject,
        const Exec_ComputationDefinition *const computationDefinition)
        : _providerObject(providerObject)
        , _computationDefinition(computationDefinition)
    {}

    /// Returns the object that provides the computation.
    const EsfObject &GetProviderObject() const {
        return _providerObject;
    }

    /// Returns the definition of the computation to compile.
    const Exec_ComputationDefinition *GetComputationDefinition() const {
        return _computationDefinition;
    }

    /// Identity class. See Exec_OutputKey::Identity below.
    class Identity;

    /// Constructs and returns an identity for this output key.
    inline Identity MakeIdentity() const;

private:
    EsfObject _providerObject;
    const Exec_ComputationDefinition *_computationDefinition;
};

/// Lightweight identity that represents an Exec_OutputKey.
/// 
/// Instances of this class contain all the information necessary to represent
/// an Exec_OutputKey, while being lightweight, comparable, and hashable. They
/// can be used, for example, as key types in hash maps.
/// 
/// \note
/// Identities are not automatically maintained across scene edits.
///
class Exec_OutputKey::Identity
{
public:
    explicit Identity(const Exec_OutputKey &key)
        : _providerPath(key._providerObject->GetPath(nullptr))
        , _computationDefinition(key._computationDefinition)
    {}

    bool operator==(const Exec_OutputKey::Identity &rhs) const {
        return _providerPath == rhs._providerPath &&
            _computationDefinition == rhs._computationDefinition;
    }

    bool operator!=(const Exec_OutputKey::Identity &rhs) const {
        return !(*this == rhs);
    }

    template <typename HashState>
    friend void TfHashAppend(
        HashState& h, const Exec_OutputKey::Identity& identity) {
        h.Append(identity._providerPath);
        h.Append(identity._computationDefinition);
    }

    /// Return a human-readable description of this value key for diagnostic
    /// purposes.
    /// 
    EXEC_API std::string GetDebugName() const;

private:
    SdfPath _providerPath;
    const Exec_ComputationDefinition *_computationDefinition;
};

Exec_OutputKey::Identity 
Exec_OutputKey::MakeIdentity() const
{
    return Identity(*this);
}

/// A vector of output keys.
///
/// This is chosen for efficient storage of output keys generated from
/// Exec_CompilationTask%s, where often just a single output key is generated
/// per input.
///
using Exec_OutputKeyVector = TfSmallVector<Exec_OutputKey, 1>;

PXR_NAMESPACE_CLOSE_SCOPE

#endif
