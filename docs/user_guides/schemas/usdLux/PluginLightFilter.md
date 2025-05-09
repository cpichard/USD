% WARNING: THIS FILE IS GENERATED BY genSchemaDocs. DO NOT EDIT.
% Generated: 11:28PM on April 14, 2025


(PluginLightFilter)=
# PluginLightFilter

Light filter that provides properties that allow it to identify an 
    external SdrShadingNode definition, through UsdShadeNodeDefAPI, that can be 
    provided to render delegates without the need to provide a schema 
    definition for the light filter's type.

```{contents}
:depth: 2
:local:
:backlinks: none
```

(PluginLightFilter_properties)=

## Properties

(PluginLightFilter_inheritedproperties_LightFilter)=

## Inherited Properties ({ref}`LightFilter`)

(PluginLightFilter_collection:filterLink:includeRoot)=

### collection:filterLink:includeRoot

**USD type**: `bool`

**Fallback value**: `True`

Use the filterLink collection to specify which
geometry Prims to be included or excluded from this light filter. Prims 
excluded or not included will not be affected by this light filter. 

The `includeRoot` attribute indicates whether the pseudo-root path `/` should 
be counted as one of the included target paths. Note that the fallback value 
is true, which means that light filters will affect all objects illuminated
by the light(s) the filter is associated with.

See {ref}`collections_and_patterns` for more details on USD collections.


(PluginLightFilter_lightFilter:shaderId)=

### lightFilter:shaderId

**USD type**: `token`

**Fallback value**: ``

The shader ID for a LightFilter. The fallback
value is no shader ID. 


(PluginLightFilter_inheritedproperties_Xformable)=

## Inherited Properties ({ref}`Xformable`)

(PluginLightFilter_xformOpOrder)=

### xformOpOrder

**USD type**: `token[]`



(PluginLightFilter_inheritedproperties_Imageable)=

## Inherited Properties ({ref}`Imageable`)

(PluginLightFilter_proxyPrim)=

### proxyPrim

**USD type**: `rel` (relationship)



(PluginLightFilter_purpose)=

### purpose

**USD type**: `token`

**Fallback value**: `default`



(PluginLightFilter_visibility)=

### visibility

**USD type**: `token`

**Fallback value**: `inherited`


