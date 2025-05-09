//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_EXEC_EXEC_DEFINITION_REGISTRY_H
#define PXR_EXEC_EXEC_DEFINITION_REGISTRY_H

#include "pxr/pxr.h"

#include "pxr/exec/exec/api.h"
#include "pxr/exec/exec/computationDefinition.h"
#include "pxr/exec/exec/inputKey.h"
#include "pxr/exec/exec/types.h"

#include "pxr/base/tf/hash.h"
#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"
#include "pxr/base/tf/weakBase.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

class EsfAttributeInterface;
class EsfJournal;
class EsfObjectInterface;
class EsfPrimInterface;

/// Singleton that stores computation definitions registered for schemas that
/// define computations.
///
class Exec_DefinitionRegistry : public TfWeakBase
{
public:
    Exec_DefinitionRegistry(
        const Exec_DefinitionRegistry &) = delete;
    Exec_DefinitionRegistry &operator=(
        const Exec_DefinitionRegistry &) = delete;

    /// Provides access to the singleton instance, first ensuring it is
    /// constructed, and ensuring that all currently-loaded plugins have
    /// registered their computations.
    ///
    EXEC_API
    static Exec_DefinitionRegistry& GetInstance();

    /// Returns the definition for the prim computation named
    /// \p computationName registered for \p providerPrim.
    ///
    /// Any scene access needed to determine the input keys is recorded in
    /// \p journal.
    ///
    EXEC_API
    const Exec_ComputationDefinition *GetComputationDefinition(
        const EsfPrimInterface &providerPrim,
        const TfToken &computationName,
        EsfJournal *journal) const;

    /// Returns the definition for the attribute computation named
    /// \p computationName registered for \p providerAttribute.
    ///
    /// Any scene access needed to determine the input keys is recorded in
    /// \p journal.
    ///
    EXEC_API
    const Exec_ComputationDefinition *GetComputationDefinition(
        const EsfAttributeInterface &providerAttribute,
        const TfToken &computationName,
        EsfJournal *journal) const;

    // Only computation builders can register plugin computations.
    class RegisterPluginComputationAccess
    {
        friend class Exec_PrimComputationBuilder;

        // Registers a prim computation for \p schemaType.
        inline static void _RegisterPrimComputation(
            TfType schemaType,
            const TfToken &computationName,
            TfType resultType,
            ExecCallbackFn &&callback,
            Exec_InputKeyVector &&inputKeys);
    };

private:

    // Only TfSingleton can create instances.
    friend class TfSingleton<Exec_DefinitionRegistry>;

    Exec_DefinitionRegistry();

    // Returns a reference to the singleton that is suitable for registering
    // new computations. This instance cannot be used to look up computations.
    EXEC_API
    static Exec_DefinitionRegistry& _GetInstanceForRegistration();

    void _RegisterPrimComputation(
        TfType schemaType,
        const TfToken &computationName,
        TfType resultType,
        ExecCallbackFn &&callback,
        Exec_InputKeyVector &&inputKeys);

    void _RegisterBuiltinStageComputation(
        const TfToken &computationName,
        std::unique_ptr<Exec_ComputationDefinition> &&definition);

    void _RegisterBuiltinPrimComputation(
        const TfToken &computationName,
        std::unique_ptr<Exec_ComputationDefinition> &&definition);

    void _RegisterBuiltinAttributeComputation(
        const TfToken &computationName,
        std::unique_ptr<Exec_ComputationDefinition> &&definition);

    void _RegisterBuiltinComputations();

    void _SetInstanceFullyConstructed();

    void _WaitForFullConstruction();

private:

    // These members ensure singleton access returns a fully-constructed
    // instance. This is the case for GetInstance(), but not required for
    // _GetInstanceForRegistration() which is called by exec definition registry
    // functions.
    std::atomic<bool> _isFullyConstructed;
    std::mutex _isFullyConstructedMutex;
    std::condition_variable _isFullyConstructedConditionVariable;

    // Map from (schemaType, computationName) to plugin prim computation
    // definition.
    //
    // TODO: When we add support for loading plugins, we will need to think about
    // how to provide thread-safe access to this map.
    std::unordered_map<
        std::tuple<TfType, TfToken>,
        Exec_PluginComputationDefinition,
        TfHash>
    _pluginPrimComputationDefinitions;

    // Map from computationName to builtin stage computation
    // definition.
    std::unordered_map<
        TfToken,
        std::unique_ptr<Exec_ComputationDefinition>,
        TfHash>
    _builtinStageComputationDefinitions;

    // Map from computationName to builtin prim computation
    // definition.
    std::unordered_map<
        TfToken,
        std::unique_ptr<Exec_ComputationDefinition>,
        TfHash>
    _builtinPrimComputationDefinitions;

    // Map from computationName to builtin attribute computation
    // definition.
    std::unordered_map<
        TfToken,
        std::unique_ptr<Exec_ComputationDefinition>,
        TfHash>
    _builtinAttributeComputationDefinitions;
};

void
Exec_DefinitionRegistry::RegisterPluginComputationAccess::
_RegisterPrimComputation(
    TfType schemaType,
    const TfToken &computationName,
    TfType resultType,
    ExecCallbackFn &&callback,
    Exec_InputKeyVector &&inputKeys)
{
    _GetInstanceForRegistration()._RegisterPrimComputation(
        schemaType,
        computationName,
        resultType,
        std::move(callback),
        std::move(inputKeys));
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
